// The title's linked-in Xbox 360 D3D, taken over call by call: the layer of
// docs/native-d3d11-plan.md. Each hook replaces one function of the library
// (0x822E0000..0x82300000) through the recompiled function's weak alias, as
// title_fixes.cpp does.
//
// F1, observing: the hooks count the calls, by the host thread that makes
// them, and call the original, so nothing changes yet but what is known.
// COD3_D3DHOOKS=1 prints the counts every five seconds.

#include "kernel.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>

#include <Windows.h>

namespace
{
    enum Call { DrawIndexed, DrawIndexedB, DrawIndexedC, DrawImmediate, RectangleEvent, ShaderLoad, SwapCountdown, IndirectA, IndirectB, GameDraw, DrawD, DrawE, DrawF, DrawG, CallCount };
    const char* const CallNames[CallCount] = { "draw indexed 822F3620", "draw indexed 822F3EA0", "draw indexed 822F4338",
        "draw immediate 822F6858", "rectangle + event 822EFFF0", "shader load 822FB4B0", "swap countdown 822F4A10",
        "(ring write 822F1E68: pool_trace.cpp)", "indirect buffer 822F8590",
        "game's own draw 82154930", "draw 822F30F0", "draw 822F3A28", "draw 822F6DF8", "draw 82301888" };

    bool Observing()
    {
        static const bool on = getenv("COD3_D3DHOOKS") != nullptr;
        return on;
    }

    std::mutex g_mutex;
    std::map<uint32_t, uint64_t> g_counts[CallCount];   // by host thread id
    std::chrono::steady_clock::time_point g_lastReport = std::chrono::steady_clock::now();

    std::map<uint32_t, std::map<uint32_t, uint64_t>> g_devices;   // by thread: the devices its draws name

    void Count(Call call, uint32_t device = 0)
    {
        if (!Observing()) return;
        const uint32_t thread = GetCurrentThreadId();
        std::lock_guard<std::mutex> lock(g_mutex);
        g_counts[call][thread]++;
        if (device != 0) g_devices[thread][device]++;
        const auto now = std::chrono::steady_clock::now();
        if (now - g_lastReport < std::chrono::seconds(5)) return;
        g_lastReport = now;
        printf("d3d hooks, the last five seconds:\n");
        for (int i = 0; i < CallCount; i++)
        {
            if (g_counts[i].empty()) continue;
            printf("  %-28s", CallNames[i]);
            for (const auto& [id, n] : g_counts[i]) printf(" thread %u x%llu", id, (unsigned long long)n);
            printf("\n");
            g_counts[i].clear();
        }
        for (const auto& [thread, devices] : g_devices)
        {
            printf("  thread %u draws on device", thread);
            for (const auto& [device, n] : devices) printf(" %08X x%llu", device, (unsigned long long)n);
            printf("\n");
        }
        g_devices.clear();
        fflush(stdout);
    }
}

#define COD3_OBSERVE(address, call)                     \
    extern "C" PPC_FUNC(__imp__sub_##address);          \
    PPC_FUNC(sub_##address)                             \
    {                                                   \
        Count(call);                                    \
        __imp__sub_##address(ctx, base);                \
    }

COD3_OBSERVE(822F3620, DrawIndexed)
COD3_OBSERVE(822F3EA0, DrawIndexedB)
COD3_OBSERVE(822F4338, DrawIndexedC)
COD3_OBSERVE(822F6858, DrawImmediate)
COD3_OBSERVE(822EFFF0, RectangleEvent)
COD3_OBSERVE(822FB4B0, ShaderLoad)
COD3_OBSERVE(822F4A10, SwapCountdown)
COD3_OBSERVE(822F8590, IndirectB)
COD3_OBSERVE(82154930, GameDraw)
COD3_OBSERVE(822F30F0, DrawD)
extern "C" PPC_FUNC(__imp__sub_822F3A28);
PPC_FUNC(sub_822F3A28)
{
    Count(DrawE, ctx.r3.u32);
    __imp__sub_822F3A28(ctx, base);
}
COD3_OBSERVE(822F6DF8, DrawF)
COD3_OBSERVE(82301888, DrawG)
