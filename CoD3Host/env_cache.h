#pragma once
// 0.5.370 (Claude, 2026-10-22): two costs the main thread paid on every
// frame, found by the sampling profiler (BIG-PROFILE-32.bat, Rimling, 28
// bots: 20 reports while the match ran):
//  - getenv: 10.7% of the main thread. Settings were read from the
//    environment on every call (MpSessionPlayerAddress, called for every
//    roster access with 32 players on, alone was 11%); the C runtime's
//    getenv takes a lock and walks the whole environment each time. The
//    environment never changes while the game runs, so each name is read
//    once and kept.
//  - the log: 7% of the main thread in WriteFile. stdout was unbuffered
//    and nearly every line was followed by fflush(stdout). stdout is now
//    buffered (log.cpp) and written out by a small thread every 100 ms;
//    here fflush(stdout) only writes when 100 ms have passed. Other
//    streams are flushed as asked.
// Included last by the files whose hot paths read settings and log.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <string_view>
#include <atomic>
#include <chrono>

namespace EnvCache
{
    inline const char* Get(const char* name)
    {
        if (name == nullptr) return nullptr;
        struct Entry { const char* key; const char* value; std::string_view text; };
        thread_local Entry recent[64] = {};
        thread_local unsigned next = 0;
        for (const Entry& e : recent)
            if (e.key == name && e.key != nullptr && e.text == std::string_view(name)) return e.value;
        static std::mutex lock;
        static std::unordered_map<std::string, std::string*> cache;
        const char* value = nullptr;
        std::string_view text;
        {
            std::lock_guard<std::mutex> hold(lock);
            auto found = cache.find(name);
            if (found == cache.end())
            {
                const char* raw = (std::getenv)(name);
                found = cache.emplace(name, raw ? new std::string(raw) : nullptr).first;
            }
            value = found->second ? found->second->c_str() : nullptr;
            text = found->first;   // the map's nodes never move
        }
        // remembered by address, with the stored name's text to compare, so a
        // buffer reused for another name is never mistaken for this one
        recent[next % 64] = Entry{ name, value, text };
        next++;
        return value;
    }
    // fflush(stdout) at most every 100 ms (the log thread writes the rest)
    inline int Flush(FILE* stream)
    {
        if (stream != stdout) return (std::fflush)(stream);
        static std::atomic<long long> last{ 0 };
        const long long now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        long long was = last.load();
        if (now - was < 100) return 0;
        if (!last.compare_exchange_strong(was, now)) return 0;
        return (std::fflush)(stream);
    }
}
#define getenv(name) EnvCache::Get(name)
#define fflush(stream) EnvCache::Flush(stream)
