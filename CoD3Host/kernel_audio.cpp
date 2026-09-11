// Xbox 360 kernel imports: audio.
//
// Two layers. XMA is the console's hardware codec: the title hands it
// compressed blocks through a context structure and reads decoded samples back
// out. XAudio is the mixer the decoded voices are submitted to.
//
// XMA is not implemented: the contexts are accepted and tracked so the title's
// own bookkeeping stays consistent, and buffers are reported as consumed so it
// never waits for the codec. Nothing compressed is decoded, so anything the
// title played through that hardware is silent.
//
// XAudio is. The frames the title mixes itself are handed to the machine's
// sound device, which is everything except the XMA voices.

#include "kernel.h"
#include "audio_out.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>

#include <Windows.h>

namespace
{
    constexpr uint32_t X_ERROR_SUCCESS = 0x00000000;

    // Each context is a block of guest memory the title also reads directly.
    constexpr uint32_t ContextSize = 64;

    std::mutex g_audioMutex;
    std::map<uint32_t, bool> g_contexts;   // address -> enabled

    // Where each context's description lives, as the title handed it over.
    std::map<uint32_t, uint32_t> g_initialisers;

    // A decoder that produces silence, but produces it on time.
    //
    // There is no XMA decoder here, and writing one is a piece of work on its
    // own. What the title needs before it can do anything at all, though, is
    // not the samples: it is the decoder moving. It hands over compressed
    // blocks, waits to be told they were taken, and waits for decoded output to
    // appear. Told that output is never ready, the film player reads its first
    // three hundred kilobytes of video and stops there for good.
    //
    // So the motions are gone through. Input blocks are taken, the write
    // position advances a block at a time, and the output buffer is left as the
    // title allocated it, which is zeroed: silence. The player runs, the film
    // plays, and the sound is missing rather than the picture.
    struct Decoder
    {
        uint32_t outputBuffer = 0;
        uint32_t outputBlocks = 0;
        uint32_t writeOffset = 0;
        uint32_t readOffset = 0;
        std::chrono::steady_clock::time_point lastBlock{};
    };
    std::map<uint32_t, Decoder> g_decoders;

    // The description the title gave, read back. The two buffer addresses and
    // the block count are the only fields anything here needs.
    Decoder& DecoderFor(uint8_t* base, uint32_t context)
    {
        Decoder& decoder = g_decoders[context];
        if (decoder.outputBlocks != 0) return decoder;

        auto found = g_initialisers.find(context);
        if (found == g_initialisers.end()) return decoder;

        const uint32_t parameters = found->second;
        decoder.outputBuffer = Guest::Read32(base, parameters + 20);
        decoder.outputBlocks = Guest::Read32(base, parameters + 24);
        if (decoder.outputBlocks == 0 || decoder.outputBlocks > 64)
            decoder.outputBlocks = 8;
        return decoder;
    }

    uint32_t g_registeredClients = 0;
    uint64_t g_submittedFrames = 0;

    // The audio pump.
    //
    // Registering a render driver client hands the console a callback, and the
    // audio hardware calls it every time it has finished a frame of samples so
    // the title can produce the next one. Registering it and never calling it
    // leaves the title's audio thread waiting for a frame boundary that never
    // arrives, and a title that waits for its own sound to start never gets
    // past the screen it is on.
    //
    // A frame is 256 samples at 48 kHz, so the callback is due every 5333
    // microseconds. Nothing is played: what matters here is that the clock
    // runs.
    std::atomic<uint32_t> g_callback{ 0 };
    std::atomic<uint32_t> g_callbackContext{ 0 };
    std::atomic<bool> g_pumpRunning{ false };
    std::atomic<uint64_t> g_callbacks{ 0 };
    std::thread g_pumpThread;

