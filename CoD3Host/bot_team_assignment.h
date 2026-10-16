#pragma once
#include <cstdint>
namespace BotTeamAssignment {
inline uint32_t Choose(uint32_t allies, uint32_t axis, uint32_t tieSide) {
    if (allies < axis) return 1;
    if (axis < allies) return 2;
    return tieSide == 1 ? 1u : 2u;
}
}
