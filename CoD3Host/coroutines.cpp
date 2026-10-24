#include "coroutines.h"
#include "kernel.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>
#include <xmmintrin.h>
#include <intrin.h>

#include <Windows.h>

namespace
{
    struct FiberSlot;
    struct Coroutine
    {
        uint32_t key = 0;
        void* fiber = nullptr;
        FiberSlot* slot = nullptr;       // the pooled fiber it runs on (0.5.397)
        void* resumer = nullptr;         // the engine's fiber, while running
        PPCContext* ctx = nullptr;
        uint8_t* base = nullptr;
        PPCFunc* entry = nullptr;
        bool running = false;            // inside its entry function
        uint32_t csr = 0;                // 0.5.399: the SSE control word handed across each switch
        bool finished = false;
        uint32_t generation = 0;         // the game module's lifetime it began in

        // The longjmp the thread asked for when it yielded.
        bool jumpPending = false;
        jmp_buf* jumpBuffer = nullptr;
        int jumpValue = 0;
    };

    // Script threads run on the title's main thread only; the table and the
    // fiber state are that thread's.
    thread_local std::map<uint32_t, Coroutine*>* t_table = nullptr;
    thread_local Coroutine* t_current = nullptr;
    thread_local void* t_engineFiber = nullptr;

    constexpr size_t FiberReserve = 4u << 20;   // host stack, reserved
    constexpr size_t FiberCommit = 64u << 10;

    int g_announced = 0;

    // The fiber pool (0.5.397, below).
    thread_local std::vector<FiberSlot*>* t_pool = nullptr;
    uint32_t g_fibersMade = 0, g_fibersReused = 0;

    // The game module's lifetimes, and what the threads did in the current one.
    uint32_t g_generation = 0;
    uint32_t g_begun = 0, g_resumed = 0, g_destroyed = 0, g_staleResumes = 0;

    // COD3_TRACECOROUTINES=1: every ten seconds, how many threads began,
    // resumed and were destroyed in them, and how many are suspended.
    uint32_t g_windowBegun = 0, g_windowResumed = 0, g_windowDestroyed = 0;
    void Window()
    {
        static const bool wanted = getenv("COD3_TRACECOROUTINES") != nullptr;
        if (!wanted) return;
        static uint64_t since = GetTickCount64();
        const uint64_t now = GetTickCount64();
        if (now - since < 10000) return;
        since = now;
        uint32_t suspended = 0;
        if (t_table != nullptr) for (const auto& entry : *t_table) if (entry.second->running) suspended++;
        printf("coroutines: in ten seconds %u begun, %u resumed, %u destroyed; %u suspended now, generation %u\n",
            g_windowBegun, g_windowResumed, g_windowDestroyed, suspended, g_generation);
        printf("coroutines: fibers made %u, used again %u, pooled now %zu\n", g_fibersMade, g_fibersReused, t_pool ? t_pool->size() : size_t(0));
        fflush(stdout);
        g_windowBegun = g_windowResumed = g_windowDestroyed = 0;
    }

    // 0.5.397: fibers are kept and used again. Marseille's main thread spent
    // about 4% making and deleting them (their stacks reserved and handed
    // back to Windows for every short script thread). A fiber is a loop:
    // it runs the thread given to it, and when that thread's start function
    // returns it switches back and waits to be given the next. Only a fiber
    // whose thread ran to its end goes back in the pool; one whose thread
    // is still inside a yield has live frames and is deleted as before.
    // COD3_FIBERPOOL=0: a new fiber for every thread, as before.
    struct FiberSlot
    {
        void* fiber = nullptr;
        Coroutine* job = nullptr;
    };
    constexpr size_t PoolLimit = 64;

    bool PoolOn()
    {
        static const bool on = []() { const char* t = getenv("COD3_FIBERPOOL"); return !(t && t[0] == '0'); }();
        return on;
    }

    // 0.5.402: a fiber used again has the last script thread's frames still
    // in its stack's memory; a new fiber's stack is all zeros. 0.5.401 still
    // showed (twice, no crash) the job worker's bad call that 0.5.397-0.5.398
    // crashed on, with the pool only, so something read stack memory it had
    // not written (zero on a fresh stack, harmless; stale on a used one).
    // The fiber's stack below this frame is cleared before each new thread,
    // so a used fiber starts as a fresh one does.
    __declspec(noinline) void ClearStackBelow()
    {
        const NT_TIB* tib = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
        const uintptr_t low = reinterpret_cast<uintptr_t>(tib->StackLimit);
        volatile uint8_t marker = 0;
        const uintptr_t here = reinterpret_cast<uintptr_t>(&marker);
        // Up to just below this (leaf) frame, with rep stosb: no call, so
        // nothing of this function's lies in what is cleared.
        const uintptr_t top = here > 64 ? (here - 64) & ~uintptr_t(15) : 0;
        if (top > low) __stosb(reinterpret_cast<unsigned char*>(low), 0, size_t(top - low));
    }

