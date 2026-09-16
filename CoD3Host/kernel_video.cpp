// Xbox 360 kernel imports: the video driver.
//
// On the console these calls talk to the Xenos GPU. The title hands over a
// command ring buffer, writes packets into it, advances a write pointer, and
// waits for the GPU to catch up and for a vertical blank interrupt before
// starting the next frame.
//
// There is no GPU here. What this file provides is the part of that contract
// the title can observe from the CPU: the ring buffer is accepted, a thread
// advances the read pointer to wherever the write pointer has reached, and the
// graphics interrupt callback is invoked at 60 Hz. That is enough for the
// title to believe frames are completing and to keep running its main loop.
//
// Nothing in the ring buffer is read. No packet is decoded, no shader is
// translated, nothing is drawn. Producing a picture means writing a real
// command processor and running XenosRecomp over the title's shaders, which
// is the largest single piece of work left in this project.

#include "kernel.h"
#include "sampler.h"
#include "audio_out.h"
#include "scheduler.h"
#include "gpu.h"
#include "edram.h"
#include "shaders.h"
#include "raster.h"
#include "window.h"
#include "d3d11_backend.h"
#include "pool_trace.h"
#include "timeline.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <condition_variable>
#include <thread>

#include <Windows.h>

namespace
{
    struct VideoState
    {
        std::atomic<bool> running{ false };

        // Where the command thread waits for work and what wakes it.
        std::mutex submitMutex;
        std::condition_variable submitted;
        std::atomic<uint64_t> submissions{ 0 };

        std::atomic<uint32_t> ringBuffer{ 0 };
        std::atomic<uint32_t> ringBufferSize{ 0 };
        std::atomic<uint32_t> readPointerWriteBack{ 0 };
        std::atomic<uint32_t> readPointer{ 0 };
        std::atomic<uint64_t> swaps{ 0 };           // VdSwap calls: frames
        std::atomic<uint32_t> gpuIdentifier{ 0 };
        std::atomic<uint32_t> interruptCallback{ 0 };
        std::atomic<uint64_t> interruptsRaised{ 0 };
        std::atomic<uint32_t> interruptContext{ 0 };
        std::atomic<uint64_t> frames{ 0 };
        std::thread thread;
        std::thread commandThread;

        // Held while the ring is walked and while it is replaced, so the two
        // never overlap.
        std::mutex ringMutex;
    };

    VideoState& Video()
    {
        static VideoState state;
        return state;
    }

    // The pretend GPU. It never looks at what is in the ring buffer; it only
    // keeps the bookkeeping the title polls consistent.
    // The command processor.
    //
    // This used to share the video thread, and that only worked while there was
    // nothing to draw. Once draws were really carried out, one busy buffer took
    // longer than a frame and the vertical blank stopped happening, which the
    // title reads as a display that has stopped. Giving the two their own
    // threads is also what the hardware does: the command processor and the
    // scanout run independently and neither waits for the other.
    // The command thread's guest context, for the interrupt hook above.
    thread_local PPCContext* t_commandContext = nullptr;
    thread_local uint32_t t_commandStackTop = 0;

    // Whether the command thread is inside the title's interrupt handler,
    // and since when. The handler is guest code run in the middle of the
    // command stream, and if it blocks, on a lock a render thread holds
    // while that thread waits for the GPU, the stream stops with it and the
    // title reports the GPU hung. The ring report below says so.
    std::atomic<bool> g_inHandler{ false };
    std::atomic<int64_t> g_handlerSince{ 0 };
    std::atomic<uint32_t> g_commandThreadId{ 0 };

    int64_t NowMilliseconds()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    // The handler itself, on the command thread's context, as the given CPU.
    void RunInterruptHandler(PPCFunc* routine, uint32_t cpu)
    {
        VideoState& video = Video();
        PPCContext& context = *t_commandContext;

        context.r1.u32 = t_commandStackTop - 0x100;
        context.r3.u32 = 1;   // source: the command processor
        context.r4.u32 = video.interruptContext.load();

        // The handler decides what to do from the mirrored scratch block:
        // register four is the callback it armed, or 0x0BADF00D when it armed
        // nothing, and an interrupt that finds the latter is one it did not
        // ask for. The first word is the mask of CPUs with an interrupt
        // pending, which the handler clears its own bit from.
        const uint32_t block = Gpu::ReadRegister(Gpu::ApertureBase + 0x1DD * 4) & ~3u;
        const uint32_t pending = block
            ? Guest::Read32(Guest::Base, Guest::PhysicalAlias(block)) : 0;
        const uint32_t armed = block
            ? Guest::Read32(Guest::Base, Guest::PhysicalAlias(block + 16)) : 0;
        const uint64_t raised = video.interruptsRaised.fetch_add(1, std::memory_order_relaxed);
        // The first forty, and forty more once a level is loading: the
        // interrupts that complete the level's recorded command buffers.
        static std::atomic<int> inLevel{ 0 };
        const bool level = Kernel::Stats().filesOpened.load(std::memory_order_relaxed) >= 40;
        const bool usual = armed == 0x82130098u || armed == 0x821300C8u ||
                           armed == 0x822F4A10u || armed == 0x0BADF00Du;
        if (raised < 40 || (level && (!usual || inLevel.fetch_add(1) < 40)))
        {
            printf("gpu: interrupt %llu on cpu %u, pending mask 0x%02X, callback "
                   "0x%08X%s\n", (unsigned long long)raised, cpu, pending, armed,
                armed == 0x0BADF00Du ? " (none armed)" : "");
            fflush(stdout);
        }

        g_handlerSince.store(NowMilliseconds());
        g_inHandler.store(true);
        Timeline::Mark("interrupt", pending, armed);
        routine(context, Guest::Base);
        Timeline::Mark("handled", pending, armed);
        g_inHandler.store(false);
    }

