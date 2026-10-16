#pragma once
#include <cmath>
#include <cstdint>
#include <limits>
#include <initializer_list>

// Local collision steering. Queries use a full body sweep for travel and a
// point ray for cover. No guessed navigation or game-memory structures.
namespace BotNavigation {
struct Point { float x, y, z; };
struct Hit { bool valid = false; bool solid = true; float fraction = 0; Point end{}, normal{}; uint32_t handle=0; };
inline float Distance(Point a, Point b) { return std::hypot(a.x-b.x, a.y-b.y); }
inline bool Finite(Point p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }

template<class Query>
bool Reachable(Point from, Point candidate, Query&& query, Point& landing) {
    if (!Finite(from) || !Finite(candidate)) return false;
    Point start{from.x, from.y, from.z+18}, end{candidate.x,candidate.y,from.z+18};
    Hit travel = query(start,end,true);
    if (!travel.valid || travel.solid || travel.fraction < 0.999f) return false;
    Hit floor = query(end,{end.x,end.y,from.z-32},true);
    if (!floor.valid || floor.solid || floor.fraction < 0 || floor.fraction >= 1 ||
        !Finite(floor.end) || !Finite(floor.normal) || floor.normal.z < 0.65f ||
        floor.end.z < from.z-30 || floor.end.z > from.z+18) return false;
    landing = floor.end; landing.z += 1;
    return true;
}

template<class Query>
bool Step(Point from, Point goal, int handedness, bool stuck, Query&& query, Point& out) {
    float distance = Distance(from,goal);
    if (!std::isfinite(distance) || distance < 20) return false;
    const float yaw = std::atan2(goal.y-from.y,goal.x-from.x);
    const float length = std::fmin(distance,128.0f);
    float best = -std::numeric_limits<float>::infinity(); bool found=false;
    for (float degrees : {0.0f,35.0f,-35.0f,70.0f,-70.0f,105.0f,-105.0f,180.0f}) {
        const float angle = yaw + degrees * handedness * 0.01745329252f;
        Point candidate{from.x+std::cos(angle)*length,from.y+std::sin(angle)*length,from.z}, landing;
        if (!Reachable(from,candidate,query,landing)) continue;
        if (!stuck && degrees == 0) { out=landing;return true; }
        const float progress=distance-Distance(landing,goal);
        // A stuck bot commits to a lateral escape rather than alternating
        // between equally blocked directions on successive frames.
        const float escape=stuck && degrees>=35 && degrees<=105 ? 95.0f : 0;
        const float score=progress-std::fabs(degrees)*0.07f+escape;
        if (score > best) { best=score;out=landing;found=true; }
    }
    return found;
}

template<class Query, class SightQuery>
bool Cover(Point from, Point threatEye, Query&& query, SightQuery&& sight, Point& out) {
    const float away = std::atan2(from.y-threatEye.y,from.x-threatEye.x);
    float best=std::numeric_limits<float>::infinity();bool found=false;
    for (float radius : {96.0f,160.0f,224.0f})
        for (float degrees : {0.0f,45.0f,-45.0f,90.0f,-90.0f,135.0f,-135.0f,180.0f}) {
            const float angle=away+degrees*0.01745329252f;
            Point landing,candidate{from.x+std::cos(angle)*radius,from.y+std::sin(angle)*radius,from.z};
            if (!Reachable(from,candidate,query,landing)) continue;
            Hit shield=sight(threatEye,{landing.x,landing.y,landing.z+32},false);
            if (!shield.valid || shield.solid || shield.fraction >= 0.97f) continue;
            // Prefer close shelter, and avoid moving toward the attacker.
            float score=radius+std::fmax(0.0f,Distance(from,threatEye)-Distance(landing,threatEye))*2;
            if (score<best) {best=score;out=landing;found=true;}
        }
    return found;
}
}