    void CALLBACK StartLoop(void* argument)
    {
        FiberSlot* slot = static_cast<FiberSlot*>(argument);
        for (;;)
        {
            Coroutine* coroutine = slot->job;
            _mm_setcsr(coroutine->csr);
            coroutine->running = true;
            coroutine->entry(*coroutine->ctx, coroutine->base);
            // The start function returned: the thread ran to its end without
            // yielding, or came back from its last yield and finished. Either
            // way the engine continues after the call that started it.
            coroutine->running = false;
            coroutine->finished = true;
            coroutine->csr = _mm_getcsr();
            SwitchToFiber(coroutine->resumer);
            // Switched to again: given the next thread (slot->job).
            static const bool clear = []() { const char* t = getenv("COD3_FIBERCLEAR"); return !(t && t[0] == '0'); }();
            if (clear) ClearStackBelow();
        }
    }

    void CALLBACK Start(void* argument)
    {
        Coroutine* coroutine = static_cast<Coroutine*>(argument);
        _mm_setcsr(coroutine->csr);
        coroutine->running = true;
        coroutine->entry(*coroutine->ctx, coroutine->base);
        // The start function returned: the thread ran to its end without
        // yielding, or came back from its last yield and finished. Either
        // way the engine continues after the call that started it.
        coroutine->running = false;
        coroutine->finished = true;
        coroutine->csr = _mm_getcsr();
        SwitchToFiber(coroutine->resumer);
    }

    // A coroutine's fiber let go: into the pool when its thread ran to the
    // end on a pooled fiber, else deleted.
    void ReleaseFiber(Coroutine& coroutine)
    {
        if (coroutine.fiber == nullptr) return;
        if (coroutine.slot != nullptr && coroutine.finished && !coroutine.running && t_pool != nullptr && t_pool->size() < PoolLimit)
        {
            coroutine.slot->job = nullptr;
            t_pool->push_back(coroutine.slot);
        }
        else
        {
            DeleteFiber(coroutine.fiber);
            delete coroutine.slot;
        }
        coroutine.fiber = nullptr;
        coroutine.slot = nullptr;
    }

    // A fiber for the coroutine: one from the pool, or a new one.
    void MakeFiber(Coroutine& coroutine)
    {
        if (!PoolOn())
        {
            coroutine.fiber = CreateFiberEx(FiberCommit, FiberReserve, FIBER_FLAG_FLOAT_SWITCH, Start, &coroutine);
            return;
        }
        if (t_pool == nullptr) t_pool = new std::vector<FiberSlot*>();
        FiberSlot* slot = nullptr;
        if (!t_pool->empty()) { slot = t_pool->back(); t_pool->pop_back(); g_fibersReused++; }
        else
        {
            slot = new FiberSlot();
            slot->fiber = CreateFiberEx(FiberCommit, FiberReserve, FIBER_FLAG_FLOAT_SWITCH, StartLoop, slot);
            if (slot->fiber == nullptr) { delete slot; return; }
            g_fibersMade++;
        }
        slot->job = &coroutine;
        coroutine.slot = slot;
        coroutine.fiber = slot->fiber;
    }

    void EnsureFiberThread()
    {
        if (t_engineFiber != nullptr) return;
        t_engineFiber = ConvertThreadToFiberEx(nullptr, FIBER_FLAG_FLOAT_SWITCH);
        if (t_engineFiber == nullptr)
        {
            // Already a fiber: the current one is the engine's.
            t_engineFiber = GetCurrentFiber();
        }
        t_table = new std::map<uint32_t, Coroutine*>();
    }

    // Switches to the thread's fiber and, once it yields or finishes, does
    // the longjmp it asked for, if any.
    void Switch(Coroutine& coroutine)
    {
        coroutine.resumer = GetCurrentFiber();
        Coroutine* outer = t_current;
        t_current = &coroutine;
        // 0.5.399: each fiber has its own SSE control word (rounding,
        // flush to zero), but the title's code keeps one copy of it in the
        // shared PPCContext and only sets the processor's when that copy
        // says it changed: so the word goes with the switch, both ways. A
        // fiber used again (0.5.397's pool) started with the word its last
        // script thread left, which is what the job worker crashes of
        // 0.5.397-0.5.398 came from (wrong rounding, wrong numbers).
        coroutine.csr = _mm_getcsr();
        SwitchToFiber(coroutine.fiber);
        _mm_setcsr(coroutine.csr);
        t_current = outer;

        if (g_announced < 60)
        {
            g_announced++;
            printf("coroutines: thread 0x%08X %s%s r3 0x%08X r1 0x%08X\n", coroutine.key,
                coroutine.finished ? "finished" : "yielded",
                coroutine.jumpPending ? " with a longjmp" : "",
                coroutine.ctx->r3.u32, coroutine.ctx->r1.u32);
            fflush(stdout);
        }
        if (coroutine.jumpPending)
        {
            coroutine.jumpPending = false;
            jmp_buf* buffer = coroutine.jumpBuffer;
            const int value = coroutine.jumpValue;
            longjmp(*buffer, value);
        }
    }
}