    void CommandThread()
    {
        VideoState& video = Video();

        // The handler this thread raises is guest code, so it needs a context
        // and a stack of its own, exactly like a thread the title made itself.
        const uint32_t stackTop = Guest::AllocateStack(128u << 10);
        alignas(0x40) PPCContext ctx{};
        ctx.r13.u32 = Guest::CreateThreadPointer(GetCurrentThreadId());
        ctx.fpscr.loadFromHost();

        // Reachable from the command processor, so an interrupt packet can
        // run the handler the moment it is decoded.
        t_commandContext = &ctx;
        Kernel::SetCurrentContext(&ctx);
        t_commandStackTop = stackTop;
        g_commandThreadId.store(GetCurrentThreadId());
        Gpu::SetSubmitHook([]() {
            VideoState& v = Video();
            v.submissions.fetch_add(1, std::memory_order_release);
            std::lock_guard<std::mutex> lock(v.submitMutex);
            v.submitted.notify_one();
        });
        uint64_t seenSubmissions = 0;
        Gpu::SetInterruptRaiser([](uint32_t cpuMask) {
            VideoState& video = Video();
            const uint32_t callback = video.interruptCallback.load();
            if (callback == 0 || t_commandContext == nullptr || t_commandStackTop == 0)
            {
                printf("gpu: interrupt with no handler: callback 0x%08X, context %s, stack %s\n",
                    callback, t_commandContext ? "set" : "null", t_commandStackTop ? "set" : "none");
                return;
            }
            PPCFunc* routine = Guest::Lookup(callback);
            if (routine == nullptr)
            {
                printf("gpu: interrupt handler 0x%08X is not recompiled code\n", callback);
                return;
            }

            Gpu::WriteRegister(Gpu::RegisterInterruptStatus,
                Gpu::ReadRegister(Gpu::RegisterInterruptStatus) | 1);

            // On the CPU the packet names.
            //
            // The handler ends by clearing its own CPU's bit out of the first
            // word of the scratch block, in memory, under a spin lock: that
            // word is the mask of CPUs the driver has interrupts pending on,
            // and the wait that follows in the stream is for it to reach zero.
            // The driver puts the mask into SCRATCH_REG0 and the same mask
            // into the packet, so the handler has to run as that CPU, or it
            // clears the wrong bit and the wait never ends. The thread this
            // runs on is not a guest thread and its processor number is
            // whatever it was given at creation, so it is set here, per
            // interrupt, to each CPU in the mask in turn.
            if (cpuMask == 0) cpuMask = 1;
            for (uint32_t cpu = 0; cpu < 6; cpu++)
            {
                if (((cpuMask >> cpu) & 1) == 0) continue;
                Guest::SetProcessor(*t_commandContext, int(cpu));
                RunInterruptHandler(routine, cpu);
            }
        });

        while (video.running.load(std::memory_order_acquire))
        {
            const uint32_t ring = video.ringBuffer.load();
            const uint32_t ringDwords = video.ringBufferSize.load() / 4;
            bool worked = false;

            if (ring != 0 && ringDwords != 0)
            {
                std::lock_guard<std::mutex> lock(video.ringMutex);
                // Read again under the lock: the ring may have been replaced
                // between the reads above and here.
                const uint32_t lockedRing = video.ringBuffer.load();
                const uint32_t lockedDwords = video.ringBufferSize.load() / 4;
                const uint32_t before = video.readPointer.load();
                const uint32_t consumed = Gpu::ProcessRing(
                    lockedRing, lockedDwords, before, Gpu::WritePointer());
                video.readPointer.store(consumed);
                Gpu::WriteRegister(Gpu::ApertureBase + 0x710, consumed);
                worked = consumed != before;

                // The read pointer, published where the driver asked for it
                // (VdEnableRingBufferRPtrWriteBack: word fifteen of the
                // write-back block). The ring writer checks it before every
                // packet it writes, and it is the only thing the writer
                // checks at the wrap: with nothing ever published there the
                // wrap looked like the GPU still reading from the start of
                // the ring, and the title sat out its five second "GPU is
                // hung" timeout on every lap of the ring.
                if (const uint32_t at = video.readPointerWriteBack.load())
                    Guest::WritePhysical32(at & ~3u, consumed);

                // Packets waiting that the walk did not take. Once is a
                // deadline; a thousand times in a row is the walk refusing
                // them, and the header it stopped at says why.
                static int refused = 0;
                const uint32_t write = Gpu::WritePointer();
                if (!worked && write != consumed && lockedDwords != 0)
                {
                    if (++refused == 1000)
                    {
                        const uint32_t at = Guest::PhysicalAlias(lockedRing) + (consumed % lockedDwords) * 4;
                        printf("cp: %u dwords waiting at ring dword %u were not taken a thousand "
                               "times running; header there 0x%08X, next 0x%08X 0x%08X\n",
                            (write + lockedDwords - consumed) % lockedDwords, consumed,
                            Guest::Read32(Guest::Base, at), Guest::Read32(Guest::Base, at + 4),
                            Guest::Read32(Guest::Base, at + 8));
                        fflush(stdout);
                    }
                }
                else refused = 0;
            }

            // The driver's write-back block, checked for the fill pattern.
            //
            // The block the driver registered for the GPU's progress was found
            // holding 0xCDCDCDCD, the pattern the title's allocator writes into
            // memory it has just handed out, and the ring buffer 0x55555555.
            // Something the title allocated landed on top of the driver's
            // state. A hardware watchpoint slows the run enough that it never
            // happens, so instead this looks every time round and, the first
            // time the pattern is there, prints what every guest thread is in
            // the middle of: one of them is the memset.
            {
                static bool reported = false;
                const uint32_t writeBack = video.readPointerWriteBack.load();
                if (!reported && writeBack != 0)
                {
                    const uint32_t block = Guest::PhysicalAlias(writeBack & ~0xFFFFu);
                    const uint32_t word = Guest::Read32(Guest::Base, block);
                    if (word == 0xCDCDCDCDu || word == 0x55555555u ||
                        word == 0xDDDDDDDDu || word == 0xFEEEFEEEu)
                    {
                        reported = true;
                        printf("gpu: the write back block at 0x%08X now holds 0x%08X, "
                               "a fill pattern: something allocated over it. Every "
                               "guest thread, right now:\n", block, word);
                        fflush(stdout);
                        Sampler::SampleNow();
                        Kernel::ReportRecentCalls();
                    }
                }
            }

            // Interrupts the stream asked for, raised now that everything
            // queued ahead of them has been carried out. Source one is the
            // command processor; the vertical blank on the other thread is
            // source zero.
            if (const uint32_t raised = Gpu::TakePendingInterrupts())
            {
                const uint32_t callback = video.interruptCallback.load();
                if (callback != 0 && stackTop != 0)
                {
                    if (PPCFunc* routine = Guest::Lookup(callback))
                    {
                        Gpu::WriteRegister(Gpu::RegisterInterruptStatus,
                            Gpu::ReadRegister(Gpu::RegisterInterruptStatus) | 1);

                        // Once, however many were queued: the handler asks the
                        // hardware what happened rather than being told, so
                        // raising it repeatedly only costs time.
                        (void)raised;
                        ctx.r1.u32 = stackTop - 0x100;
                        ctx.r3.u32 = 1;   // source: the command processor
                        ctx.r4.u32 = video.interruptContext.load();
                        routine(ctx, Guest::Base);
                        worked = true;
                    }
                }
            }

            // What the title waits on is not that register. It spins comparing
            // its own write pointer at 0x2A1C against whatever is at the
            // address in 0x2A10, and a GPU that has caught up publishes exactly
            // the write pointer. Publishing anything past it makes the title's
            // unsigned distance arithmetic wrap and it spins forever.
            const uint32_t context = video.interruptContext.load();
            if (context != 0)
            {
                const uint32_t readPointerAt =
                    Guest::Read32(Guest::Base, context + 0x2A10);
                const uint32_t titleWritePointer =
                    Guest::Read32(Guest::Base, context + 0x2A1C);
                if (readPointerAt != 0)
                {
                    Guest::WritePhysical32(readPointerAt, titleWritePointer);
                    Guest::WritePhysical32(readPointerAt & ~0x3Fu, titleWritePointer);
                }
            }

            // Nothing done: wait for the title to move the write pointer,
            // or a millisecond, whichever is first. The write pointer is
            // read again before waiting because a submission between the
            // walk and here would otherwise be sat on for that millisecond.
            if (!worked)
            {
                std::unique_lock<std::mutex> lock(video.submitMutex);
                if (video.submissions.load(std::memory_order_acquire) == seenSubmissions)
                    video.submitted.wait_for(lock, std::chrono::milliseconds(1));
                seenSubmissions = video.submissions.load(std::memory_order_acquire);
            }
        }
    }

