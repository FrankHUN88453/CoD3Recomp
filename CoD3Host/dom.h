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
}
