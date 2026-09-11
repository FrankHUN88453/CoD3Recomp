// EDRAM and the resolve.
//
// The register numbering used here was not taken on trust. It was read off the
// title's own register file at the moment it issued a rectangle list draw, and
// three independent things line up: the viewport scale and offset at 0x210F to
// 0x2112 are 520, 520, -312, 312, which is exactly a 1040 by 624 target; the
// window scissor at 0x2082 is 1040 by 624; and the copy destination at 0x2319
// is 0x19CC0000, the same buffer VdSwap names as the front buffer. A guessed
// map does not produce three agreements.

#include "kernel.h"
#include "gpu.h"
#include "edram.h"
#include "window.h"

#include <atomic>
#include <cstdio>
#include <map>
#include <mutex>
#include <vector>

namespace
{
    std::vector<uint32_t> g_edram(Edram::SizeDwords, 0u);

    std::mutex g_statisticsMutex;
    Edram::Statistics g_statistics;

    // Set by the self test at start up. When the tiled address function is not
    // a permutation it cannot be used, and a linear layout is used instead:
    // both ends of the copy go through the same function, so the picture is
    // right either way, but only one of them matches the hardware.
    bool g_tilingUsable = false;
    bool g_tilingChecked = false;

    // Render backend registers, by their index in the aperture.
    constexpr uint32_t RegSurfaceInfo   = 0x2000;
    constexpr uint32_t RegColorInfo     = 0x2001;
    constexpr uint32_t RegScissorTl     = 0x2081;
    constexpr uint32_t RegScissorBr     = 0x2082;
    constexpr uint32_t RegModeControl   = 0x2208;
    constexpr uint32_t RegCopyControl   = 0x2318;
    constexpr uint32_t RegCopyDestBase  = 0x2319;
    constexpr uint32_t RegCopyDestPitch = 0x231A;
    constexpr uint32_t RegCopyDestInfo  = 0x231B;

    uint32_t Reg(uint32_t index)
    {
        return Gpu::ReadRegister(Gpu::ApertureBase + index * 4);
    }

    uint32_t Offset(uint32_t x, uint32_t y, uint32_t pitch)
    {
        if (g_tilingUsable) return Edram::TiledOffset(x, y, pitch, 2);
        return (y * pitch + x) * 4;
    }

    const char* CopyCommandName(uint32_t command)
    {
        switch (command)
        {
        case 0:  return "raw";
        case 1:  return "convert";
        case 2:  return "constant one";
        default: return "null";
        }
    }

    // Every distinct copy control word is reported once with its fields spelled
    // out. Which of them the title uses decides what has to be implemented, and
    // the alternative to reading it off the title is guessing.
    void ReportControl(uint32_t control, uint32_t destBase, uint32_t destPitch,
                       uint32_t destInfo, uint32_t width, uint32_t height)
    {
        static std::mutex mutex;
        static std::map<uint32_t, bool> seen;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (seen.find(control) != seen.end()) return;
            seen[control] = true;
        }

        printf("resolve: control %08X -> source %u, samples %u, command %s,"
               " colour clear %s, depth clear %s\n",
            control, control & 7, (control >> 4) & 7,
            CopyCommandName((control >> 20) & 3),
            ((control >> 8) & 1) ? "yes" : "no",
            ((control >> 9) & 1) ? "yes" : "no");
        printf("         destination 0x%08X, %ux%u, pitch %u, info %08X\n",
            destBase, width, height, destPitch, destInfo);
        printf("         copy block:");
        for (uint32_t index = RegCopyControl; index <= 0x2330; index++)
        {
            const uint32_t value = Reg(index);
            if (value != 0) printf(" [%04X]=%08X", index, value);
        }
        printf("\n");
        fflush(stdout);
    }
}

uint32_t* Edram::Data() { return g_edram.data(); }

Edram::Statistics Edram::Stats()
{
    std::lock_guard<std::mutex> lock(g_statisticsMutex);
    return g_statistics;
}