    void VideoThread()
    {
        VideoState& video = Video();

        // Guest code runs on this thread when the interrupt callback fires, so
        // it needs a processor context and a stack of its own, exactly like a
        // thread the title created itself.
        const uint32_t stackTop = Guest::AllocateStack(256u << 10);
        if (stackTop == 0)
        {
            fprintf(stderr, "video: no room for the interrupt stack\n");
            return;
        }

        alignas(0x40) PPCContext ctx{};
        ctx.r13.u32 = Guest::CreateThreadPointer(GetCurrentThreadId());
        ctx.fpscr.loadFromHost();
        Kernel::SetCurrentContext(&ctx);

        auto nextFrame = std::chrono::steady_clock::now();
        while (video.running.load(std::memory_order_acquire))
        {
            nextFrame += std::chrono::microseconds(16667);   // 60 Hz

            // Two separate things, which looked like one and cost a while to
            // untangle. The command ring is the buffer VdInitializeRingBuffer
            // named, and the register at 0x714 says how far the title has
            // filled it. The field at 0x2A04 in the title's own structure is
            // the system command buffer, a different allocation that stays
            // empty here.
            // The word at offset 4 of the write back block is how far the GPU
            // has got: an address in the upper thirty bits and a two bit tag
            // below it. The render thread waits until that address reaches the
            // one it cares about.
            //
            // This used to be cycled through all four tag values from here, as
            // a stand in for not knowing what wrote it. The command stream
            // writes it: that is what an EVENT_WRITE_SHD packet is for. Now
            // those packets are carried out, forcing a tag from here would
            // overwrite the real answer with a rotating guess.

            const uint32_t callback = video.interruptCallback.load();
            if (callback != 0)
            {
                if (PPCFunc* routine = Guest::Lookup(callback))
                {
                    // The handler checks the interrupt status register before
                    // it does anything, so raising the interrupt means setting
                    // the pending bit as well as making the call.
                    Gpu::WriteRegister(Gpu::RegisterInterruptStatus,
                        Gpu::ReadRegister(Gpu::RegisterInterruptStatus) | 1);

                    ctx.r1.u32 = stackTop - 0x100;
                    ctx.r3.u32 = 0;   // source: vertical blank
                    ctx.r4.u32 = video.interruptContext.load();

                    // What the vertical blank does to the swap flag.
                    //
                    // The handler's vertical blank path counts down a word in
                    // its context and, when that reaches zero, clears the
                    // second word of the scratch block: the flag the stream
                    // waits on after every swap. Printing the countdown and the
                    // flag around each call says whether the countdown is ever
                    // armed, and if so whether the clear happens.
                    const uint32_t context = ctx.r4.u32;
                    const uint32_t block = Gpu::ReadRegister(Gpu::ApertureBase + 0x1DD * 4) & ~3u;
                    const uint32_t countdownBefore = context ? Guest::Read32(Guest::Base, context + 15132) : 0;
                    const uint32_t flagBefore = block ? Guest::Read32(Guest::Base, Guest::PhysicalAlias(block + 4)) : 0;

                    routine(ctx, Guest::Base);

                    static std::atomic<int> announced{ 0 };
                    if (block != 0 && (flagBefore != 0 || countdownBefore != 0) &&
                        announced.fetch_add(1) < 24)
                    {
                        printf("vblank: countdown %u -> %u, swap flag %u -> %u, status 0x%08X\n",
                            countdownBefore, Guest::Read32(Guest::Base, context + 15132),
                            flagBefore, Guest::Read32(Guest::Base, Guest::PhysicalAlias(block + 4)),
                            Gpu::ReadRegister(Gpu::RegisterInterruptStatus));
                        fflush(stdout);
                    }
                }
            }

            const uint64_t frame = video.frames.fetch_add(1, std::memory_order_relaxed) + 1;
            Kernel::Stats().frames.store(frame, std::memory_order_relaxed);

            // Without a picture, this line is the only evidence the title is
            // doing anything. Once a second is often enough to see movement
            // and rare enough not to bury the rest of the output.
            if (frame % 60 == 0)
            {
                const auto& stats = Kernel::Stats();
                // The ring pointers say whether the command processor is
                // keeping up, and whether the size the driver asked for is
                // being interpreted the way the driver meant it.
                printf("ring: write pointer %u, read pointer %u, size %u dwords\n",
                    Gpu::WritePointer(), video.readPointer.load(),
                    video.ringBufferSize.load() / 4);

                // What sits at the read pointer when it is behind the write
                // pointer: the packet the command processor has not taken.
                {
                    const uint32_t ringAt = video.ringBuffer.load();
                    const uint32_t dwords = video.ringBufferSize.load() / 4;
                    const uint32_t read = video.readPointer.load();
                    const uint32_t write = Gpu::WritePointer();
                    if (ringAt != 0 && dwords != 0 && read != write)
                    {
                        printf("  at the read pointer:");
                        for (uint32_t i = 0; i < 12; i++)
                        {
                            const uint32_t at = (read + i) % dwords;
                            printf(" %08X", Guest::Read32(Guest::Base,
                                Guest::PhysicalAlias(ringAt) + at * 4));
                        }
                        printf("\n");
                    }
                    else if (ringAt != 0 && dwords != 0 && read + 1 >= dwords)
                    {
                        // The pointers at the very end of the ring: what the
                        // last packets before the wrap were, and what sits at
                        // the start.
                        printf("  behind the pointer:");
                        for (uint32_t i = 8; i >= 1; i--)
                            printf(" %08X", Guest::Read32(Guest::Base,
                                Guest::PhysicalAlias(ringAt) + ((read + dwords - i) % dwords) * 4));
                        printf("  | at the pointer and after:");
                        for (uint32_t i = 0; i < 6; i++)
                            printf(" %08X", Guest::Read32(Guest::Base,
                                Guest::PhysicalAlias(ringAt) + ((read + i) % dwords) * 4));
                        printf("\n");

                    }
                }
                // The title's indirect buffer pool and the fence it
                // waits on before reusing part of it: the device's
                // fields at 0x34AC.. and the write-back block's words.
                const uint32_t device = video.interruptContext.load();
                if (device != 0)
                {
                    const uint32_t blockAt = Guest::Read32(Guest::Base, device + 0x2A10);
                    printf("  ib pool: base %08X end %08X min %08X at %08X lap %u start %08X limit %08X | gpu %08X lap %u\n",
                        Guest::Read32(Guest::Base, device + 0x34AC), Guest::Read32(Guest::Base, device + 0x34B0),
                        Guest::Read32(Guest::Base, device + 0x34B4), Guest::Read32(Guest::Base, device + 0x34B8),
                        Guest::Read32(Guest::Base, device + 0x34BC), Guest::Read32(Guest::Base, device + 0x34C0),
                        Guest::Read32(Guest::Base, device + 0x34C4), Guest::Read32(Guest::Base, device + 0x3290),
                        Guest::Read32(Guest::Base, device + 0x3294));
                    if (blockAt != 0)
                    {
                        printf("  write back block %08X:", blockAt);
                        for (uint32_t i = 0; i < 8; i++)
                            printf(" %08X", Guest::Read32(Guest::Base, Guest::PhysicalAlias(blockAt) + i * 4));
                        printf("\n");
                    }
                }
                if (g_inHandler.load())
                {
                    const int64_t held = NowMilliseconds() - g_handlerSince.load();
                    if (held > 500)
                    {
                        printf("cp: inside the title's graphics interrupt handler for %.1f s; "
                               "the stream is stopped behind it\n", held / 1000.0);
                        Kernel::ReportRecentCalls(g_commandThreadId.load());
                    }
                }

                // The write back block and the fields around the ring
                // description. Which field the render thread compares against
                // the progress pointer is not known yet, so the whole
                // neighbourhood is printed and the one that moves is the one
                // that matters.
                const uint32_t identifier = video.gpuIdentifier.load();
                if (identifier >= 8)
                {
                    const uint32_t blockAt = identifier - 8;
                    printf("block:");
                    for (int i = 0; i < 6; i++)
                        printf(" %08X", Guest::Read32(Guest::Base, blockAt + i * 4));
                    printf("\n");
                }

                // Strings the title keeps near the code that is stuck. Its own
                // words about what it is waiting for are worth more than any
                // guess made from the outside.
                {
                    static bool once = false;
                    if (!once)
                    {
                        once = true;
                        for (uint32_t at = 0x82A8DE00; at < 0x82A8E100; at++)
                        {
                            const char* text =
                                reinterpret_cast<const char*>(Guest::Ptr(at));
                            size_t length = 0;
                            while (length < 90 && text[length] >= 0x20 &&
                                   text[length] < 0x7F)
                                length++;
                            if (length >= 12 && text[length] == 0)
                            {
                                printf("string 0x%08X: %.*s\n", at, int(length), text);
                                at += uint32_t(length);
                            }
                        }
                        fflush(stdout);
                    }
                }

                // The render thread waits on the word four bytes into whatever
                // this field points at. Printing the pointer and what is around
                // it is the only way to tell whether the fences the command
                // stream writes are landing in the block it is watching.
                const uint32_t deviceContext = video.interruptContext.load();
                if (deviceContext != 0)
                {
                    const uint32_t watched =
                        Guest::Read32(Guest::Base, deviceContext + 0x2A10);
                    printf("watched: 0x%08X ->", watched);
                    if (watched != 0)
                    {
                        const uint32_t at = Guest::PhysicalAlias(watched);
                        for (int i = 0; i < 4; i++)
                            printf(" %08X", Guest::Read32(Guest::Base, at + i * 4));
                    }
                    printf("\n");
                }

                const uint32_t context = video.interruptContext.load();
                if (context != 0)
                {
                    printf("ctx:  ");
                    for (uint32_t offset = 0x2A00; offset <= 0x2A3C; offset += 4)
                        printf(" %08X", Guest::Read32(Guest::Base, context + offset));
                    printf("\n");
                }

                Gpu::ReportPacketMix();
                Edram::Report();
                Shaders::Report();
                Raster::Report();
                D3D11Backend::Report();
                PoolTrace::Report();
                Timeline::Report();
                Kernel::ReportImports();
                Kernel::ReportApcs();
                Scheduler::Report();
                Audio::Report();
                Gpu::ReportFences();
                Kernel::ReportFileReads();
                Gpu::ReportPolling();
                if (frame >= 600) Kernel::ReportWaitTraffic();
                const Gpu::Statistics gpu = Gpu::Stats();
                static uint64_t swapsBefore = 0;
                const uint64_t swaps = video.swaps.load();
                printf("[%4llus] fps %llu  frames %llu  files %llu (%.1f MB read)  threads %llu  "
                       "allocated %.1f MB  audio %llu  packets %llu  draws %llu\n",
                    (unsigned long long)(frame / 60),
                    (unsigned long long)(swaps - swapsBefore),
                    (unsigned long long)frame,
                    (unsigned long long)stats.filesOpened.load(),
                    stats.fileBytesRead.load() / 1048576.0,
                    (unsigned long long)stats.threadsCreated.load(),
                    stats.bytesAllocated.load() / 1048576.0,
                    (unsigned long long)stats.audioFrames.load(),
                    (unsigned long long)gpu.packets,
                    (unsigned long long)gpu.draws);
                swapsBefore = swaps;
                fflush(stdout);
            }

            std::this_thread::sleep_until(nextFrame);
        }
    }

