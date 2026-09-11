// The GPU side of the runtime: the register file the graphics driver writes to,
// and the command processor that reads what it queues.
//
// The driver's contract is small enough to state. It allocates a ring buffer,
// tells the GPU where it is, writes command packets into it, advances a write
// pointer register, and waits for the GPU to publish how far it has read. Some
// packets ask for an interrupt when they complete.
//
// This implements that bookkeeping and decodes the packet stream. Nothing is
// rasterised: a draw packet is counted and skipped. Turning those into pictures
// needs a shader translator and a graphics backend, which is the work after
// this.

// Ahead of kernel.h, which pulls in ppc_context.h: the memory macros there are
// guarded with #ifndef, so this file has to define its versions first or it
// would redefine them.
#include "cod3_mmio.h"

// The recompiled code has its time base instruction redirected by that header.
// This file is host code and includes the Windows headers, which declare the
// intrinsic it replaces, so the redirection is undone here.
#undef __rdtsc

#include "kernel.h"
#include "gpu.h"
#include <cstdlib>
#include "scheduler.h"
#include "sampler.h"
#include "edram.h"
#include "shaders.h"
#include "raster.h"

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
    std::mutex g_registerMutex;
    std::map<uint32_t, uint32_t> g_registers;   // keyed by aperture byte address

    std::mutex g_statisticsMutex;
    Gpu::Statistics g_statistics;

    // What actually flows through the pipe, by packet type. A frame of GPU
    // work is mostly register writes, so seeing which type 3 opcodes appear is
    // the only way to tell a real command stream from a walk over noise.
    std::mutex g_histogramMutex;
    std::map<uint32_t, uint64_t> g_type3Opcodes;
    uint64_t g_type0 = 0, g_type2 = 0, g_type3 = 0;

    void CountPacket(uint32_t type, uint32_t opcode)
    {
        std::lock_guard<std::mutex> lock(g_histogramMutex);
        if (type == 0) g_type0++;
        else if (type == 2) g_type2++;
        else if (type == 3) { g_type3++; g_type3Opcodes[opcode]++; }
    }

    // Discovery. Every distinct aperture address the driver touches is reported
    // once. This is how RegisterWritePointer was found rather than guessed, and
    // it is what will name the next register when one is needed.
    std::mutex g_discoveryMutex;
    std::map<uint32_t, bool> g_seen;

    // A driver that is waiting for the GPU polls a register in a tight loop.
    // Counting reads per address makes that loop visible, and names the
    // register the title is waiting on.
    std::mutex g_readCountMutex;
    std::map<uint32_t, uint64_t> g_readCounts;
    std::atomic<int> g_reported{ 0 };
    constexpr int ReportLimit = 48;

    void ReportOnce(const char* kind, uint32_t address, uint32_t value)
    {
        {
            std::lock_guard<std::mutex> lock(g_discoveryMutex);
            if (g_seen.find(address) != g_seen.end()) return;
            g_seen[address] = true;
        }
        if (g_reported.fetch_add(1, std::memory_order_relaxed) >= ReportLimit) return;

        printf("mmio: first %s of 0x%08X", kind, address);
        if (value != 0xFFFFFFFF) printf(" = 0x%08X", value);
        printf("\n");
        fflush(stdout);
    }

    // --- PM4 packets -------------------------------------------------------
    // A packet header's top two bits pick the format. Type 0 writes a run of
    // registers, type 2 is filler, type 3 carries an opcode.
    // The type 3 opcodes this title uses. The names are the Xenos command
    // processor's own; the ones that were showing as bare numbers turned out to
    // be shader uploads and binning state, which is exactly what a renderer
    // needs to see.
    constexpr uint32_t OpRegRmw          = 0x21;
    constexpr uint32_t OpDrawIndx        = 0x22;
    constexpr uint32_t OpImLoad          = 0x27;
    constexpr uint32_t OpImLoadImmediate = 0x2B;
    constexpr uint32_t OpSetConstant     = 0x2D;
    constexpr uint32_t OpInvalidateState = 0x3B;
    constexpr uint32_t OpWaitRegMem      = 0x3C;
    constexpr uint32_t OpIndirectBuffer  = 0x3F;
    constexpr uint32_t OpCondWrite       = 0x45;
    constexpr uint32_t OpEventWrite      = 0x46;
    constexpr uint32_t OpMeInit          = 0x48;
    constexpr uint32_t OpInterrupt       = 0x54;
    constexpr uint32_t OpEventWriteShd   = 0x58;
    constexpr uint32_t OpDrawIndx2       = 0x36;
    constexpr uint32_t OpSetBinMaskLo    = 0x60;
    constexpr uint32_t OpSetBinMaskHi    = 0x61;
    constexpr uint32_t OpSetBinSelectLo  = 0x62;
    constexpr uint32_t OpSetBinSelectHi  = 0x63;
    constexpr uint32_t OpSwap            = 0x64;
    constexpr uint32_t OpNop             = 0x10;

    const char* Type3Name(uint32_t opcode)
    {
        switch (opcode)
        {
        case OpNop:             return "NOP";
        case OpRegRmw:          return "REG_RMW";
        case OpDrawIndx:        return "DRAW_INDX";
        case OpImLoad:          return "IM_LOAD";
        case OpImLoadImmediate: return "IM_LOAD_IMMEDIATE";
        case OpSetConstant:     return "SET_CONSTANT";
        case OpDrawIndx2:       return "DRAW_INDX_2";
        case OpInvalidateState: return "INVALIDATE_STATE";
        case OpWaitRegMem:      return "WAIT_REG_MEM";
        case OpIndirectBuffer:  return "INDIRECT_BUFFER";
        case OpCondWrite:       return "COND_WRITE";
        case OpEventWrite:      return "EVENT_WRITE";
        case OpMeInit:          return "ME_INIT";
        case OpInterrupt:       return "INTERRUPT";
        case OpEventWriteShd:   return "EVENT_WRITE_SHD";
        case OpSetBinMaskLo:    return "SET_BIN_MASK_LO";
        case OpSetBinMaskHi:    return "SET_BIN_MASK_HI";
        case OpSetBinSelectLo:  return "SET_BIN_SELECT_LO";
        case OpSetBinSelectHi:  return "SET_BIN_SELECT_HI";
        case OpSwap:            return "SWAP";
        default:                return nullptr;
        }
    }

    // What the title is actually drawing, accumulated as the stream is decoded.
    const char* PrimitiveName(uint32_t type)
    {
        switch (type)
        {
        case 1:  return "point list";
        case 2:  return "line list";
        case 3:  return "line strip";
        case 4:  return "triangle list";
        case 5:  return "triangle fan";
        case 6:  return "triangle strip";
        case 8:  return "rectangle list";
        case 13: return "quad list";
        default: return "unknown primitive";
        }
    }

    struct DrawInfo
    {
        std::map<uint32_t, uint64_t> primitives;   // type -> count
        uint64_t indices = 0;
        uint64_t vertexShaders = 0;
        uint64_t pixelShaders = 0;
        uint64_t shaderDwords = 0;
        uint32_t lastVertexShader = 0;
        uint32_t lastPixelShader = 0;
    };

    std::mutex g_drawMutex;
    DrawInfo g_draws;

    // DRAW_INDX carries a viz query word first; DRAW_INDX_2 starts straight at
    // the initiator. Both pack the primitive type in the low six bits and the
    // index count in the top sixteen.
    // The first few draw packets are printed raw. A decode that looks odd is
    // either the title doing something odd or the reader being off by a word,
    // and only the raw bytes tell the two apart.
    std::atomic<int> g_drawsDumped{ 0 };

    void DumpDraw(const char* which, uint32_t header, uint32_t w1, uint32_t w2)
    {
        if (g_drawsDumped.fetch_add(1) >= 2) return;
        printf("draw: %s header %08X payload %08X %08X -> count %u, prim %u\n",
            which, header, w1, w2, w1 >> 16, w1 & 0x3F);
        fflush(stdout);
    }

    // Where a draw is going. The registers that name the render target and the
    // resolve destination are not guessed: on the first draw of each primitive
    // type the whole render backend block is printed, and the addresses in it
    // are matched against what VdSwap reports as the front buffer.
    std::mutex g_stateMutex;
    std::map<uint32_t, int> g_statedPrimitives;

    void DumpDrawState(uint32_t primitive)
    {
        {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            if (g_statedPrimitives[primitive]++ >= 1) return;
        }

        const uint32_t mode = Gpu::ReadRegister(Gpu::ApertureBase + 0x2208 * 4) & 7;
        static const char* const modeNames[8] = {
            "ignore", "?", "?", "?", "colour and depth", "depth only", "copy", "?" };

        printf("\nstate at the first %s draw, render backend in %s mode:\n",
            PrimitiveName(primitive), modeNames[mode]);
        printf("  programs: vertex %016llx, pixel %016llx\n",
            (unsigned long long)Shaders::LastHash(false),
            (unsigned long long)Shaders::LastHash(true));

        // The float constant file holds shader inputs. Vertex programs read the
        // first 256 entries and pixel programs the second 256, so a pixel
        // program's c0 is entry 256. A program that only exports a constant
        // colour is reading exactly this.
        for (int half = 0; half < 2; half++)
        {
            printf("  %s c0..c1:", half == 0 ? "vertex" : "pixel ");
            for (uint32_t i = 0; i < 8; i++)
            {
                const uint32_t bits = Gpu::ReadRegister(
                    Gpu::ApertureBase + (0x4000 + half * 1024 + i) * 4);
                float value;
                memcpy(&value, &bits, 4);
                printf(" %.4f", value);
            }
            printf("\n");
        }

        // What the draw reads its vertices from, and the first of them.
        const uint32_t fetch0 = Gpu::ReadRegister(Gpu::ApertureBase + 0x4800 * 4);
        const uint32_t fetch1 = Gpu::ReadRegister(Gpu::ApertureBase + 0x4801 * 4);
        if ((fetch0 & 3) == 3)
        {
            const uint32_t address = Guest::PhysicalAlias(fetch0 & ~3u);
            const uint32_t dwords = (fetch1 >> 2) & 0x3FFFFF;
            printf("  vertices at 0x%08X, %u dwords:", address, dwords);
            for (uint32_t i = 0; i < 12 && i < dwords; i++)
            {
                const uint32_t bits = Guest::Read32(Guest::Base, address + i * 4);
                float value;
                memcpy(&value, &bits, 4);
                printf(" %.3f", value);
            }
            printf("\n");
        }


        // 0x2000 to 0x23FF is the render backend: surface, colour and depth
        // info, mode control, and the copy block that names the resolve
        // destination. Everything non zero is printed, because which of these
        // matters is exactly what has to be worked out.
        // The registers that decide where and how the draw lands. These were
        // found by dumping the whole file at a draw and matching values against
        // things already known: 0x210F to 0x2112 held 520, 520, -312, 312,
        // which is a viewport for a 1040 by 624 target, and 0x2319 held the
        // address VdSwap names as the front buffer.
        static const struct { uint32_t index; const char* name; } interesting[] = {
            { 0x2000, "surface pitch and samples" },
            { 0x2001, "colour buffer in EDRAM" },
            { 0x2002, "depth buffer in EDRAM" },
            { 0x2082, "window scissor" },
            { 0x210F, "viewport x scale" },
            { 0x2110, "viewport x offset" },
            { 0x2111, "viewport y scale" },
            { 0x2112, "viewport y offset" },
            { 0x2180, "shader register counts" },
            { 0x2200, "depth control" },
            { 0x2208, "render backend mode" },
            { 0x2318, "copy control" },
            { 0x2319, "copy destination" },
            { 0x231A, "copy destination pitch and height" },
            { 0x231B, "copy destination format" },
        };
        for (const auto& entry : interesting)
        {
            const uint32_t value = Gpu::ReadRegister(Gpu::ApertureBase + entry.index * 4);
            if (value != 0)
                printf("  %-34s %04X = %08X\n", entry.name, entry.index, value);
        }
        fflush(stdout);
    }

    void DecodeDraw(uint32_t initiator, uint32_t indexBase = 0, uint32_t indexWord = 0)
    {
        const uint32_t primitive = initiator & 0x3F;
        {
            std::lock_guard<std::mutex> lock(g_drawMutex);
            g_draws.primitives[primitive]++;
            g_draws.indices += (initiator >> 16);
        }
        DumpDrawState(primitive);

        // A resolve is spelled as a rectangle list draw with the render backend
        // in copy mode. It is the only draw in the stream that this runtime can
        // carry out completely, because it moves pixels rather than making them.
        const uint32_t mode = Gpu::ReadRegister(Gpu::ApertureBase + 0x2208 * 4) & 7;
        if (primitive == 8 && mode == 6) Edram::Resolve();
        else Raster::Draw(initiator, indexBase, indexWord);
    }

    // A fence.
    //
    // EVENT_WRITE_SHD tells the GPU to write a value into memory once it has
    // reached this point in the stream. The graphics driver then spins on that
    // memory to know the work is done. Counting the packet and moving on
    // leaves the driver waiting for a word that never changes, which is what
    // held the title on its loading screen with every worker thread parked.
    //
    // The address is physical and its low two bits are a byte order selector
    // rather than part of the address.
    std::atomic<uint64_t> g_fences{ 0 };
    std::atomic<uint64_t> g_fenceCounter{ 0 };

    // The last few packets, so a wait that is abandoned can be seen in the
    // company it keeps: what was queued just before it is what should have
    // satisfied it.
    struct Recent
    {
        uint32_t header = 0;
        uint32_t words[5] = {};
    };
    constexpr int RecentCount = 12;
    Recent g_recent[RecentCount];
    int g_recentAt = 0;

    void Remember(uint32_t header, const uint32_t* words, uint32_t available)
    {
        Recent& slot = g_recent[g_recentAt];
        g_recentAt = (g_recentAt + 1) % RecentCount;
        slot.header = header;
        for (uint32_t i = 0; i < 5; i++)
            slot.words[i] = (i < available) ? words[i] : 0;
    }

    void PrintRecent()
    {
        printf("  the packets just before it, oldest first:\n");
        for (int i = 0; i < RecentCount; i++)
        {
            const Recent& slot = g_recent[(g_recentAt + i) % RecentCount];
            if (slot.header == 0) continue;
            const uint32_t type = slot.header >> 30;
            if (type == 3)
                printf("    %-18s", Type3Name((slot.header >> 8) & 0x7F));
            else if (type == 0)
                printf("    reg 0x%04X x%-6u", slot.header & 0x7FFF,
                    ((slot.header >> 16) & 0x3FFF) + 1);
            else
                printf("    type %u            ", type);
            for (uint32_t w = 0; w < 5; w++) printf(" %08X", slot.words[w]);
            printf("\n");
        }
        fflush(stdout);
    }

    // Where conditional writes would go: how many were asked for, and how many
    // the condition allowed.
    std::mutex g_condTargetMutex;
    std::map<uint32_t, std::pair<uint64_t, uint64_t>> g_condTargets;

    // Where fences have gone, and how many to each.
    std::mutex g_fenceTargetMutex;
    std::map<uint32_t, uint64_t> g_fenceTargets;

    // Interrupts the command stream has asked for and nobody has raised yet.
    std::atomic<uint32_t> g_pendingInterrupts{ 0 };

    // Raising one where it stands in the stream. The handler is guest code
    // and needs the command thread's own context, which lives on the video
    // side; it installs this so the packet can be acted on the moment it is
    // decoded rather than when the buffer ends. That order matters: the
    // driver puts a wait immediately after the interrupt for a value the
    // handler writes, and a handler that runs after the wait has given up is
    // a handler that ran for nothing.
    void (*g_raiseInterrupt)() = nullptr;

    void RaiseInterruptNow()
    {
        if (g_raiseInterrupt != nullptr) g_raiseInterrupt();
        else g_pendingInterrupts.fetch_add(1, std::memory_order_relaxed);
    }

    void WriteFence(uint32_t initiator, uint32_t address, uint32_t value)
    {
        const uint32_t target = address & ~3u;
        if (target == 0) return;

        // With the top bit set the GPU writes its own progress counter rather
        // than the value the packet carries.
        const uint32_t written = ((initiator >> 31) & 1)
            ? uint32_t(g_fenceCounter.fetch_add(1, std::memory_order_relaxed))
            : value;

        Guest::WritePhysical32(target, written);
        g_fences.fetch_add(1, std::memory_order_relaxed);

        // Every destination, counted. The line below only ever printed the
        // first twelve of a run, which is enough to see where fences start
        // going and no use at all for seeing where they go later.
        {
            std::lock_guard<std::mutex> guard(g_fenceTargetMutex);
            g_fenceTargets[target]++;
        }

        static std::atomic<int> announced{ 0 };
        if (announced.fetch_add(1) < 12)
        {
            printf("gpu: fence write of 0x%08X to physical 0x%08X\n", written, target);
            fflush(stdout);
        }
    }

    // WAIT_REG_MEM holds the command processor until a value satisfies a
    // condition. It is how the title keeps one piece of work behind another,
    // and skipping it lets everything after it run early: fences are written
    // before the work they stand for has happened, and the driver is told its
    // work is done when it is not.
    //
    // Waiting here is bounded, and tightly. Everything earlier in the stream
    // has already happened by the time this runs, so a wait on the GPU's own
    // progress is satisfied at once; the ones that are not are waiting on the
    // guest, and holding the whole stream up for those starves the title of
    // frames. Two milliseconds was enough to do exactly that.
    std::atomic<uint64_t> g_waits{ 0 };
    std::atomic<uint64_t> g_waitTimeouts{ 0 };

    bool ConditionHolds(uint32_t function, uint32_t left, uint32_t reference)
    {
        switch (function)
        {
        case 0: return false;
        case 1: return left < reference;
        case 2: return left <= reference;
        case 3: return left == reference;
        case 4: return left != reference;
        case 5: return left >= reference;
        case 6: return left > reference;
        default: return true;
        }
    }

    void WaitForValue(uint32_t control, uint32_t pollAddress, uint32_t reference,
                      uint32_t mask)
    {
        const uint32_t function = control & 0x7;
        const bool memory = ((control >> 4) & 1) != 0;

        g_waits.fetch_add(1, std::memory_order_relaxed);

        // How long the command processor will wait for a value the title is
        // supposed to write. The console waits as long as it takes; giving up
        // early lets the next packet run against state that is not ready yet,
        // which is a corruption rather than a stall. The bound is here because
        // a wait that never ends would hang the whole run, and it can be moved
        // from the environment so the right value can be measured rather than
        // guessed.
        static const long limit = []() -> long {
            const char* text = getenv("COD3_GPUWAIT");
            const long value = (text != nullptr) ? strtol(text, nullptr, 10) : 200;
            return (value > 0) ? value : 200;
        }();

        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::microseconds(limit);
        for (;;)
        {
            const uint32_t current = memory
                ? Guest::Read32(Guest::Base, Guest::PhysicalAlias(pollAddress & ~3u))
                : Gpu::ReadRegister(Gpu::ApertureBase + (pollAddress & 0xFFFF) * 4);

            if (ConditionHolds(function, current & mask, reference)) return;
            if (std::chrono::steady_clock::now() > deadline)
            {
                g_waitTimeouts.fetch_add(1, std::memory_order_relaxed);

                // What it was waiting for, once per distinct address. A wait
                // the command processor abandons is the title's own driver
                // being told the work is done when it is not, and that is what
                // it eventually reports as a hung GPU. Naming the address and
                // the value says which fence never arrived.
                {
                    static std::mutex mutex;
                    static std::map<uint32_t, bool> seen;
                    bool first = false;
                    {
                        std::lock_guard<std::mutex> guard(mutex);
                        // Keyed by address and value together, so a wait on
                        // the same fence for the next number is still shown.
                        const uint32_t key = pollAddress ^ (reference << 20);
                        if (seen.size() < 16 && seen.find(key) == seen.end())
                        {
                            seen[key] = true;
                            first = true;
                        }
                    }
                    if (first)
                    {
                        printf("gpu: gave up waiting on %s 0x%08X: it holds "
                               "0x%08X, the wait wants 0x%08X under mask "
                               "0x%08X, test %u\n",
                            memory ? "memory" : "register", pollAddress,
                            current, reference, mask, function);
                        PrintRecent();
                    }
                }
                return;
            }
            std::this_thread::yield();
        }
    }

    // COND_WRITE polls a register or memory until a condition holds and then
    // writes a value. The command processor here runs a buffer to completion
    // before the guest looks at anything it produced, so the condition is
    // evaluated once and the write happens if it holds.
    void ConditionalWrite(uint32_t control, uint32_t pollAddress, uint32_t reference,
                          uint32_t mask, uint32_t writeAddress, uint32_t writeValue)
    {
        const uint32_t function = control & 0x7;
        const bool memory = ((control >> 4) & 1) != 0;

        // Where the write goes is a separate question from where the value
        // being tested comes from, and it has its own bit. Every one of these
        // in this title writes to a register, and every one of them was being
        // written to guest memory instead: two thousand writes a run to
        // physical address 0x1920, which is the first few kilobytes of the
        // address space and belongs to something else entirely.
        const bool writeToMemory = ((control >> 8) & 1) != 0;

        const uint32_t current = memory
            ? Guest::Read32(Guest::Base, Guest::PhysicalAlias(pollAddress & ~3u))
            : Gpu::ReadRegister(Gpu::ApertureBase + (pollAddress & 0xFFFF) * 4);

        const uint32_t left = current & mask;
        bool holds = false;
        switch (function)
        {
        case 0: holds = false; break;
        case 1: holds = left < reference; break;
        case 2: holds = left <= reference; break;
        case 3: holds = left == reference; break;
        case 4: holds = left != reference; break;
        case 5: holds = left >= reference; break;
        case 6: holds = left > reference; break;
        default: holds = true; break;
        }

        // Where a conditional write would go, whether or not the condition let
        // it. A write that never happens is invisible otherwise, and a
        // condition that is never true is exactly the shape of a missing piece.
        {
            std::lock_guard<std::mutex> guard(g_condTargetMutex);
            auto& entry = g_condTargets[
                writeToMemory ? (writeAddress & ~3u) : (writeAddress & 0xFFFF)];
            entry.first++;
            if (holds) entry.second++;
        }

        if (!holds) return;

        if (writeToMemory)
        {
            if ((writeAddress & ~3u) != 0)
                Guest::WritePhysical32(writeAddress & ~3u, writeValue);
        }
        else
        {
            Gpu::WriteRegister(
                Gpu::ApertureBase + (writeAddress & 0xFFFF) * 4, writeValue);
        }
    }

    // A shader upload. The first word says which pipeline stage, the second
    // packs where it goes and how long it is; the microcode follows inline.
    void DecodeShaderLoad(uint32_t typeWord, uint32_t startSize, uint32_t address)
    {
        const uint32_t sizeDwords = startSize & 0xFFFF;
        const bool pixel = (typeWord & 1) != 0;
        {
            std::lock_guard<std::mutex> lock(g_drawMutex);
            g_draws.shaderDwords += sizeDwords;
            if (pixel)
            {
                g_draws.pixelShaders++;
                g_draws.lastPixelShader = address;
            }
            else
            {
                g_draws.vertexShaders++;
                g_draws.lastVertexShader = address;
            }
        }
        Shaders::Capture(pixel, address, sizeDwords);
    }

    // IM_LOAD is the same upload by reference: the packet names where the
    // microcode already is instead of carrying it. The address is physical.
    void DecodeShaderLoadIndirect(uint32_t addressWord, uint32_t startSize)
    {
        DecodeShaderLoad(addressWord & 3,
                         startSize,
                         Guest::PhysicalAlias(addressWord & ~3u));
    }
}

