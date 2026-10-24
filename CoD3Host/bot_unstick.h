#pragma once
#include "bot_navigation.h"
#include <array>
#include <cmath>
#include <cstdint>

// 0.5.196: Justin, on 0.5.195: many bots stuck high up, near ladders they
// never used, or bunched together in front of an obstacle. The 900 ms
// "moved under 12 units" stall test missed them: a bot jittering back and
// forth moves more than that each sample while going nowhere (one bot sat
// 20 s in one place reporting a route). These pieces are pure logic, tested
// on their own (docs/build-logs/test_bot_unstick_196.cpp).
namespace BotNavigation {

// Net progress: stuck when, while he wants to move, he is not 56 units from
// where he was 3.5 s ago. Strikes count repeats and drop back to zero after
// 15 s of real progress.
struct NetProgress {
    static constexpr uint32_t Window = 3500, Forgive = 15000;
    static constexpr float MinNet = 56;
    Point from{}; uint32_t since = 0, lastStuck = 0; unsigned strikes = 0; bool primed = false;

    void Reset() { primed = false; strikes = 0; since = lastStuck = 0; }
    // Returns true once per stuck window (and counts a strike).
    bool Track(Point feet, uint32_t now, bool wantsMove) {
        if (!Finite(feet)) return false;
        if (strikes && now - lastStuck >= Forgive) strikes = 0;   // a quiet 15 s: forgiven
        if (!wantsMove || !primed) { from = feet; since = now; primed = true; return false; }
        if (Distance(feet, from) >= MinNet) { from = feet; since = now; return false; }
        if (now - since < Window) return false;
        from = feet; since = now; lastStuck = now;
        ++strikes;
        return true;
    }
};

// A ledge he can step off toward a lower goal: 64 units out, clear at his
// feet, a floor 30..maxDrop below with a walkable slope. The direction
// nearest the goal wins; never through walls or into a pit.
template<class Query>
bool DropDown(Point from, Point goal, Query&& query, Point& landing, float maxDrop = 180.0f) {
    if (!Finite(from) || !Finite(goal)) return false;
    const float yaw = std::atan2(goal.y - from.y, goal.x - from.x);
    for (float degrees : {0.0f, 35.0f, -35.0f, 70.0f, -70.0f, 110.0f, -110.0f, 180.0f}) {
        const float angle = yaw + degrees * 0.01745329252f;
        const Point out{from.x + std::cos(angle) * 64, from.y + std::sin(angle) * 64, from.z + 1};
        const Hit travel = query({from.x, from.y, from.z + 1}, out, true);
        if (!travel.valid || travel.solid || travel.fraction < 0.999f) continue;
        const Hit floor = query(out, {out.x, out.y, from.z - maxDrop - 4}, true);
        if (!floor.valid || floor.solid || floor.fraction >= 1 || !Finite(floor.end) || !Finite(floor.normal)) continue;
        if (floor.normal.z < 0.65f) continue;
        const float drop = from.z - floor.end.z;
        if (drop < 30 || drop > maxDrop) continue;
        landing = floor.end; landing.z += 1;
        return true;
    }
    return false;
}

// A way out when strikes pile up: the reachable point 300..500 units off
// that is farthest from where he is stuck, away from teammates crowding him
// and from remembered failures, preferring directions not straight at the
// goal he keeps failing to reach.
template<class Query, class Avoid>
bool Wander(Point from, Point goal, unsigned seed, Query&& query, Avoid&& avoid, Point& out) {
    if (!Finite(from)) return false;
    const float base = std::atan2(goal.y - from.y, goal.x - from.x) + 1.5708f * ((seed & 1) ? 1 : -1);
    float best = -1e30f; bool found = false;
    for (int i = 0; i < 8; i++) {
        const float angle = base + i * 0.785398f;
        for (float radius : {480.0f, 380.0f, 300.0f}) {
            Point candidate{from.x + std::cos(angle) * radius, from.y + std::sin(angle) * radius, from.z}, landing;
            if (!Reachable(from, candidate, query, landing)) continue;
            float score = Distance(landing, from) - avoid(landing);
            const float towardGoal = std::cos(angle - std::atan2(goal.y - from.y, goal.x - from.x));
            score -= towardGoal * 120.0f;   // not straight back into the same wall
            if (score > best) { best = score; out = landing; found = true; }
            break;
        }
    }
    return found;
}

// Teammates too close (under radius): a push away, stronger the closer, as a
// world-space direction (x, y). Zero when nobody crowds him.
struct Push { float x = 0, y = 0; };
inline Push Separation(Point me, const Point* others, int count, float radius = 72.0f) {
    Push push;
    for (int i = 0; i < count; i++) {
        const float dx = me.x - others[i].x, dy = me.y - others[i].y;
        const float d = std::hypot(dx, dy);
        if (!std::isfinite(d) || d >= radius || std::fabs(me.z - others[i].z) > 64) continue;
        if (d < 1.0f) { push.x += (i & 1) ? 1.0f : -1.0f; continue; }   // exactly on top: any side
        const float w = (radius - d) / radius;
        push.x += dx / d * w; push.y += dy / d * w;
    }
    return push;
}

// Each objective player his own place around a flag: a ring 40..100 units
// out, spread by the golden angle, so twenty bots do not all run to one point.
inline Point SpreadGoal(Point goal, uint32_t slot) {
    const float angle = float(slot) * 2.39996323f;
    const float radius = 40.0f + float(slot % 4) * 20.0f;
    return {goal.x + std::cos(angle) * radius, goal.y + std::sin(angle) * radius, goal.z};
}
}
