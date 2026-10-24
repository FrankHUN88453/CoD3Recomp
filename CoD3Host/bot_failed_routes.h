#pragma once
#include "bot_navigation.h"
#include <array>
#include <cstdint>

// 0.5.195: a bot remembers where he failed to get past something (a jump
// that left him where he was, or a straight route he stalled on) and does
// not try that same move again for a while: he reroutes instead.
// Pure logic, no game memory, so it can be tested on its own.
namespace BotNavigation {
struct FailedSpot { Point start{}, target{}; uint32_t until = 0; uint8_t strikes = 0; bool valid = false; };

struct FailedRoutes {
    static constexpr uint32_t Memory = 45000;   // ms one failure is remembered
    static constexpr float Near = 72;           // same place: start and target this close
    static constexpr float SameTarget = 48;     // same landing whatever the start
    std::array<FailedSpot, 6> spots{};
    unsigned next = 0;
    uint32_t lastNow = 0;

    void Clear() { spots = {}; next = 0; lastNow = 0; }

    // The game's time going backwards is a new map: forget the old one.
    void Tick(uint32_t now) {
        if (lastNow && now + 1000 < lastNow) Clear();
        lastNow = now;
    }

    // A failure going from start toward target. Failing again at the same
    // place counts a strike and keeps it remembered longer (up to 4x).
    // Returns the strikes for that place.
    unsigned Remember(Point start, Point target, uint32_t now) {
        Tick(now);
        if (!Finite(start) || !Finite(target)) return 0;
        for (auto& s : spots) {
            if (!s.valid || now >= s.until) continue;
            if (Distance(s.start, start) < Near && Distance(s.target, target) < Near) {
                if (s.strikes < 255) ++s.strikes;
                const uint32_t scale = s.strikes < 4 ? s.strikes : 4;
                s.until = now + Memory * scale;
                return s.strikes;
            }
        }
        // A free or expired place first, else the oldest.
        for (auto& s : spots)
            if (!s.valid || now >= s.until) { s = {start, target, now + Memory, 1, true}; return 1; }
        spots[next++ % spots.size()] = {start, target, now + Memory, 1, true};
        return 1;
    }

    // Whether moving from start to target repeats a remembered failure.
    bool Blocks(Point start, Point target, uint32_t now) const {
        if (!Finite(start) || !Finite(target)) return false;
        for (const auto& s : spots) {
            if (!s.valid || now >= s.until) continue;
            if (Distance(s.target, target) < SameTarget) return true;
            if (Distance(s.start, start) < Near && Distance(s.target, target) < Near) return true;
        }
        return false;
    }

    // Whether a waypoint lands where a remembered failure was headed.
    bool NearFailedTarget(Point p, uint32_t now) const {
        if (!Finite(p)) return false;
        for (const auto& s : spots)
            if (s.valid && now < s.until && Distance(s.target, p) < SameTarget) return true;
        return false;
    }

    unsigned Active(uint32_t now) const {
        unsigned n = 0;
        for (const auto& s : spots) if (s.valid && now < s.until) ++n;
        return n;
    }
};
}
