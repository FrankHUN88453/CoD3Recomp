// Running the title's shader programs, and filling EDRAM with the result.
//
// The encoding this depends on was worked out from the programs the title
// actually uploads, and each correction was forced by the programs themselves
// rather than chosen. Two of them mattered. Source operands say which file they
// read from in the last word of the instruction, not the middle one: reading
// the middle one made a vertex program export a constant as its position, which
// would collapse every triangle to a point. And whether an instruction is
// arithmetic or a memory fetch is carried by the control flow instruction that
// runs it, two bits each, not by the shape of the instruction word.
//
// One register decides the whole coordinate question. PA_CL_VTE_CNTL at 0x2206
// reads 0x300 here: the two format bits are set and every viewport scale and
// offset enable is clear, which says positions arrive already in screen space
// with no perspective divide. The vertex buffers agree, running from -0.5 to
// 1039.5 across a 1040 wide target.

#include "kernel.h"
#include "gpu.h"
#include "edram.h"
#include "raster.h"
#include "parallel.h"
#include "shaders.h"

#include <algorithm>
#include <chrono>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <immintrin.h>

namespace
{
    constexpr uint32_t MaxInterpolators = 16;
    constexpr uint32_t MaxRegisters = 64;

    struct alignas(16) Vec4
    {
        float v[4] = { 0, 0, 0, 0 };
    };

    inline __m128 Load(const Vec4& value) { return _mm_load_ps(value.v); }
    inline void Store(Vec4& value, __m128 m) { _mm_store_ps(value.v, m); }

    std::function<void()> ReportSkips;
    std::mutex g_statisticsMutex;
    Raster::Statistics g_statistics;

    // The registers a draw reads, copied once when the draw starts.
    //
    // Every band of every triangle reads the fetch constants and the shader
    // constants for every pixel, and reading them from the live register file
    // means one mutex per read. Sixteen cores doing that spend their time on
    // the mutex. A draw's registers do not change while it is drawn, so they
    // are copied out once, under the lock, and read from here without it.
    constexpr uint32_t SnapshotFirst = 0x2000;
    constexpr uint32_t SnapshotCount = 0x3000;
    uint32_t g_snapshot[SnapshotCount];

    // Time spent inside triangles, so the rate can be stated rather than guessed.
    std::atomic<uint64_t> g_rasterNanoseconds{ 0 };

    // The shader constant file as floats, the vertex half then the pixel
    // half, so a program reads a constant with one aligned load rather than
    // four register lookups.
    constexpr uint32_t ConstantFloats = 512 * 4;
    alignas(16) float g_constants[ConstantFloats];

    // What each texture slot describes, decoded once a draw rather than once
    // a texel. Slot n is decoded the first time a pixel asks for it.
    struct TextureDesc
    {
        bool decoded = false;
        bool usable = false;
        bool tiled = false;
        bool bilinear = false;
        uint32_t format = 0;
        uint32_t pitch = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        const uint8_t* data = nullptr;   // host pointer to the first texel

        // A resolved surface at the resolution scale, in place of the small
        // copy the title has: what it draws with a frame stays as sharp as
        // the frame.
        const uint32_t* image = nullptr;
    };
    TextureDesc g_textures[32];
    std::mutex g_textureMutex;

    void SnapshotRegisters()
    {
        Gpu::SnapshotRegisters(SnapshotFirst, SnapshotCount, g_snapshot);
        memcpy(g_constants, &g_snapshot[0x4000 - SnapshotFirst], sizeof(g_constants));
        for (TextureDesc& texture : g_textures) texture.decoded = false;
    }

    uint32_t Reg(uint32_t index)
    {
        if (index >= SnapshotFirst && index < SnapshotFirst + SnapshotCount)
            return g_snapshot[index - SnapshotFirst];
        return Gpu::ReadRegister(Gpu::ApertureBase + index * 4);
    }

    float RegFloat(uint32_t index)
    {
        const uint32_t bits = Reg(index);
        float value;
        memcpy(&value, &bits, 4);
        return value;
    }

