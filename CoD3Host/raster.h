#pragma once

// The middle of the graphics pipeline: running the title's own shader programs
// and filling EDRAM with what they produce.
//
// Everything either side of this already works. The command stream is decoded,
// the register file is tracked, the shader microcode is captured, and finished
// surfaces resolve out of EDRAM and reach the window. This is the part that
// makes the pixels.
//
// It is an interpreter, not a translator. The programs a frame uses here are a
// handful of instructions each, and running them directly needs no compiler and
// no graphics API, which keeps the whole path inspectable.

#include <cstdint>

namespace Raster
{
    // Carries out one draw packet. The initiator is the word the packet holds:
    // primitive type in the low six bits, where indices come from above that,
    // and how many of them in the top sixteen.
    void Draw(uint32_t initiator, uint32_t indexBase = 0, uint32_t indexWord = 0);

    struct Statistics
    {
        uint64_t draws = 0;
        uint64_t skipped = 0;
        uint64_t triangles = 0;
        uint64_t pixels = 0;
        uint64_t needTexture = 0;
        uint64_t unknownFormat = 0;
        uint64_t unknownOpcode = 0;
    };

    Statistics Stats();
    void Report();
}
