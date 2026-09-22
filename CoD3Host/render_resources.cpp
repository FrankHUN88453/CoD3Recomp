#include "render_resources.h"
#include "render_table.h"
#include "render_stats.h"
#include "kernel.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>

#include <d3d11_1.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace
{
    using namespace RenderResources;

    ID3D11Device* g_device = nullptr;
    ID3D11DeviceContext* g_context = nullptr;
    uint64_t g_frame = 0;

    // Released when unused this long: half a minute of frames, so a level
    // change lets the last level's textures go.
    constexpr uint64_t UnusedFrames = 1800;

    std::atomic<uint64_t> g_textureUploads{ 0 }, g_textureBytes{ 0 }, g_bufferUploads{ 0 }, g_bufferBytes{ 0 };
    std::atomic<uint64_t> g_indexBytes{ 0 }, g_constantBytes{ 0 };
    uint64_t g_resourceBytes = 0;
    uint32_t g_released = 0;

    // --- the scale ----------------------------------------------------------------------------

    float g_scale = 1.0f;
    float g_requestedScale = 1.0f;
    uint32_t g_samples = 1;
    uint32_t g_requestedSamples = 1;

    // The count the device can do, at or under the one asked for.
    uint32_t SupportedSamples(uint32_t wanted)
    {
        for (uint32_t samples = wanted; samples > 1; samples /= 2)
        {
            UINT colour = 0, depth = 0;
            if (SUCCEEDED(g_device->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, samples, &colour)) && colour > 0 &&
                SUCCEEDED(g_device->CheckMultisampleQualityLevels(DXGI_FORMAT_D32_FLOAT_S8X24_UINT, samples, &depth)) && depth > 0)
                return samples;
        }
        return 1;
    }

    // Rows a surface of a pitch gets. The console's EDRAM would hold more,
    // but the title draws into 624 rows of its 1040 wide surfaces most of
    // the time, 512 of the 560 wide ones and 256 of the 320 wide, and a
    // surface as tall as EDRAM allows at four times the size is a lot of
    // memory for nothing: 720 rows for the wide ones and the pitch for the
    // rest to start with, and more when a level asks (EnsureRows: the
    // crossroads level's shadow map is 1024 rows of the 1040 wide surface).
    // COD3_TARGETROWS=N pins it.
    uint32_t g_rowsAsked[16384 / 32 + 1] = {};   // by pitch in 32s, the most a draw or resolve has used

    uint32_t TargetRows(uint32_t pitch, uint32_t bytesPerSample)
    {
        if (pitch == 0) return 0;
        static const uint32_t pinned = []() { const char* t = getenv("COD3_TARGETROWS"); return t ? uint32_t(strtoul(t, nullptr, 10)) : 0u; }();
        const uint32_t edram = (10u << 20) / (pitch * bytesPerSample);
        uint32_t rows = pinned != 0 ? pinned : pitch >= 1024 ? 720 : std::max(pitch, 256u);
        if (pinned == 0 && pitch / 32 < sizeof(g_rowsAsked) / sizeof(g_rowsAsked[0])) rows = std::max(rows, g_rowsAsked[pitch / 32]);
        return std::min(std::min(rows, edram), 1440u);
    }

    // --- the console's texture tiling ---------------------------------------------------------

    // Where a texel lies in a tiled surface, in bytes: the console's 2D
    // tiling, coordinates split into a 32 by 32 macro tile and a position
    // inside it with the bits of the two interleaved. From Xenia; checked
    // one to one against the resolve when this runtime still resolved on
    // the CPU.
    inline uint32_t TiledOffset(uint32_t x, uint32_t y, uint32_t pitch, uint32_t bytesPerTexelLog2)
    {
        const uint32_t alignedPitch = (pitch + 31u) & ~31u;
        const uint32_t macro = ((x >> 5) + (y >> 5) * (alignedPitch >> 5)) << (bytesPerTexelLog2 + 7);
        const uint32_t micro = ((x & 7u) + ((y & 0xEu) << 2)) << bytesPerTexelLog2;
        const uint32_t offset = macro + ((micro & ~0xFu) << 1) + (micro & 0xFu) + ((y & 1u) << 4);
        return ((offset & ~0x1FFu) << 3) + ((y & 16u) << 7) + ((offset & 0x1C0u) << 2)
             + (((((y & 8u) >> 2) + (x >> 3)) & 3u) << 6) + (offset & 0x3Fu);
    }

    uint64_t Fingerprint(const uint8_t* data, size_t bytes)
    {
        // Sixty four samples across the range plus the ends: a change
        // anywhere large shows, a change in one texel may not, and a texture
        // that changes by one texel is one this runtime redraws slightly late.
        uint64_t hash = 1469598103934665603ull ^ bytes;
        const size_t step = std::max<size_t>(bytes / 64, 64);
        for (size_t at = 0; at + 8 <= bytes; at += step)
        {
            uint64_t word;
            memcpy(&word, data + at, 8);
            hash = (hash ^ word) * 1099511628211ull;
        }
        if (bytes >= 8)
        {
            uint64_t word;
            memcpy(&word, data + bytes - 8, 8);
            hash = (hash ^ word) * 1099511628211ull;
        }
        return hash;
    }

    // --- render targets -----------------------------------------------------------------------

    struct ColorEntry { ColorTarget target; ComPtr<ID3D11Texture2D> texture; ComPtr<ID3D11RenderTargetView> view; ComPtr<ID3D11ShaderResourceView> resource; uint32_t key[2] = {}; };
    struct DepthEntry { DepthTarget target; ComPtr<ID3D11Texture2D> texture; ComPtr<ID3D11DepthStencilView> view; ComPtr<ID3D11ShaderResourceView> resource; uint32_t key[2] = {}; };
    std::deque<ColorEntry> g_colorTargets;   // by handle less one
    std::deque<DepthEntry> g_depthTargets;
    RenderTable::KeyTable<2> g_colorKeys;
    RenderTable::KeyTable<2> g_depthKeys;

    DXGI_FORMAT ColorFormat(uint32_t format, uint32_t& bytesPerSample)
    {
        bytesPerSample = 4;
        switch (format)
        {
        case 0: case 1: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case 2: case 10: return DXGI_FORMAT_R10G10B10A2_UNORM;
        case 3: case 12: bytesPerSample = 8; return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case 4: return DXGI_FORMAT_R16G16_UNORM;
        case 5: bytesPerSample = 8; return DXGI_FORMAT_R16G16B16A16_UNORM;
        case 6: return DXGI_FORMAT_R16G16_FLOAT;
        case 7: bytesPerSample = 8; return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case 14: return DXGI_FORMAT_R32_FLOAT;
        case 15: bytesPerSample = 8; return DXGI_FORMAT_R32G32_FLOAT;
        default: return DXGI_FORMAT_R8G8B8A8_UNORM;
        }
    }

    void HostSize(uint32_t pitch, uint32_t rows, uint32_t& width, uint32_t& height)
    {
        width = std::max(1u, uint32_t(std::lround(double(pitch) * g_scale)));
        height = std::max(1u, uint32_t(std::lround(double(rows) * g_scale)));
    }

    // --- resolved surfaces -----------------------------------------------------------------------

    struct ResolvedEntry { Resolved surface; ComPtr<ID3D11Texture2D> texture; ComPtr<ID3D11ShaderResourceView> resource; ComPtr<ID3D11RenderTargetView> view; uint32_t physical = 0; };
    std::deque<ResolvedEntry> g_resolved;
    RenderTable::KeyTable<1> g_resolvedKeys;
    uint64_t g_resolveSerial = 0;

    // --- textures --------------------------------------------------------------------------------

    struct TextureEntry
    {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11ShaderResourceView> resource;
        ComPtr<ID3D11ShaderResourceView> face;   // a cube's first face as a flat texture
        bool cube = false;
        uint32_t key[4] = {};
        uint64_t fingerprint = 0;
        uint64_t checkedFrame = 0;      // the frame the fingerprint was last compared in
        uint64_t usedFrame = 0;
        uint32_t width = 0, height = 0, levels = 0;
        uint64_t bytes = 0;
        bool live = false;
    };
    std::deque<TextureEntry> g_textures;   // by handle less one; handle 1 is the white texture
    ComPtr<ID3D11Texture2D> g_whiteCube;
    ComPtr<ID3D11ShaderResourceView> g_whiteCubeView;
    ComPtr<ID3D11Texture3D> g_whiteVolume;
    ComPtr<ID3D11ShaderResourceView> g_whiteVolumeView;
    RenderTable::KeyTable<4> g_textureKeys;
    std::vector<uint32_t> g_freeTextures;

    struct BufferEntry
    {
        ComPtr<ID3D11Buffer> buffer;
        ComPtr<ID3D11ShaderResourceView> resource;
        uint32_t key[2] = {};
        uint64_t fingerprint = 0;
        uint64_t checkedFrame = 0;
        uint64_t usedFrame = 0;
        uint32_t bytes = 0;             // the buffer's size
        uint32_t seen = 0;              // the largest size a fetch asked for
        bool live = false;
    };
    std::deque<BufferEntry> g_buffers;
    RenderTable::KeyTable<2> g_bufferKeys;
    std::vector<uint32_t> g_freeBuffers;

    // The bytes of a texture level as the host wants them: untiled, and
    // the console's byte order undone. `x0`, `y0` are where the level lies
    // in its tile when the levels are packed.
    void LinearLevel(const uint8_t* data, uint32_t format, uint32_t endian, uint32_t width, uint32_t height,
                     uint32_t pitchTexels, bool tiled, uint32_t x0, uint32_t y0,
                     std::vector<uint8_t>& out, uint32_t& rowBytes)
    {
        if (format == 18 || format == 19 || format == 20)
        {
            const uint32_t blockBytes = format == 18 ? 8 : 16;
            const uint32_t blockShift = blockBytes == 8 ? 3 : 4;
            const uint32_t blocksAcross = std::max(pitchTexels / 4, 1u);
            const uint32_t blocksWide = (width + 3) / 4, blocksHigh = (height + 3) / 4;
            rowBytes = blocksWide * blockBytes;
            out.resize(size_t(rowBytes) * blocksHigh);
            for (uint32_t by = 0; by < blocksHigh; by++)
                for (uint32_t bx = 0; bx < blocksWide; bx++)
                {
                    const uint32_t sx = bx + x0 / 4, sy = by + y0 / 4;
                    const uint32_t offset = tiled ? TiledOffset(sx, sy, blocksAcross, blockShift)
                                                  : (sy * blocksAcross + sx) * blockBytes;
                    uint8_t* to = out.data() + size_t(by) * rowBytes + size_t(bx) * blockBytes;
                    // The blocks' sixteen bit fields are stored the console's
                    // way round: each pair of bytes swapped.
                    for (uint32_t i = 0; i < blockBytes; i++) to[i] = data[offset + (i ^ 1)];
                }
        }
        else if (format == 6)
        {
            rowBytes = width * 4;
            out.resize(size_t(rowBytes) * height);
            for (uint32_t y = 0; y < height; y++)
                for (uint32_t x = 0; x < width; x++)
                {
                    const uint32_t offset = tiled ? TiledOffset(x + x0, y + y0, pitchTexels, 2)
                                                  : ((y + y0) * pitchTexels + x + x0) * 4;
                    uint32_t word;
                    memcpy(&word, data + offset, 4);
                    switch (endian)
                    {
                    case 1: word = ((word & 0xFF00FF00u) >> 8) | ((word & 0x00FF00FFu) << 8); break;
                    case 2: word = _byteswap_ulong(word); break;
                    case 3: word = (word >> 16) | (word << 16); break;
                    default: break;
                    }
                    // The word is A R G B from the top, and the console's
                    // component x is its lowest byte: blue. The title's fetch
                    // swizzle puts it right, so the bytes stay as they are.
                    memcpy(out.data() + size_t(y) * rowBytes + size_t(x) * 4, &word, 4);
                }
        }
        else   // 2: one byte a texel
        {
            rowBytes = width;
            out.resize(size_t(rowBytes) * height);
            for (uint32_t y = 0; y < height; y++)
                for (uint32_t x = 0; x < width; x++)
                {
                    const uint32_t offset = tiled ? TiledOffset(x + x0, y + y0, pitchTexels, 0)
                                                  : ((y + y0) * pitchTexels + x + x0);
                    out[size_t(y) * rowBytes + x] = data[offset];
                }
        }
    }

    // Where a texture's first level lies when its levels are packed into
    // one tile: a texture of sixteen texels or fewer on a side shares the
    // tile with its own mips, and the first level is not at the corner but
    // sixteen texels along, the smaller levels in the corner before it.
    // From Xenia's texture_info.cc, which had it from graph paper.
    void PackedBaseOffset(bool packed, uint32_t width, uint32_t height, uint32_t& x, uint32_t& y)
    {
        x = 0; y = 0;
        if (!packed) return;
        uint32_t log2Width = 0, log2Height = 0;
        while ((1u << log2Width) < width) log2Width++;
        while ((1u << log2Height) < height) log2Height++;
        if (std::min(log2Width, log2Height) > 4) return;
        if (log2Width > log2Height) y = 16; else x = 16;
    }

    bool IsPowerOfTwo(uint32_t v) { return v != 0 && (v & (v - 1)) == 0; }

    // The texture's levels from guest memory into a new texture. The
    // levels past the first lie one after another from the mip address,
    // each with its pitch and height rounded up to whole 32 block tiles;
    // once a level is sixteen texels or fewer on a side the rest are
    // packed into one tile, and those are left out: the sampler clamps to
    // the last level there is, thirty two texels, which is close enough
    // to a texel on the screen for nobody to see the difference.
    bool UploadTexture(TextureEntry& entry, const uint32_t fetch[6], const uint8_t* data)
    {
        const uint32_t format = fetch[1] & 0x3F;
        const uint32_t endian = (fetch[1] >> 6) & 3;
        const uint32_t pitch = ((fetch[0] >> 22) & 0x1FF) * 32;
        const bool tiled = ((fetch[0] >> 31) & 1) != 0;
        const uint32_t width = (fetch[2] & 0x1FFF) + 1;
        const uint32_t height = ((fetch[2] >> 13) & 0x1FFF) + 1;
        const bool packedMips = ((fetch[5] >> 11) & 1) != 0;
        const uint32_t mipAddress = fetch[5] & 0xFFFFF000u;
        const uint32_t mipMax = (fetch[4] >> 6) & 0xF;
        const bool compressed = format == 18 || format == 19 || format == 20;
        const uint32_t blockBytes = format == 18 ? 8 : format == 19 || format == 20 ? 16 : format == 6 ? 4 : 1;
        const uint32_t blockTexels = compressed ? 4 : 1;

        DXGI_FORMAT hostFormat;
        switch (format)
        {
        case 2: hostFormat = DXGI_FORMAT_R8_UNORM; break;
        case 6: hostFormat = DXGI_FORMAT_R8G8B8A8_UNORM; break;
        case 18: hostFormat = DXGI_FORMAT_BC1_UNORM; break;
        case 19: hostFormat = DXGI_FORMAT_BC2_UNORM; break;
        case 20: hostFormat = DXGI_FORMAT_BC3_UNORM; break;
        default: return false;
        }

        // How many levels can be had: down to thirty two texels, as many as
        // the fetch says exist, and only for a power of two size, whose
        // levels are where the arithmetic below says.
        uint32_t levels = 1;
        if (mipAddress != 0 && mipMax > 0 && IsPowerOfTwo(width) && IsPowerOfTwo(height) && std::min(width, height) >= 64)
        {
            while (levels <= mipMax && std::min(width >> levels, height >> levels) >= 32) levels++;
        }
        static const bool noMips = getenv("COD3_NOMIPS") != nullptr;
        if (noMips) levels = 1;

        // A cube map: six faces, each a whole tiled picture of the size,
        // one after another, each taking its rows rounded up to whole
        // tiles. Its first level only: the faces' mips are not laid out
        // where the arithmetic below would look.
        const bool cube = ((fetch[5] >> 9) & 3) == 3;
        const uint32_t faces = cube ? 6 : 1;
        if (cube) levels = 1;
        std::vector<std::vector<uint8_t>> pixels(levels * faces);
        std::vector<D3D11_SUBRESOURCE_DATA> initial(levels * faces);
        uint32_t packedX = 0, packedY = 0;
        PackedBaseOffset(packedMips, width, height, packedX, packedY);
        uint32_t rowBytes = 0;
        entry.bytes = 0;
        for (uint32_t face = 0; face < faces; face++)
        {
            const uint32_t blocksHigh = (height + blockTexels - 1) / blockTexels;
            const uint32_t faceBytes = std::max(pitch / blockTexels, 1u) * ((blocksHigh + 31) & ~31u) * blockBytes;
            LinearLevel(data + size_t(face) * faceBytes, format, endian, width, height, pitch, tiled, packedX, packedY, pixels[face * levels], rowBytes);
            initial[face * levels] = { pixels[face * levels].data(), rowBytes, 0 };
            entry.bytes += pixels[face * levels].size();
        }

        uint32_t offset = 0;
        for (uint32_t level = 1; level < levels; level++)
        {
            const uint32_t w = std::max(width >> level, 1u), h = std::max(height >> level, 1u);
            const uint32_t blocksWide = (w + blockTexels - 1) / blockTexels, blocksHigh = (h + blockTexels - 1) / blockTexels;
            const uint32_t pitchBlocks = (blocksWide + 31) & ~31u, rowsBlocks = (blocksHigh + 31) & ~31u;
            const uint8_t* levelData = Guest::Base + Guest::PhysicalAlias(mipAddress + offset);
            LinearLevel(levelData, format, endian, w, h, pitchBlocks * blockTexels, tiled, 0, 0, pixels[level], rowBytes);
            initial[level] = { pixels[level].data(), rowBytes, 0 };
            entry.bytes += pixels[level].size();
            offset += pitchBlocks * rowsBlocks * blockBytes;
        }

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = compressed ? ((width + 3) & ~3u) : width;
        desc.Height = compressed ? ((height + 3) & ~3u) : height;
        desc.MipLevels = levels;
        desc.ArraySize = faces;
        desc.Format = hostFormat;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = cube ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0;
        entry.texture.Reset();
        entry.resource.Reset();
        if (FAILED(g_device->CreateTexture2D(&desc, initial.data(), &entry.texture))) return false;
        if (FAILED(g_device->CreateShaderResourceView(entry.texture.Get(), nullptr, &entry.resource))) return false;
        entry.cube = cube;
        entry.face.Reset();
        if (cube)
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC view{};
            view.Format = hostFormat;
            view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
            view.Texture2DArray.MipLevels = 1;
            view.Texture2DArray.ArraySize = 1;
            g_device->CreateShaderResourceView(entry.texture.Get(), &view, &entry.face);
        }
        entry.width = width;
        entry.height = height;
        entry.levels = levels;
        g_textureUploads.fetch_add(1, std::memory_order_relaxed);
        g_textureBytes.fetch_add(entry.bytes, std::memory_order_relaxed);
        RenderStats::Frame().textureUploads++;
        RenderStats::Frame().uploadBytes += entry.bytes;

        // COD3_D3DTEXDUMP=directory keeps every texture uploaded, the block
        // compressed ones as DDS and the rest as BMP, named by address.
        static const char* const dumpTextures = getenv("COD3_D3DTEXDUMP");
        if (dumpTextures != nullptr)
        {
            char name[512];
            snprintf(name, sizeof(name), "%s/tex-%08X-%ux%u-f%u.%s", dumpTextures, fetch[1] & 0xFFFFF000u, width, height, format, compressed ? "dds" : "bmp");
            if (FILE* out = fopen(name, "wb"))
            {
                const std::vector<uint8_t>& linear = pixels[0];
                if (compressed)
                {
                    uint32_t header[32] = {};
                    header[0] = 0x20534444; header[1] = 124; header[2] = 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000;
                    header[3] = height; header[4] = width; header[5] = uint32_t(linear.size());
                    header[19] = 32; header[20] = 0x4;
                    header[21] = format == 18 ? 0x31545844 : format == 19 ? 0x33545844 : 0x35545844;
                    header[27] = 0x1000;
                    fwrite(header, 4, 32, out);
                    fwrite(linear.data(), 1, linear.size(), out);
                }
                else
                {
                    const uint32_t bytesPerTexel = format == 6 ? 4 : 1;
                    std::vector<uint8_t> bgra(size_t(width) * height * 4);
                    for (uint32_t y = 0; y < height; y++)
                        for (uint32_t x = 0; x < width; x++)
                        {
                            const uint8_t* from = linear.data() + size_t(y) * width * bytesPerTexel + size_t(x) * bytesPerTexel;
                            uint8_t* to = bgra.data() + (size_t(y) * width + x) * 4;
                            if (bytesPerTexel == 1) { to[0] = to[1] = to[2] = from[0]; to[3] = 255; }
                            else { to[0] = from[2]; to[1] = from[1]; to[2] = from[0]; to[3] = from[3]; }
                        }
                    BITMAPFILEHEADER file{};
                    BITMAPINFOHEADER info{};
                    info.biSize = sizeof(info); info.biWidth = int(width); info.biHeight = -int(height); info.biPlanes = 1;
                    info.biBitCount = 32; info.biCompression = BI_RGB; info.biSizeImage = DWORD(bgra.size());
                    file.bfType = 0x4D42; file.bfOffBits = sizeof(file) + sizeof(info); file.bfSize = file.bfOffBits + info.biSizeImage;
                    fwrite(&file, sizeof(file), 1, out); fwrite(&info, sizeof(info), 1, out); fwrite(bgra.data(), 1, bgra.size(), out);
                }
                fclose(out);
            }
        }
        return true;
    }

    // How many bytes of guest memory the first level takes: what the
    // fingerprint covers.
    size_t SourceBytes(const uint32_t fetch[6])
    {
        const uint32_t format = fetch[1] & 0x3F;
        const uint32_t pitch = ((fetch[0] >> 22) & 0x1FF) * 32;
        const uint32_t height = ((fetch[2] >> 13) & 0x1FFF) + 1;
        if (format == 18 || format == 19 || format == 20)
            return size_t(std::max(pitch / 4, 1u)) * ((height + 3) / 4) * (format == 18 ? 8 : 16) + 4096;
        if (format == 6) return size_t(pitch) * height * 4;
        return size_t(pitch) * height;
    }

    // --- the rings --------------------------------------------------------------------------------

    struct Ring
    {
        ComPtr<ID3D11Buffer> buffer;
        uint32_t bytes = 0;
        uint32_t offset = 0;

        bool Make(uint32_t size, UINT bind)
        {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = size;
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = bind;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (FAILED(g_device->CreateBuffer(&desc, nullptr, &buffer))) return false;
            bytes = size;
            return true;
        }

        // Room for `count` bytes, aligned, mapped for writing: where to write
        // and the offset it will have; null when the map failed. The ring is
        // discarded and started over when it is full.
        uint8_t* Map(uint32_t count, uint32_t alignment, uint32_t& at)
        {
            if (!buffer) return nullptr;
            const uint32_t aligned = (count + alignment - 1) & ~(alignment - 1);
            D3D11_MAP mode = D3D11_MAP_WRITE_NO_OVERWRITE;
            if (offset + aligned > bytes || offset == 0) { offset = 0; mode = D3D11_MAP_WRITE_DISCARD; }
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(g_context->Map(buffer.Get(), 0, mode, 0, &mapped))) return nullptr;
            at = offset;
            offset += aligned;
            return static_cast<uint8_t*>(mapped.pData) + at;
        }

        void Unmap() { g_context->Unmap(buffer.Get(), 0); }

        uint32_t Append(const void* data, uint32_t count, uint32_t alignment)
        {
            uint32_t at;
            uint8_t* to = Map(count, alignment, at);
            if (to == nullptr) return 0xFFFFFFFFu;
            memcpy(to, data, count);
            Unmap();
            return at;
        }
    };
    Ring g_indexRing, g_constantRing;
    bool g_constantOffsets = false;
    constexpr uint32_t IndexRingBytes = 16u << 20;
    constexpr uint32_t ConstantRingBytes = 16u << 20;
}