    // Reports something the interpreter cannot do yet, once per kind, so a gap
    // is named rather than silently producing a wrong picture.
    void ReportOnce(const char* what, uint32_t detail)
    {
        static std::mutex mutex;
        static std::map<uint64_t, bool> seen;
        const uint64_t key = uint64_t(uintptr_t(what)) ^ (uint64_t(detail) << 32);
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (seen.find(key) != seen.end()) return;
            seen[key] = true;
        }
        printf("raster: %s (%u / 0x%X)\n", what, detail, detail);
        fflush(stdout);
    }

    // --- programs ----------------------------------------------------------

    // An arithmetic instruction with everything a pixel needs pulled out of
    // the words once: which registers, the lane permutation for each source
    // as a vector the permute instruction takes directly, the sign to flip,
    // and the write masks as blend masks.
    struct DecodedSource
    {
        uint8_t reg = 0;
        bool temporary = false;
        bool negate = false;
        uint8_t swizzle = 0;
        alignas(16) int32_t permute[4] = { 0, 1, 2, 3 };
    };

    struct DecodedAlu
    {
        uint8_t vectorOpcode = 0, scalarOpcode = 0;
        uint8_t vectorDest = 0, scalarDest = 0;
        uint8_t vectorMask = 0, scalarMask = 0;
        bool exported = false, clamp = false;
        DecodedSource source[3];
    };

    struct DecodedStep
    {
        bool fetch = false;
        uint32_t d0 = 0, d1 = 0, d2 = 0;
        DecodedAlu alu;
    };

    struct Program
    {
        std::vector<uint32_t> words;
        std::vector<uint64_t> controlFlow;
        std::vector<uint8_t> isFetch;
        std::vector<DecodedStep> steps;   // the instructions in the order they run
        uint32_t firstSlot = 0;
        uint32_t registersUsed = 1;
        bool valid = false;
    };

    DecodedSource DecodeSource(uint32_t reg, bool temporary, uint32_t swizzle, bool negate)
    {
        DecodedSource source;
        source.reg = uint8_t(reg & (MaxRegisters - 1));
        source.temporary = temporary;
        source.negate = negate;
        source.swizzle = uint8_t(swizzle);
        for (uint32_t i = 0; i < 4; i++)
            source.permute[i] = int32_t((i + ((swizzle >> (i * 2)) & 3)) & 3);
        return source;
    }

    DecodedAlu DecodeAlu(uint32_t d0, uint32_t d1, uint32_t d2)
    {
        DecodedAlu alu;
        alu.vectorDest = uint8_t(d0 & 0x3F);
        alu.scalarDest = uint8_t((d0 >> 8) & 0x3F);
        alu.exported = ((d0 >> 15) & 1) != 0;
        alu.vectorMask = uint8_t((d0 >> 16) & 0xF);
        alu.scalarMask = uint8_t((d0 >> 20) & 0xF);
        alu.scalarOpcode = uint8_t((d0 >> 26) & 0x3F);
        alu.clamp = ((d0 >> 24) & 1) != 0;
        alu.vectorOpcode = uint8_t((d2 >> 24) & 0x1F);
        alu.source[0] = DecodeSource((d2 >> 16) & 0x3F, ((d2 >> 31) & 1) != 0, (d1 >> 16) & 0xFF, ((d2 >> 22) & 1) != 0);
        alu.source[1] = DecodeSource((d2 >> 8) & 0x3F, ((d2 >> 30) & 1) != 0, (d1 >> 8) & 0xFF, ((d2 >> 14) & 1) != 0);
        alu.source[2] = DecodeSource(d2 & 0x3F, ((d2 >> 29) & 1) != 0, d1 & 0xFF, ((d2 >> 6) & 1) != 0);
        return alu;
    }

    Program Parse(const std::vector<uint32_t>& words)
    {
        Program program;
        if (words.size() < 3) return program;
        program.words = words;

        // The control flow block runs until the earliest instruction any exec
        // points at, because nothing else states its length.
        uint32_t firstTarget = uint32_t(words.size() / 3);
        for (uint32_t slot = 0; slot * 3 + 2 < words.size() && slot < firstTarget; slot++)
        {
            const uint32_t d0 = words[slot * 3 + 0];
            const uint32_t d1 = words[slot * 3 + 1];
            const uint32_t d2 = words[slot * 3 + 2];

            const uint64_t a = uint64_t(d0) | (uint64_t(d1 & 0xFFFF) << 32);
            const uint64_t b = uint64_t(d1 >> 16) | (uint64_t(d2) << 16);

            for (uint64_t instruction : { a, b })
            {
                program.controlFlow.push_back(instruction);
                const uint32_t opcode = uint32_t(instruction >> 44) & 0xF;
                if (opcode >= 1 && opcode <= 6)
                {
                    const uint32_t address = uint32_t(instruction) & 0xFFF;
                    if (address != 0 && address < firstTarget) firstTarget = address;
                }
            }
        }
        program.firstSlot = firstTarget;

        program.isFetch.assign(words.size() / 3 + 1, 0);
        for (uint64_t instruction : program.controlFlow)
        {
            const uint32_t opcode = uint32_t(instruction >> 44) & 0xF;
            if (opcode < 1 || opcode > 6) continue;

            const uint32_t address = uint32_t(instruction) & 0xFFF;
            const uint32_t count = (uint32_t(instruction) >> 12) & 7;
            const uint32_t serialize = uint32_t(instruction >> 16) & 0xFFF;
            for (uint32_t i = 0; i < count; i++)
            {
                if (address + i >= program.isFetch.size()) break;
                program.isFetch[address + i] = ((serialize >> (i * 2)) & 1) != 0;
            }
        }

        // How many registers the program reads. Interpolating all sixteen for
        // every pixel costs sixty four multiply adds each and the programs
        // here read one or two, which is the difference between a few frames a
        // second and a usable rate.
        uint32_t highest = 0;
        for (uint32_t slot = program.firstSlot; slot * 3 + 2 < words.size(); slot++)
        {
            if (slot < program.isFetch.size() && program.isFetch[slot])
            {
                const uint32_t source = (words[slot * 3] >> 5) & 0x3F;
                highest = std::max(highest, source);
                continue;
            }
            const uint32_t d2 = words[slot * 3 + 2];
            if ((d2 >> 31) & 1) highest = std::max(highest, (d2 >> 16) & 0x3F);
            if ((d2 >> 30) & 1) highest = std::max(highest, (d2 >> 8) & 0x3F);
            if ((d2 >> 29) & 1) highest = std::max(highest, d2 & 0x3F);
        }
        program.registersUsed = std::min(highest + 1, MaxInterpolators);

        // The instructions in the order Run walks them, decoded once here
        // rather than once a pixel.
        for (uint64_t instruction : program.controlFlow)
        {
            const uint32_t opcode = uint32_t(instruction >> 44) & 0xF;
            if (opcode < 1 || opcode > 6) continue;
            const uint32_t address = uint32_t(instruction) & 0xFFF;
            const uint32_t count = (uint32_t(instruction) >> 12) & 7;
            for (uint32_t i = 0; i < count; i++)
            {
                const uint32_t slot = address + i;
                if (slot * 3 + 2 >= words.size()) break;
                DecodedStep step;
                step.d0 = words[slot * 3 + 0];
                step.d1 = words[slot * 3 + 1];
                step.d2 = words[slot * 3 + 2];
                step.fetch = slot < program.isFetch.size() && program.isFetch[slot];
                if (!step.fetch) step.alu = DecodeAlu(step.d0, step.d1, step.d2);
                program.steps.push_back(step);
            }
            if (opcode == 2 || opcode == 4 || opcode == 6) break;
        }

        program.valid = !program.controlFlow.empty();
        return program;
    }

    // Programs are parsed once and kept: the same few are bound for every
    // draw of a frame, and decoding them again for each was measurable.
    const Program& CachedProgram(bool pixel)
    {
        static std::mutex mutex;
        static std::map<std::vector<uint32_t>, Program> programs;
        const std::vector<uint32_t> words = Shaders::Microcode(pixel);
        std::lock_guard<std::mutex> lock(mutex);
        auto found = programs.find(words);
        if (found == programs.end())
            found = programs.emplace(words, Parse(words)).first;
        return found->second;
    }

    // --- interpreter -------------------------------------------------------

    struct Context
    {
        Vec4 registers[MaxRegisters];
        Vec4 exports[MaxRegisters];
        bool exported[MaxRegisters] = {};
        bool pixel = false;
        uint32_t vertexIndex = 0;
        uint32_t lastStride = 0;
        uint32_t lastBase = 0;
        bool textureNeeded = false;
        float previousScalar = 0.0f;
    };

    Vec4 ReadConstant(bool pixel, uint32_t index)
    {
        // Vertex programs read the first 256 entries of the float file and
        // pixel programs the second 256.
        const uint32_t at = ((pixel ? 256u : 0u) + (index & 255u)) * 4;
        Vec4 value;
        memcpy(value.v, &g_constants[at], sizeof(value.v));
        return value;
    }

    Vec4 ReadSource(const Context& context, uint32_t reg, bool temporary,
                    uint32_t swizzle, bool negate)
    {
        Vec4 source = temporary ? context.registers[reg & (MaxRegisters - 1)]
                                : ReadConstant(context.pixel, reg);
        Vec4 result;
        for (uint32_t i = 0; i < 4; i++)
        {
            const uint32_t component = (i + ((swizzle >> (i * 2)) & 3)) & 3;
            result.v[i] = negate ? -source.v[component] : source.v[component];
        }
        return result;
    }

    bool ApplyVector(uint32_t opcode, const Vec4& a, const Vec4& b, const Vec4& c,
                     Vec4& out)
    {
        switch (opcode)
        {
        case 0: for (int i = 0; i < 4; i++) out.v[i] = a.v[i] + b.v[i]; return true;
        case 1: for (int i = 0; i < 4; i++) out.v[i] = a.v[i] * b.v[i]; return true;
        case 2: for (int i = 0; i < 4; i++) out.v[i] = std::max(a.v[i], b.v[i]); return true;
        case 3: for (int i = 0; i < 4; i++) out.v[i] = std::min(a.v[i], b.v[i]); return true;
        case 4: for (int i = 0; i < 4; i++) out.v[i] = a.v[i] == b.v[i] ? 1.0f : 0.0f; return true;
        case 5: for (int i = 0; i < 4; i++) out.v[i] = a.v[i] >  b.v[i] ? 1.0f : 0.0f; return true;
        case 6: for (int i = 0; i < 4; i++) out.v[i] = a.v[i] >= b.v[i] ? 1.0f : 0.0f; return true;
        case 7: for (int i = 0; i < 4; i++) out.v[i] = a.v[i] != b.v[i] ? 1.0f : 0.0f; return true;
        case 8: for (int i = 0; i < 4; i++) out.v[i] = a.v[i] - std::floor(a.v[i]); return true;
        case 9: for (int i = 0; i < 4; i++) out.v[i] = std::trunc(a.v[i]); return true;
        case 10: for (int i = 0; i < 4; i++) out.v[i] = std::floor(a.v[i]); return true;
        case 11: for (int i = 0; i < 4; i++) out.v[i] = a.v[i] * b.v[i] + c.v[i]; return true;
        case 12: for (int i = 0; i < 4; i++) out.v[i] = a.v[i] == 0.0f ? b.v[i] : c.v[i]; return true;
        case 13: for (int i = 0; i < 4; i++) out.v[i] = a.v[i] >= 0.0f ? b.v[i] : c.v[i]; return true;
        case 14: for (int i = 0; i < 4; i++) out.v[i] = a.v[i] >  0.0f ? b.v[i] : c.v[i]; return true;
        case 15:
        {
            float sum = 0;
            for (int i = 0; i < 4; i++) sum += a.v[i] * b.v[i];
            for (int i = 0; i < 4; i++) out.v[i] = sum;
            return true;
        }
        case 16:
        {
            float sum = 0;
            for (int i = 0; i < 3; i++) sum += a.v[i] * b.v[i];
            for (int i = 0; i < 4; i++) out.v[i] = sum;
            return true;
        }
        case 19:
        {
            float best = a.v[0];
            for (int i = 1; i < 4; i++) best = std::max(best, a.v[i]);
            for (int i = 0; i < 4; i++) out.v[i] = best;
            return true;
        }
        default:
            return false;
        }
    }

    bool ApplyScalar(uint32_t opcode, const Vec4& a, float previous, float& out)
    {
        switch (opcode)
        {
        case 0:  out = a.v[0] + a.v[1]; return true;   // adds
        case 2:  out = a.v[0] * a.v[1]; return true;   // muls
        case 5:  out = std::max(a.v[0], a.v[1]); return true;
        case 6:  out = std::min(a.v[0], a.v[1]); return true;
        case 11: out = a.v[0] - std::floor(a.v[0]); return true;
        case 12: out = std::trunc(a.v[0]); return true;
        case 13: out = std::floor(a.v[0]); return true;
        case 14: out = std::exp2(a.v[0]); return true;
        case 15: case 16: out = a.v[0] > 0 ? std::log2(a.v[0]) : -3.402823466e38f; return true;
        case 17: case 18: case 19:
            out = a.v[0] != 0.0f ? 1.0f / a.v[0] : 0.0f; return true;
        case 20: case 21: case 22:
            out = a.v[0] > 0.0f ? 1.0f / std::sqrt(a.v[0]) : 0.0f; return true;
        case 40: out = a.v[0] > 0.0f ? std::sqrt(a.v[0]) : 0.0f; return true;
        case 48: out = std::sin(a.v[0]); return true;
        case 49: out = std::cos(a.v[0]); return true;
        case 50: out = previous; return true;          // retain_prev
        default: return false;
        }
    }

    // --- vertex fetch ------------------------------------------------------

    // How many components a format carries and how wide each one is. Only the
    // formats this title's programs ask for are here; anything else is counted
    // and reported rather than approximated.
    bool FetchAttribute(uint32_t format, uint32_t address, Vec4& out)
    {
        auto ReadFloat = [&](uint32_t offset) {
            const uint32_t bits = Guest::Read32(Guest::Base, address + offset * 4);
            float value;
            memcpy(&value, &bits, 4);
            return value;
        };

        switch (format)
        {
        case 36:   // one float
            out.v[0] = ReadFloat(0);
            return true;
        case 37:   // two floats
            out.v[0] = ReadFloat(0); out.v[1] = ReadFloat(1);
            return true;
        case 57:   // three floats
            out.v[0] = ReadFloat(0); out.v[1] = ReadFloat(1); out.v[2] = ReadFloat(2);
            return true;
        case 38:   // four floats
            for (int i = 0; i < 4; i++) out.v[i] = ReadFloat(i);
            return true;
        case 6:    // four bytes, first component in the least significant one
        {
            // The order matters more than it looks. Reading it the other way
            // round put the alpha where the blue belongs, and a background
            // quad with an alpha of zero blends away to nothing.
            const uint32_t packed = Guest::Read32(Guest::Base, address);
            out.v[0] = (packed & 0xFF) / 255.0f;
            out.v[1] = ((packed >> 8) & 0xFF) / 255.0f;
            out.v[2] = ((packed >> 16) & 0xFF) / 255.0f;
            out.v[3] = ((packed >> 24) & 0xFF) / 255.0f;
            return true;
        }
        default:
            return false;
        }
    }

    // Compressed textures.
    //
    // Most of what a menu is made of is stored four texels at a time in blocks:
    // two colours at either end of a line, and two bits a texel saying where
    // along it to sit. The alpha comes either as four bits a texel or as the
    // same trick again with eight interpolated levels.
    //
    // The blocks themselves are laid out with the same tiling everything else
    // uses, counted in blocks rather than texels.
    struct Rgba { float r, g, b, a; };

    // A block is stored with every pair of bytes the other way round from the
    // way the rest of the world writes these, so a byte is fetched through its
    // neighbour's index. Everything below then follows the ordinary layout,
    // which is worth doing: getting the index bits the wrong way round still
    // produces plausible colours from the right two endpoints, so it looks
    // almost right and is not.
    inline uint32_t BlockByte(uint32_t address, uint32_t index)
    {
        return *(Guest::Base + address + (index ^ 1));
    }

    Rgba FromRgb565(uint32_t value)
    {
        Rgba colour;
        colour.r = float((value >> 11) & 0x1F) / 31.0f;
        colour.g = float((value >> 5) & 0x3F) / 63.0f;
        colour.b = float(value & 0x1F) / 31.0f;
        colour.a = 1.0f;
        return colour;
    }

    Rgba Mix(const Rgba& a, const Rgba& b, float t)
    {
        Rgba out;
        out.r = a.r + (b.r - a.r) * t;
        out.g = a.g + (b.g - a.g) * t;
        out.b = a.b + (b.b - a.b) * t;
        out.a = 1.0f;
        return out;
    }

    // The colour half of a block, which DXT1, DXT3 and DXT5 all share: two
    // colours and two bits a texel saying where between them to sit.
    Rgba DecodeColourBlock(uint32_t address, uint32_t inX, uint32_t inY,
                           bool oneBitAlpha, bool& transparent)
    {
        const uint32_t first = BlockByte(address, 0) | (BlockByte(address, 1) << 8);
        const uint32_t second = BlockByte(address, 2) | (BlockByte(address, 3) << 8);

        uint32_t bits = 0;
        for (uint32_t i = 0; i < 4; i++)
            bits |= BlockByte(address, 4 + i) << (i * 8);

        const Rgba a = FromRgb565(first);
        const Rgba b = FromRgb565(second);
        const uint32_t index = (bits >> ((inY * 4 + inX) * 2)) & 3;

        transparent = false;
        if (oneBitAlpha && first <= second)
        {
            // The four colour form gives up its fourth entry to say
            // "nothing here".
            switch (index)
            {
            case 0: return a;
            case 1: return b;
            case 2: return Mix(a, b, 0.5f);
            default: transparent = true; return Mix(a, b, 0.5f);
            }
        }

        switch (index)
        {
        case 0: return a;
        case 1: return b;
        case 2: return Mix(a, b, 1.0f / 3.0f);
        default: return Mix(a, b, 2.0f / 3.0f);
        }
    }

    // The eight level interpolated alpha DXT5 uses: two ends and three bits a
    // texel across six bytes.
    float DecodeAlphaBlock(uint32_t address, uint32_t inX, uint32_t inY)
    {
        const uint32_t first = BlockByte(address, 0);
        const uint32_t second = BlockByte(address, 1);

        uint64_t bits = 0;
        for (uint32_t i = 0; i < 6; i++)
            bits |= uint64_t(BlockByte(address, 2 + i)) << (i * 8);

        const uint32_t texel = inY * 4 + inX;
        const uint32_t index = uint32_t((bits >> (texel * 3)) & 7);

        if (index == 0) return first / 255.0f;
        if (index == 1) return second / 255.0f;

        if (first > second)
            return (first * (8 - index) + second * (index - 1)) / (7.0f * 255.0f);

        if (index == 6) return 0.0f;
        if (index == 7) return 1.0f;
        return (first * (6 - index) + second * (index - 1)) / (5.0f * 255.0f);
    }

    // Reading a texture.
    //
    // A texture fetch constant names everything about the surface in six
    // dwords, and the numbering was confirmed against one whose answer was
    // already known: the constant VdSwap hands over for the front buffer reads
    // back as 1056 pitch, 1040 by 624, format 6, tiled, based at 0xB9CC0000,
    // every one of which matches what the resolve writes.
    // Which filter the title asked for, once for each distinct answer, so the
    // choice made below can be checked against what it actually wanted.
    void ReportFilter(uint32_t magFilter)
    {
        static std::atomic<uint32_t> seen{ 0 };
        const uint32_t bit = 1u << (magFilter & 3);
        if ((seen.fetch_or(bit) & bit) != 0) return;

        static const char* const names[4] =
            { "point", "linear", "base map", "from the instruction" };
        printf("raster: a texture asks for %s filtering\n", names[magFilter & 3]);
        fflush(stdout);
    }

    // The description of a texture slot, decoded from its fetch constant the
    // first time a draw samples it and reused for every texel after.
    const TextureDesc& DescribeTexture(uint32_t slot)
    {
        TextureDesc& texture = g_textures[slot & 31];
        if (texture.decoded) return texture;

        std::lock_guard<std::mutex> lock(g_textureMutex);
        if (texture.decoded) return texture;

        const uint32_t word0 = Reg(0x4800 + slot * 6);
        const uint32_t word1 = Reg(0x4800 + slot * 6 + 1);
        const uint32_t word2 = Reg(0x4800 + slot * 6 + 2);
        const uint32_t word3 = Reg(0x4800 + slot * 6 + 3);

        TextureDesc decoded;
        decoded.usable = false;
        if ((word0 & 3) != 2)
        {
            ReportOnce("texture slot does not hold a texture", slot);
        }
        else
        {
            decoded.pitch = ((word0 >> 22) & 0x1FF) * 32;
            decoded.tiled = ((word0 >> 31) & 1) != 0;
            decoded.format = word1 & 0x3F;
            const uint32_t base = (word1 & 0xFFFFF000);
            decoded.width = (word2 & 0x1FFF) + 1;
            decoded.height = ((word2 >> 13) & 0x1FFF) + 1;
            decoded.data = Guest::Base + Guest::PhysicalAlias(base);

            // How to sample, from the fetch constant. Zero is point and one
            // is linear; three means take it from the fetch instruction,
            // which for a title that never asks for point sampling means
            // linear. Point sampling is what this did before, and it is why
            // a menu drawn at 1040 by 624 and stretched to the window looked
            // coarser than the console's.
            const uint32_t magFilter = (word3 >> 19) & 3;
            decoded.bilinear = (magFilter != 0);
            ReportFilter(magFilter);

            // 6 is four bytes a texel; 18, 19 and 20 are the block compressed
            // forms, which is what most of a menu is made of. 2 is one byte
            // a texel: the films arrive as three of those, the luma plane
            // and the two chroma planes, which the title's own pixel shader
            // turns into colour.
            const bool compressed = (decoded.format == 18 || decoded.format == 19 || decoded.format == 20);
            const bool oneByte = (decoded.format == 2);
            const Edram::Shadow shadow = (decoded.format == 6) ? Edram::ShadowFor(base) : Edram::Shadow{};
            if (shadow.pixels != nullptr && shadow.width >= decoded.width && shadow.height >= decoded.height)
            {
                decoded.image = shadow.pixels;
                decoded.width = shadow.width;
                decoded.height = shadow.height;
                decoded.usable = true;
            }
            else if (decoded.format != 6 && !compressed && !oneByte)
                ReportOnce("unhandled texture format", decoded.format);
            else if (decoded.pitch == 0 || decoded.width == 0 || decoded.height == 0 ||
                     decoded.width > 8192 || decoded.height > 8192)
                ReportOnce("texture size makes no sense", decoded.width);
            else
                decoded.usable = true;
        }
        decoded.decoded = true;
        texture = decoded;
        return texture;
    }

    // One texel, by integer coordinates, whatever the format. Both sampling
    // modes go through here so the formats are decoded in one place.
    inline __m128 Texel(const TextureDesc& texture, int tx, int ty)
    {
        tx = std::min(int(texture.width) - 1, std::max(0, tx));
        ty = std::min(int(texture.height) - 1, std::max(0, ty));

        if (texture.image != nullptr)
        {
            const uint32_t value = texture.image[size_t(ty) * texture.width + uint32_t(tx)];
            const __m128i bytes = _mm_set_epi32(
                int((value >> 24) & 0xFF), int(value & 0xFF),
                int((value >> 8) & 0xFF), int((value >> 16) & 0xFF));
            return _mm_mul_ps(_mm_cvtepi32_ps(bytes), _mm_set1_ps(1.0f / 255.0f));
        }
        if (texture.format == 6)
        {
            const uint32_t offset = texture.tiled
                ? Edram::TiledOffset(uint32_t(tx), uint32_t(ty), texture.pitch, 2)
                : (uint32_t(ty) * texture.pitch + uint32_t(tx)) * 4;
            uint32_t raw;
            memcpy(&raw, texture.data + offset, 4);
            const uint32_t value = __builtin_bswap32(raw);
            // ARGB in the word: red is the second byte from the top.
            const __m128i bytes = _mm_set_epi32(
                int((value >> 24) & 0xFF), int(value & 0xFF),
                int((value >> 8) & 0xFF), int((value >> 16) & 0xFF));
            return _mm_mul_ps(_mm_cvtepi32_ps(bytes), _mm_set1_ps(1.0f / 255.0f));
        }
        if (texture.format == 2)
        {
            const uint32_t offset = texture.tiled
                ? Edram::TiledOffset(uint32_t(tx), uint32_t(ty), texture.pitch, 0)
                : (uint32_t(ty) * texture.pitch + uint32_t(tx));
            // The one component in every lane, so whichever the shader's
            // swizzle picks is the texel.
            return _mm_set1_ps(texture.data[offset] / 255.0f);
        }

        // Blocks of four by four texels, addressed as blocks.
        const uint32_t format = texture.format;
        const uint32_t blockBytes = (format == 18) ? 8u : 16u;
        const uint32_t blockShift = (format == 18) ? 3u : 4u;
        const uint32_t blocksAcross = texture.pitch / 4;

        const uint32_t blockX = uint32_t(tx) / 4;
        const uint32_t blockY = uint32_t(ty) / 4;
        const uint32_t inX = uint32_t(tx) % 4;
        const uint32_t inY = uint32_t(ty) % 4;

        const uint32_t blockOffset = texture.tiled
            ? Edram::TiledOffset(blockX, blockY, blocksAcross, blockShift)
            : (blockY * blocksAcross + blockX) * blockBytes;

        const uint32_t block = uint32_t(texture.data - Guest::Base) + blockOffset;

        bool transparent = false;
        Rgba colour;
        float alpha = 1.0f;

        if (format == 18)
        {
            colour = DecodeColourBlock(block, inX, inY, true, transparent);
            if (transparent) alpha = 0.0f;
        }
        else if (format == 19)
        {
            // Four bits a texel, straight through.
            const uint32_t index = inY * 4 + inX;
            const uint32_t byte = BlockByte(block, index / 2);
            const uint32_t nibble = (index & 1) ? (byte >> 4) : (byte & 0xF);
            alpha = nibble / 15.0f;
            colour = DecodeColourBlock(block + 8, inX, inY, false, transparent);
        }
        else
        {
            alpha = DecodeAlphaBlock(block, inX, inY);
            colour = DecodeColourBlock(block + 8, inX, inY, false, transparent);
        }
        return _mm_set_ps(alpha, colour.b, colour.g, colour.r);
    }

    void RunTextureFetch(Context& context, uint32_t d0, uint32_t d1, uint32_t d2)
    {
        const uint32_t slot = (d0 >> 20) & 0x1F;
        const uint32_t sourceReg = (d0 >> 5) & 0x3F;
        const uint32_t destReg = (d0 >> 12) & 0x3F;

        const TextureDesc& texture = DescribeTexture(slot);
        if (!texture.usable)
        {
            context.textureNeeded = true;
            return;
        }

        // Where in the texture this pixel lands.
        const Vec4& coordinates = context.registers[sourceReg & (MaxRegisters - 1)];
        const float u = coordinates.v[0] * float(texture.width);
        const float v = coordinates.v[1] * float(texture.height);

        __m128 sampled;
        if (!texture.bilinear)
        {
            sampled = Texel(texture, int(std::floor(u)), int(std::floor(v)));
        }
        else
        {
            // A texel's colour sits at its centre, so the weights come from the
            // distance to the centres either side of the sample point.
            const float fu = u - 0.5f;
            const float fv = v - 0.5f;
            const int x0 = int(std::floor(fu));
            const int y0 = int(std::floor(fv));
            const __m128 ax = _mm_set1_ps(fu - float(x0));
            const __m128 ay = _mm_set1_ps(fv - float(y0));

            const __m128 t00 = Texel(texture, x0, y0);
            const __m128 t10 = Texel(texture, x0 + 1, y0);
            const __m128 t01 = Texel(texture, x0, y0 + 1);
            const __m128 t11 = Texel(texture, x0 + 1, y0 + 1);

            const __m128 top = _mm_add_ps(t00, _mm_mul_ps(_mm_sub_ps(t10, t00), ax));
            const __m128 bottom = _mm_add_ps(t01, _mm_mul_ps(_mm_sub_ps(t11, t01), ax));
            sampled = _mm_add_ps(top, _mm_mul_ps(_mm_sub_ps(bottom, top), ay));
        }

        // The instruction rearranges the components on the way into the
        // register, the same way an arithmetic source does.
        const uint32_t swizzle = d1 & 0xFFF;
        Vec4& destination = context.registers[destReg & (MaxRegisters - 1)];
        alignas(16) float lanes[4];
        _mm_store_ps(lanes, sampled);
        for (uint32_t i = 0; i < 4; i++)
        {
            const uint32_t selector = (swizzle >> (i * 3)) & 7;
            switch (selector)
            {
            case 0: case 1: case 2: case 3:
                destination.v[i] = lanes[selector]; break;
            case 4: destination.v[i] = 0.0f; break;
            case 5: destination.v[i] = 1.0f; break;
            case 7: break;
            default: destination.v[i] = 0.0f; break;
            }
        }
        (void)d2;
    }

    void RunFetch(Context& context, uint32_t d0, uint32_t d1, uint32_t d2)
    {
        const uint32_t opcode = d0 & 0x1F;
        const uint32_t destReg = (d0 >> 12) & 0x3F;

        if (opcode == 1)
        {
            RunTextureFetch(context, d0, d1, d2);
            return;
        }
        if (opcode != 0) { ReportOnce("unhandled fetch opcode", opcode); return; }

        // Three vertex fetch constants fit in each six dword slot of the fetch
        // file, so the index is scaled by three and the selector picks one.
        const uint32_t index = ((d0 >> 20) & 0x1F) * 3 + ((d0 >> 25) & 3);
        const uint32_t word0 = Reg(0x4800 + index * 2);
        const uint32_t word1 = Reg(0x4800 + index * 2 + 1);

        const uint32_t format = (d1 >> 16) & 0x3F;
        const uint32_t swizzle = d1 & 0xFFF;
        const bool mini = ((d1 >> 30) & 1) != 0;

        uint32_t stride = d2 & 0xFF;
        const uint32_t offset = (d2 >> 8) & 0x7FFFFF;

        // A follow up fetch from the same buffer carries no stride of its own.
        uint32_t base = Guest::PhysicalAlias(word0 & ~3u);
        if (mini && stride == 0) stride = context.lastStride;
        if (mini && (word0 & 3) != 3) base = context.lastBase;
        if ((word0 & 3) == 3) { context.lastBase = base; }
        if (stride != 0) context.lastStride = stride;

        (void)word1;

        Vec4 fetched;
        const uint32_t address = base + (context.vertexIndex * stride + offset) * 4;
        if (!FetchAttribute(format, address, fetched))
        {
            ReportOnce("unhandled vertex format", format);
            std::lock_guard<std::mutex> lock(g_statisticsMutex);
            g_statistics.unknownFormat++;
            return;
        }

        // Each destination component picks a fetched component, or a literal.
        Vec4& destination = context.registers[destReg & (MaxRegisters - 1)];
        for (uint32_t i = 0; i < 4; i++)
        {
            const uint32_t selector = (swizzle >> (i * 3)) & 7;
            switch (selector)
            {
            case 0: case 1: case 2: case 3:
                destination.v[i] = fetched.v[selector]; break;
            case 4: destination.v[i] = 0.0f; break;
            case 5: destination.v[i] = 1.0f; break;
            case 7: break;                       // leave the component alone
            default: destination.v[i] = 0.0f; break;
            }
        }
    }

    void RunAlu(Context& context, uint32_t d0, uint32_t d1, uint32_t d2)
    {
        const uint32_t vectorDest = d0 & 0x3F;
        const uint32_t scalarDest = (d0 >> 8) & 0x3F;
        const bool exported = ((d0 >> 15) & 1) != 0;
        const uint32_t vectorMask = (d0 >> 16) & 0xF;
        const uint32_t scalarMask = (d0 >> 20) & 0xF;
        const uint32_t scalarOpcode = (d0 >> 26) & 0x3F;
        const bool vectorClamp = ((d0 >> 24) & 1) != 0;

        const uint32_t src3Swizzle = d1 & 0xFF;
        const uint32_t src2Swizzle = (d1 >> 8) & 0xFF;
        const uint32_t src1Swizzle = (d1 >> 16) & 0xFF;

        const uint32_t src3Reg = d2 & 0x3F;
        const uint32_t src2Reg = (d2 >> 8) & 0x3F;
        const uint32_t src1Reg = (d2 >> 16) & 0x3F;
        const uint32_t vectorOpcode = (d2 >> 24) & 0x1F;
        const bool src1Temporary = ((d2 >> 31) & 1) != 0;
        const bool src2Temporary = ((d2 >> 30) & 1) != 0;
        const bool src3Temporary = ((d2 >> 29) & 1) != 0;
        const bool src3Negate = ((d2 >> 6) & 1) != 0;
        const bool src2Negate = ((d2 >> 14) & 1) != 0;
        const bool src1Negate = ((d2 >> 22) & 1) != 0;

        const Vec4 a = ReadSource(context, src1Reg, src1Temporary, src1Swizzle, src1Negate);
        const Vec4 b = ReadSource(context, src2Reg, src2Temporary, src2Swizzle, src2Negate);
        const Vec4 c = ReadSource(context, src3Reg, src3Temporary, src3Swizzle, src3Negate);

        if (vectorMask != 0)
        {
            Vec4 result;
            if (!ApplyVector(vectorOpcode, a, b, c, result))
            {
                ReportOnce("unhandled vector opcode", vectorOpcode);
                std::lock_guard<std::mutex> lock(g_statisticsMutex);
                g_statistics.unknownOpcode++;
            }
            else
            {
                if (vectorClamp)
                    for (int i = 0; i < 4; i++)
                        result.v[i] = std::min(1.0f, std::max(0.0f, result.v[i]));

                Vec4& destination = exported
                    ? context.exports[vectorDest & (MaxRegisters - 1)]
                    : context.registers[vectorDest & (MaxRegisters - 1)];
                for (int i = 0; i < 4; i++)
                    if (vectorMask & (1u << i)) destination.v[i] = result.v[i];
                if (exported) context.exported[vectorDest & (MaxRegisters - 1)] = true;
            }
        }

        if (scalarMask != 0)
        {
            float result = 0;
            bool handled = false;

            // The two operand scalar forms, 42 to 47: one operand is a single
            // component of a constant, the other a single component of a
            // register, and the register's number is spread across the
            // low bit of the opcode, the middle of the third swizzle and
            // the third source's select bit. The film's colour conversion
            // is written with these; nothing in the menus was.
            if (scalarOpcode >= 42 && scalarOpcode <= 47)
            {
                const uint32_t reg2 = (scalarOpcode & 1) | (src3Swizzle & 0x3C)
                                    | (uint32_t(src3Temporary) << 1);
                const Vec4 constant = ReadConstant(context.pixel, src3Reg);
                const Vec4 temporary = context.registers[reg2 & (MaxRegisters - 1)];
                float x = constant.v[src3Swizzle & 3];
                float y = temporary.v[(src3Swizzle >> 6) & 3];
                if (src3Negate) x = -x;
                switch (scalarOpcode)
                {
                case 42: case 43: result = x * y; break;
                case 44: case 45: result = x + y; break;
                default:          result = x - y; break;
                }
                handled = true;
            }
            else
            {
                handled = ApplyScalar(scalarOpcode, c, context.previousScalar, result);
            }

            if (!handled)
            {
                ReportOnce("unhandled scalar opcode", scalarOpcode);
                std::lock_guard<std::mutex> lock(g_statisticsMutex);
                g_statistics.unknownOpcode++;
            }
            else
            {
                context.previousScalar = result;
                Vec4& destination = exported
                    ? context.exports[scalarDest & (MaxRegisters - 1)]
                    : context.registers[scalarDest & (MaxRegisters - 1)];
                for (int i = 0; i < 4; i++)
                    if (scalarMask & (1u << i)) destination.v[i] = result;
                if (exported) context.exported[scalarDest & (MaxRegisters - 1)] = true;
            }
        }
    }

    void Run(const Program& program, Context& context)
    {
        for (uint64_t instruction : program.controlFlow)
        {
            const uint32_t opcode = uint32_t(instruction >> 44) & 0xF;
            if (opcode < 1 || opcode > 6)
            {
                if (opcode == 0 && instruction == 0) continue;
                continue;   // alloc and the rest need nothing here
            }

            const uint32_t address = uint32_t(instruction) & 0xFFF;
            const uint32_t count = (uint32_t(instruction) >> 12) & 7;
            for (uint32_t i = 0; i < count; i++)
            {
                const uint32_t slot = address + i;
                if (slot * 3 + 2 >= program.words.size()) break;

                const uint32_t d0 = program.words[slot * 3 + 0];
                const uint32_t d1 = program.words[slot * 3 + 1];
                const uint32_t d2 = program.words[slot * 3 + 2];

                if (slot < program.isFetch.size() && program.isFetch[slot])
                    RunFetch(context, d0, d1, d2);
                else
                    RunAlu(context, d0, d1, d2);

                if (context.textureNeeded) return;
            }

            if (opcode == 2 || opcode == 4 || opcode == 6) return;
        }
    }

    // --- the pixel interpreter -----------------------------------------------
    //
    // The same semantics as Run above, on the decoded form and four lanes at
    // a time. A source is one aligned load, one permute and one sign flip;
    // an operation is one or two vector instructions; the write is a masked
    // blend. This is what the difference between five and thirty frames a
    // second is made of.

    inline __m128 GatherSource(const Context& context, const DecodedSource& source)
    {
        const float* from = source.temporary
            ? context.registers[source.reg].v
            : &g_constants[((context.pixel ? 256u : 0u) + source.reg) * 4];
        __m128 value = _mm_load_ps(from);
        value = _mm_permutevar_ps(value, _mm_load_si128(reinterpret_cast<const __m128i*>(source.permute)));
        if (source.negate) value = _mm_xor_ps(value, _mm_set1_ps(-0.0f));
        return value;
    }

    inline __m128 MaskFromBits(uint32_t bits)
    {
        return _mm_castsi128_ps(_mm_set_epi32(
            (bits & 8) ? -1 : 0, (bits & 4) ? -1 : 0, (bits & 2) ? -1 : 0, (bits & 1) ? -1 : 0));
    }

    inline float LaneSum4(__m128 v)
    {
        __m128 t = _mm_add_ps(v, _mm_movehl_ps(v, v));
        t = _mm_add_ss(t, _mm_shuffle_ps(t, t, 1));
        return _mm_cvtss_f32(t);
    }

    bool ApplyVectorFast(uint32_t opcode, __m128 a, __m128 b, __m128 c, __m128& out)
    {
        const __m128 one = _mm_set1_ps(1.0f);
        const __m128 zero = _mm_setzero_ps();
        switch (opcode)
        {
        case 0:  out = _mm_add_ps(a, b); return true;
        case 1:  out = _mm_mul_ps(a, b); return true;
        case 2:  out = _mm_max_ps(a, b); return true;
        case 3:  out = _mm_min_ps(a, b); return true;
        case 4:  out = _mm_and_ps(_mm_cmpeq_ps(a, b), one); return true;
        case 5:  out = _mm_and_ps(_mm_cmpgt_ps(a, b), one); return true;
        case 6:  out = _mm_and_ps(_mm_cmpge_ps(a, b), one); return true;
        case 7:  out = _mm_and_ps(_mm_cmpneq_ps(a, b), one); return true;
        case 8:  out = _mm_sub_ps(a, _mm_floor_ps(a)); return true;
        case 9:  out = _mm_round_ps(a, _MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC); return true;
        case 10: out = _mm_floor_ps(a); return true;
        case 11: out = _mm_add_ps(_mm_mul_ps(a, b), c); return true;
        case 12: out = _mm_blendv_ps(c, b, _mm_cmpeq_ps(a, zero)); return true;
        case 13: out = _mm_blendv_ps(c, b, _mm_cmpge_ps(a, zero)); return true;
        case 14: out = _mm_blendv_ps(c, b, _mm_cmpgt_ps(a, zero)); return true;
        case 15: out = _mm_set1_ps(LaneSum4(_mm_mul_ps(a, b))); return true;
        case 16:
        {
            const __m128 product = _mm_mul_ps(a, b);
            alignas(16) float lanes[4];
            _mm_store_ps(lanes, product);
            out = _mm_set1_ps(lanes[0] + lanes[1] + lanes[2]);
            return true;
        }
        case 17:
        {
            alignas(16) float pa[4], pc[4];
            _mm_store_ps(pa, _mm_mul_ps(a, b));
            _mm_store_ps(pc, c);
            out = _mm_set1_ps(pa[0] + pa[1] + pc[0]);
            return true;
        }
        case 19:
        {
            __m128 m = _mm_max_ps(a, _mm_movehl_ps(a, a));
            m = _mm_max_ss(m, _mm_shuffle_ps(m, m, 1));
            out = _mm_set1_ps(_mm_cvtss_f32(m));
            return true;
        }
        default:
            return false;
        }
    }

    void RunAluFast(Context& context, const DecodedAlu& alu)
    {
        const __m128 a = GatherSource(context, alu.source[0]);
        const __m128 b = GatherSource(context, alu.source[1]);
        const __m128 c = GatherSource(context, alu.source[2]);

        if (alu.vectorMask != 0)
        {
            __m128 result;
            if (!ApplyVectorFast(alu.vectorOpcode, a, b, c, result))
            {
                ReportOnce("unhandled vector opcode", alu.vectorOpcode);
                std::lock_guard<std::mutex> lock(g_statisticsMutex);
                g_statistics.unknownOpcode++;
            }
            else
            {
                if (alu.clamp)
                    result = _mm_min_ps(_mm_max_ps(result, _mm_setzero_ps()), _mm_set1_ps(1.0f));
                Vec4& destination = alu.exported
                    ? context.exports[alu.vectorDest] : context.registers[alu.vectorDest];
                Store(destination, _mm_blendv_ps(Load(destination), result, MaskFromBits(alu.vectorMask)));
                if (alu.exported) context.exported[alu.vectorDest] = true;
            }
        }

        if (alu.scalarMask != 0)
        {
            float result = 0;
            bool handled = false;
            const uint32_t opcode = alu.scalarOpcode;
            if (opcode >= 42 && opcode <= 47)
            {
                const DecodedSource& third = alu.source[2];
                const uint32_t reg2 = (opcode & 1) | (third.swizzle & 0x3C)
                                    | (uint32_t(third.temporary) << 1);
                const Vec4 constant = ReadConstant(context.pixel, third.reg);
                const Vec4& temporary = context.registers[reg2 & (MaxRegisters - 1)];
                float x = constant.v[third.swizzle & 3];
                const float y = temporary.v[(third.swizzle >> 6) & 3];
                if (third.negate) x = -x;
                switch (opcode)
                {
                case 42: case 43: result = x * y; break;
                case 44: case 45: result = x + y; break;
                default:          result = x - y; break;
                }
                handled = true;
            }
            else
            {
                Vec4 lanes;
                Store(lanes, c);
                handled = ApplyScalar(opcode, lanes, context.previousScalar, result);
            }

            if (!handled)
            {
                ReportOnce("unhandled scalar opcode", opcode);
                std::lock_guard<std::mutex> lock(g_statisticsMutex);
                g_statistics.unknownOpcode++;
            }
            else
            {
                context.previousScalar = result;
                Vec4& destination = alu.exported
                    ? context.exports[alu.scalarDest] : context.registers[alu.scalarDest];
                Store(destination, _mm_blendv_ps(Load(destination), _mm_set1_ps(result), MaskFromBits(alu.scalarMask)));
                if (alu.exported) context.exported[alu.scalarDest] = true;
            }
        }
    }

    void RunPixel(const Program& program, Context& context)
    {
        for (const DecodedStep& step : program.steps)
        {
            if (step.fetch) RunFetch(context, step.d0, step.d1, step.d2);
            else RunAluFast(context, step.alu);
            if (context.textureNeeded) return;
        }
    }

    // --- the surface -------------------------------------------------------

    struct Surface
    {
        uint32_t pitch = 0;
        uint32_t baseTile = 0;
        uint32_t samplesX = 1;
        uint32_t samplesY = 1;
        uint32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        bool valid = false;
    };

    Surface CurrentSurface()
    {
        Surface surface;
        const uint32_t info = Reg(0x2000);
        const uint32_t colour = Reg(0x2001);
        const uint32_t tl = Reg(0x2081);
        const uint32_t br = Reg(0x2082);

        // At the resolution scale: the pitch and the scissor grow with it,
        // and a surface's tile base grows with its square, since each tile
        // row of the title's becomes that many rows of that many tiles.
        const uint32_t scale = Edram::Scale();
        surface.pitch = (info & 0x3FFF) * scale;
        const uint32_t msaa = (info >> 16) & 3;
        surface.samplesX = (msaa == 2) ? 2u : 1u;
        surface.samplesY = (msaa >= 1) ? 2u : 1u;
        surface.baseTile = (colour & 0xFFF) * scale * scale;

        surface.x0 = (tl & 0x7FFF) * scale;
        surface.y0 = ((tl >> 16) & 0x7FFF) * scale;
        surface.x1 = (br & 0x7FFF) * scale;
        surface.y1 = ((br >> 16) & 0x7FFF) * scale;

        surface.valid = surface.pitch != 0 && surface.x1 > surface.x0
                     && surface.y1 > surface.y0 && surface.x1 <= 4096 * scale
                     && surface.y1 <= 4096 * scale;
        return surface;
    }

    // Blending.
    //
    // A menu is drawn as text and panels with transparent edges over what is
    // already there, so a runtime that only ever writes the source colour draws
    // every letter inside a black box. The equation is in one register: a
    // factor for the source, a factor for what is already there, and how to
    // combine them.
    struct Blend
    {
        uint32_t sourceFactor = 1;      // one
        uint32_t destFactor = 0;        // zero
        uint32_t function = 0;          // add
        uint32_t sourceAlphaFactor = 1;
        uint32_t destAlphaFactor = 0;
        uint32_t alphaFunction = 0;
        bool needed = false;
    };

    Blend CurrentBlend()
    {
        const uint32_t control = Reg(0x2201);

        Blend blend;
        blend.sourceFactor = control & 0x1F;
        blend.function = (control >> 5) & 7;
        blend.destFactor = (control >> 8) & 0x1F;
        blend.sourceAlphaFactor = (control >> 16) & 0x1F;
        blend.alphaFunction = (control >> 21) & 7;
        blend.destAlphaFactor = (control >> 24) & 0x1F;

        // Source times one plus destination times zero is a plain write, and
        // most draws are that.
        blend.needed = !(blend.sourceFactor == 1 && blend.destFactor == 0 &&
                         blend.function == 0 && blend.sourceAlphaFactor == 1 &&
                         blend.destAlphaFactor == 0 && blend.alphaFunction == 0);
        return blend;
    }

    float Factor(uint32_t which, float sourceChannel, float sourceAlpha,
                 float destChannel, float destAlpha)
    {
        switch (which)
        {
        case 0:  return 0.0f;
        case 1:  return 1.0f;
        case 4:  return sourceChannel;
        case 5:  return 1.0f - sourceChannel;
        case 6:  return sourceAlpha;
        case 7:  return 1.0f - sourceAlpha;
        case 8:  return destChannel;
        case 9:  return 1.0f - destChannel;
        case 10: return destAlpha;
        case 11: return 1.0f - destAlpha;
        default: return 1.0f;
        }
    }

    float Combine(uint32_t function, float source, float dest)
    {
        switch (function)
        {
        case 0:  return source + dest;
        case 1:  return source - dest;
        case 2:  return std::min(source, dest);
        case 3:  return std::max(source, dest);
        case 4:  return dest - source;
        default: return source + dest;
        }
    }

    uint32_t Pack8888(const Vec4& colour)
    {
        auto Component = [](float value) -> uint32_t {
            const float clamped = std::min(1.0f, std::max(0.0f, value));
            return uint32_t(clamped * 255.0f + 0.5f);
        };
        // Stored so that the resolve, and then the presenter, read it as the
        // alpha, red, green and blue the window expects.
        return (Component(colour.v[3]) << 24) | (Component(colour.v[0]) << 16)
             | (Component(colour.v[1]) << 8) | Component(colour.v[2]);
    }

    Vec4 Unpack8888(uint32_t value)
    {
        Vec4 colour;
        colour.v[0] = ((value >> 16) & 0xFF) / 255.0f;
        colour.v[1] = ((value >> 8) & 0xFF) / 255.0f;
        colour.v[2] = (value & 0xFF) / 255.0f;
        colour.v[3] = ((value >> 24) & 0xFF) / 255.0f;
        return colour;
    }

    uint32_t ReadPixel(const Surface& surface, uint32_t x, uint32_t y)
    {
        const uint32_t* edram = Edram::Data();
        const uint32_t pitchSamples = surface.pitch * surface.samplesX;
        const uint32_t offset = Edram::SampleOffset(
            x * surface.samplesX, y * surface.samplesY, pitchSamples, surface.baseTile);
        return offset < Edram::Capacity() ? edram[offset] : 0;
    }

    void WritePixel(const Surface& surface, uint32_t x, uint32_t y, uint32_t value)
    {
        uint32_t* edram = Edram::Data();
        const uint32_t pitchSamples = surface.pitch * surface.samplesX;
        for (uint32_t j = 0; j < surface.samplesY; j++)
        {
            for (uint32_t i = 0; i < surface.samplesX; i++)
            {
                const uint32_t offset = Edram::SampleOffset(
                    x * surface.samplesX + i, y * surface.samplesY + j,
                    pitchSamples, surface.baseTile);
                if (offset < Edram::Capacity()) edram[offset] = value;
            }
        }
    }

    // --- rasterisation -----------------------------------------------------

    struct Vertex
    {
        Vec4 position;                            // in screen space
        float inverseW = 1.0f;
        Vec4 interpolants[MaxInterpolators];
    };

    // Turns what a vertex program exported into a position on the target.
    //
    // PA_CL_VTE_CNTL says which parts of this to do. Its two format bits say
    // whether the coordinates have already been divided by w, and six enable
    // bits say whether each viewport scale and offset applies. The clear this
    // title issues has the format bits set and every enable clear, so its
    // coordinates pass straight through; its textured geometry does not, and
    // needs the whole transform.
    void ApplyViewport(Vec4& position, float& inverseW)
    {
        const uint32_t control = Reg(0x2206);
        const float w = position.v[3];
        inverseW = (w != 0.0f) ? 1.0f / w : 0.0f;

        if ((control & 0x100) == 0)   // xy not already divided
        {
            position.v[0] *= inverseW;
            position.v[1] *= inverseW;
        }
        if ((control & 0x200) == 0) position.v[2] *= inverseW;

        if (control & 0x01) position.v[0] *= RegFloat(0x210F);
        if (control & 0x02) position.v[0] += RegFloat(0x2110);
        if (control & 0x04) position.v[1] *= RegFloat(0x2111);
        if (control & 0x08) position.v[1] += RegFloat(0x2112);
        if (control & 0x10) position.v[2] *= RegFloat(0x2113);
        if (control & 0x20) position.v[2] += RegFloat(0x2114);

        // Screen space at the resolution scale.
        const float scale = float(Edram::Scale());
        position.v[0] *= scale;
        position.v[1] *= scale;
    }

    void RasterizeTriangle(const Surface& surface, const Program& pixelProgram,
                           const Blend& blend,
                           const Vertex& a, const Vertex& b, const Vertex& c,
                           uint64_t& pixelsWritten)
    {
        const float ax = a.position.v[0], ay = a.position.v[1];
        const float bx = b.position.v[0], by = b.position.v[1];
        const float cx = c.position.v[0], cy = c.position.v[1];

        const float area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
        if (std::fabs(area) < 1e-6f) return;
        const float inverseArea = 1.0f / area;

        // A vertex behind the eye cannot be interpolated towards, and the
        // triangle would need clipping first.
        if (a.inverseW <= 0.0f || b.inverseW <= 0.0f || c.inverseW <= 0.0f) return;

        int minX = int(std::floor(std::min({ ax, bx, cx })));
        int maxX = int(std::ceil (std::max({ ax, bx, cx })));
        int minY = int(std::floor(std::min({ ay, by, cy })));
        int maxY = int(std::ceil (std::max({ ay, by, cy })));

        minX = std::max(minX, int(surface.x0));
        minY = std::max(minY, int(surface.y0));
        maxX = std::min(maxX, int(surface.x1) - 1);
        maxY = std::min(maxY, int(surface.y1) - 1);
        if (minX > maxX || minY > maxY) return;

        // The rows, in bands, across every core.
        //
        // Each band has a shader context of its own, because the interpreter
        // keeps its registers there, and its own count of pixels written.
        // Nothing else is shared that is written: the tiled address function
        // is a permutation so no two rows meet in EDRAM, blending reads only
        // the pixel it is about to write, and textures are read only. A
        // triangle that turns out to need a texture the runtime cannot read
        // stops every band through one flag rather than being half drawn.
        std::atomic<uint64_t> written{ 0 };
        std::atomic<bool> missing{ false };

        auto rows = [&](int firstRow, int lastRow)
        {
        Context context;
        context.pixel = true;
        uint64_t writtenHere = 0;   // one atomic add a band, not one a pixel

        for (int y = firstRow; y < lastRow; y++)
        {
            if (missing.load(std::memory_order_relaxed)) break;
            for (int x = minX; x <= maxX; x++)
            {
                const float px = float(x);
                const float py = float(y);

                // Barycentric weights. A pixel is covered when all three have
                // the same sign as the triangle's own area.
                float w0 = ((bx - px) * (cy - py) - (by - py) * (cx - px)) * inverseArea;
                float w1 = ((cx - px) * (ay - py) - (cy - py) * (ax - px)) * inverseArea;
                float w2 = 1.0f - w0 - w1;
                if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;

                // Interpolation is done in a space where it is linear, which
                // means dividing each attribute by w first and dividing the
                // result back afterwards. With w one throughout, as the clear
                // has, this reduces to the plain weighted average.
                const float oneOverW = a.inverseW * w0 + b.inverseW * w1
                                     + c.inverseW * w2;
                if (oneOverW <= 0.0f) continue;
                const float perspective = 1.0f / oneOverW;

                for (uint32_t i = 0; i < pixelProgram.registersUsed; i++)
                    for (int k = 0; k < 4; k++)
                        context.registers[i].v[k] = perspective *
                            (a.interpolants[i].v[k] * a.inverseW * w0 +
                             b.interpolants[i].v[k] * b.inverseW * w1 +
                             c.interpolants[i].v[k] * c.inverseW * w2);

                context.textureNeeded = false;
                RunPixel(pixelProgram, context);
                if (context.textureNeeded) { missing.store(true); break; }

                Vec4 colour = context.exports[0];
                if (blend.needed)
                {
                    const Vec4 behind =
                        Unpack8888(ReadPixel(surface, uint32_t(x), uint32_t(y)));
                    Vec4 mixed;
                    for (int k = 0; k < 3; k++)
                    {
                        const float source = colour.v[k] * Factor(blend.sourceFactor,
                            colour.v[k], colour.v[3], behind.v[k], behind.v[3]);
                        const float dest = behind.v[k] * Factor(blend.destFactor,
                            colour.v[k], colour.v[3], behind.v[k], behind.v[3]);
                        mixed.v[k] = Combine(blend.function, source, dest);
                    }
                    const float sourceAlpha = colour.v[3] * Factor(
                        blend.sourceAlphaFactor, colour.v[3], colour.v[3],
                        behind.v[3], behind.v[3]);
                    const float destAlpha = behind.v[3] * Factor(
                        blend.destAlphaFactor, colour.v[3], colour.v[3],
                        behind.v[3], behind.v[3]);
                    mixed.v[3] = Combine(blend.alphaFunction, sourceAlpha, destAlpha);
                    colour = mixed;
                }

                WritePixel(surface, uint32_t(x), uint32_t(y), Pack8888(colour));
                writtenHere++;
            }
        }
        written.fetch_add(writtenHere, std::memory_order_relaxed);
        };

        // Eight rows a piece is fine enough that a full screen quad spreads
        // over every core and coarse enough that handing out a piece is not
        // the work. A triangle under a band tall runs inline.
        const auto started = std::chrono::steady_clock::now();
        Parallel::For(minY, maxY + 1, 8, rows);
        const auto took = std::chrono::steady_clock::now() - started;
        g_rasterNanoseconds.fetch_add(
            uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(took).count()),
            std::memory_order_relaxed);
        pixelsWritten += written.load();
    }
}