    void StartVideoThread()
    {
        VideoState& video = Video();
        bool expected = false;
        if (!video.running.compare_exchange_strong(expected, true))
            return;
        video.thread = std::thread(VideoThread);
        video.commandThread = std::thread(CommandThread);
    }
}

// VOID VdInitializeEngines(ULONG unk, VOID* callback, ULONG unk, ULONG* unk, ULONG* unk)
PPC_FUNC(__imp__VdInitializeEngines)
{
    Kernel::CountImport("VdInitializeEngines");
    printf("video: engines initialised (no GPU behind them, nothing will be drawn)\n");
    StartVideoThread();
}

// VOID VdShutdownEngines(void)
PPC_FUNC(__imp__VdShutdownEngines)
{
    Kernel::CountImport("VdShutdownEngines");
    VideoState& video = Video();
    if (video.running.exchange(false) && video.thread.joinable())
        video.thread.join();
        if (video.commandThread.joinable()) video.commandThread.join();
    printf("video: engines shut down after %llu frames\n",
        (unsigned long long)video.frames.load());
}

// VOID VdInitializeRingBuffer(ULONG address, ULONG sizeLog2)
PPC_FUNC(__imp__VdInitializeRingBuffer)
{
    Kernel::CountImport("VdInitializeRingBuffer");
    // The second argument is the log2 of the size in quadwords, which is how
    // CP_RB_CNTL encodes it, so the ring is 8 << log2 bytes. Reading it as a
    // plain byte count makes the ring eight times too small, and the read
    // pointer then wraps before it can ever reach the value the title is
    // spinning for.
    VideoState& video = Video();

    // A new ring starts empty: the command processor is reset with it, so
    // both pointers are zero. This used to keep the old read pointer and the
    // old write pointer register, and the title initialises the ring twice at
    // start up; if the command thread ran between the second initialisation
    // and the title's first write pointer, it walked the new ring from the
    // old read position to the old write position, through whatever the
    // allocator had left there. Executing 0xCDCDCDCD as packets was the
    // start of every "GPU is hung". The lock keeps the command thread out
    // while the three values change together.
    std::lock_guard<std::mutex> lock(video.ringMutex);

    // Whatever the old ring still holds is carried out first. The console
    // is idle by the time a title re-initialises its ring, because the title
    // waited for it to be; here the command thread may simply not have got
    // to the tail yet, and resetting the pointers over it dropped those
    // packets, fences included, and the title then waited for them forever.
    {
        const uint32_t oldRing = video.ringBuffer.load();
        const uint32_t oldDwords = video.ringBufferSize.load() / 4;
        const uint32_t oldRead = video.readPointer.load();
        const uint32_t oldWrite = Gpu::WritePointer();
        if (oldRing != 0 && oldDwords != 0 && oldRead != oldWrite && oldWrite < oldDwords)
        {
            const uint32_t consumed = Gpu::ProcessRing(oldRing, oldDwords, oldRead, oldWrite);
            printf("video: the previous ring was drained from dword %u to %u before "
                   "being replaced\n", oldRead, consumed);
        }
    }

    Gpu::WriteRegister(Gpu::RegisterWritePointer, 0);
    video.readPointer.store(0);
    video.ringBuffer.store(ctx.r3.u32);
    video.ringBufferSize.store(8u << (ctx.r4.u32 & 0x1F));
    printf("video: ring buffer at 0x%08X, %u bytes (%u dwords), pointers reset\n",
        video.ringBuffer.load(), video.ringBufferSize.load(),
        video.ringBufferSize.load() / 4);
    fflush(stdout);
}

