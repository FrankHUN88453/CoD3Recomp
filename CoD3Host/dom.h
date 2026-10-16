#pragma once

// Domination (dom.cpp, multiplayer only): the title's cut mode, made on top
// of Team Battle on a map that has its flags.

#include <cstdint>

struct PPCContext;

namespace Domination
{
    // Each server frame, on the game's thread (bots.cpp's SV_Frame hook).
    void Frame(PPCContext& ctx, uint8_t* base);

    // The host's state, as it came over the side channel ("C3DS"), on the
    // side channel's thread.
    void OnState(const uint8_t* packet, int size, unsigned int from);

    // 0.5.88: where a bot of this side (1 the allies, 2 the axis) should go,
    // on the host while a Domination runs with its flags placed: the flag it
    // should take (or hold), its feet-level place in out. atFlag: he stands
    // within its reach. False when there's no Domination to play.
    bool BotGoal(uint32_t side, uint32_t slot, const float me[3], float out[3], bool& atFlag);
}
