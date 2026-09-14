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
#include "parallel.h"
#include "window.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <vector>

namespace
{
    std::vector<uint32_t> g_edram(Edram::SizeDwords, 0u);

    // The scale in force and the one the window last asked for.
    std::atomic<uint32_t> g_scale{ 1 };
    std::atomic<uint32_t> g_requestedScale{ 1 };

    // The large copies of resolved surfaces, keyed by physical address.
    struct ShadowImage
    {
        uint32_t width = 0, height = 0;
        std::vector<uint32_t> pixels;
    };
    std::mutex g_shadowMutex;
    std::map<uint32_t, ShadowImage> g_shadows;

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

uint32_t Edram::Scale() { return g_scale.load(std::memory_order_relaxed); }
uint32_t Edram::Capacity() { return uint32_t(g_edram.size()); }

void Edram::RequestScale(uint32_t scale)
{
    if (scale < 1) scale = 1;
    if (scale > 4) scale = 4;
    g_requestedScale.store(scale);
}

Edram::Shadow Edram::ShadowFor(uint32_t physicalAddress)
{
    std::lock_guard<std::mutex> lock(g_shadowMutex);
    auto found = g_shadows.find(physicalAddress & 0x1FFFFFFFu);
    if (found == g_shadows.end() || found->second.pixels.empty()) return Shadow{};
    return Shadow{ found->second.width, found->second.height, found->second.pixels.data() };
}

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

    // Everything on the EDRAM side is at the scale the frame was drawn at;
    // the destination in the title's memory stays the size it expects.
    const uint32_t scale = g_scale.load();
    const uint32_t surfacePitch = (surfaceInfo & 0x3FFF) * scale;
    const uint32_t msaa = (surfaceInfo >> 16) & 3;
    const uint32_t colorBaseTile = (colorInfo & 0xFFF) * scale * scale;
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

    std::atomic<uint64_t> written{ 0 };
    std::atomic<uint64_t> nonZero{ 0 };

