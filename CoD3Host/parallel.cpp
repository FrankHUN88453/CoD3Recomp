#include "parallel.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace
{
    // The batch in flight. Workers take pieces from it with one atomic add
    // each, so handing out a piece costs nothing worth measuring.
    struct Batch
    {
        const std::function<void(int, int)>* work = nullptr;
        std::atomic<int> next{ 0 };
        int end = 0;
        int grain = 1;
        std::atomic<int> pending{ 0 };   // pieces taken and not yet finished
        std::atomic<int> generation{ 0 };
    };

    std::mutex g_mutex;
    std::condition_variable g_wake;
    std::condition_variable g_done;
    Batch g_batch;
    std::atomic<bool> g_quit{ false };
    std::vector<std::thread> g_threads;
    int g_width = 1;

    // Takes pieces until the batch is exhausted. Both the workers and the
    // caller run this, so the caller is never idle while others work.
    void Drain(Batch& batch)
    {
        for (;;)
        {
            const int first = batch.next.fetch_add(batch.grain, std::memory_order_acq_rel);
            if (first >= batch.end) return;
            const int last = std::min(first + batch.grain, batch.end);
            (*batch.work)(first, last);
            if (batch.pending.fetch_sub(1, std::memory_order_acq_rel) == 1)
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                g_done.notify_all();
            }
        }
    }

    void Worker()
    {
        int seen = 0;
        for (;;)
        {
            {
                std::unique_lock<std::mutex> lock(g_mutex);
                g_wake.wait(lock, [&] {
                    return g_quit.load() || g_batch.generation.load() != seen;
                });
                if (g_quit.load()) return;
                seen = g_batch.generation.load();
            }
            Drain(g_batch);
        }
    }

    void Start()
    {
        static std::once_flag once;
        std::call_once(once, [] {
            // Two cores are left for the title's own threads and the window;
            // the rest take rows. Fewer than one worker means the caller alone.
            const int cores = int(std::thread::hardware_concurrency());
            const int workers = std::max(0, std::min(cores - 2, 16));
            g_width = workers + 1;
            for (int i = 0; i < workers; i++)
                g_threads.emplace_back(Worker);
            for (std::thread& thread : g_threads) thread.detach();
        });
    }
}

int Parallel::Width()
{
    Start();
    return g_width;
}

void Parallel::For(int begin, int end, int grain,
                   const std::function<void(int, int)>& work)
{
    if (begin >= end) return;
    Start();

    const int rows = end - begin;
    if (grain < 1) grain = 1;
    if (g_width == 1 || rows <= grain)
    {
        work(begin, end);
        return;
    }

    // One batch at a time: the caller owns it until it is drained. The
    // rasteriser only ever dispatches from its own thread, so this never
    // contends, and if it ever did the second caller simply waits its turn.
    static std::mutex owner;
    std::lock_guard<std::mutex> own(owner);

    const int pieces = (rows + grain - 1) / grain;
    g_batch.work = &work;
    g_batch.end = end;
    g_batch.grain = grain;
    // The count of pieces goes in before the first piece is offered, so a
    // worker that takes one can never finish it before it was counted.
    g_batch.pending.store(pieces, std::memory_order_relaxed);
    g_batch.next.store(begin, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_batch.generation.fetch_add(1, std::memory_order_release);
        g_wake.notify_all();
    }

    Drain(g_batch);

    std::unique_lock<std::mutex> lock(g_mutex);
    while (!g_done.wait_for(lock, std::chrono::seconds(2),
               [] { return g_batch.pending.load(std::memory_order_acquire) == 0; }))
    {
        // Two seconds with pieces outstanding and nobody working on them
        // is not a slow piece; it is the count and the work disagreeing.
        printf("parallel: waiting on %d of %d pieces, rows %d to %d in %d, next %d\n",
            g_batch.pending.load(), pieces, begin, end, grain, g_batch.next.load());
        fflush(stdout);
    }
    g_batch.work = nullptr;
}
