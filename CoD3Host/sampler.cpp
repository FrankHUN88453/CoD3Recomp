// Finds out where the guest is, when it is not asking the runtime for anything.
//
// A recompiled title that stops making progress without blocking on a kernel
// call is spinning somewhere in its own code, and from outside that is
// invisible. This suspends the guest threads, walks their stacks, and names the
// guest function each frame belongs to.
//
// Naming them needs no debug symbols. Every recompiled function is a real
// function in this binary, and PPCFuncMappings already pairs each one with the
// guest address it came from. Inverting that table turns a host return address
// into `sub_82XXXXXX`, which can be looked up in the disassembly.

#include "kernel.h"
#include "sampler.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <map>
#include <utility>
#include <thread>
#include <vector>

#include <Windows.h>

namespace
{
    struct HostFunction
    {
        uintptr_t host;
        uint32_t guest;
    };

    std::vector<HostFunction> g_functions;   // sorted by host address
    std::atomic<bool> g_running{ false };
    std::thread g_thread;

    void BuildFunctionMap()
    {
        if (!g_functions.empty()) return;

        for (size_t i = 0; PPCFuncMappings[i].guest != 0; i++)
        {
            g_functions.push_back({
                reinterpret_cast<uintptr_t>(PPCFuncMappings[i].host),
                static_cast<uint32_t>(PPCFuncMappings[i].guest) });
        }
        std::sort(g_functions.begin(), g_functions.end(),
            [](const HostFunction& a, const HostFunction& b) { return a.host < b.host; });
    }

    // The guest function a host address falls inside, or zero when the address
    // belongs to the runtime rather than to translated code.
    uint32_t GuestFunctionAt(uintptr_t address)
    {
        if (g_functions.empty()) return 0;

        auto it = std::upper_bound(g_functions.begin(), g_functions.end(), address,
            [](uintptr_t value, const HostFunction& f) { return value < f.host; });
        if (it == g_functions.begin()) return 0;
        --it;

        // Recompiled functions are large but not unbounded. A hit far past a
        // function start is host code that happens to sit after it.
        if (address - it->host > (1u << 20)) return 0;
        return it->guest;
    }