// --- The aperture -----------------------------------------------------------

void Mmio::Store32(uint32_t address, uint32_t value)
{
    ReportOnce("write", address, value);

    // Through the same door the command stream uses. This used to drop the
    // value straight into the register map, which is fine for most registers
    // and wrong for the scratch ones: those are mirrored into memory on every
    // write, and the driver clears one from the CPU after each swap and then
    // waits, in the stream, for the clear to show up in memory. Written here
    // directly it never did, and the wait was abandoned every frame.
    Gpu::WriteRegister(address, value);
}

uint32_t Mmio::Load32(uint32_t address)
{
    // Guest code that spins on a register is spinning, not blocking, so this is
    // where it gets the chance to hand its hardware thread over.
    Scheduler::Checkpoint();
    ReportOnce("read", address, 0xFFFFFFFF);
    {
        std::lock_guard<std::mutex> lock(g_readCountMutex);
        g_readCounts[address]++;
    }
    std::lock_guard<std::mutex> lock(g_registerMutex);
    auto found = g_registers.find(address);
    return found != g_registers.end() ? found->second : 0;
}

void Gpu::SnapshotRegisters(uint32_t firstIndex, uint32_t count, uint32_t* out)
{
    // One walk of the map under the lock, instead of one lock per register.
    // The rasteriser reads registers for every pixel of every band on every
    // core, and a mutex taken that often is what the cores spend their time
    // on rather than the pixels.
    memset(out, 0, size_t(count) * sizeof(uint32_t));
    const uint32_t firstAddress = ApertureBase + firstIndex * 4;
    const uint32_t endAddress = firstAddress + count * 4;

    std::lock_guard<std::mutex> lock(g_registerMutex);
    for (auto it = g_registers.lower_bound(firstAddress);
         it != g_registers.end() && it->first < endAddress; ++it)
        out[(it->first - firstAddress) / 4] = it->second;
}