void RenderResources::Initialize(ID3D11Device* device, ID3D11DeviceContext* context)
{
    g_device = device;
    g_context = context;
    g_indexRing.Make(IndexRingBytes, D3D11_BIND_INDEX_BUFFER);
    D3D11_FEATURE_DATA_D3D11_OPTIONS options{};
    if (SUCCEEDED(device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &options, sizeof(options))) && options.ConstantBufferOffsetting)
        g_constantOffsets = g_constantRing.Make(ConstantRingBytes, D3D11_BIND_CONSTANT_BUFFER);
    if (!g_constantOffsets) printf("render: constant buffer offsetting is not available on this device\n");

    // Handle 1: the white texture, for what cannot be uploaded.
    g_textures.emplace_back();
    TextureEntry& white = g_textures.back();
    const uint32_t pixel = 0xFFFFFFFFu;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = desc.Height = 1;
    desc.MipLevels = desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial{ &pixel, 4, 4 };
    if (SUCCEEDED(device->CreateTexture2D(&desc, &initial, &white.texture)))
        device->CreateShaderResourceView(white.texture.Get(), nullptr, &white.resource);
    white.live = true;
    white.width = white.height = white.levels = 1;
    // And a white cube for a program that reads a cube where the fetch
    // constant names none.
    desc.ArraySize = 6;
    desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
    D3D11_SUBRESOURCE_DATA faces[6];
    for (D3D11_SUBRESOURCE_DATA& f : faces) f = initial;
    if (SUCCEEDED(device->CreateTexture2D(&desc, faces, &g_whiteCube)))
        device->CreateShaderResourceView(g_whiteCube.Get(), nullptr, &g_whiteCubeView);
    // And a white volume: the title's volume textures are not uploaded
    // (their tiling is another matter), and a program reading one reads
    // white, as it did before.
    D3D11_TEXTURE3D_DESC volume{};
    volume.Width = volume.Height = volume.Depth = 1;
    volume.MipLevels = 1;
    volume.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    volume.Usage = D3D11_USAGE_IMMUTABLE;
    volume.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (SUCCEEDED(device->CreateTexture3D(&volume, &initial, &g_whiteVolume)))
        device->CreateShaderResourceView(g_whiteVolume.Get(), nullptr, &g_whiteVolumeView);
}