void Raster::Draw(uint32_t initiator, uint32_t indexBase, uint32_t indexWord)
{
    SnapshotRegisters();
    const uint32_t primitive = initiator & 0x3F;
    const uint32_t sourceSelect = (initiator >> 6) & 3;
    const uint32_t indexCount = initiator >> 16;
    const bool wideIndices = ((initiator >> 11) & 1) != 0;

    // Why a draw was skipped, counted by reason. A single total says nothing
    // about which check is the one standing in the way.
    static std::mutex reasonMutex;
    static std::map<std::string, uint64_t> reasons;
    auto Skip = [&](const char* reason) {
        {
            std::lock_guard<std::mutex> lock(reasonMutex);
            char key[96];
            snprintf(key, sizeof(key), "%s (primitive %u)", reason, primitive);
            reasons[key]++;
        }
        std::lock_guard<std::mutex> lock(g_statisticsMutex);
        g_statistics.skipped++;
    };
    ReportSkips = [&]() {
        std::lock_guard<std::mutex> lock(reasonMutex);
        for (const auto& entry : reasons)
            printf("        skipped: %s x%llu\n", entry.first.c_str(),
                (unsigned long long)entry.second);
    };

    // Only whole triangles, rectangles and quads put pixels anywhere. A point
    // list of one index covers a single pixel and is not worth the work.
    if (primitive != 4 && primitive != 6 && primitive != 8 && primitive != 13)
    { Skip("not a filled primitive"); return; }
    if (sourceSelect != 0 && sourceSelect != 2) { Skip("indices come from the packet"); return; }
    if (sourceSelect == 0 && indexBase == 0) { Skip("no index buffer address"); return; }
    if (indexCount < 3 || indexCount > 4096) { Skip("too few or too many indices"); return; }
    (void)indexWord;

    const Surface surface = CurrentSurface();
    if (!surface.valid) { Skip("no usable render target"); return; }

    // The render backend has to be writing colour. Copy mode is a resolve and
    // ignore mode draws nothing at all.
    if ((Reg(0x2208) & 7) != 4) { Skip("render backend is not writing colour"); return; }

    const Blend blend = CurrentBlend();
    const Program& vertexProgram = CachedProgram(false);
    const Program& pixelProgram = CachedProgram(true);
    if (!vertexProgram.valid || !pixelProgram.valid) { Skip("no shader program bound"); return; }

    // With no index buffer the indices are simply nought upwards; with one,
    // they are read from memory at the width the initiator states.
    const uint32_t indexAddress = Guest::PhysicalAlias(indexBase);

    std::vector<Vertex> vertices(indexCount);
    for (uint32_t i = 0; i < indexCount; i++)
    {
        uint32_t index = i;
        if (sourceSelect == 0)
        {
            index = wideIndices
                ? Guest::Read32(Guest::Base, indexAddress + i * 4)
                : Guest::Read16(Guest::Base, indexAddress + i * 2);
        }

        Context context;
        context.pixel = false;
        context.vertexIndex = index;
        Run(vertexProgram, context);
        if (context.textureNeeded) { Skip("the vertex program needs something missing"); return; }

        vertices[i].position = context.exports[62];
        ApplyViewport(vertices[i].position, vertices[i].inverseW);
        for (uint32_t k = 0; k < MaxInterpolators; k++)
            vertices[i].interpolants[k] = context.exports[k];
    }

    uint64_t triangles = 0;
    uint64_t pixels = 0;

    if (primitive == 8)
    {
        // A rectangle list gives three corners and the fourth is implied.
        for (uint32_t i = 0; i + 2 < indexCount; i += 3)
        {
            Vertex fourth = vertices[i];
            fourth.inverseW = vertices[i].inverseW + vertices[i + 2].inverseW
                            - vertices[i + 1].inverseW;
            for (int k = 0; k < 4; k++)
                fourth.position.v[k] = vertices[i].position.v[k]
                                     + vertices[i + 2].position.v[k]
                                     - vertices[i + 1].position.v[k];
            for (uint32_t j = 0; j < MaxInterpolators; j++)
                for (int k = 0; k < 4; k++)
                    fourth.interpolants[j].v[k] = vertices[i].interpolants[j].v[k]
                                                + vertices[i + 2].interpolants[j].v[k]
                                                - vertices[i + 1].interpolants[j].v[k];

            RasterizeTriangle(surface, pixelProgram, blend,
                vertices[i], vertices[i + 1], vertices[i + 2], pixels);
            RasterizeTriangle(surface, pixelProgram, blend,
                vertices[i], vertices[i + 2], fourth, pixels);
            triangles += 2;
        }
    }
    else if (primitive == 13)
    {
        // A quad list is four corners in order, which is two triangles sharing
        // the diagonal between the first and the third.
        for (uint32_t i = 0; i + 3 < indexCount; i += 4)
        {
            RasterizeTriangle(surface, pixelProgram, blend,
                vertices[i], vertices[i + 1], vertices[i + 2], pixels);
            RasterizeTriangle(surface, pixelProgram, blend,
                vertices[i], vertices[i + 2], vertices[i + 3], pixels);
            triangles += 2;
        }
    }
    else if (primitive == 4)
    {
        for (uint32_t i = 0; i + 2 < indexCount; i += 3)
        {
            RasterizeTriangle(surface, pixelProgram, blend,
                vertices[i], vertices[i + 1], vertices[i + 2], pixels);
            triangles++;
        }
    }
    else
    {
        for (uint32_t i = 0; i + 2 < indexCount; i++)
        {
            RasterizeTriangle(surface, pixelProgram, blend,
                vertices[i], vertices[i + 1], vertices[i + 2], pixels);
            triangles++;
        }
    }

    std::lock_guard<std::mutex> lock(g_statisticsMutex);
    g_statistics.draws++;
    g_statistics.triangles += triangles;
    g_statistics.pixels += pixels;
    if (pixels == 0) g_statistics.needTexture++;
}

Raster::Statistics Raster::Stats()
{
    std::lock_guard<std::mutex> lock(g_statisticsMutex);
    return g_statistics;
}

void Raster::Report()
{
    const Statistics stats = Stats();
    if (stats.draws + stats.skipped == 0) return;

    printf("raster: %llu draws rasterised, %llu triangles, %llu pixels written, "
           "%llu draws skipped\n",
        (unsigned long long)stats.draws, (unsigned long long)stats.triangles,
        (unsigned long long)stats.pixels, (unsigned long long)stats.skipped);
    {
        const double seconds = g_rasterNanoseconds.load() / 1e9;
        if (seconds > 0.0)
            printf("        %.1f s inside triangles, %.1f million pixels a second, "
                   "%d threads\n",
                seconds, stats.pixels / seconds / 1e6, Parallel::Width());
    }
    if (ReportSkips) ReportSkips();
    if (stats.unknownOpcode || stats.unknownFormat)
        printf("        %llu unhandled opcodes, %llu unhandled vertex formats\n",
            (unsigned long long)stats.unknownOpcode,
            (unsigned long long)stats.unknownFormat);
    fflush(stdout);
}
