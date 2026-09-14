#pragma once

// The last stage of the graphics pipeline: EDRAM, and the resolve that moves a
// finished surface out of it into memory the video scaler can read.
//
// The console's GPU does not render into main memory. It renders into ten
// megabytes of embedded DRAM on the GPU die, and a separate operation, the
// resolve, copies a region of that into a normal texture in main memory. The
// title's frame ends with a resolve into the buffer VdSwap then presents.
//
// Everything here is addressing. Both ends have a layout that has to be got
// exactly right, and neither is linear.

#include <cstdint>

namespace Edram
{
    // Ten megabytes, addressed as dwords. Two thousand and forty eight tiles of
    // 80x16 samples, 1280 dwords each.
    inline constexpr uint32_t SizeDwords = (10u * 1024u * 1024u) / 4u;
    inline constexpr uint32_t TileDwords = 1280u;
    inline constexpr uint32_t TileWidth = 80u;
    inline constexpr uint32_t TileHeight = 16u;

    uint32_t* Data();

    // Resolution scale.
    //
    // The title renders 1040 by 624 and the console scaled that to the
    // screen. Here the rasteriser can draw every surface at a whole multiple
    // of that instead: positions, scissors, pitches and tile bases are all
    // multiplied by the scale, EDRAM grows to hold the result, and the
    // resolve keeps the large picture for the window while writing the
    // title the small one it expects. The window asks for the scale that
    // matches its own height; the change takes effect at the next resolve
    // so no frame is drawn half at one scale and half at another.
    uint32_t Scale();
    void RequestScale(uint32_t scale);

    // How much EDRAM there is at the current scale, in dwords.
    uint32_t Capacity();

    // The large copy of a resolved surface, by the physical address the
    // title knows it by. Null when nothing was resolved there.
    struct Shadow
    {
        uint32_t width = 0;
        uint32_t height = 0;
        const uint32_t* pixels = nullptr;   // packed the way EDRAM packs them
    };
    Shadow ShadowFor(uint32_t physicalAddress);

    // Where a sample lives in EDRAM. Tiles are laid out in rows across the
    // surface, and a sample sits at its own offset inside its tile.
    inline uint32_t SampleOffset(uint32_t sampleX, uint32_t sampleY,
                                 uint32_t pitchSamples, uint32_t baseTile)
    {
        const uint32_t tilesPerRow = pitchSamples / TileWidth;
        const uint32_t tile = baseTile
                            + (sampleY / TileHeight) * tilesPerRow
                            + (sampleX / TileWidth);
        return tile * TileDwords
             + (sampleY % TileHeight) * TileWidth
             + (sampleX % TileWidth);
    }

    // Where a texel lives in a tiled surface in main memory, in bytes.
    //
    // This is the console's 2D tiling. Coordinates are split into a 32 by 32
    // macro tile index and a position inside it, and the bits of the two are
    // then interleaved so that texels near each other in the picture are near
    // each other in memory. The shape of it is not something to take on trust,
    // so SelfTest checks that it really is one to one before it is used.
    inline uint32_t TiledOffset(uint32_t x, uint32_t y, uint32_t pitch,
                                uint32_t bytesPerTexelLog2)
    {
        const uint32_t alignedPitch = (pitch + 31u) & ~31u;

        const uint32_t macro =
            ((x >> 5) + (y >> 5) * (alignedPitch >> 5)) << (bytesPerTexelLog2 + 7);
        const uint32_t micro =
            ((x & 7u) + ((y & 0xEu) << 2)) << bytesPerTexelLog2;

        const uint32_t offset = macro
                              + ((micro & ~0xFu) << 1)
                              + (micro & 0xFu)
                              + ((y & 1u) << 4);

        return ((offset & ~0x1FFu) << 3)
             + ((y & 16u) << 7)
             + ((offset & 0x1C0u) << 2)
             + (((((y & 8u) >> 2) + (x >> 3)) & 3u) << 6)
             + (offset & 0x3Fu);
    }

    // Runs the resolve the register file currently describes. Called when a
    // rectangle list draw arrives with the render backend in copy mode, which
    // is how the console spells "resolve".
    void Resolve();

    struct Statistics
    {
        uint64_t resolves = 0;
        uint64_t clears = 0;
        uint64_t texelsWritten = 0;
        uint64_t texelsNonZero = 0;
        uint32_t lastDestination = 0;
        uint32_t lastWidth = 0;
        uint32_t lastHeight = 0;
    };

    Statistics Stats();
    void Report();

    // Checks that TiledOffset really is a permutation over a surface, so a
    // resolve written through it can be read back through it without loss.
    bool SelfTest();
}