// VOID VdEnableRingBufferRPtrWriteBack(ULONG address, ULONG blockSize)
PPC_FUNC(__imp__VdEnableRingBufferRPtrWriteBack)
{
    Kernel::CountImport("VdEnableRingBufferRPtrWriteBack");
    printf("video: read pointer write back at 0x%08X, block size %u\n",
        ctx.r3.u32, ctx.r4.u32);
    // The address where the GPU is expected to publish how far it has read,
    // in dwords. The command thread writes its read pointer there after
    // every walk of the ring.
    Video().readPointerWriteBack.store(ctx.r3.u32);
    Guest::WritePhysical32(ctx.r3.u32 & ~3u, Video().readPointer.load());
}

// VOID VdSetSystemCommandBufferGpuIdentifierAddress(ULONG address)
PPC_FUNC(__imp__VdSetSystemCommandBufferGpuIdentifierAddress)
{
    Kernel::CountImport("VdSetSystemCommandBufferGpuIdentifierAddress");
    printf("video: gpu identifier address 0x%08X\n", ctx.r3.u32);
    fflush(stdout);
    Video().gpuIdentifier.store(ctx.r3.u32);
    if (ctx.r3.u32 != 0)
        Guest::Write32(base, ctx.r3.u32, 0);
}

// VOID VdSetGraphicsInterruptCallback(VOID* callback, ULONG context)
PPC_FUNC(__imp__VdSetGraphicsInterruptCallback)
{
    Kernel::CountImport("VdSetGraphicsInterruptCallback");
    Video().interruptCallback.store(ctx.r3.u32);
    Video().interruptContext.store(ctx.r4.u32);
    printf("video: interrupt callback at 0x%08X\n", ctx.r3.u32);
}

