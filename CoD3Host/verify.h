#pragma once

// Verify mode (F11, Graphics, "Verify the shortcuts"): each shortcut the
// renderer takes is checked against the slow, exact answer, and every
// disagreement is counted, named in the log and shown in the menu. For
// finding a missed change (an effect drawn with old vertices, a texture
// left stale) before a player does. Slow; switched on and off in the menu
// at once, never saved.
//
// Buffers: the bytes each draw reads from a vertex buffer, held against a
// copy of what was sent to the GPU; a difference is a change the renderer
// missed, named with the way the buffer was last sent, and sent again.
// Textures: a texture's memory at its first use in a frame, held against
// a copy of what it was made from. Constants: an upload skipped because
// nothing the program reads changed, held against what the last upload
// packed (COD3_CONSTCHECK). Watched memory: a buffer or texture found the
// same by its pages or by samples, hashed whole anyway (COD3_WATCHAUDIT,
// COD3_MISSAUDIT). Native records: the register image a draw is made from,
// held against one made whole from its record (COD3_IMAGECHECK; with
// "Native command lists" on).

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>

namespace Verify
{
    enum Kind { Buffers, Textures, Constants, Watched, Native, KindCount };
    inline const char* Name(Kind kind)
    {
        static const char* const names[KindCount] = { "Vertex buffers", "Textures", "Constants", "Watched memory", "Native records" };
        return names[kind];
    }

    inline std::atomic<bool> on[KindCount] = {};
    // Bumped when buffers or textures are switched on: each is sent again
    // whole at its next use, so the copy it is held against is known good.
    inline std::atomic<uint32_t> epoch{ 1 };
    inline std::atomic<uint64_t> checks[KindCount] = {}, mismatches[KindCount] = {};
    inline std::mutex lastMutex;
    inline std::string last[KindCount];

    inline bool On(Kind kind) { return on[kind].load(std::memory_order_relaxed); }
    inline void Checked(Kind kind, uint64_t count = 1) { checks[kind].fetch_add(count, std::memory_order_relaxed); }

    inline void Set(Kind kind, bool value)
    {
        if (value && !on[kind].load() && (kind == Buffers || kind == Textures)) epoch.fetch_add(1);
        on[kind].store(value);
    }

    inline void Reset()
    {
        std::lock_guard<std::mutex> lock(lastMutex);
        for (int k = 0; k < KindCount; k++) { checks[k] = 0; mismatches[k] = 0; last[k].clear(); }
    }

    // A disagreement: counted, kept as the kind's last, and printed (the
    // first fifty of a kind, then one in five hundred).
    inline void Mismatch(Kind kind, const char* format, ...)
    {
        char text[400];
        va_list args;
        va_start(args, format);
        vsnprintf(text, sizeof(text), format, args);
        va_end(args);
        const uint64_t n = mismatches[kind].fetch_add(1, std::memory_order_relaxed) + 1;
        {
            std::lock_guard<std::mutex> lock(lastMutex);
            last[kind] = text;
        }
        if (n <= 50 || n % 500 == 0)
        {
            printf("verify: %s #%llu: %s\n", Name(kind), (unsigned long long)n, text);
            fflush(stdout);
        }
    }

    inline std::string Last(Kind kind)
    {
        std::lock_guard<std::mutex> lock(lastMutex);
        return last[kind];
    }
}
