// A check on the title's own heap: every block malloc hands out is
// remembered, and a free of one it does not hold is reported with the
// caller's chain. A block freed twice puts itself on a bin's list twice,
// the list turns into a cycle, and the next malloc that walks it never
// returns: that was the second level's load, one run in two. This says
// who frees what twice. Behind COD3_HEAPCHECK=1, nothing otherwise.
//
// The title's heap is dlmalloc under a critical section: sub_820CC1D0 and
// its siblings allocate (sub_820CBC40 under the lock; two of them from a
// heap the caller names, the level's), sub_820CC468(block) and
// sub_820CC4C0(heap, block) free (sub_820CBF58 under the lock). The recompiled
// functions are reached through weak aliases, which these definitions
// replace.

#include "kernel.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <unordered_set>

#include <Windows.h>

namespace
{
    bool Wanted()
    {
        static const bool wanted = getenv("COD3_HEAPCHECK") != nullptr;
        return wanted;
    }

    std::mutex g_mutex;
    std::unordered_set<uint32_t> g_live;
    std::atomic<int> g_reported{ 0 };

    // The guest call chain from the context: the link register, then the
    // return addresses the prologues saved eight bytes below each back
    // chain pointer.
    void PrintChain(const PPCContext& ctx, uint8_t* base)
    {
        printf(" from %08X", uint32_t(ctx.lr));
        uint32_t frame = ctx.r1.u32;
        for (int depth = 0; depth < 12; depth++)
        {
            if (frame < 0x10000 || frame >= 0xC0000000u) break;
            const uint32_t caller = Guest::Read32(base, frame);
            if (caller <= frame || caller - frame > 0x100000 || (caller & 7) != 0 || caller >= 0xC0000000u) break;
            const uint32_t address = Guest::Read32(base, caller - 8);
            if (address < 0x82000000u || address >= 0x8A000000u) break;
            printf(" %08X", address);
            frame = caller;
        }
    }

    void Allocated(uint32_t pointer)
    {
        if (pointer == 0) return;
        std::lock_guard<std::mutex> lock(g_mutex);
        g_live.insert(pointer);
    }

    void Freed(const PPCContext& ctx, uint8_t* base, uint32_t pointer, const char* what)
    {
        if (pointer == 0) return;
        bool known;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            known = g_live.erase(pointer) != 0;
        }
        if (known || g_reported.fetch_add(1) >= 12) return;
        const uint32_t header = Guest::Read32(base, pointer - 4);
        printf("heap: %s of 0x%08X, which is not a live block (chunk header %08X, size %u)", what, pointer, header, header & ~7u);
        PrintChain(ctx, base);
        printf("\n");
        fflush(stdout);
    }
}

// The heap's free lists, checked around a call that walks them. A bin's
// list is circular through fd; a chunk that appears twice makes a cycle,
// and the malloc that walks it never returns. This looks for a cycle in
// every bin, and says which chunk repeats and after how many steps.
namespace
{
    bool BinsAreSound(uint8_t* base, uint32_t mstate, uint32_t& bad, uint32_t& repeat)
    {
        auto word = [&](uint32_t a) { return Guest::Read32(base, a); };
        for (uint32_t bin = 1; bin < 128; bin++)
        {
            const uint32_t head = mstate + 44 + (bin << 3);
            uint32_t slow = word(head + 8), fast = slow;
            int steps = 0;
            while (fast != head && steps++ < 20000)
            {
                if (fast < 0x10000 || fast >= 0xC0000000u) break;
                fast = word(fast + 8);
                if (fast == head || fast < 0x10000 || fast >= 0xC0000000u) break;
                fast = word(fast + 8);
                slow = word(slow + 8);
                if (fast == slow) { bad = bin; repeat = fast; return false; }
            }
        }
        return true;
    }

    // The title's one malloc state, from the first call that names it.
    std::atomic<uint32_t> g_mstate{ 0 };
    std::atomic<int> g_binsReported{ 0 };

    void CheckBins(PPCContext& ctx, uint8_t* base, const char* when, uint32_t pointer)
    {
        const uint32_t mstate = g_mstate.load(std::memory_order_relaxed);
        if (mstate == 0 || g_binsReported.load(std::memory_order_relaxed) >= 4) return;
        uint32_t bad = 0, repeat = 0;
        if (BinsAreSound(base, mstate, bad, repeat)) return;
        if (g_binsReported.fetch_add(1) >= 4) return;
        printf("heap: bin %u holds a cycle through %08X %s 0x%08X", bad, repeat, when, pointer);
        PrintChain(ctx, base);
        printf("\n");
        fflush(stdout);
    }
}

// sub_820CD6B0(alignment, size) calls the core with the malloc state in
// r3: the state's address, noted once, is what the bin check walks.
extern "C" PPC_FUNC(__imp__sub_820CCF00);
PPC_FUNC(sub_820CCF00)
{
    if (Wanted() && g_mstate.load(std::memory_order_relaxed) == 0 && ctx.r3.u32 >= 0x82000000u && ctx.r3.u32 < 0x8A000000u)
    {
        g_mstate.store(ctx.r3.u32, std::memory_order_relaxed);
        printf("heap: the malloc state is at %08X\n", ctx.r3.u32);
        fflush(stdout);
    }
    __imp__sub_820CCF00(ctx, base);
}

extern "C" PPC_FUNC(__imp__sub_820CC1D0);
PPC_FUNC(sub_820CC1D0)
{
    __imp__sub_820CC1D0(ctx, base);
    if (Wanted()) Allocated(ctx.r3.u32);
}

extern "C" PPC_FUNC(__imp__sub_820CC238);
PPC_FUNC(sub_820CC238)
{
    __imp__sub_820CC238(ctx, base);
    if (Wanted()) Allocated(ctx.r3.u32);
}

extern "C" PPC_FUNC(__imp__sub_820CC2A0);
PPC_FUNC(sub_820CC2A0)
{
    // From a heap of the title's choosing (the level's), the same block shape.
    __imp__sub_820CC2A0(ctx, base);
    if (Wanted()) Allocated(ctx.r3.u32);
}

extern "C" PPC_FUNC(__imp__sub_820CC330);
PPC_FUNC(sub_820CC330)
{
    __imp__sub_820CC330(ctx, base);
    if (Wanted()) Allocated(ctx.r3.u32);
}

extern "C" PPC_FUNC(__imp__sub_820CC398);
PPC_FUNC(sub_820CC398)
{
    __imp__sub_820CC398(ctx, base);
    if (Wanted()) Allocated(ctx.r3.u32);
}

extern "C" PPC_FUNC(__imp__sub_820CC468);
PPC_FUNC(sub_820CC468)
{
    const uint32_t pointer = ctx.r3.u32;
    if (Wanted()) Freed(ctx, base, pointer, "free");
    __imp__sub_820CC468(ctx, base);
    // After the free: a list it has just joined may have closed on itself.
    if (Wanted()) CheckBins(ctx, base, "after freeing", pointer);
}

extern "C" PPC_FUNC(__imp__sub_820CC4C0);
PPC_FUNC(sub_820CC4C0)
{
    // free(heap, block): the block is the second argument.
    const uint32_t pointer = ctx.r4.u32;
    if (Wanted()) Freed(ctx, base, pointer, "free from a heap");
    __imp__sub_820CC4C0(ctx, base);
    if (Wanted()) CheckBins(ctx, base, "after freeing", pointer);
}
