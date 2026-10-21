// The button prompts drawn as keys: a picture of key caps put in place of the
// button icon font's texture (pc_prompts.cpp says which texture, which cells
// and which labels). The picture is four times the texture's size, so the
// labels stay sharp at the window's resolution; the font's texture
// coordinates go on finding the same cells in it.

#include "render.h"
#include "render_internal.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace
{
    std::mutex g_mutex;
    std::vector<Render::PromptCell> g_cells;     // the cells asked for
    uint64_t g_asked = 0;                        // bumped when they change
    std::atomic<uint32_t> g_physical{ 0 };       // the texture they stand in for, nought for none

    ID3D11Device* g_device = nullptr;
    uint64_t g_made = ~0ull;                     // the cells the picture was made from
    ComPtr<ID3D11ShaderResourceView> g_view;

    constexpr uint32_t Width = 1024, Height = 512;

    // The labels' coverage, white on black, drawn by GDI: a font as large as
    // fits the cell, the arrows from the symbol font.
    std::vector<uint8_t> Labels(const std::vector<Render::PromptCell>& cells)
    {
        std::vector<uint8_t> coverage(size_t(Width) * Height);
        HDC dc = CreateCompatibleDC(nullptr);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = int(Width);
        info.bmiHeader.biHeight = -int(Height);
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (bitmap == nullptr || bits == nullptr) { DeleteDC(dc); return coverage; }
        HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
        memset(bits, 0, size_t(Width) * Height * 4);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        for (const Render::PromptCell& cell : cells)
        {
            const int x0 = int(std::lround(cell.u0 * Width)), y0 = int(std::lround(cell.v0 * Height));
            const int x1 = int(std::lround(cell.u1 * Width)), y1 = int(std::lround(cell.v1 * Height));
            const int w = x1 - x0, h = y1 - y0;
            if (w <= 4 || h <= 4 || cell.label[0] == 0) continue;
            wchar_t text[32] = {};
            MultiByteToWideChar(CP_UTF8, 0, cell.label, -1, text, 31);
            const bool symbol = text[0] >= 0x2190 && text[0] <= 0x21FF;
            int size = int(h * 0.62f);
            for (; size > 6; size -= 2)
            {
                HFONT font = CreateFontW(-size, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                    ANTIALIASED_QUALITY, DEFAULT_PITCH, symbol ? L"Segoe UI Symbol" : L"Segoe UI");
                HGDIOBJ oldFont = SelectObject(dc, font);
                SIZE extent{};
                GetTextExtentPoint32W(dc, text, int(wcslen(text)), &extent);
                const bool fits = extent.cx <= int(w * 0.78f);
                if (fits)
                {
                    RECT box{ x0, y0, x1, y1 };
                    DrawTextW(dc, text, -1, &box, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                }
                SelectObject(dc, oldFont);
                DeleteObject(font);
                if (fits) break;
            }
        }
        GdiFlush();
        const uint8_t* pixels = static_cast<const uint8_t*>(bits);
        for (size_t i = 0; i < coverage.size(); i++) coverage[i] = std::max(pixels[i * 4], std::max(pixels[i * 4 + 1], pixels[i * 4 + 2]));
        SelectObject(dc, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(dc);
        return coverage;
    }

    // The picture: in each cell a dark key cap with a brown rim, the label
    // light on it; nothing elsewhere. Straight alpha, as the font's own.
    std::vector<uint32_t> Picture(const std::vector<Render::PromptCell>& cells)
    {
        std::vector<uint32_t> rgba(size_t(Width) * Height, 0);
        const std::vector<uint8_t> coverage = Labels(cells);
        for (const Render::PromptCell& cell : cells)
        {
            const float x0 = cell.u0 * Width, y0 = cell.v0 * Height, x1 = cell.u1 * Width, y1 = cell.v1 * Height;
            const float w = x1 - x0, h = y1 - y0;
            if (w <= 4.0f || h <= 4.0f) continue;
            const float inset = std::min(w, h) * 0.06f, radius = std::min(w, h) * 0.2f, rim = std::max(1.5f, std::min(w, h) * 0.06f);
            const float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
            const float hx = w * 0.5f - inset - radius, hy = h * 0.5f - inset - radius;
            for (int y = int(y0); y < int(std::ceil(y1)) && y < int(Height); y++)
                for (int x = int(x0); x < int(std::ceil(x1)) && x < int(Width); x++)
                {
                    if (x < 0 || y < 0) continue;
                    // The rounded rectangle's signed distance at the pixel's centre.
                    const float dx = std::max(std::fabs(x + 0.5f - cx) - hx, 0.0f), dy = std::max(std::fabs(y + 0.5f - cy) - hy, 0.0f);
                    const float d = std::sqrt(dx * dx + dy * dy) - radius;
                    const float inside = std::clamp(0.5f - d, 0.0f, 1.0f);
                    if (inside <= 0.0f) continue;
                    const float onRim = std::clamp(d + rim + 0.5f, 0.0f, 1.0f);
                    const float text = coverage[size_t(y) * Width + x] / 255.0f;
                    // In the browns of the title's own words (its chapter
                    // titles and subtitles, about 168 130 78): a dark brown
                    // body under a brown rim, the label a light brown over
                    // both.
                    constexpr float Body[3] = { 34.0f, 26.0f, 16.0f }, Rim[3] = { 168.0f, 130.0f, 78.0f }, Label[3] = { 222.0f, 188.0f, 132.0f };
                    uint32_t rgb = 0;
                    for (int k = 0; k < 3; k++)
                    {
                        float c = Body[k] + (Rim[k] - Body[k]) * onRim;
                        c = c + (Label[k] - c) * text;
                        rgb |= uint32_t(std::lround(c)) << (8 * k);
                    }
                    const float a = inside * (0.86f + 0.14f * std::max(onRim, text));
                    rgba[size_t(y) * Width + x] = rgb | (uint32_t(std::lround(a * 255.0f)) << 24);
                }
        }
        return rgba;
    }
}

void Render::SetPromptKeys(uint32_t physical, const PromptCell* cells, uint32_t count)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (count == 0 || physical == 0)
    {
        g_physical.store(0);
        return;
    }
    const bool same = g_cells.size() == count && memcmp(g_cells.data(), cells, count * sizeof(PromptCell)) == 0;
    if (!same)
    {
        g_cells.assign(cells, cells + count);
        g_asked++;
    }
    g_physical.store(physical & 0x1FFFF000u);
}

void RenderInternal::SetPromptDevice(ID3D11Device* device) { g_device = device; }

ID3D11ShaderResourceView* RenderInternal::PromptTexture(uint32_t physical)
{
    const uint32_t wanted = g_physical.load(std::memory_order_relaxed);
    if (wanted == 0 || (physical & 0x1FFFF000u) != wanted || g_device == nullptr) return nullptr;
    ID3D11Device* const device = g_device;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_made != g_asked)
    {
        g_made = g_asked;
        g_view.Reset();
        const std::vector<uint32_t> rgba = Picture(g_cells);
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = Width;
        desc.Height = Height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        const D3D11_SUBRESOURCE_DATA initial{ rgba.data(), Width * 4, 0 };
        ComPtr<ID3D11Texture2D> texture;
        if (SUCCEEDED(device->CreateTexture2D(&desc, &initial, &texture))) device->CreateShaderResourceView(texture.Get(), nullptr, &g_view);
        printf("render: the button prompts drawn as keys (%zu cells)\n", g_cells.size());
        fflush(stdout);
    }
    return g_view.Get();
}