bool RenderResources::BeginFrame(uint64_t frame)
{
    g_frame = frame;
    bool remade = false;
    if (g_requestedSamples != g_samples)
    {
        remade = true;
        g_samples = g_requestedSamples;
        g_colorTargets.clear();
        g_depthTargets.clear();
        g_colorKeys.Clear();
        g_depthKeys.Clear();
        printf("render: the frame is drawn with %u sample%s a pixel\n", g_samples, g_samples == 1 ? "" : "s");
    }
    if (g_requestedScale != g_scale)
    {
        remade = true;
        g_scale = g_requestedScale;
        // The targets are made again at the new size as they are asked for.
        g_colorTargets.clear();
        g_depthTargets.clear();
        g_colorKeys.Clear();
        g_depthKeys.Clear();
        printf("render: the frame is drawn at %.3f times the title's size\n", g_scale);
    }
    // The release of what has not been used: a walk every few seconds.
    if (frame % 300 != 0) return remade;
    for (uint32_t i = 1; i < g_textures.size(); i++)
    {
        TextureEntry& entry = g_textures[i];
        if (!entry.live || frame - entry.usedFrame < UnusedFrames) continue;
        g_textureKeys.Erase(entry.key);
        g_resourceBytes -= entry.bytes;
        entry.texture.Reset();
        entry.resource.Reset();
        entry.live = false;
        g_freeTextures.push_back(i + 1);
        g_released++;
    }
    for (uint32_t i = 0; i < g_buffers.size(); i++)
    {
        BufferEntry& entry = g_buffers[i];
        if (!entry.live || frame - entry.usedFrame < UnusedFrames) continue;
        g_bufferKeys.Erase(entry.key);
        g_resourceBytes -= entry.bytes;
        entry.buffer.Reset();
        entry.resource.Reset();
        entry.live = false;
        g_freeBuffers.push_back(i + 1);
        g_released++;
    }
    return remade;
}