    // Unwinds without debug symbols, using the exception tables the linker
    // already puts in the image.
    void WalkStack(HANDLE thread, uint32_t* frames, int& count, int limit)
    {
        count = 0;

        CONTEXT context{};
        context.ContextFlags = CONTEXT_FULL;
        if (!GetThreadContext(thread, &context)) return;

        for (int depth = 0; depth < 64 && count < limit; depth++)
        {
            const uint32_t guest = GuestFunctionAt(static_cast<uintptr_t>(context.Rip));
            if (guest != 0)
            {
                // Collapse the long runs of one function calling itself that a
                // deep guest call chain produces.
                if (count == 0 || frames[count - 1] != guest) frames[count++] = guest;
            }

            DWORD64 imageBase = 0;
            PRUNTIME_FUNCTION entry = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
            if (entry == nullptr) break;

            void* handlerData = nullptr;
            DWORD64 establisherFrame = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, entry,
                &context, &handlerData, &establisherFrame, nullptr);
            if (context.Rip == 0) break;
        }
    }

    void SampleOnce()
    {
        std::vector<Kernel::ThreadSample> threads = Kernel::SampleGuestThreads();
        if (threads.empty()) return;

        // Three samples a fifth of a second apart. A thread doing work shows a
        // different stack each time; a stuck one shows the same stack, and that
        // difference is the whole question when nothing is progressing.
        // One stack tells you where a thread is. A histogram of many tells you
        // whether it is going anywhere. A thread doing work spreads across its
        // call graph; a thread stuck in a loop piles up on a few addresses, and
        // that difference is the whole question when nothing is progressing.
        constexpr int Samples = 40;

        printf("\n");
        printf("guest threads, %d samples each, innermost frame:\n", Samples);

        for (const auto& thread : threads)
        {
            if (thread.handle == nullptr) continue;

            std::map<uint32_t, int> innermost;
            std::map<uint32_t, int> callers;
            int outside = 0;
            uint32_t deepest[10] = {};
            int deepestCount = 0;

            for (int sample = 0; sample < Samples; sample++)
            {
                uint32_t frames[10] = {};
                int count = 0;

                if (SuspendThread(thread.handle) == DWORD(-1)) break;
                WalkStack(thread.handle, frames, count, 10);
                ResumeThread(thread.handle);

                if (count == 0) { outside++; }
                else
                {
                    innermost[frames[0]]++;
                    if (count > 1) callers[frames[1]]++;
                    if (count > deepestCount)
                    {
                        deepestCount = count;
                        for (int i = 0; i < count; i++) deepest[i] = frames[i];
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }

            printf("  thread %u (os %u):", thread.id, thread.osId);
            if (innermost.empty())
            {
                printf(" not in recompiled code\n");
                continue;
            }
            if (outside > 0) printf(" outside x%d", outside);

            // Busiest first, so a loop shows up at the front.
            std::vector<std::pair<int, uint32_t>> ordered;
            for (const auto& entry : innermost)
                ordered.push_back({ entry.second, entry.first });
            std::sort(ordered.begin(), ordered.end(),
                [](const auto& a, const auto& b) { return a.first > b.first; });

            for (size_t i = 0; i < ordered.size() && i < 5; i++)
                printf(" sub_%08X x%d", ordered[i].second, ordered[i].first);
            printf(" (%zu distinct)\n", innermost.size());

            if (deepestCount > 1)
            {
                printf("      stack:");
                for (int i = 0; i < deepestCount; i++) printf(" sub_%08X", deepest[i]);
                printf("\n");
            }
        }
        Kernel::ReportRecentCalls();
        fflush(stdout);
    }
}

void Sampler::Start(int intervalSeconds)
{
    bool expected = false;
    if (!g_running.compare_exchange_strong(expected, true)) return;

    BuildFunctionMap();
    g_thread = std::thread([intervalSeconds]()
    {
        // One report after things have settled, then at the requested interval.
        std::this_thread::sleep_for(std::chrono::seconds(8));
        while (g_running.load(std::memory_order_acquire))
        {
            SampleOnce();
            for (int i = 0; i < intervalSeconds && g_running.load(); i++)
                std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    });
}

void Sampler::Stop()
{
    if (g_running.exchange(false) && g_thread.joinable())
        g_thread.join();
}

uint32_t Sampler::FunctionAt(unsigned long long hostAddress)
{
    BuildFunctionMap();
    return GuestFunctionAt(static_cast<uintptr_t>(hostAddress));
}

int Sampler::WalkGuestStack(void* winContext, uint32_t* functions, int limit)
{
    BuildFunctionMap();

    // The handler's context is the state at the fault, so it is walked in
    // place rather than copied: nothing else is going to run on this stack.
    CONTEXT context = *static_cast<CONTEXT*>(winContext);
    int count = 0;

    for (int depth = 0; depth < 64 && count < limit; depth++)
    {
        const uint32_t guest = GuestFunctionAt(static_cast<uintptr_t>(context.Rip));
        if (guest != 0 && (count == 0 || functions[count - 1] != guest))
            functions[count++] = guest;

        DWORD64 imageBase = 0;
        PRUNTIME_FUNCTION entry = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
        if (entry == nullptr) break;

        void* handlerData = nullptr;
        DWORD64 establisherFrame = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, entry,
            &context, &handlerData, &establisherFrame, nullptr);
        if (context.Rip == 0) break;
    }
    return count;
}

int Sampler::FunctionsOnStack(uint32_t* functions, int limit)
{
    CONTEXT context{};
    RtlCaptureContext(&context);
    return WalkGuestStack(&context, functions, limit);
}