void Coroutines::Begin(PPCContext& ctx, uint8_t* base, uint32_t key, PPCFunc* entry)
{
    EnsureFiberThread();
    Coroutine*& slot = (*t_table)[key];
    if (slot == nullptr) slot = new Coroutine();
    Coroutine& coroutine = *slot;
    if (coroutine.fiber != nullptr)
    {
        if (coroutine.running)
        {
            printf("coroutines: thread 0x%08X is started again while suspended; "
                   "its old fiber is abandoned\n", key);
            fflush(stdout);
        }
        ReleaseFiber(coroutine);
    }
    coroutine.key = key;
    coroutine.ctx = &ctx;
    coroutine.base = base;
    coroutine.entry = entry;
    coroutine.running = false;
    coroutine.finished = false;
    coroutine.jumpPending = false;
    coroutine.generation = g_generation;
    g_begun++;
    g_windowBegun++;
    Window();
    MakeFiber(coroutine);
    if (g_announced++ < 12)
    {
        printf("coroutines: thread 0x%08X begins\n", key);
        fflush(stdout);
    }
    if (coroutine.fiber == nullptr)
    {
        printf("coroutines: no fiber for thread 0x%08X (error %lu); running it inline\n",
            key, GetLastError());
        fflush(stdout);
        entry(ctx, base);
        return;
    }
    Switch(coroutine);
}

void Coroutines::Resume(PPCContext& ctx, uint8_t* base, uint32_t key)
{
    EnsureFiberThread();
    auto found = t_table->find(key);
    Coroutine* coroutine = found != t_table->end() ? found->second : nullptr;
    if (coroutine == nullptr || coroutine->fiber == nullptr || !coroutine->running)
    {
        // Nothing suspended under this key. A thread that was never begun
        // through Begin, or one that finished, has no fiber to continue;
        // the engine's continuation address says where it wanted to go.
        printf("coroutines: thread 0x%08X resumed at 0x%08X but nothing is suspended for it\n",
            key, uint32_t(ctx.lr));
        fflush(stdout);
        return;
    }
    coroutine->ctx = &ctx;
    coroutine->base = base;
    g_resumed++;
    g_windowResumed++;
    Window();
    if (coroutine->generation != g_generation && g_staleResumes++ < 20)
    {
        printf("coroutines: thread 0x%08X resumed on a fiber begun in generation %u, now %u: its host frames are from before the restart\n",
            key, coroutine->generation, g_generation);
        fflush(stdout);
    }
    if (g_announced < 60)
    {
        g_announced++;
        printf("coroutines: thread 0x%08X resumes at 0x%08X, r1 0x%08X\n", key, uint32_t(ctx.lr), ctx.r1.u32);
        fflush(stdout);
    }
    Switch(*coroutine);
}

bool Coroutines::YieldIfCoroutine(jmp_buf& buffer, int value)
{
    Coroutine* coroutine = t_current;
    if (coroutine == nullptr) return false;
    coroutine->jumpPending = true;
    coroutine->jumpBuffer = &buffer;
    coroutine->jumpValue = value;
    coroutine->csr = _mm_getcsr();
    SwitchToFiber(coroutine->resumer);
    _mm_setcsr(coroutine->csr);   // the engine's, handed over by Switch
    // Resumed: the engine has loaded this thread's registers and switched
    // back; t_current was set by Switch.
    return true;
}

bool Coroutines::Finished(uint32_t key)
{
    if (t_table == nullptr) return false;
    auto found = t_table->find(key);
    return found != t_table->end() && found->second->finished;
}

void Coroutines::Destroy(uint32_t key)
{
    if (t_table == nullptr) return;
    auto found = t_table->find(key);
    if (found == t_table->end()) return;
    Coroutine* coroutine = found->second;
    t_table->erase(found);
    g_destroyed++;
    g_windowDestroyed++;
    if (coroutine == t_current)
    {
        // Destroying oneself: the fiber cannot be deleted from inside.
        // It is left to leak, which a level does a handful of times.
        return;
    }
    ReleaseFiber(*coroutine);
    delete coroutine;
}

bool Coroutines::Inside()
{
    return t_current != nullptr;
}

void Coroutines::NewGeneration()
{
    uint32_t suspended = 0, finished = 0;
    if (t_table != nullptr)
        for (const auto& entry : *t_table)
        {
            if (entry.second->running) suspended++;
            else if (entry.second->finished) finished++;
        }
    printf("coroutines: generation %u ends: %u threads begun, %u resumes, %u destroyed, %u fibers still suspended, %u finished and kept, %u stale resumes so far\n",
        g_generation, g_begun, g_resumed, g_destroyed, suspended, finished, g_staleResumes);
    fflush(stdout);
    g_generation++;
    g_begun = g_resumed = g_destroyed = 0;
}