    void AudioPump()
    {
        // Guest code runs on this thread, so it needs a context and a stack of
        // its own, exactly like a thread the title created itself.
        const uint32_t stackTop = Guest::AllocateStack(128u << 10);
        if (stackTop == 0)
        {
            fprintf(stderr, "audio: no room for the callback stack\n");
            return;
        }

        alignas(0x40) PPCContext ctx{};
        ctx.r13.u32 = Guest::CreateThreadPointer(GetCurrentThreadId());
        ctx.fpscr.loadFromHost();

        auto next = std::chrono::steady_clock::now();
        while (g_pumpRunning.load(std::memory_order_acquire))
        {
            next += std::chrono::microseconds(5333);

            const uint32_t callback = g_callback.load();
            if (callback != 0)
            {
                if (PPCFunc* routine = Guest::Lookup(callback))
                {
                    ctx.r1.u32 = stackTop - 0x100;
                    ctx.r3.u32 = g_callbackContext.load();
                    routine(ctx, Guest::Base);
                    g_callbacks.fetch_add(1, std::memory_order_relaxed);
                }
                else
                {
                    printf("audio: the render callback at 0x%08X was not "
                           "recompiled, the pump cannot run\n", callback);
                    fflush(stdout);
                    g_callback.store(0);
                }
            }

            std::this_thread::sleep_until(next);
        }
    }

    void StartPump()
    {
        bool expected = false;
        if (!g_pumpRunning.compare_exchange_strong(expected, true)) return;
        g_pumpThread = std::thread(AudioPump);
    }
}

void Kernel::StopAudioPump()
{
    if (g_pumpRunning.exchange(false) && g_pumpThread.joinable())
        g_pumpThread.join();
}

uint64_t Kernel::AudioCallbacks() { return g_callbacks.load(); }

// --- XMA contexts ----------------------------------------------------------

// X_RESULT XMACreateContext(ULONG* contextOut, ...)
PPC_FUNC(__imp__XMACreateContext)
{
    Kernel::CountImport("XMACreateContext");
    std::lock_guard<std::mutex> lock(g_audioMutex);

    const uint32_t context = Guest::AllocateStack(ContextSize);
    if (context == 0) { ctx.r3.u32 = 0x8007000E; return; }   // E_OUTOFMEMORY

    const uint32_t address = context - ContextSize;
    memset(Guest::Ptr(address), 0, ContextSize);
    g_contexts[address] = false;

    if (ctx.r3.u32 != 0)
        Guest::Write32(base, ctx.r3.u32, address);
    ctx.r3.u32 = X_ERROR_SUCCESS;
}

// X_RESULT XMAReleaseContext(ULONG context)
PPC_FUNC(__imp__XMAReleaseContext)
{
    Kernel::CountImport("XMAReleaseContext");
    std::lock_guard<std::mutex> lock(g_audioMutex);
    g_contexts.erase(ctx.r3.u32);
    ctx.r3.u32 = X_ERROR_SUCCESS;
}

// X_RESULT XMAInitializeContext(ULONG context, XMA_CONTEXT_INIT* parameters)
//
// This used to clear the context and throw the parameters away, which is why
// every context read back as sixty four zero bytes: the title had described its
// buffers and been ignored. The description is what says where the compressed
// data is and where the decoded samples are to go, so nothing downstream could
// work without it.
PPC_FUNC(__imp__XMAInitializeContext)
{
    Kernel::CountImport("XMAInitializeContext");

    const uint32_t context = ctx.r3.u32;
    const uint32_t parameters = ctx.r4.u32;
    if (context == 0) { ctx.r3.u32 = X_ERROR_SUCCESS; return; }

    memset(Guest::Ptr(context), 0, ContextSize);

    {
        static std::atomic<int> announced{ 0 };
        if (announced.fetch_add(1) < 3 && parameters != 0)
        {
            printf("xma: context 0x%08X described from 0x%08X:\n  ",
                context, parameters);
            for (uint32_t word = 0; word < 12; word++)
                printf(" %08X", Guest::Read32(base, parameters + word * 4));
            printf("\n");
            fflush(stdout);
        }
    }

    // Kept as the title gave it, so the buffer addresses and sizes in it can be
    // read back when the decoder is asked to do something with them.
    if (parameters != 0)
    {
        std::lock_guard<std::mutex> lock(g_audioMutex);
        g_initialisers[context] = parameters;
    }

    ctx.r3.u32 = X_ERROR_SUCCESS;
}