// VOID VdCallGraphicsNotificationRoutines(ULONG value)
PPC_FUNC(__imp__VdCallGraphicsNotificationRoutines) {}

// VOID VdGetSystemCommandBuffer(ULONG* bufferOut, ULONG* valueOut)
PPC_FUNC(__imp__VdGetSystemCommandBuffer)
{
    Kernel::CountImport("VdGetSystemCommandBuffer");
    // Two addresses in physical memory: the system command buffer, and the
    // block of GPU state the driver keeps beside it. The kernel hands back
    // two pointers into the area it reserves for the GPU near the top of RAM,
    // and the driver works relative to both. This used to answer the second
    // one with zero, and the driver then wrote its fences and state relative
    // to zero: a fence to physical 0x100, which is where the title had put
    // its job queue, and a corrupt job descriptor from then on. The two are
    // laid out the way the kernel lays them out, sixteen bytes apart, in a
    // block nothing else is given.
    static uint32_t block = 0;
    if (block == 0)
    {
        block = Guest::AllocatePhysical(64u << 10);
        printf("video: system command buffer at 0x%08X\n", block);
        fflush(stdout);
    }

    if (ctx.r3.u32 != 0) Guest::Write32(base, ctx.r3.u32, block + 0x900);
    if (ctx.r4.u32 != 0) Guest::Write32(base, ctx.r4.u32, block + 0x910);
}