void RenderResources::RequestScale(float scale)
{
    g_requestedScale = scale < 0.25f ? 0.25f : scale > 8.0f ? 8.0f : scale;
}

float RenderResources::Scale() { return g_scale; }

void RenderResources::RequestMultisample(uint32_t samples)
{
    samples = samples >= 8 ? 8 : samples >= 4 ? 4 : samples >= 2 ? 2 : 1;
    g_requestedSamples = SupportedSamples(samples);
}

uint32_t RenderResources::Multisample() { return g_samples; }

void RenderResources::TargetSize(uint32_t pitch, uint32_t bytesPerSample, uint32_t& width, uint32_t& height, float& scaleX, float& scaleY)
{
    const uint32_t rows = TargetRows(pitch, bytesPerSample);
    HostSize(pitch, rows, width, height);
    scaleX = pitch != 0 ? float(width) / float(pitch) : 1.0f;
    scaleY = rows != 0 ? float(height) / float(rows) : 1.0f;
}

RenderState::Handle RenderResources::ColorTargetFor(uint32_t colorInfo, uint32_t pitch)
{
    const uint32_t key[2] = { colorInfo & 0x000F0FFFu, pitch };   // base tile and format
    if (const Handle found = g_colorKeys.Find(key)) return found;

    g_colorTargets.emplace_back();
    ColorEntry& entry = g_colorTargets.back();
    const Handle handle = Handle(g_colorTargets.size());
    uint32_t bytesPerSample = 4;
    const uint32_t format = (colorInfo >> 16) & 0xF;
    entry.target.format = ColorFormat(format, bytesPerSample);
    entry.target.pitch = pitch;
    entry.target.rows = TargetRows(pitch, bytesPerSample);
    HostSize(pitch, entry.target.rows, entry.target.width, entry.target.height);
    memcpy(entry.key, key, sizeof(entry.key));
    g_colorKeys.Insert(key, handle);
    if (pitch == 0 || entry.target.rows == 0) return handle;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = entry.target.width;
    desc.Height = entry.target.height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = entry.target.format;
    desc.SampleDesc.Count = g_samples;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(g_device->CreateTexture2D(&desc, nullptr, &entry.texture))) return handle;
    g_device->CreateRenderTargetView(entry.texture.Get(), nullptr, &entry.view);
    g_device->CreateShaderResourceView(entry.texture.Get(), nullptr, &entry.resource);
    entry.target.texture = entry.texture.Get();
    entry.target.view = entry.view.Get();
    entry.target.resource = entry.resource.Get();
    entry.target.samples = g_samples;
    g_resourceBytes += uint64_t(entry.target.width) * entry.target.height * bytesPerSample * g_samples;
    printf("render: colour target at tile %u, pitch %u, format %u: %ux%u, %u sample%s\n",
        colorInfo & 0xFFF, pitch, format, entry.target.width, entry.target.height, g_samples, g_samples == 1 ? "" : "s");
    fflush(stdout);
    return handle;
}