uint32_t Gpu::ReadRegister(uint32_t address)
{
    std::lock_guard<std::mutex> lock(g_registerMutex);
    auto found = g_registers.find(address);
    return found != g_registers.end() ? found->second : 0;
}

void Gpu::WriteRegister(uint32_t address, uint32_t value)
{
    uint32_t mirrorTo = 0;
    {
        std::lock_guard<std::mutex> lock(g_registerMutex);
        g_registers[address] = value;

        // The scratch registers are mirrored into memory.
        //
        // Eight registers at 0x578 are the command processor's scratch space,
        // and the hardware copies each one into memory whenever it changes:
        // SCRATCH_ADDR says where the block starts and SCRATCH_UMSK which of
        // the eight are copied. The driver writes them from the stream and
        // then waits, in the stream, for the copy to show up in memory.
        //
        // This is the write nothing else was making. After every swap the
        // driver waited at 0x1A780000 for a four and at 0x1A780004 for a one,
        // no packet targeted either, and the wait was abandoned; with the
        // fence writes just before it going to 0x1A770000, the block sixty
        // four kilobytes on had no obvious author. It is SCRATCH_REG0 and
        // SCRATCH_REG1, mirrored to where SCRATCH_ADDR points.
        const uint32_t index = (address - ApertureBase) / 4;
        if (index >= 0x578 && index < 0x580)
        {
            const uint32_t which = index - 0x578;
            const uint32_t mask = g_registers[ApertureBase + 0x1DC * 4];
            const uint32_t block = g_registers[ApertureBase + 0x1DD * 4];
            if (((mask >> which) & 1) != 0 && block != 0)
                mirrorTo = (block & ~3u) + which * 4;
        }
    }

    if (mirrorTo != 0)
    {
        Guest::WritePhysical32(mirrorTo, value);

        static std::atomic<int> announced{ 0 };
        if (announced.fetch_add(1) < 6)
        {
            printf("gpu: scratch register %u = 0x%08X, mirrored to physical "
                   "0x%08X\n", (address - ApertureBase) / 4 - 0x578, value, mirrorTo);
            fflush(stdout);
        }
    }
}