bool Edram::SelfTest()
{
    if (g_tilingChecked) return g_tilingUsable;
    g_tilingChecked = true;

    // A resolve destination this title uses: 1056 texels across, 624 down.
    // A tiled surface is allocated in whole 32 by 32 blocks, so the offsets
    // for the last rows land inside a surface rounded up to 640.
    constexpr uint32_t pitch = 1056;
    constexpr uint32_t height = 624;
    constexpr uint32_t allocatedHeight = 640;
    const size_t texels = size_t(pitch) * allocatedHeight;

    std::vector<uint8_t> hit(texels, 0);
    uint32_t collisions = 0;
    uint32_t outOfRange = 0;

    for (uint32_t y = 0; y < height; y++)
    {
        for (uint32_t x = 0; x < pitch; x++)
        {
            const uint32_t byteOffset = TiledOffset(x, y, pitch, 2);
            if (byteOffset % 4 != 0 || byteOffset / 4 >= texels) { outOfRange++; continue; }
            if (hit[byteOffset / 4]++) collisions++;
        }
    }

    g_tilingUsable = (collisions == 0 && outOfRange == 0);

    if (g_tilingUsable)
    {
        printf("edram: the tiled address function maps %ux%u one to one, using it\n",
            pitch, height);
    }
    else
    {
        printf("edram: the tiled address function is not a permutation over %ux%u "
               "(%u collisions, %u out of range), falling back to a linear layout\n",
            pitch, height, collisions, outOfRange);
        printf("       the picture is still correct: the resolve writes and the "
               "presenter reads through the same function\n");
    }
    fflush(stdout);
    return g_tilingUsable;
}