RenderState::Handle RenderResources::DepthTargetFor(uint32_t depthInfo, uint32_t pitch)
{
    const uint32_t key[2] = { depthInfo & 0xFFF, pitch };
    if (const Handle found = g_depthKeys.Find(key)) return found;

    g_depthTargets.emplace_back();
    DepthEntry& entry = g_depthTargets.back();
    const Handle handle = Handle(g_depthTargets.size());
    entry.target.pitch = pitch;
    entry.target.rows = TargetRows(pitch, 4);
    HostSize(pitch, entry.target.rows, entry.target.width, entry.target.height);
    memcpy(entry.key, key, sizeof(entry.key));
    g_depthKeys.Insert(key, handle);
    if (pitch == 0 || entry.target.rows == 0) return handle;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = entry.target.width;
    desc.Height = entry.target.height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R32G8X24_TYPELESS;
    desc.SampleDesc.Count = g_samples;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(g_device->CreateTexture2D(&desc, nullptr, &entry.texture))) return handle;
    D3D11_DEPTH_STENCIL_VIEW_DESC viewDesc{};
    viewDesc.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    viewDesc.ViewDimension = g_samples > 1 ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D;
    g_device->CreateDepthStencilView(entry.texture.Get(), &viewDesc, &entry.view);
    D3D11_SHADER_RESOURCE_VIEW_DESC resourceDesc{};
    resourceDesc.Format = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    resourceDesc.ViewDimension = g_samples > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D;
    resourceDesc.Texture2D.MipLevels = 1;
    g_device->CreateShaderResourceView(entry.texture.Get(), &resourceDesc, &entry.resource);
    entry.target.texture = entry.texture.Get();
    entry.target.view = entry.view.Get();
    entry.target.resource = entry.resource.Get();
    entry.target.samples = g_samples;
    g_resourceBytes += uint64_t(entry.target.width) * entry.target.height * 8 * g_samples;
    printf("render: depth target at tile %u, pitch %u: %ux%u, %u sample%s\n", depthInfo & 0xFFF, pitch, entry.target.width, entry.target.height, g_samples, g_samples == 1 ? "" : "s");
    fflush(stdout);
    return handle;
}