uint32_t Gpu::WritePointer() { return ReadRegister(RegisterWritePointer); }

Gpu::Statistics Gpu::Stats()
{
    std::lock_guard<std::mutex> lock(g_statisticsMutex);
    return g_statistics;
}

void Gpu::ReportPolling()
{
    std::lock_guard<std::mutex> lock(g_readCountMutex);
    uint32_t busiest = 0;
    uint64_t most = 0;
    for (const auto& [address, count] : g_readCounts)
        if (count > most) { most = count; busiest = address; }

    if (most < 1000) return;   // not a spin

    static uint64_t reportedAt = 0;
    if (most - reportedAt < 100000) return;
    reportedAt = most;

    printf("gpu: the driver has read register 0x%08X %llu times, it is polling "
           "for a value\n", busiest, (unsigned long long)most);
    fflush(stdout);
}

// --- The command processor --------------------------------------------------

namespace
{
    // Executes a run of packets that is not the ring: an indirect buffer names
    // an address and a length, and its contents are ordinary packets.
    void ExecuteBuffer(uint32_t base, uint32_t dwords, Gpu::Statistics& local, int depth);

    // The whole walk shares one deadline. Almost every draw in this title's
    // stream is inside an indirect buffer, so a budget that only covered the
    // ring's own packets was a budget on nothing: one buffer would run for
    // seconds and the vertical blank would not happen until it finished.
    thread_local std::chrono::steady_clock::time_point g_deadline{};
    thread_local bool g_haveDeadline = false;

