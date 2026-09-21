#pragma once

// A draw's state, read once from the register file into plain fields, and
// the command the backend runs it as.
//
// The title's driver leaves the register file describing the draw it is
// about to make: the surface, the depth and blend control, the scissor,
// the fetch constants of the programs' inputs. Snapshot is those words,
// nothing decoded from them but the fields the backend keys its caches by.
// Reading them is thirty relaxed loads; everything after works on the copy.

#include <cstdint>

namespace RenderState
{
    // The register indices, by name, as the title's driver writes them.
    enum Register : uint32_t
    {
        SurfaceInfo = 0x2000,          // pitch in the low bits
        ColorInfo0 = 0x2001,           // base tile, format
        DepthInfo = 0x2002,
        ColorInfo1 = 0x2003, ColorInfo2 = 0x2004, ColorInfo3 = 0x2005,
        WindowOffset = 0x2080,
        ScissorTopLeft = 0x2081,
        ScissorBottomRight = 0x2082,
        MaxVertexIndex = 0x2100, MinVertexIndex = 0x2101,
        IndexOffset = 0x2102,
        ResetIndex = 0x2103,
        ColorMask = 0x2104,
        BlendRed = 0x2105,             // the constant colour, four floats
        StencilRefMask = 0x210D,
        AlphaReference = 0x210E,
        ViewportXScale = 0x210F,       // then offset x, scale y, offset y, scale z, offset z
        ProgramControl = 0x2180,
        DepthControl = 0x2200,
        BlendControl0 = 0x2201,
        ColorControl = 0x2202,         // the alpha test
        ClipControl = 0x2204,
        SuScModeControl = 0x2205,      // cull, facing, strip reset
        VteControl = 0x2206,           // which viewport terms apply
        ModeControl = 0x2208,          // the render backend's mode: 4 draws, 6 copies
        BlendControl1 = 0x2209, BlendControl2 = 0x220A, BlendControl3 = 0x220B,
        PointSize = 0x2280,
        CopyControl = 0x2318,          // the resolve
        CopyDestBase = 0x2319, CopyDestPitch = 0x231A, CopyDestInfo = 0x231B,
        CopyDepthClear = 0x231D, CopyColorClear = 0x231E,
        VertexConstants = 0x4000,      // 256 float4
        PixelConstants = 0x4400,       // 256 float4
        FetchConstants = 0x4800,       // 32 texture fetches of 6 words, or 96 vertex fetches of 2
        BoolConstants = 0x4900,        // 8 words of booleans, then 32 loop constants
    };

    enum Primitive : uint32_t
    {
        Points = 1, Lines = 2, LineStrip = 3, Triangles = 4, TriangleFan = 5, TriangleStrip = 6,
        Rectangles = 8, Quads = 13,
    };

    struct Snapshot
    {
        // The initiator word, decoded.
        uint32_t primitive = 0;
        uint32_t sourceSelect = 0;     // 0 indexed from memory, 2 from the vertex id
        uint32_t indexCount = 0;
        bool wideIndices = false;      // 32 bit indices, else 16
        uint32_t indexBase = 0;        // physical, when indexed

        // The surface.
        uint32_t pitch = 0;
        uint32_t colorInfo[4] = {};
        uint32_t depthInfo = 0;
        uint32_t modeControl = 0;      // 0x2208 & 7
        uint32_t colorMask = 0;
        uint32_t depthControl = 0;
        uint32_t stencilRefMask = 0;

        // The rasteriser.
        uint32_t suScModeControl = 0;
        uint32_t scissorTopLeft = 0, scissorBottomRight = 0, windowOffset = 0;
        int32_t indexOffset = 0;
        uint32_t resetIndex = 0;
        uint32_t pointSize = 0;

        // Blending.
        uint32_t blendControl[4] = {};
        float blendFactor[4] = {};

        // What the vertex program's output goes through, and the alpha test.
        uint32_t vteControl = 0;
        uint32_t colorControl = 0;
        uint32_t alphaReference = 0;
        float viewport[6] = {};        // x scale, x offset, y scale, y offset, z scale, z offset

        // The programs.
        uint64_t vertexProgram = 0;    // microcode hashes
        uint64_t pixelProgram = 0;
    };

    // The state from the register file, for a draw with this initiator.
    void Read(uint32_t initiator, uint32_t indexBase, Snapshot& out);

    // A fetch constant's words, straight from the file.
    void ReadTextureFetch(uint32_t slot, uint32_t words[6]);
    void ReadVertexFetch(uint32_t slot, uint32_t& word0, uint32_t& word1);

    // --- the command -------------------------------------------------------------

    // Everything the backend binds is an integer handle into one of its
    // caches, so a command compares to what is bound field by field and
    // only what changed is set on the context. Handle 0 is "none". The
    // textures, samplers and vertex buffers are bound by slot as the
    // programs name them, through the same comparison, and are not here.
    using Handle = uint32_t;

    struct DrawCommand
    {
        Handle vertexShader = 0, pixelShader = 0;
        Handle blend = 0, depth = 0, rasterizer = 0;
        Handle colorTarget[4] = {}, depthTarget = 0;
        uint32_t targetCount = 0;
        uint32_t topology = 0;         // D3D11_PRIMITIVE_TOPOLOGY
        bool rectangles = false;       // through the rectangle list geometry program
        bool depthOnly = false;
        uint32_t stencilReference = 0;
        float blendFactor[4] = {};
        int32_t scissor[4] = {};       // left, top, right, bottom in host pixels
        uint32_t targetWidth = 0, targetHeight = 0;

        // Indices, in the index ring, or none: a plain draw of indexCount.
        uint32_t indexCount = 0;
        uint32_t indexRingOffset = 0;
        bool indexed = false;
        bool indices32 = false;
        int32_t baseVertex = 0;
    };
}