// VOID VdInitializeScalerCommandBuffer(...)
PPC_FUNC(__imp__VdInitializeScalerCommandBuffer)
{
    Kernel::CountImport("VdInitializeScalerCommandBuffer");
    // Returns the number of words written into the caller's buffer.
    ctx.r3.u32 = 0;
}

// VOID VdSwap(...)
// The title's wait for the GPU to pass a point in its indirect buffer pool,
// sub_822F16A0(device, end, lap): it waits while the GPU's last fence, the
// second word of the write-back block, is on an earlier lap and behind
// `end`. The arguments are kept while the wait lasts, for the stall report:
// a wait that never ends says exactly which fence the GPU never wrote.
namespace
{
    struct PoolWait { uint32_t device, end, lap; int64_t since; };
    std::mutex g_poolWaitMutex;
    std::map<uint32_t, PoolWait> g_poolWaits;
}

extern "C" PPC_FUNC(__imp__sub_822F16A0);
PPC_FUNC(sub_822F16A0)
{
    const uint32_t osId = GetCurrentThreadId();
    {
        std::lock_guard<std::mutex> lock(g_poolWaitMutex);
        g_poolWaits[osId] = { ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, NowMilliseconds() };
    }
    const int64_t started = NowMilliseconds();
    Timeline::Mark("gpu wait", ctx.r4.u32, ctx.r5.u32);
    __imp__sub_822F16A0(ctx, base);
    Timeline::Mark("gpu waited", ctx.r4.u32, ctx.r5.u32);
    PoolTrace::Waited(uint64_t(NowMilliseconds() - started) * 1000000ull);
    std::lock_guard<std::mutex> lock(g_poolWaitMutex);
    g_poolWaits.erase(osId);
}

void Kernel::ReportPoolWaits()
{
    std::lock_guard<std::mutex> lock(g_poolWaitMutex);
    if (g_poolWaits.empty()) return;
    printf("\n");
    printf("waits for the GPU to pass a point in the indirect buffer pool:\n");
    for (const auto& entry : g_poolWaits)
    {
        const PoolWait& wait = entry.second;
        const uint32_t block = Guest::Read32(Guest::Base, wait.device + 10768);
        const uint32_t word = block ? Guest::Read32(Guest::Base, block + 4) : 0;
        printf("  thread %-5u wants the GPU past 0x%08X on lap %u (%u); the GPU's word is "
               "0x%08X (lap %u), %llu ms so far\n",
            entry.first, wait.end, wait.lap, wait.lap & 3, word, word & 3,
            (unsigned long long)(NowMilliseconds() - wait.since));
    }
}

PPC_FUNC(__imp__VdSwap)
{
    Kernel::CountImport("VdSwap");
    // VdSwap is where the title hands over a finished frame. Its arguments
    // name the buffer, so the first few are reported in full: that address is
    // what the window would present if anything had rendered into it.
    const uint64_t count = Video().swaps.fetch_add(1) + 1;
    Kernel::Stats().swaps.store(count, std::memory_order_relaxed);
    // The fourth argument points at the texture fetch constant that describes
    // the front buffer: its address, its size and its format. That is the one
    // piece of information the window needs to show a real frame, so it is
    // decoded here and handed over.
    const uint32_t fetchConstant = ctx.r4.u32;

    // Every argument, the first few times. Only the fetch constant was ever
    // read, and a swap the console performs is more than a picture: the first
    // argument is where in the ring buffer the driver expects a swap packet to
    // be written, and one of the others may well be where it expects the swap
    // to be acknowledged, which is the address a wait has been abandoned on
    // after every first frame of every run.
    if (count <= 3)
    {
        printf("video: VdSwap arguments r3..r10:");
        for (int i = 3; i <= 10; i++)
            printf(" %08X", Kernel::Register(ctx, i));
        printf("\n");
        const uint32_t buffer = ctx.r3.u32;
        if (buffer != 0)
        {
            printf("video:   at r3, the buffer holds:");
            for (int i = 0; i < 8; i++)
                printf(" %08X", Guest::Read32(base, buffer + i * 4));
            printf("\n");
        }
        printf("video:   ring write pointer register reads %u\n",
            Gpu::WritePointer());

        // What the stack arguments point at. On the console VdSwap is the
        // kernel's, and a kernel call that takes four pointers into the
        // caller's stack is writing results into them or reading a request
        // out of them; either way the words there say what the driver expects
        // the swap to do beyond changing the picture.
        for (int reg = 5; reg <= 9; reg++)
        {
            const uint32_t at = Kernel::Register(ctx, reg);
            if (at < 0x10000 || at >= 0xC0000000) continue;
            printf("video:   r%d 0x%08X ->", reg, at);
            for (int i = 0; i < 8; i++) printf(" %08X", Guest::Read32(base, at + i * 4));
            printf("\n");
        }
        fflush(stdout);
    }

    if (fetchConstant != 0)
    {
        const uint32_t word0 = Guest::Read32(base, fetchConstant + 0);
        const uint32_t word1 = Guest::Read32(base, fetchConstant + 4);
        const uint32_t word2 = Guest::Read32(base, fetchConstant + 8);

        // The address is in the upper bits of the second word, in 4 KB units.
        const uint32_t surface = (word1 & 0xFFFFF000);
        const uint32_t width  = ((word2 & 0x00001FFF) + 1);
        const uint32_t height = (((word2 >> 13) & 0x00001FFF) + 1);

        if (count <= 3)
        {
            printf("video: VdSwap %llu fetch constant %08X %08X %08X\n",
                (unsigned long long)count, word0, word1, word2);
            printf("video:   surface 0x%08X, %ux%u\n", surface, width, height);

            bool anything = false;
            for (uint32_t i = 0; i < 4096 && !anything; i += 4)
                if (Guest::Read32(Guest::Base, Guest::PhysicalAlias(surface) + i) != 0)
                    anything = true;
            printf("video:   the surface is %s\n",
                anything ? "not empty" : "entirely zero, nothing was rendered into it");
            fflush(stdout);
        }

        // The swap goes into the stream, not on the screen here.
        //
        // The driver reserves sixty four words of its command buffer for
        // this call, hands it the first of them in r3 and moves its cursor
        // to the last of them when it returns (its cursor is the last word
        // written, and every writer stores ahead of it): on the console the
        // kernel fills them with the packets that change the picture. Nothing was written
        // into them here, and the command processor read whatever the
        // memory held: zeros at first, which parse as writes of register
        // zero and leave the stream misaligned by a word at the end of the
        // run, and later the packets of whatever the memory was used for
        // before. Draws of one point, interrupts nobody had asked for,
        // waits on the display scaler's registers with a mask the reference
        // could never satisfy: those came from here, and a wait like that
        // was every stall of five seconds.
        //
        // So: one packet the command processor knows, and the rest filler,
        // and the picture changes when the GPU reaches it, after the frame's
        // draws, rather than when the CPU finished recording them.
        if (surface != 0 && width > 1 && height > 1 && width <= 4096 && height <= 4096)
        {
            const uint32_t buffer = ctx.r3.u32;
            if (buffer != 0)
            {
                constexpr uint32_t Words = 64;
                uint32_t offset = 0;
                auto put = [&](uint32_t value) { Guest::Write32(base, buffer + offset * 4, value); offset++; };
                put(0xC0000000u | (3u << 16) | (0x64u << 8));   // SWAP, four words
                put(0x50415753u);                   // 'SWAP'
                put(surface);
                put(width);
                put(height);
                while (offset < Words) put(0x80000000u);
            }
            else
            {
                Window::SetFrontBuffer(Guest::PhysicalAlias(surface), width, height);
                D3D11Backend::Swap(surface, width, height);
            }
        }
    }
    ctx.r3.u32 = 0;
}