    bool OutOfTime()
    {
        return g_haveDeadline && std::chrono::steady_clock::now() > g_deadline;
    }
}

uint32_t Gpu::ProcessRing(uint32_t ringBase, uint32_t ringSizeDwords,
                          uint32_t readPointer, uint32_t writePointer,
                          uint32_t budgetMicroseconds)
{
    const auto started = std::chrono::steady_clock::now();
    g_haveDeadline = budgetMicroseconds != 0;
    if (g_haveDeadline)
        g_deadline = started + std::chrono::microseconds(budgetMicroseconds);
    if (ringSizeDwords == 0 || writePointer == readPointer) return readPointer;
    if (writePointer >= ringSizeDwords) return readPointer;

    Statistics local{};
    uint32_t cursor = readPointer;
    int guard = 0;

    auto ReadDword = [&](uint32_t index) {
        return Guest::Read32(Guest::Base, ringBase + (index % ringSizeDwords) * 4);
    };

    while (cursor != writePointer)
    {
        // A walk that does not land exactly on the write pointer has lost
        // sync, and continuing would read the rest of the ring as commands. A
        // few dozen packets past the write pointer is already wrong.
        if (++guard > 4096)
        {
            static bool reported = false;
            if (!reported)
            {
                reported = true;
                printf("gpu: lost sync walking the ring, stopping at dword %u of %u\n",
                    cursor, ringSizeDwords);
                fflush(stdout);
            }
            return writePointer;
        }

        // Stopping part way is what the hardware looks like from the driver's
        // side anyway: the read pointer simply has not got there yet.
        if ((guard & 0xF) == 0 && OutOfTime()) break;

        const uint32_t header = ReadDword(cursor);
        const uint32_t type = header >> 30;
        uint32_t length = 1;

        if (type == 0)
        {
            const uint32_t count = ((header >> 16) & 0x3FFF) + 1;
            const uint32_t baseRegister = (header & 0x7FFF);
            const bool oneRegister = ((header >> 15) & 1) != 0;

            for (uint32_t i = 0; i < count; i++)
            {
                const uint32_t value = ReadDword(cursor + 1 + i);
                const uint32_t target = oneRegister ? baseRegister : baseRegister + i;
                WriteRegister(ApertureBase + target * 4, value);
            }
            local.registerWrites += count;
            length = 1 + count;
        }
        else if (type == 2)
        {
            length = 1;   // filler to the end of a ring wrap
        }
        else if (type == 3)
        {
            const uint32_t opcode = (header >> 8) & 0x7F;
            const uint32_t count = ((header >> 16) & 0x3FFF) + 1;
            length = 1 + count;

            {
                uint32_t words[5];
                const uint32_t available = count < 5 ? count : 5;
                for (uint32_t i = 0; i < available; i++)
                    words[i] = ReadDword(cursor + 1 + i);
                Remember(header, words, available);
            }

            switch (opcode)
            {
            case OpDrawIndx:
                local.draws++;
                DecodeDraw(ReadDword(cursor + 2), ReadDword(cursor + 3),
                           ReadDword(cursor + 4));
                break;
            case OpDrawIndx2:
                local.draws++;
                DecodeDraw(ReadDword(cursor + 1));
                break;
            case OpImLoadImmediate:
                DecodeShaderLoad(ReadDword(cursor + 1), ReadDword(cursor + 2),
                                 ringBase + ((cursor + 3) % ringSizeDwords) * 4);
                break;
            case OpImLoad:
                DecodeShaderLoadIndirect(ReadDword(cursor + 1), ReadDword(cursor + 2));
                break;
            case OpEventWriteShd:
                WriteFence(ReadDword(cursor + 1), ReadDword(cursor + 2),
                           ReadDword(cursor + 3));
                break;
            case OpWaitRegMem:
                WaitForValue(ReadDword(cursor + 1), ReadDword(cursor + 2),
                             ReadDword(cursor + 3), ReadDword(cursor + 4));
                break;
            case OpCondWrite:
                ConditionalWrite(ReadDword(cursor + 1), ReadDword(cursor + 2),
                                 ReadDword(cursor + 3), ReadDword(cursor + 4),
                                 ReadDword(cursor + 5), ReadDword(cursor + 6));
                break;
            case OpSwap:
                local.swaps++;
                break;
            case OpInterrupt:
                // The stream asking for the CPU to be interrupted.
                //
                // These were counted and dropped, two hundred and forty six of
                // them a run. The title's driver learns that work has finished
                // in the handler this raises, and a driver that is never told
                // anything finished eventually decides the GPU has stopped and
                // says so in as many words. It is raised where it appears in
                // the stream, after everything queued before it has been done.
                RaiseInterruptNow();
                break;
            case OpIndirectBuffer:
            {
                // The real command stream lives in these. Skipping them would
                // leave the driver waiting for work that was never done.
                const uint32_t target = ReadDword(cursor + 1);
                const uint32_t targetDwords = ReadDword(cursor + 2) & 0xFFFFF;
                ExecuteBuffer(Guest::PhysicalAlias(target), targetDwords, local, 1);
                break;
            }
            default:
                if (Type3Name(opcode) == nullptr) local.unknown++;
                break;
            }

            // Every type 3 packet is reported the first time it appears, which
            // is how the list above grows as the title exercises more of the
            // command set.
            static std::mutex opcodeMutex;
            static std::map<uint32_t, bool> seenOpcodes;
            bool first = false;
            {
                std::lock_guard<std::mutex> lock(opcodeMutex);
                if (seenOpcodes.find(opcode) == seenOpcodes.end())
                {
                    seenOpcodes[opcode] = true;
                    first = true;
                }
            }
            if (first)
            {
                const char* name = Type3Name(opcode);
                printf("gpu: first %s packet (opcode 0x%02X, %u dwords)\n",
                    name ? name : "unrecognised", opcode, count);
                fflush(stdout);
            }
        }
        else
        {
            // Type 1 is two register writes and is not used by this title. A
            // header that decodes as neither means the walk has lost sync.
            printf("gpu: lost sync at ring dword %u, header 0x%08X\n", cursor, header);
            fflush(stdout);
            return writePointer;
        }

        CountPacket(type, type == 3 ? ((header >> 8) & 0x7F) : 0);
        local.packets++;
        cursor = (cursor + length) % ringSizeDwords;
    }

    {
        std::lock_guard<std::mutex> lock(g_statisticsMutex);
        g_statistics.packets += local.packets;
        g_statistics.registerWrites += local.registerWrites;
        g_statistics.draws += local.draws;
        g_statistics.swaps += local.swaps;
        g_statistics.unknown += local.unknown;
        g_statistics.readPointer = cursor;
    }

    return cursor;
}