bool RenderResources::EnsureRows(uint32_t pitch, uint32_t rows)
{
    if (pitch == 0 || pitch / 32 >= sizeof(g_rowsAsked) / sizeof(g_rowsAsked[0])) return false;
    rows = std::min(rows, 1440u);
    if (rows <= g_rowsAsked[pitch / 32] || rows <= TargetRows(pitch, 4)) return false;
    // Whole 64s, so a scissor that grows a row at a time does not make the
    // targets again a row at a time.
    g_rowsAsked[pitch / 32] = (rows + 63) & ~63u;
    // The targets of this pitch go: they come back taller at the next ask.
    bool dropped = false;
    for (uint32_t i = 0; i < g_colorTargets.size(); i++)
    {
        ColorEntry& entry = g_colorTargets[i];
        if (entry.target.pitch != pitch || entry.texture == nullptr) continue;
        g_colorKeys.Erase(entry.key);
        g_resourceBytes -= uint64_t(entry.target.width) * entry.target.height * 4 * entry.target.samples;
        entry.texture.Reset(); entry.view.Reset(); entry.resource.Reset();
        entry.target = ColorTarget();
        dropped = true;
    }
    for (uint32_t i = 0; i < g_depthTargets.size(); i++)
    {
        DepthEntry& entry = g_depthTargets[i];
        if (entry.target.pitch != pitch || entry.texture == nullptr) continue;
        g_depthKeys.Erase(entry.key);
        g_resourceBytes -= uint64_t(entry.target.width) * entry.target.height * 8 * entry.target.samples;
        entry.texture.Reset(); entry.view.Reset(); entry.resource.Reset();
        entry.target = DepthTarget();
        dropped = true;
    }
    printf("render: the %u wide surfaces get %u rows\n", pitch, g_rowsAsked[pitch / 32]);
    fflush(stdout);
    return dropped;
}

const RenderResources::ColorTarget* RenderResources::ColorTargetOf(Handle handle)
{
    if (handle == 0 || handle > g_colorTargets.size()) return nullptr;
    const ColorTarget& target = g_colorTargets[handle - 1].target;
    return target.view != nullptr ? &target : nullptr;
}

const RenderResources::DepthTarget* RenderResources::DepthTargetOf(Handle handle)
{
    if (handle == 0 || handle > g_depthTargets.size()) return nullptr;
    const DepthTarget& target = g_depthTargets[handle - 1].target;
    return target.view != nullptr ? &target : nullptr;
}

