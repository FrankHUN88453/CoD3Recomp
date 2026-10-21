// Pictures wider than 16:9 (a PC addition).
//
// The title draws a 16:9 frame: its 3D through a projection of fov_x by
// fov_y degrees in that shape (the renderer's view, 0x82AAD790: +636 fov_x,
// +640 fov_y, set up by sub_82419470 once a frame from the client's refdef),
// its 2D (the HUD, the menus, the names over the soldiers) through two
// programs whose vertices are already in clip space, from a 640 by 480
// space laid over the whole frame.
//
// Wider, in a level: the renderer's horizontal field of view is opened by
// the stretch k (the picture's aspect over 16:9), keeping the vertical, so
// the frame holds more on either side; the frame is shown k times as wide;
// and the 2D is drawn k times narrower about the middle (render_commands
// .cpp), so it keeps its shape in the middle 16:9 of the picture, except a
// quad across the whole width (a fade, a flash, the pause menu's shade),
// which spreads with the picture. The client's own refdef is left alone:
// the names and markers it projects from the world land where the 16:9
// frame had them, which the narrowing puts back over what they name.
// The menus outside a level, the films and the loading keep 16:9.

#include "widescreen.h"
#include "kernel.h"
#include "settings.h"

#include <Windows.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
    constexpr float Console = 16.0f / 9.0f;
    constexpr uint32_t RendererView = 0x82AAD790, ViewFovX = 636, ViewFovY = 640;

    float ReadFloat(uint8_t* base, uint32_t address)
    {
        const uint32_t v = Guest::Read32(base, address);
        float f;
        memcpy(&f, &v, 4);
        return f;
    }
    void WriteFloat(uint8_t* base, uint32_t address, float f)
    {
        uint32_t v;
        memcpy(&v, &f, 4);
        Guest::Write32(base, address, v);
    }

    std::atomic<int> g_windowWidth{ 0 }, g_windowHeight{ 0 };

    // A level being played (kernel_video.cpp has the same): the title's
    // "running" variable (its pointer at 0x829BAB18, its integer at +32)
    // and no film open (0x82A2A24C).
    bool Playing()
    {
        const uint32_t running = Guest::Read32(Guest::Base, 0x829BAB18u);
        if (running == 0 || Guest::Read32(Guest::Base, running + 32) == 0) return false;
        return Guest::Read32(Guest::Base, 0x82A2A24Cu) == 0;
    }

    // The aspect the settings ask: the console's 16:9, the window's, or a
    // fixed wider one. Narrower than 16:9 stays 16:9, with bars.
    float Wanted()
    {
        static const float pinned = []() { const char* t = getenv("COD3_ASPECT"); return t ? float(atof(t)) : 0.0f; }();
        if (pinned > 0.0f) return pinned;
        switch (Settings::Get().aspectRatio)
        {
        case 1:
        {
            const int w = g_windowWidth.load(std::memory_order_relaxed), h = g_windowHeight.load(std::memory_order_relaxed);
            return w > 0 && h > 0 ? float(w) / float(h) : Console;
        }
        case 2: return 21.0f / 9.0f;
        case 3: return 32.0f / 9.0f;
        default: return Console;
        }
    }
}

float Widescreen::Stretch()
{
    const float aspect = Wanted();
    if (!(aspect > Console + 0.01f) || !Playing()) return 1.0f;
    return std::fmin(aspect / Console, 2.5f);
}

float Widescreen::PictureAspect() { return Console * Stretch(); }

void Widescreen::SetWindowSize(int width, int height)
{
    g_windowWidth.store(width, std::memory_order_relaxed);
    g_windowHeight.store(height, std::memory_order_relaxed);
}

// The renderer's view's projection made (sub_82419470, once a frame for the
// main view): its horizontal field of view opened by the stretch first. The
// view is the renderer's copy of the refdef; should the copy not be made
// again by the next frame, the value written here is known and the one it
// replaced used again, so the opening never adds up.
extern "C" PPC_FUNC(__imp__sub_82419470);
PPC_FUNC(sub_82419470)
{
    static float written = 0.0f, original = 0.0f;
    const float k = Widescreen::Stretch();
    float fovX = ReadFloat(base, RendererView + ViewFovX);
    if (fovX == written && written != 0.0f) fovX = original;
    if (k > 1.0f && fovX > 1.0f && fovX < 179.0f)
    {
        // tan(fov/2) k times as large: degrees to half an angle in radians
        // and back.
        constexpr float DegreesToHalfRadians = 3.14159265f / 360.0f;
        const float wide = std::atan(std::tan(fovX * DegreesToHalfRadians) * k) / DegreesToHalfRadians;
        original = fovX;
        written = std::fmin(wide, 170.0f);
        WriteFloat(base, RendererView + ViewFovX, written);
    }
    else if (fovX != ReadFloat(base, RendererView + ViewFovX))
    {
        WriteFloat(base, RendererView + ViewFovX, fovX);
        written = 0.0f;
    }
    __imp__sub_82419470(ctx, base);
}