namespace
{
    void ExecuteBuffer(uint32_t base, uint32_t dwords, Gpu::Statistics& local, int depth)
    {
        // Indirect buffers can chain. A few levels is normal; more than that
        // means a corrupt address, and following it would read arbitrary guest
        // memory as commands.
        if (depth > 4 || base == 0 || dwords == 0 || dwords > (16u << 20)) return;

        static std::atomic<int> announced{ 0 };
        if (announced.fetch_add(1) < 3)
        {
            printf("gpu: indirect buffer at 0x%08X, %u dwords, starts:", base, dwords);
            for (uint32_t i = 0; i < 6 && i < dwords; i++)
                printf(" %08X", Guest::Read32(Guest::Base, base + i * 4));
            printf("\n");
            fflush(stdout);
        }

        uint32_t cursor = 0;
        uint32_t stepped = 0;
        while (cursor < dwords)
        {
            // A buffer is always run to the end. Abandoning one part way
            // through loses the fence writes that live at its end, and the
            // driver then waits for work it believes is still running: the
            // title went quiet after two hundred draws when this was tried.
            ++stepped;

            const uint32_t header = Guest::Read32(Guest::Base, base + cursor * 4);
            const uint32_t type = header >> 30;
            uint32_t length = 1;

            if (type == 0)
            {
                const uint32_t count = ((header >> 16) & 0x3FFF) + 1;
                const uint32_t baseRegister = header & 0x7FFF;
                const bool oneRegister = ((header >> 15) & 1) != 0;
                for (uint32_t i = 0; i < count; i++)
                {
                    if (cursor + 1 + i >= dwords) break;
                    const uint32_t value = Guest::Read32(Guest::Base, base + (cursor + 1 + i) * 4);
                    const uint32_t target = oneRegister ? baseRegister : baseRegister + i;
                    Gpu::WriteRegister(Gpu::ApertureBase + target * 4, value);
                }
                local.registerWrites += count;
                length = 1 + count;
            }
            else if (type == 2)
            {
                length = 1;
            }
            else if (type == 3)
            {
                const uint32_t opcode = (header >> 8) & 0x7F;
                const uint32_t count = ((header >> 16) & 0x3FFF) + 1;
                length = 1 + count;

                {
                    uint32_t words[5];
                    uint32_t available = 0;
                    for (; available < 5 && available < count &&
                           cursor + 1 + available < dwords; available++)
                        words[available] = Guest::Read32(Guest::Base,
                            base + (cursor + 1 + available) * 4);
                    Remember(header, words, available);
                }

                if (opcode == OpInterrupt)
                {
                    // Nearly all of the stream is inside these buffers, and so
                    // are nearly all of the interrupts; raising them only from
                    // the outer ring missed almost every one.
                    RaiseInterruptNow();
                }
                else if (opcode == OpDrawIndx && cursor + 2 < dwords)
                {
                    local.draws++;
                    DecodeDraw(
                        Guest::Read32(Guest::Base, base + (cursor + 2) * 4),
                        cursor + 3 < dwords
                            ? Guest::Read32(Guest::Base, base + (cursor + 3) * 4) : 0,
                        cursor + 4 < dwords
                            ? Guest::Read32(Guest::Base, base + (cursor + 4) * 4) : 0);
                }
                else if (opcode == OpDrawIndx2 && cursor + 1 < dwords)
                {
                    local.draws++;
                    const uint32_t w1 = Guest::Read32(Guest::Base, base + (cursor + 1) * 4);
                    DumpDraw("DRAW_INDX_2", header, w1,
                        cursor + 2 < dwords ? Guest::Read32(Guest::Base, base + (cursor + 2) * 4) : 0);
                    DecodeDraw(w1);
                }
                else if (opcode == OpImLoadImmediate && cursor + 2 < dwords)
                {
                    DecodeShaderLoad(Guest::Read32(Guest::Base, base + (cursor + 1) * 4),
                                     Guest::Read32(Guest::Base, base + (cursor + 2) * 4),
                                     base + (cursor + 3) * 4);
                }
                else if (opcode == OpImLoad && cursor + 2 < dwords)
                {
                    DecodeShaderLoadIndirect(
                        Guest::Read32(Guest::Base, base + (cursor + 1) * 4),
                        Guest::Read32(Guest::Base, base + (cursor + 2) * 4));
                }
                else if (opcode == OpWaitRegMem && cursor + 4 < dwords)
                {
                    WaitForValue(
                        Guest::Read32(Guest::Base, base + (cursor + 1) * 4),
                        Guest::Read32(Guest::Base, base + (cursor + 2) * 4),
                        Guest::Read32(Guest::Base, base + (cursor + 3) * 4),
                        Guest::Read32(Guest::Base, base + (cursor + 4) * 4));
                }
                else if (opcode == OpEventWriteShd && cursor + 3 < dwords)
                {
                    WriteFence(Guest::Read32(Guest::Base, base + (cursor + 1) * 4),
                               Guest::Read32(Guest::Base, base + (cursor + 2) * 4),
                               Guest::Read32(Guest::Base, base + (cursor + 3) * 4));
                }
                else if (opcode == OpCondWrite && cursor + 6 < dwords)
                {
                    ConditionalWrite(
                        Guest::Read32(Guest::Base, base + (cursor + 1) * 4),
                        Guest::Read32(Guest::Base, base + (cursor + 2) * 4),
                        Guest::Read32(Guest::Base, base + (cursor + 3) * 4),
                        Guest::Read32(Guest::Base, base + (cursor + 4) * 4),
                        Guest::Read32(Guest::Base, base + (cursor + 5) * 4),
                        Guest::Read32(Guest::Base, base + (cursor + 6) * 4));
                }
                else if (opcode == OpSwap)
                    local.swaps++;
                else if (opcode == OpIndirectBuffer && cursor + 2 < dwords)
                {
                    ExecuteBuffer(
                        Guest::PhysicalAlias(Guest::Read32(Guest::Base, base + (cursor + 1) * 4)),
                        Guest::Read32(Guest::Base, base + (cursor + 2) * 4) & 0xFFFFF,
                        local, depth + 1);
                }
                else if (Type3Name(opcode) == nullptr)
                {
                    local.unknown++;
                }
            }
            else
            {
                return;   // lost sync inside the buffer
            }

            CountPacket(type, type == 3 ? ((header >> 8) & 0x7F) : 0);
            local.packets++;
            cursor += length;
        }
    }
}