// X_RESULT XMAEnableContext(ULONG context)
PPC_FUNC(__imp__XMAEnableContext)
{
    Kernel::CountImport("XMAEnableContext");

    // What the title put in the context before asking for it to be decoded.
    //
    // The fields are bit packed and the layout has to be read off a working
    // one rather than guessed: the buffer addresses and their sizes are in
    // here, and without them there is no way to hand back even silence in the
    // shape the caller expects.
    {
        static std::atomic<int> announced{ 0 };
        if (announced.fetch_add(1) < 3 && ctx.r3.u32 != 0)
        {
            printf("xma: context 0x%08X enabled, its %u bytes are\n",
                ctx.r3.u32, ContextSize);
            for (uint32_t word = 0; word < ContextSize / 4; word++)
            {
                if ((word % 8) == 0) printf(" ");
                printf(" %08X", Guest::Read32(base, ctx.r3.u32 + word * 4));
                if ((word % 8) == 7) printf("\n");
            }
            fflush(stdout);
        }
    }

    std::lock_guard<std::mutex> lock(g_audioMutex);
    g_contexts[ctx.r3.u32] = true;
    ctx.r3.u32 = X_ERROR_SUCCESS;
}

// X_RESULT XMADisableContext(ULONG context, BOOL wait)
PPC_FUNC(__imp__XMADisableContext)
{
    Kernel::CountImport("XMADisableContext");
    std::lock_guard<std::mutex> lock(g_audioMutex);
    g_contexts[ctx.r3.u32] = false;
    ctx.r3.u32 = X_ERROR_SUCCESS;
}

// --- Buffer state ----------------------------------------------------------
// The title fills an input buffer, marks it valid, and waits for the codec to
// clear the flag. Reporting every input as already consumed and every output
// as never ready keeps it feeding the decoder without ever blocking on one.

PPC_FUNC(__imp__XMAIsInputBuffer0Valid)  { ctx.r3.u32 = 0; }
PPC_FUNC(__imp__XMAIsInputBuffer1Valid)  { ctx.r3.u32 = 0; }
// Whether there is decoded audio waiting. There is, and it is silent.
PPC_FUNC(__imp__XMAIsOutputBufferValid)
{
    std::lock_guard<std::mutex> lock(g_audioMutex);

    auto enabled = g_contexts.find(ctx.r3.u32);
    if (enabled == g_contexts.end() || !enabled->second) { ctx.r3.u32 = 0; return; }

    Decoder& decoder = DecoderFor(base, ctx.r3.u32);
    if (decoder.outputBlocks == 0) { ctx.r3.u32 = 0; return; }

    // Blocks become ready at the rate real hardware would produce them, not at
    // the rate the title asks about them. A block is 128 samples at 48 kHz,
    // which is 2.67 milliseconds; handing them over as fast as they are asked
    // for runs the film's clock far ahead of itself.
    const auto now = std::chrono::steady_clock::now();
    if (decoder.lastBlock.time_since_epoch().count() == 0)
        decoder.lastBlock = now;

    while (now - decoder.lastBlock >= std::chrono::microseconds(2667))
    {
        decoder.lastBlock += std::chrono::microseconds(2667);
        const uint32_t next = (decoder.writeOffset + 1) % decoder.outputBlocks;
        if (next == decoder.readOffset) break;   // the title has not drained it
        decoder.writeOffset = next;
    }

    ctx.r3.u32 = (decoder.writeOffset != decoder.readOffset) ? 1u : 0u;
}

PPC_FUNC(__imp__XMASetInputBuffer0Valid) { ctx.r3.u32 = X_ERROR_SUCCESS; }
PPC_FUNC(__imp__XMASetInputBuffer1Valid) { ctx.r3.u32 = X_ERROR_SUCCESS; }
PPC_FUNC(__imp__XMASetOutputBufferValid) { ctx.r3.u32 = X_ERROR_SUCCESS; }

PPC_FUNC(__imp__XMAGetInputBufferReadOffset)  { ctx.r3.u32 = 0; }
PPC_FUNC(__imp__XMAGetOutputBufferReadOffset)
{
    std::lock_guard<std::mutex> lock(g_audioMutex);
    ctx.r3.u32 = DecoderFor(base, ctx.r3.u32).readOffset;
}