    if (command != 3)   // null: clear only, nothing is copied
    {
        // The rows across every core. Each texel is written once, through
        // the window the GPU and the presenter read it back through: this
        // used to write every alias of the physical address, four stores to
        // four distant pages a texel, and the resolve was the largest single
        // cost of a frame.
        uint8_t* destination = Guest::Base + Guest::PhysicalAlias(destBase);

        // The large picture, one texel per scaled sample position, kept for
        // the window and for anything that draws with this surface as a
        // texture. Built here and swapped in under the lock when complete.
        const uint32_t largeWidth = width * scale;
        const uint32_t largeHeight = height * scale;
        std::vector<uint32_t> large(size_t(largeWidth) * largeHeight);

        auto rows = [&](int firstRow, int lastRow)
        {
            uint64_t writtenHere = 0, nonZeroHere = 0;
            for (uint32_t y = uint32_t(firstRow); y < uint32_t(lastRow); y++)
            {
                for (uint32_t x = 0; x < largeWidth; x++)
                {
                    const uint32_t sx = (x0 * scale + x) * samplesX;
                    const uint32_t sy = (y0 * scale + y) * samplesY;

                    // Gather the samples this pixel is made of, then combine
                    // them the way the copy control asks for.
                    uint32_t samples[4];
                    uint32_t count = 0;
                    for (uint32_t j = 0; j < samplesY; j++)
                    {
                        for (uint32_t i = 0; i < samplesX; i++)
                        {
                            const uint32_t offset =
                                SampleOffset(sx + i, sy + j, pitchSamples, colorBaseTile);
                            samples[count++] = offset < g_edram.size() ? g_edram[offset] : 0u;
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
                            // The average of the samples, all four byte lanes
                            // at once: the shared bits plus half the differing
                            // ones, which is exact for two and a rounding away
                            // for four.
                            auto Average = [](uint32_t a, uint32_t b) {
                                return (a & b) + (((a ^ b) & 0xFEFEFEFEu) >> 1);
                            };
                            value = Average(samples[0], samples[1]);
                            if (count == 4)
                                value = Average(value, Average(samples[2], samples[3]));
                        }
                    }

                    if (value != 0) nonZeroHere++;
                    writtenHere++;
                    large[size_t(y) * largeWidth + x] = value;
                }
            }
            written.fetch_add(writtenHere, std::memory_order_relaxed);
            nonZero.fetch_add(nonZeroHere, std::memory_order_relaxed);
        };
        Parallel::For(0, int(largeHeight), 16, rows);

        // The title's copy at its own size: each of its texels is the
        // average of the scale by scale block behind it, so a scaled frame
        // it reads back is a filtered one rather than a sampled one.
        auto small = [&](int firstRow, int lastRow)
        {
            for (uint32_t y = uint32_t(firstRow); y < uint32_t(lastRow); y++)
            {
                for (uint32_t x = 0; x < width; x++)
                {
                    uint32_t value;
                    if (scale == 1)
                    {
                        value = large[size_t(y) * largeWidth + x];
                    }
                    else
                    {
                        uint32_t sum[4] = { 0, 0, 0, 0 };
                        for (uint32_t j = 0; j < scale; j++)
                            for (uint32_t i = 0; i < scale; i++)
                            {
                                const uint32_t texel = large[size_t(y * scale + j) * largeWidth + x * scale + i];
                                for (int lane = 0; lane < 4; lane++)
                                    sum[lane] += (texel >> (lane * 8)) & 0xFF;
                            }
                        const uint32_t n = scale * scale;
                        value = 0;
                        for (int lane = 0; lane < 4; lane++)
                            value |= ((sum[lane] / n) & 0xFF) << (lane * 8);
                    }
                    const uint32_t swapped = __builtin_bswap32(value);
                    memcpy(destination + Offset(x, y, destPitch), &swapped, 4);
                }
            }
        };
        Parallel::For(0, int(height), 16, small);

        {
            std::lock_guard<std::mutex> lock(g_shadowMutex);
            ShadowImage& shadow = g_shadows[destBase & 0x1FFFFFFFu];
            shadow.width = largeWidth;
            shadow.height = largeHeight;
            shadow.pixels.swap(large);
        }
    }

    // The clear happens after the copy: the surface is emptied ready for the
    // next frame's geometry.
    if (colorClear)
    {
        const uint32_t clearValue = Reg(0x2320);
        auto rows = [&](int firstRow, int lastRow)
        {
            for (uint32_t y = uint32_t(firstRow); y < uint32_t(lastRow); y++)
            {
                for (uint32_t x = 0; x < width * scale * samplesX; x++)
                {
                    const uint32_t offset = SampleOffset(
                        (x0 * scale * samplesX) + x, (y0 * scale * samplesY) + y,
                        pitchSamples, colorBaseTile);
                    if (offset < g_edram.size()) g_edram[offset] = clearValue;
                }
            }
        };
        Parallel::For(0, int(height * scale * samplesY), 32, rows);
    }

    // A new scale takes effect between frames: here, once this frame's
    // picture is out. EDRAM grows to a scale's worth of the console's ten
    // megabytes; what it held is not needed after a resolve with a clear.
    {
        const uint32_t requested = g_requestedScale.load();
        if (requested != scale)
        {
            g_edram.assign(size_t(SizeDwords) * requested * requested, 0u);
            g_scale.store(requested);
            printf("edram: resolution scale %u, %ux%u, %.1f MB of EDRAM\n", requested,
                width * requested, height * requested,
                double(g_edram.size()) * 4.0 / (1024.0 * 1024.0));
            fflush(stdout);
        }
    }

    {
        std::lock_guard<std::mutex> lock(g_statisticsMutex);
        g_statistics.resolves++;
        if (colorClear) g_statistics.clears++;
        g_statistics.texelsWritten += written.load();
        g_statistics.texelsNonZero += nonZero.load();
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
