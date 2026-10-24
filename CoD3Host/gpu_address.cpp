// Graphics addresses past 512 MB.
//
// The Xbox's graphics library inside the game cuts every address it hands
// the GPU down to 29 bits (512 MB) with `clrlwi rX,rY,3`, about 200 times in
// about 40 functions. To let graphics data live past the 512 MB of physical
// memory, those cuts have to keep the addresses of a new window. Before any
// of that changes, the multiplayer build's patch (scripts/patch_recomp.py
// mp) routes each of those cuts through GpuMask, which here does exactly
// what the instruction did and counts what it is given at each place: an
// address in one of the physical windows (0xA0000000, 0xC0000000,
// 0xE0000000), one already physical (below 0x20000000), one in the virtual
// heap (0x40000000 to 0x6FFFFFFF), or something else. Places that are only
// ever given addresses can be changed safely; the rest are left alone.

#include "kernel.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>

namespace
{
    struct Site
    {
        std::atomic<uint32_t> function{ 0 }, place{ 0 };
        std::atomic<uint64_t> windows{ 0 }, physical{ 0 }, virtualHeap{ 0 }, other{ 0 };
        std::atomic<uint32_t> lastOther{ 0 }, lastVirtual{ 0 };
    };
    constexpr uint32_t SiteCount = 1024;
    Site g_sites[SiteCount];

    Site& Find(uint32_t function, uint32_t place)
    {
        uint32_t slot = (function * 2654435761u + place * 40503u) % SiteCount;
        for (uint32_t probe = 0; probe < SiteCount; probe++, slot = (slot + 1) % SiteCount)
        {
            Site& site = g_sites[slot];
            const uint32_t owner = site.function.load(std::memory_order_acquire);
            if (owner == function && site.place.load(std::memory_order_relaxed) == place) return site;
            if (owner == 0)
            {
                static std::mutex claiming;
                std::lock_guard<std::mutex> lock(claiming);
                if (site.function.load(std::memory_order_relaxed) == 0)
                {
                    site.place.store(place, std::memory_order_relaxed);
                    site.function.store(function, std::memory_order_release);
                    return site;
                }
                if (site.function.load(std::memory_order_relaxed) == function && site.place.load(std::memory_order_relaxed) == place) return site;
            }
        }
        return g_sites[0];
    }
}

// The patched `clrlwi rX,rY,3`: the value with its top three bits cleared,
// as the instruction did, counted by its place (the function, and which of
// its cuts in order).
uint32_t GpuMask(uint32_t value, uint32_t function, uint32_t place)
{
    Site& site = Find(function, place);
    const uint32_t top = value >> 29;
    if (top == 5 || top == 6 || top == 7) site.windows.fetch_add(1, std::memory_order_relaxed);
    else if (top == 0) site.physical.fetch_add(1, std::memory_order_relaxed);
    else if (value >= 0x40000000u && value < 0x70000000u) { site.virtualHeap.fetch_add(1, std::memory_order_relaxed); site.lastVirtual.store(value, std::memory_order_relaxed); }
    else { site.other.fetch_add(1, std::memory_order_relaxed); site.lastOther.store(value, std::memory_order_relaxed); }
    // The measured places (0.5.19, two maps) were given nothing but
    // addresses: the cut keeps the extended window's (kernel.h), and does
    // what it did with everything else.
    if (Guest::IsExtendedCpu(value)) return value - Guest::ExtendedShift;
    return value & 0x1FFFFFFFu;
}

// The patched `addis rX,rY,-16384` of the same library: a GPU address made a
// processor one again, through the window at 0xC0000000. An extended GPU
// address goes back to its own processor address instead.
int64_t GpuWindow(int64_t value)
{
    const uint32_t low = uint32_t(value);
    if (Guest::IsExtendedGpu(low)) return int64_t(low + Guest::ExtendedShift);
    return value + -1073741824;
}

// The library's other cuts (`rlwinm rX,rY,0,3,22/29/30`, patched the same
// way): a processor address made physical with some low bits cleared too,
// written straight into the GPU's command packets (the addresses of buffers
// the GPU reads). An address in the extended window keeps its own place
// there, as GpuMask's do; without this its data was read from the wrong
// memory (dark and black patches on some surfaces with big_memory, 0.5.32).
uint32_t GpuMaskBits(uint32_t value, uint32_t mask)
{
    if (Guest::IsExtendedCpu(value)) return (value - Guest::ExtendedShift) & (mask | 0xE0000000u);   // 0.5.287: bits 29-31 (the window reaches 0x47000000)
    return value & mask;
}

// Written to the log with the memory report (kernel_memory.cpp): each place,
// what it was given. "only addresses" places are the ones safe to change.
void GpuMaskReport()
{
    uint32_t places = 0, clean = 0;
    for (const Site& site : g_sites)
    {
        const uint32_t function = site.function.load();
        if (!function) continue;
        places++;
        const uint64_t w = site.windows.load(), p = site.physical.load(), v = site.virtualHeap.load(), o = site.other.load();
        if (o == 0 && v == 0) clean++;
        printf("gpu masks: sub_%08X cut %u: %llu window addresses, %llu physical, %llu virtual heap (last %08X), %llu other (last %08X)%s\n",
               function, site.place.load(), (unsigned long long)w, (unsigned long long)p, (unsigned long long)v, site.lastVirtual.load(),
               (unsigned long long)o, site.lastOther.load(), (o == 0 && v == 0) ? ", only addresses" : "");
    }
    printf("gpu masks: %u places used, %u given only addresses\n", places, clean);
    fflush(stdout);
}