void Gpu::ReportPacketMix()
{
    std::lock_guard<std::mutex> lock(g_histogramMutex);
    if (g_type0 + g_type2 + g_type3 == 0) return;

    printf("packets: %llu register writes, %llu filler, %llu commands\n",
        (unsigned long long)g_type0, (unsigned long long)g_type2,
        (unsigned long long)g_type3);

    {
        std::lock_guard<std::mutex> draws(g_drawMutex);
        if (!g_draws.primitives.empty())
        {
            printf("geometry:");
            for (const auto& entry : g_draws.primitives)
                printf(" %s x%llu", PrimitiveName(entry.first),
                    (unsigned long long)entry.second);
            printf(", %llu indices\n", (unsigned long long)g_draws.indices);
        }
        if (g_draws.vertexShaders + g_draws.pixelShaders > 0)
            printf("shaders: %llu vertex, %llu pixel, %llu dwords of microcode, "
                   "last at 0x%08X and 0x%08X\n",
                (unsigned long long)g_draws.vertexShaders,
                (unsigned long long)g_draws.pixelShaders,
                (unsigned long long)g_draws.shaderDwords,
                g_draws.lastVertexShader, g_draws.lastPixelShader);
    }

    if (g_waits.load() != 0)
        printf("waits: %llu, of which %llu gave up waiting\n",
            (unsigned long long)g_waits.load(),
            (unsigned long long)g_waitTimeouts.load());

    if (g_fences.load() != 0)
        printf("fences: %llu written\n", (unsigned long long)g_fences.load());

    if (g_type3Opcodes.empty()) return;
    printf("commands:");
    for (const auto& entry : g_type3Opcodes)
    {
        const char* name = Type3Name(entry.first);
        if (name) printf(" %s x%llu", name, (unsigned long long)entry.second);
        else      printf(" 0x%02X x%llu", entry.first, (unsigned long long)entry.second);
    }
    printf("\n");
    fflush(stdout);
}