PPC_FUNC(__imp__XMAGetOutputBufferWriteOffset)
{
    std::lock_guard<std::mutex> lock(g_audioMutex);
    ctx.r3.u32 = DecoderFor(base, ctx.r3.u32).writeOffset;
}

PPC_FUNC(__imp__XMASetInputBufferReadOffset)  { ctx.r3.u32 = X_ERROR_SUCCESS; }
PPC_FUNC(__imp__XMASetOutputBufferReadOffset)
{
    std::lock_guard<std::mutex> lock(g_audioMutex);
    Decoder& decoder = DecoderFor(base, ctx.r3.u32);
    if (decoder.outputBlocks != 0) decoder.readOffset = ctx.r4.u32 % decoder.outputBlocks;
    ctx.r3.u32 = X_ERROR_SUCCESS;
}

// --- XAudio ----------------------------------------------------------------

// X_RESULT XAudioRegisterRenderDriverClient(ULONG* callback, ULONG* clientOut)
PPC_FUNC(__imp__XAudioRegisterRenderDriverClient)
{
    Kernel::CountImport("XAudioRegisterRenderDriverClient");

    // The first argument points at two words: the function to call, and the
    // value to call it with.
    if (ctx.r3.u32 != 0)
    {
        const uint32_t callback = Guest::Read32(base, ctx.r3.u32);
        const uint32_t context = Guest::Read32(base, ctx.r3.u32 + 4);
        g_callback.store(callback);
        g_callbackContext.store(context);
        printf("audio: render callback at 0x%08X, context 0x%08X, "
               "pumping at 187 Hz\n", callback, context);
        fflush(stdout);
        StartPump();
    }

    std::lock_guard<std::mutex> lock(g_audioMutex);
    if (ctx.r4.u32 != 0)
        Guest::Write32(base, ctx.r4.u32, ++g_registeredClients);
    ctx.r3.u32 = X_ERROR_SUCCESS;
}

// X_RESULT XAudioUnregisterRenderDriverClient(ULONG client)
PPC_FUNC(__imp__XAudioUnregisterRenderDriverClient)
{
    Kernel::CountImport("XAudioUnregisterRenderDriverClient");
    g_callback.store(0);
    ctx.r3.u32 = X_ERROR_SUCCESS;
}

// X_RESULT XAudioSubmitRenderDriverFrame(ULONG client, VOID* samples)
PPC_FUNC(__imp__XAudioSubmitRenderDriverFrame)
{
    Kernel::CountImport("XAudioSubmitRenderDriverFrame");

    // The frame the title just finished mixing, played. Success is reported
    // whatever happens to it: a title that waits for its own audio to drain
    // stops dead, so a frame the device could not take is dropped quietly.
    Audio::SubmitFrame(ctx.r4.u32);

    std::lock_guard<std::mutex> lock(g_audioMutex);
    g_submittedFrames++;
    Kernel::Stats().audioFrames.fetch_add(1, std::memory_order_relaxed);
    ctx.r3.u32 = X_ERROR_SUCCESS;
}

// X_RESULT XAudioGetVoiceCategoryVolume(ULONG category, float* volume)
PPC_FUNC(__imp__XAudioGetVoiceCategoryVolume)
{
    Kernel::CountImport("XAudioGetVoiceCategoryVolume");
    if (ctx.r4.u32 != 0)
        Guest::Write32(base, ctx.r4.u32, 0x3F800000);   // 1.0f
    ctx.r3.u32 = X_ERROR_SUCCESS;
}

// ULONG XAudioGetVoiceCategoryVolumeChangeMask(ULONG driver, ULONG* mask)
PPC_FUNC(__imp__XAudioGetVoiceCategoryVolumeChangeMask)
{
    Kernel::CountImport("XAudioGetVoiceCategoryVolumeChangeMask");
    if (ctx.r4.u32 != 0)
        Guest::Write32(base, ctx.r4.u32, 0);   // nothing changed
    ctx.r3.u32 = X_ERROR_SUCCESS;
}
