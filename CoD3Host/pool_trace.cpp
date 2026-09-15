// A trace of the title's indirect buffer pool, for the stall at the start
// of a level: COD3_TRACEPOOL=1.
//
// The title carves its command buffers out of one pool, in segments, and
// before it reuses a segment it waits for the GPU's last fence to have
// passed it. Every kick appends such a fence, but a recorded command buffer
// (begun by sub_82302DB0, ended by sub_82302E88) is kicked without one, and
// a lap of the pool spent on recordings leaves nothing for the wait to see.
// This prints each of those steps with the pool's state, so the sequence
// that ends in the wait can be read off.

#include "kernel.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>

#include <Windows.h>

namespace
{
    bool Enabled()
    {
        static const bool enabled = []() {
            const char* text = getenv("COD3_TRACEPOOL");
            return text != nullptr && text[0] != 0 && text[0] != '0';
        }();
        return enabled;
    }

    std::atomic<int> g_lines{ 0 };
    constexpr int LineLimit = 4000;

    void Line(const char* what, uint32_t device, uint8_t* base, uint32_t a, uint32_t b)
    {
        // From the level load on: the title screen alone would use up the
        // line budget in seconds.
        if (!Enabled() || Kernel::Stats().filesOpened.load(std::memory_order_relaxed) < 40) return;
        if (g_lines.fetch_add(1) >= LineLimit) return;
        const uint32_t block = Guest::Read32(base, device + 10768);
        printf("pool: %-9s os %-5lu at %08X lap %u segment %08X.. next %08X recording %08X/%u "
               "gpu word %08X counter %u owner %u pending %u/%u flags %02X%02X  [%08X %08X]\n",
            what, GetCurrentThreadId(),
            Guest::Read32(base, device + 40), Guest::Read32(base, device + 13500),
            Guest::Read32(base, device + 13508), Guest::Read32(base, device + 13496),
            Guest::Read32(base, device + 12944), Guest::Read32(base, device + 12948),
            block ? Guest::Read32(base, block + 4) : 0, Guest::Read32(base, device + 10780),
            Guest::Read32(base, device + 10760),
            Guest::Read32(base, device + 10864), Guest::Read32(base, device + 10868),
            Guest::Read8(base, device + 10808), Guest::Read8(base, device + 10809),
            a, b);
    }
}

// sub_822F2818(device): the current segment is kicked to the GPU.
extern "C" PPC_FUNC(__imp__sub_822F2818);
PPC_FUNC(sub_822F2818)
{
    const uint32_t device = ctx.r3.u32;
    Line("kick", device, base, uint32_t(ctx.lr), 0);
    __imp__sub_822F2818(ctx, base);
}

// sub_822F1B78(device, flags, &size): a segment is taken from the pool.
extern "C" PPC_FUNC(__imp__sub_822F1B78);
PPC_FUNC(sub_822F1B78)
{
    const uint32_t device = ctx.r3.u32;
    const uint32_t flags = ctx.r4.u32;
    const uint32_t sizeAt = ctx.r5.u32;
    const uint32_t wanted = Guest::Read32(base, sizeAt);
    Line("alloc", device, base, flags, wanted);
    __imp__sub_822F1B78(ctx, base);
    Line("allocated", device, base, ctx.r3.u32, Guest::Read32(base, sizeAt));
}

// sub_822F22C0(device, end, ib, dwords, count, list): an indirect buffer
// goes to the ring, or to the list when submissions are pending.
extern "C" PPC_FUNC(__imp__sub_822F22C0);
PPC_FUNC(sub_822F22C0)
{
    const uint32_t device = ctx.r3.u32;
    const uint32_t dwords = ctx.r6.u32;
    const uint32_t count = ctx.r7.u32;
    Line("submit", device, base, ctx.r5.u32, (count << 28) | dwords);
    if (count != 0 && Enabled() && dwords <= 64 &&
        Kernel::Stats().filesOpened.load(std::memory_order_relaxed) >= 40)
    {
        // The buffer of a counted submission, in full: what the title
        // expects the GPU to do for it.
        printf("pool:   counted buffer at physical %08X:", ctx.r5.u32);
        for (uint32_t i = 0; i < dwords; i++)
            printf(" %08X", Guest::Read32(base, Guest::PhysicalAlias(ctx.r5.u32) + i * 4));
        printf("\n");
    }
    __imp__sub_822F22C0(ctx, base);
}

// sub_82302DB0(device) and sub_82302E88(device, ...): a recording begins and ends.
extern "C" PPC_FUNC(__imp__sub_82302DB0);
PPC_FUNC(sub_82302DB0)
{
    const uint32_t device = ctx.r3.u32;
    Line("record", device, base, uint32_t(ctx.lr), 0);
    __imp__sub_82302DB0(ctx, base);
    Line("recording", device, base, 0, 0);
}

extern "C" PPC_FUNC(__imp__sub_82302E88);
PPC_FUNC(sub_82302E88)
{
    const uint32_t device = ctx.r3.u32;
    Line("end rec", device, base, uint32_t(ctx.lr), 0);
    __imp__sub_82302E88(ctx, base);
    Line("recorded", device, base, ctx.r3.u32, 0);
}