uint64_t Mmio::Timebase()
{
    // Counted from the first call so the numbers stay small enough to scale
    // without a wider multiply.
    static const int64_t frequency = []() {
        LARGE_INTEGER value;
        QueryPerformanceFrequency(&value);
        return value.QuadPart;
    }();
    static const int64_t origin = []() {
        LARGE_INTEGER value;
        QueryPerformanceCounter(&value);
        return value.QuadPart;
    }();

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    const int64_t ticks = now.QuadPart - origin;
    if (frequency <= 0 || ticks < 0) return 0;

    constexpr int64_t ConsoleFrequency = 49875000;
    return uint64_t((ticks / frequency) * ConsoleFrequency
                  + (ticks % frequency) * ConsoleFrequency / frequency);
}

void Mmio::CallIndirect(PPCContext& ctx, uint8_t* base, uint32_t address)
{
    PPCFunc* routine = (address >= PPC_CODE_BASE &&
                        address < PPC_CODE_BASE + PPC_CODE_SIZE)
        ? PPC_LOOKUP_FUNC(base, address) : nullptr;

    if (routine != nullptr)
    {
        routine(ctx, base);
        return;
    }

    // Once per distinct address: the same bad call in a loop would otherwise
    // bury everything else.
    static std::mutex mutex;
    static std::map<uint32_t, bool> seen;
    bool first = false;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (seen.find(address) == seen.end()) { seen[address] = true; first = true; }
    }

    if (first)
    {
        printf("\ncall: through a pointer to 0x%08X, which is not a function\n",
            address);
        printf("  the call was made with r3 0x%08X, r4 0x%08X, r5 0x%08X\n",
            ctx.r3.u32, ctx.r4.u32, ctx.r5.u32);
        if (ctx.r3.u32 >= 0x10000 && ctx.r3.u32 < 0xC0000000)
            printf("  the object at r3 begins %08X %08X %08X %08X\n",
                Guest::Read32(base, ctx.r3.u32),
                Guest::Read32(base, ctx.r3.u32 + 4),
                Guest::Read32(base, ctx.r3.u32 + 8),
                Guest::Read32(base, ctx.r3.u32 + 12));

        // Every register, because the one that matters is never the one the
        // call convention names: the object came out of a container held in a
        // callee saved register, and that is what says where it came from.
        printf("  registers r0 to r31:");
        for (int i = 0; i < 32; i++)
        {
            if ((i % 8) == 0) printf("\n   r%-2d", i);
            printf(" %08X", (&ctx.r0)[i].u32);
        }
        printf("\n");

        // The same physical address through every window it has.
        //
        // Guest memory below 0x20000000 is visible at four more addresses, and
        // the console makes them one and the same memory. If they are not the
        // same here, a structure written through one window reads back as zero
        // through another, which is exactly what an object with no vtable looks
        // like. Printing all five says which it is.
        if (ctx.r3.u32 >= 0x80000000u)
        {
            const uint32_t offset = ctx.r3.u32 & 0x1FFFFFFFu;
            static const uint32_t windows[] = {
                0x00000000u, 0x80000000u, 0xA0000000u, 0xC0000000u, 0xE0000000u };
            for (uint32_t window : windows)
            {
                printf("  window 0x%08X ->", window | offset);
                for (int word = 0; word < 4; word++)
                    printf(" %08X", Guest::Read32(base, (window | offset) + word * 4));
                printf("\n");
            }
        }

        // What the callee saved registers point at.
        //
        // The object a virtual call is made on comes out of a container, and
        // the container is what says why it is empty. Those live in the
        // registers a function keeps across calls, so every one of them that
        // looks like a pointer into the image gets its first words printed.
        for (int i = 14; i < 32; i++)
        {
            const uint32_t value = (&ctx.r0)[i].u32;
            if (value < 0x82000000u || value >= 0x82C30000u) continue;
            printf("  r%-2d 0x%08X ->", i, value);
            for (int word = 0; word < 6; word++)
                printf(" %08X", Guest::Read32(base, value + word * 4));
            printf("\n");
        }

        uint32_t functions[10] = {};
        const int count = Sampler::FunctionsOnStack(functions, 10);
        if (count > 0)
        {
            printf("  guest call chain, innermost first:");
            for (int i = 0; i < count; i++)
                printf("%ssub_%08X", i == 0 ? " " : " <- ", functions[i]);
            printf("\n");
        }
        fflush(stdout);
    }

    // Nothing is called and nothing is returned. The console would fault here
    // too, so the title is somewhere it did not mean to be; carrying on shows
    // what it does next instead of ending the run at the first sign of it.
    ctx.r3.u64 = 0;
}

uint32_t Gpu::TakePendingInterrupts()
{
    return g_pendingInterrupts.exchange(0, std::memory_order_relaxed);
}

void Gpu::ReportFences()
{
    std::lock_guard<std::mutex> guard(g_fenceTargetMutex);
    if (g_fenceTargets.empty()) return;

    printf("fences went to:");
    for (const auto& entry : g_fenceTargets)
        printf(" 0x%08X x%llu", entry.first, (unsigned long long)entry.second);
    printf("\n");

    {
        std::lock_guard<std::mutex> guard(g_condTargetMutex);
        if (!g_condTargets.empty())
        {
            printf("conditional writes to registers, allowed of asked:");
            int shown = 0;
            for (const auto& entry : g_condTargets)
            {
                if (shown++ >= 8) break;
                printf(" 0x%08X %llu/%llu", entry.first,
                    (unsigned long long)entry.second.second,
                    (unsigned long long)entry.second.first);
            }
            printf("\n");
        }
    }
    fflush(stdout);
}

void Gpu::SetInterruptRaiser(void (*raiser)())
{
    g_raiseInterrupt = raiser;
}