// --- Display queries -------------------------------------------------------

// VOID VdQueryVideoMode(X_VIDEO_MODE* mode)
PPC_FUNC(__imp__VdQueryVideoMode)
{
    Kernel::CountImport("VdQueryVideoMode");
    const uint32_t mode = ctx.r3.u32;
    if (mode == 0) return;

    // The same mode XGetVideoMode reports, so the title never sees the two
    // disagree.
    memset(Guest::Ptr(mode), 0, 48);
    Guest::Write32(base, mode + 0x00, 1280);
    Guest::Write32(base, mode + 0x04, 720);
    Guest::Write32(base, mode + 0x08, 0);
    Guest::Write32(base, mode + 0x0C, 1);
    Guest::Write32(base, mode + 0x10, 1);
    Guest::Write32(base, mode + 0x14, 0x42700000);   // 60.0f
    Guest::Write32(base, mode + 0x18, 1);
}

// ULONG VdQueryVideoFlags(void)
PPC_FUNC(__imp__VdQueryVideoFlags)
{
    Kernel::CountImport("VdQueryVideoFlags");
    // Widescreen and high definition.
    ctx.r3.u32 = 0x00000003;
}

// VOID VdGetCurrentDisplayInformation(void* information)
PPC_FUNC(__imp__VdGetCurrentDisplayInformation)
{
    Kernel::CountImport("VdGetCurrentDisplayInformation");
    const uint32_t information = ctx.r3.u32;
    if (information == 0) return;

    memset(Guest::Ptr(information), 0, 0x58);
    Guest::Write32(base, information + 0x00, 1280);
    Guest::Write32(base, information + 0x04, 720);
}

// VOID VdGetCurrentDisplayGamma(ULONG* unk, float* gamma)
PPC_FUNC(__imp__VdGetCurrentDisplayGamma)
{
    Kernel::CountImport("VdGetCurrentDisplayGamma");
    if (ctx.r3.u32 != 0) Guest::Write32(base, ctx.r3.u32, 2);
    if (ctx.r4.u32 != 0) Guest::Write32(base, ctx.r4.u32, 0x40133333);   // 2.3f
}

// VOID VdSetDisplayMode(ULONG mode)
PPC_FUNC(__imp__VdSetDisplayMode) { ctx.r3.u32 = 0; }

// BOOL VdPersistDisplay(ULONG unk, ULONG* out)
PPC_FUNC(__imp__VdPersistDisplay)
{
    Kernel::CountImport("VdPersistDisplay");
    if (ctx.r4.u32 != 0) Guest::Write32(base, ctx.r4.u32, 0);
    ctx.r3.u32 = 1;
}

// BOOL VdIsHSIOTrainingSucceeded(void)
PPC_FUNC(__imp__VdIsHSIOTrainingSucceeded) { ctx.r3.u32 = 1; }

// VOID VdEnableDisableClockGating(ULONG enable)
PPC_FUNC(__imp__VdEnableDisableClockGating) {}

// ULONG VdRetrainEDRAM(...)
PPC_FUNC(__imp__VdRetrainEDRAM) { ctx.r3.u32 = 0; }

// ULONG VdRetrainEDRAMWorker(...)
PPC_FUNC(__imp__VdRetrainEDRAMWorker) { ctx.r3.u32 = 0; }

uint32_t Gpu::InterruptContext()
{
    return Video().interruptContext.load();
}