RenderResources::Resolved* RenderResources::ResolvedFor(uint32_t physical, uint32_t width, uint32_t height, DXGI_FORMAT format, bool depth)
{
    const uint32_t key[1] = { physical & 0x1FFFFFFFu };
    Handle handle = g_resolvedKeys.Find(key);
    if (handle == 0)
    {
        g_resolved.emplace_back();
        handle = Handle(g_resolved.size());
        g_resolved.back().physical = key[0];
        g_resolvedKeys.Insert(key, handle);
    }
    ResolvedEntry& entry = g_resolved[handle - 1];
    Resolved& out = entry.surface;
    if (!entry.texture || out.width != width || out.height != height || out.format != format)
    {
        if (entry.texture) g_resourceBytes -= uint64_t(out.width) * out.height * 4;
        entry.texture.Reset(); entry.resource.Reset(); entry.view.Reset();
        out.texture = nullptr; out.resource = nullptr; out.view = nullptr;
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(g_device->CreateTexture2D(&desc, nullptr, &entry.texture))) return nullptr;
        g_device->CreateShaderResourceView(entry.texture.Get(), nullptr, &entry.resource);
        g_device->CreateRenderTargetView(entry.texture.Get(), nullptr, &entry.view);
        out.texture = entry.texture.Get();
        out.resource = entry.resource.Get();
        out.view = entry.view.Get();
        out.width = width;
        out.height = height;
        out.format = format;
        g_resourceBytes += uint64_t(width) * height * 4;
    }
    out.depth = depth;
    out.serial = ++g_resolveSerial;
    return &out;
}

const RenderResources::Resolved* RenderResources::ResolvedAt(uint32_t physical)
{
    const uint32_t key[1] = { physical & 0x1FFFFFFFu };
    const Handle handle = g_resolvedKeys.Find(key);
    if (handle == 0) return nullptr;
    const Resolved& out = g_resolved[handle - 1].surface;
    return out.resource != nullptr ? &out : nullptr;
}

RenderState::Handle RenderResources::WhiteTexture() { return 1; }

RenderState::Handle RenderResources::TextureFor(const uint32_t fetch[6], uint32_t& width, uint32_t& height, bool& resolved)
{
    const uint32_t format = fetch[1] & 0x3F;
    const uint32_t base = fetch[1] & 0xFFFFF000u;
    width = (fetch[2] & 0x1FFF) + 1;
    height = ((fetch[2] >> 13) & 0x1FFF) + 1;
    resolved = false;
    if ((fetch[0] & 3) != 2 || base == 0 || width > 8192 || height > 8192) return 1;

    // A surface a resolve made, at the size the frame was drawn: those are
    // the shadow maps and the post processing's copies of the frame.
    if (const Resolved* surface = ResolvedAt(base))
    {
        width = surface->guestWidth;
        height = surface->guestHeight;
        resolved = true;
        // Handle 0 with `resolved` set: the caller binds the surface itself.
        return 0;
    }
    if (format != 2 && format != 6 && format != 18 && format != 19 && format != 20) return 1;

    const uint32_t dimension = (fetch[5] >> 9) & 3;
    // What is not uploaded is said once each, so a level that needs a
    // format this does not have is a level that says so.
    static uint64_t announced = 0;
    if ((dimension == 2 || (format != 2 && format != 6 && format != 18 && format != 19 && format != 20)) && (announced & (1ull << (format & 63))) == 0)
    {
        announced |= 1ull << (format & 63);
        printf("render: texture format %u%s at %08X (%ux%u) is not uploaded; it reads white\n", format, dimension == 2 ? " (a volume)" : dimension == 3 ? " (a cube)" : "", base, width, height);
        fflush(stdout);
    }
    if (dimension == 2) return 1;   // a volume: not uploaded, white
    const uint32_t key[4] = { base & 0x1FFFFFFFu, format | (dimension << 8), width, height };
    Handle handle = g_textureKeys.Find(key);
    const uint8_t* data = Guest::Base + Guest::PhysicalAlias(base);
    if (handle != 0)
    {
        TextureEntry& entry = g_textures[handle - 1];
        entry.usedFrame = g_frame;
        if (entry.checkedFrame == g_frame) return handle;
        entry.checkedFrame = g_frame;
        const uint64_t fingerprint = Fingerprint(data, std::min(SourceBytes(fetch), size_t(64u << 20)));
        if (fingerprint == entry.fingerprint) return handle;
        entry.fingerprint = fingerprint;
        g_resourceBytes -= entry.bytes;
        if (!UploadTexture(entry, fetch, data)) { g_textureKeys.Erase(entry.key); entry.live = false; g_freeTextures.push_back(handle); return 1; }
        g_resourceBytes += entry.bytes;
        return handle;
    }

    if (!g_freeTextures.empty()) { handle = g_freeTextures.back(); g_freeTextures.pop_back(); }
    else { g_textures.emplace_back(); handle = Handle(g_textures.size()); }
    TextureEntry& entry = g_textures[handle - 1];
    entry = TextureEntry();
    memcpy(entry.key, key, sizeof(entry.key));
    entry.fingerprint = Fingerprint(data, std::min(SourceBytes(fetch), size_t(64u << 20)));
    entry.checkedFrame = entry.usedFrame = g_frame;
    if (!UploadTexture(entry, fetch, data)) { g_freeTextures.push_back(handle); return 1; }
    entry.live = true;
    g_textureKeys.Insert(key, handle);
    g_resourceBytes += entry.bytes;
    return handle;
}

ID3D11ShaderResourceView* RenderResources::TextureView(Handle handle, uint32_t dimension)
{
    if (handle == 0 || handle > g_textures.size()) return nullptr;
    const TextureEntry& entry = g_textures[handle - 1];
    if (dimension == 3) return entry.cube ? entry.resource.Get() : g_whiteCubeView.Get();
    // A volume fetch reads a flat texture (xenos_hlsl.cpp says why).
    return entry.cube ? entry.face.Get() : entry.resource.Get();
}

