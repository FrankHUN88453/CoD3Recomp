// Memory mapped I/O for the recompiled code.
//
// The Xbox 360 GPU has no separate I/O space: its register file is mapped into
// the address space, and the graphics driver talks to it with ordinary load and
// store instructions. XenonRecomp translates those as plain memory accesses, so
// without this they land in guest RAM and the GPU never hears anything. That is
// exactly why the title stalls after queueing its first command packet.
//
// The recompiler's memory macros are all guarded with #ifndef, so redefining
// them here and force including this into every recompiled translation unit
// turns each guest access into a range check. The check costs one subtract and
// one compare, and the window is chosen to sit in a part of the address space
// nothing else uses: guest heaps are below 0x70000000, the image starts at
// 0x82000000, and physical allocations start at 0xA0000000.

#pragma once

#include <cstdint>

// Declared here so the call below names the one in the global namespace
// rather than starting a new type inside this one.
struct PPCContext;

namespace Mmio
{
    // The time base register.
    //
    // Guest code reads it with mftb and turns the differences into seconds
    // using the frequency the kernel reports, which this runtime gives as
    // 49,875,000: the console's. XenonRecomp translates mftb as the host's
    // cycle counter, which on this machine runs about seventy times faster, so
    // every duration the title measured came out seventy times too long.
    //
    // Redirecting it here rather than in the recompiler means no
    // recompilation: this header is force included ahead of everything, and
    // mftb is rare enough that a call costs nothing.
    uint64_t Timebase();

    // Calling through a function pointer.
    //
    // The translated form of an indirect call reads a table and jumps to what
    // it finds, and a null pointer there takes the whole process down with no
    // sign of what was being called or on what. Going through here instead
    // costs one compare on a path that is already a load and an indirect jump,
    // and turns that into a report: the guest address, the object the call was
    // made on, and the chain of guest functions that got there.
    void CallIndirect(::PPCContext& ctx, uint8_t* base, uint32_t address);

    // The register aperture.
    //
    // 0x7FC80000 is where the console maps it, and the registers are indexed by
    // a sixteen bit number four bytes apart, so it is a quarter of a megabyte
    // wide and no more.
    //
    // This used to start at 0x7C000000 and run for sixty four megabytes, on the
    // reasoning that a wider net would catch a driver using a nearby address.
    // What it caught was ordinary memory. Reads at 0x7FFFA2C3 and writes that
    // put a floating point 1.0 into the ring buffer's write pointer came
    // through here, and a command processor whose write pointer reads as
    // 1065353216 is a command processor the title's own driver declares hung:
    //
    //   The GPU appears to have hung while executing the command buffer
    //   represented by the D3DCommandBuffer structure at address 0x0.
    //
    // Narrow is right. An address outside this is memory, and memory is what it
    // is treated as.
    inline constexpr uint32_t WindowBase = 0x7FC80000u;
    inline constexpr uint32_t WindowSize = 0x00040000u;

    inline bool InWindow(uint32_t address)
    {
        return (address - WindowBase) < WindowSize;
    }

    void Store32(uint32_t address, uint32_t value);
    uint32_t Load32(uint32_t address);
}

#define PPC_LOAD_U32(x) \
    (Mmio::InWindow(static_cast<uint32_t>(x)) \
        ? Mmio::Load32(static_cast<uint32_t>(x)) \
        : __builtin_bswap32(*(volatile uint32_t*)(base + (x))))

#define PPC_STORE_U32(x, y) \
    do { \
        const uint32_t mmioAddress = static_cast<uint32_t>(x); \
        const uint32_t mmioValue = static_cast<uint32_t>(y); \
        if (Mmio::InWindow(mmioAddress)) Mmio::Store32(mmioAddress, mmioValue); \
        else *(volatile uint32_t*)(base + mmioAddress) = __builtin_bswap32(mmioValue); \
    } while (0)

// On x86-64 __rdtsc is a compiler intrinsic with no declaration to collide
// with, so this simply takes its place in the translated code.
#define __rdtsc() ::Mmio::Timebase()

// The recompiler guards this one, so defining it first replaces it everywhere.
#define PPC_CALL_INDIRECT_FUNC(x) ::Mmio::CallIndirect(ctx, base, uint32_t(x))