void Edram::Resolve()
{
    SelfTest();

    const uint32_t control = Reg(RegCopyControl);
    const uint32_t destBase = Reg(RegCopyDestBase);
    const uint32_t destPitchWord = Reg(RegCopyDestPitch);
    const uint32_t destInfo = Reg(RegCopyDestInfo);
    const uint32_t surfaceInfo = Reg(RegSurfaceInfo);
    const uint32_t colorInfo = Reg(RegColorInfo);
    const uint32_t scissorTl = Reg(RegScissorTl);
    const uint32_t scissorBr = Reg(RegScissorBr);

    if (destBase == 0) return;

    const uint32_t x0 = scissorTl & 0x7FFF;
    const uint32_t y0 = (scissorTl >> 16) & 0x7FFF;
    const uint32_t x1 = scissorBr & 0x7FFF;
    const uint32_t y1 = (scissorBr >> 16) & 0x7FFF;
    if (x1 <= x0 || y1 <= y0) return;

    const uint32_t width = x1 - x0;
    const uint32_t height = y1 - y0;
    if (width > 4096 || height > 4096) return;

    const uint32_t surfacePitch = surfaceInfo & 0x3FFF;
    const uint32_t msaa = (surfaceInfo >> 16) & 3;
    const uint32_t colorBaseTile = colorInfo & 0xFFF;
    if (surfacePitch == 0) return;

    const uint32_t destPitch = destPitchWord & 0x3FFF;
    if (destPitch == 0) return;

    ReportControl(control, destBase, destPitch, destInfo, width, height);

    // Every distinct destination, once, with the range it covers. A resolve
    // writes into guest memory the title is also using for other things, so a
    // destination or an extent that is wrong here corrupts the title rather
    // than merely drawing badly.
    {
        static std::mutex mutex;
        static std::map<uint32_t, bool> seen;
        bool first = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (seen.find(destBase) == seen.end()) { seen[destBase] = true; first = true; }
        }
        if (first)
        {
            const uint32_t last = destPitch * height * 4;
            printf("resolve: writing 0x%08X to 0x%08X, %ux%u, pitch %u\n",
                destBase, destBase + last, width, height, destPitch);
            fflush(stdout);
        }
    }

    // A copy that is only there to clear does not move any pixels.
    const uint32_t command = (control >> 20) & 3;
    const bool colorClear = ((control >> 8) & 1) != 0;

    const uint32_t samplesX = (msaa == 2) ? 2u : 1u;
    const uint32_t samplesY = (msaa >= 1) ? 2u : 1u;
    const uint32_t pitchSamples = surfacePitch * samplesX;
    const uint32_t sampleSelect = (control >> 4) & 7;

    uint64_t written = 0;
    uint64_t nonZero = 0;

    if (command != 3)   // null: clear only, nothing is copied
    {
        for (uint32_t y = 0; y < height; y++)
        {
            for (uint32_t x = 0; x < width; x++)
            {
                const uint32_t sx = (x0 + x) * samplesX;
                const uint32_t sy = (y0 + y) * samplesY;

                // Gather the samples this pixel is made of, then combine them
                // the way the copy control asks for.
                uint32_t samples[4];
                uint32_t count = 0;
                for (uint32_t j = 0; j < samplesY; j++)
                {
                    for (uint32_t i = 0; i < samplesX; i++)
                    {
                        const uint32_t offset =
                            SampleOffset(sx + i, sy + j, pitchSamples, colorBaseTile);
                        samples[count++] = offset < SizeDwords ? g_edram[offset] : 0u;
                    }
                }

                uint32_t value = samples[0];
                if (count > 1)
                {
                    if (sampleSelect < count)
                    {
                        value = samples[sampleSelect];
                    }
                    else
                    {
                        // An average of the samples, one byte lane at a time.
                        uint32_t sum[4] = { 0, 0, 0, 0 };
                        for (uint32_t s = 0; s < count; s++)
                            for (int lane = 0; lane < 4; lane++)
                                sum[lane] += (samples[s] >> (lane * 8)) & 0xFF;
                        value = 0;
                        for (int lane = 0; lane < 4; lane++)
                            value |= ((sum[lane] / count) & 0xFF) << (lane * 8);
                    }
                }

                if (value != 0) nonZero++;
                written++;

                const uint32_t address = destBase + Offset(x, y, destPitch);
                Guest::WritePhysical32(address, value);
            }
        }
    }

    // The clear happens after the copy: the surface is emptied ready for the
    // next frame's geometry.
    if (colorClear)
    {
        const uint32_t clearValue = Reg(0x2320);
        for (uint32_t y = 0; y < height * samplesY; y++)
        {
            for (uint32_t x = 0; x < width * samplesX; x++)
            {
                const uint32_t offset = SampleOffset(
                    (x0 * samplesX) + x, (y0 * samplesY) + y, pitchSamples, colorBaseTile);
                if (offset < SizeDwords) g_edram[offset] = clearValue;
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(g_statisticsMutex);
        g_statistics.resolves++;
        if (colorClear) g_statistics.clears++;
        g_statistics.texelsWritten += written;
        g_statistics.texelsNonZero += nonZero;
        g_statistics.lastDestination = destBase;
        g_statistics.lastWidth = width;
        g_statistics.lastHeight = height;
    }

    // The presenter reads this buffer through the same layout the copy wrote,
    // so it has to be told the pitch as well as the visible size.
    Window::SetFrontBuffer(Guest::PhysicalAlias(destBase), width, height);
    Window::SetFrontBufferLayout(destPitch, g_tilingUsable);
}

void Edram::Report()
{
    const Statistics stats = Stats();
    if (stats.resolves == 0)
    {
        printf("resolves: none, nothing has been copied out of EDRAM\n");
        fflush(stdout);
        return;
    }

    printf("resolves: %llu into 0x%08X, %ux%u, %llu texels written, %llu of them "
           "not black\n",
        (unsigned long long)stats.resolves, stats.lastDestination,
        stats.lastWidth, stats.lastHeight,
        (unsigned long long)stats.texelsWritten,
        (unsigned long long)stats.texelsNonZero);
    fflush(stdout);
}