RenderState::Handle RenderResources::VertexBufferFor(uint32_t physical, uint32_t bytes, uint32_t endian)
{
    if (bytes == 0 || bytes > (128u << 20)) return 0;
    const uint32_t key[2] = { physical & 0x1FFFFFFFu, endian };
    Handle handle = g_bufferKeys.Find(key);
    const uint8_t* data = Guest::Base + Guest::PhysicalAlias(physical);
    bytes = (bytes + 3) & ~3u;
    uint32_t wanted = bytes;

    if (handle != 0)
    {
        BufferEntry& entry = g_buffers[handle - 1];
        entry.usedFrame = g_frame;
        if (entry.checkedFrame == g_frame && entry.seen >= bytes) return handle;
        entry.checkedFrame = g_frame;
        wanted = std::max(bytes, entry.seen);
        const uint64_t fingerprint = Fingerprint(data, wanted);
        if (fingerprint == entry.fingerprint && entry.bytes >= wanted) return handle;
        entry.fingerprint = fingerprint;
        entry.seen = wanted;
        if (entry.bytes < wanted)
        {
            // Grown: made again below.
            g_resourceBytes -= entry.bytes;
            g_bufferKeys.Erase(entry.key);
            entry.buffer.Reset();
            entry.resource.Reset();
            entry.live = false;
            g_freeBuffers.push_back(handle);
            handle = 0;
        }
        else
        {
            D3D11_BOX box{ 0, 0, 0, wanted, 1, 1 };
            g_context->UpdateSubresource(entry.buffer.Get(), 0, &box, data, 0, 0);
            g_bufferUploads.fetch_add(1, std::memory_order_relaxed);
            g_bufferBytes.fetch_add(wanted, std::memory_order_relaxed);
            RenderStats::Frame().bufferUploads++;
            RenderStats::Frame().uploadBytes += wanted;
            return handle;
        }
    }

    if (!g_freeBuffers.empty()) { handle = g_freeBuffers.back(); g_freeBuffers.pop_back(); }
    else { g_buffers.emplace_back(); handle = Handle(g_buffers.size()); }
    BufferEntry& entry = g_buffers[handle - 1];
    entry = BufferEntry();
    memcpy(entry.key, key, sizeof(entry.key));
    entry.fingerprint = Fingerprint(data, wanted);
    entry.checkedFrame = entry.usedFrame = g_frame;
    entry.seen = wanted;
    // A little room to grow: the title's dynamic buffers are asked for at
    // whatever size the frame needs, and a buffer made again every time
    // that grows a little is a buffer made again every frame.
    entry.bytes = (wanted + wanted / 8 + 4095) & ~4095u;
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = entry.bytes;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
    if (FAILED(g_device->CreateBuffer(&desc, nullptr, &entry.buffer))) { g_freeBuffers.push_back(handle); return 0; }
    D3D11_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = DXGI_FORMAT_R32_TYPELESS;
    view.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
    view.BufferEx.FirstElement = 0;
    view.BufferEx.NumElements = entry.bytes / 4;
    view.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
    if (FAILED(g_device->CreateShaderResourceView(entry.buffer.Get(), &view, &entry.resource))) { entry.buffer.Reset(); g_freeBuffers.push_back(handle); return 0; }
    D3D11_BOX box{ 0, 0, 0, wanted, 1, 1 };
    g_context->UpdateSubresource(entry.buffer.Get(), 0, &box, data, 0, 0);
    entry.live = true;
    g_bufferKeys.Insert(key, handle);
    g_resourceBytes += entry.bytes;
    g_bufferUploads.fetch_add(1, std::memory_order_relaxed);
    g_bufferBytes.fetch_add(wanted, std::memory_order_relaxed);
    RenderStats::Frame().bufferUploads++;
    RenderStats::Frame().uploadBytes += wanted;
    return handle;
}

ID3D11ShaderResourceView* RenderResources::VertexBufferView(Handle handle)
{
    if (handle == 0 || handle > g_buffers.size()) return nullptr;
    return g_buffers[handle - 1].resource.Get();
}

uint32_t RenderResources::IndexAppend(const void* data, uint32_t bytes)
{
    g_indexBytes.fetch_add(bytes, std::memory_order_relaxed);
    return g_indexRing.Append(data, bytes, 4);
}

ID3D11Buffer* RenderResources::IndexRing() { return g_indexRing.buffer.Get(); }

bool RenderResources::ConstantReserve(uint32_t bytes)
{
    const uint32_t aligned = (bytes + 255) & ~255u;
    if (g_constantRing.offset != 0 && g_constantRing.offset + aligned <= g_constantRing.bytes) return false;
    g_constantRing.offset = 0;
    return true;
}

uint32_t RenderResources::ConstantAppend(const void* data, uint32_t bytes)
{
    g_constantBytes.fetch_add(bytes, std::memory_order_relaxed);
    return g_constantRing.Append(data, bytes, 256);
}

uint8_t* RenderResources::ConstantMap(uint32_t bytes, uint32_t& offset)
{
    g_constantBytes.fetch_add(bytes, std::memory_order_relaxed);
    return g_constantRing.Map(bytes, 256, offset);
}

void RenderResources::ConstantUnmap() { g_constantRing.Unmap(); }

ID3D11Buffer* RenderResources::ConstantRing() { return g_constantRing.buffer.Get(); }
bool RenderResources::ConstantOffsetsAvailable() { return g_constantOffsets; }

RenderResources::Statistics RenderResources::Stats()
{
    Statistics s;
    s.textureUploads = g_textureUploads.load(std::memory_order_relaxed);
    s.textureBytes = g_textureBytes.load(std::memory_order_relaxed);
    s.bufferUploads = g_bufferUploads.load(std::memory_order_relaxed);
    s.bufferBytes = g_bufferBytes.load(std::memory_order_relaxed);
    s.indexBytes = g_indexBytes.load(std::memory_order_relaxed);
    s.constantBytes = g_constantBytes.load(std::memory_order_relaxed);
    s.resourceBytes = g_resourceBytes;
    s.textures = g_textureKeys.Size();
    s.buffers = g_bufferKeys.Size();
    s.colorTargets = g_colorKeys.Size();
    s.depthTargets = g_depthKeys.Size();
    s.resolved = g_resolvedKeys.Size();
    s.released = g_released;
    return s;
}
