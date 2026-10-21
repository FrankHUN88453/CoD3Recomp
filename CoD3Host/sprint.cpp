// Sprinting in the single player.
//
// The console's single player keeps the whole of the multiplayer's sprint
// (the movement, the fatigue, the weapon lowered), and only its commands
// were emptied: +sprint and -sprint (sub_8250A030, sub_8250A028) return at
// once, and no layout binds a button to them. In the multiplayer
// (codmp_xenonf.xex, 0x825273A8 and 0x82525810) they press and let go of
// the key button at +360 of the input's key buttons, which the single
// player's are laid out as too (0x829C8E68, 24 bytes each; +216 is +speed,
// +312 +attack, as in both); IN_KeyDown and IN_KeyUp are sub_8250BB50 and
// sub_8250A110 here. So the two are given their multiplayer bodies, and the
// sprint key sends them (input.cpp) while it is held in a level.

#include "sprint.h"
#include "kernel.h"

#include <Windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <utility>

extern "C" PPC_FUNC(__imp__sub_8250BB50);   // IN_KeyDown(button, ...)
extern "C" PPC_FUNC(__imp__sub_8250A110);   // IN_KeyUp(button, ...)

namespace
{
    constexpr uint32_t SprintButton = 0x829C8E68u + 360;
    std::mutex g_mutex;
    bool g_on = false;
}

// +sprint, as the multiplayer's: the arguments moved on by one and the
// sprint key button pressed.
PPC_FUNC(sub_8250A030)
{
    ctx.r5.u64 = ctx.r4.u64;
    ctx.r4.u64 = ctx.r3.u64;
    ctx.r3.u64 = SprintButton;
    sub_8250BB50(ctx, base);
}

// -sprint: let go.
PPC_FUNC(sub_8250A028)
{
    ctx.r5.u64 = ctx.r4.u64;
    ctx.r4.u64 = ctx.r3.u64;
    ctx.r3.u64 = SprintButton;
    sub_8250A110(ctx, base);
}

extern "C" PPC_FUNC(__imp__sub_824C2088);

namespace Mmio { void CallIndirect(PPCContext& ctx, uint8_t* base, uint32_t address); }

namespace
{
    // The fatigue meter. The multiplayer's stance widget (IGOStanceWidget,
    // its draw 0x824FB520) shows the sprint's stamina in the stance figure:
    // the figure at half its alpha, and over it, at full alpha, as much of
    // it from the feet up as there is stamina left. The single player's
    // widget (vtable 0x820711F4) lost that draw, and the helper that clips
    // the figure's quad (0x824D8610 there) with it, so both are done here.
    //
    // The stamina is the player state's float at +1136 (+1168 in the
    // multiplayer): 1 rested, spent at two thirds a second while sprinting
    // and back at a third a second from a second after (sub_82477F28).
    // The widget: +4 shown, +8 + 8 * stance the figure (stance_stand,
    // _crouch, _prone), +12 + 8 * stance its "_man" flash, +36 the stance,
    // +48 whether the flash shows. A figure (an FEImage, vtable 0x82070E04)
    // keeps its quads at +32 (+40 of them): four corners of 20 bytes from
    // +16, each x, y, u, v and an ARGB colour; its alpha (vtable +136)
    // sets the colours' top byte and its draw is vtable +4.
    constexpr uint32_t ClientIndex = 0x82A2A1C8, Clients = 0x82A2AB40, GameTime = 0x82A95244;
    constexpr uint32_t ClientState = 528, StateFlags = 44, StateStamina = 1136;
    constexpr uint32_t Sprinting = 0x10000;
    constexpr uint32_t WidgetShown = 4, WidgetFigures = 8, WidgetStance = 36, WidgetFlash = 48;
    constexpr uint32_t ImageQuads = 32, ImageQuadCount = 40, ImageAlpha = 136, ImageDraw = 4;
    constexpr uint32_t QuadCorners = 16, CornerSize = 20;
    // Once the stamina is back and the sprint over, the meter stays this
    // long and then fades in the next (ms of the game's time).
    constexpr uint32_t MeterHold = 500, MeterFade = 500;

    uint32_t g_activeAt = 0;    // the game's time the meter was last needed, 0 for not since the level began

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

    void CallImage(PPCContext& ctx, uint8_t* base, uint32_t image, uint32_t slot, float f1 = 0.0f)
    {
        PPCContext call = ctx;
        call.r3.u64 = image;
        call.f1.f64 = f1;
        Mmio::CallIndirect(call, base, Guest::Read32(base, Guest::Read32(base, image) + slot));
    }

    // Keeps of the quad only the part from its bottom edge up to fraction
    // of its height, the texture cut with it.
    void ClipFromBottom(uint8_t* base, uint32_t quad, float fraction)
    {
        // The corners are top left, bottom left, top right, bottom right;
        // which of each pair is the top is read rather than assumed.
        for (uint32_t pair = 0; pair < 2; pair++)
        {
            uint32_t top = quad + QuadCorners + pair * 2 * CornerSize, bottom = top + CornerSize;
            if (ReadFloat(base, top + 4) > ReadFloat(base, bottom + 4)) std::swap(top, bottom);
            for (uint32_t field : { 4u, 12u })     // y, v
            {
                const float b = ReadFloat(base, bottom + field);
                WriteFloat(base, top + field, b + (ReadFloat(base, top + field) - b) * fraction);
            }
        }
    }
}

// The stance widget's draw.
PPC_FUNC(sub_824C2088)
{
    const uint32_t widget = ctx.r3.u32;
    const uint32_t client = Guest::Read32(base, Guest::Read32(base, Clients) + (Guest::Read32(base, ClientIndex) + 1) * 4);
    const uint32_t state = client != 0 ? Guest::Read32(base, client + ClientState) : 0;
    const uint32_t stance = Guest::Read32(base, widget + WidgetStance);
    const uint32_t figure = stance < 3 ? Guest::Read32(base, widget + WidgetFigures + stance * 8) : 0;
    if (state == 0 || figure == 0 || Guest::Read8(base, widget + WidgetShown) == 0 || Guest::Read32(base, figure + ImageQuadCount) == 0)
    {
        __imp__sub_824C2088(ctx, base);
        return;
    }

    float stamina = ReadFloat(base, state + StateStamina);
    stamina = stamina > 0.0f ? (stamina < 1.0f ? stamina : 1.0f) : 0.0f;
    const bool sprinting = (Guest::Read32(base, state + StateFlags) & Sprinting) != 0;
    const uint32_t now = Guest::Read32(base, GameTime);
    if (now < g_activeAt) g_activeAt = 0;       // a new level, or the same again
    if (sprinting || stamina < 1.0f) g_activeAt = now != 0 ? now : 1;
    float meter = 0.0f;
    if (g_activeAt != 0)
    {
        const uint32_t since = now - g_activeAt;
        meter = since <= MeterHold ? 1.0f : since >= MeterHold + MeterFade ? 0.0f : 1.0f - float(since - MeterHold) / float(MeterFade);
    }

    static const bool log = getenv("COD3_STANCELOG") != nullptr;
    if (log)
    {
        static uint32_t last = 0;
        if (now - last >= 250 || now < last)
        {
            last = now;
            printf("stance: %u stance %u stamina %.3f sprinting %d meter %.2f\n", now, stance, stamina, sprinting ? 1 : 0, meter);
        }
    }

    if (meter <= 0.0f)
    {
        __imp__sub_824C2088(ctx, base);
        return;
    }

    // The figure's own alpha (the widget's update shows a standing figure
    // only for a while after a change; crouched and prone it stays), so
    // the meter fades out to it, not past it.
    const uint32_t quad = Guest::Read32(base, Guest::Read32(base, figure + ImageQuads));
    const float own = float(Guest::Read32(base, quad + QuadCorners + 16) >> 24) / 255.0f;
    const float alpha = meter > own ? meter : own;
    uint8_t saved[4 * CornerSize];
    memcpy(saved, base + quad + QuadCorners, sizeof(saved));

    // As the multiplayer draws it: the stamina left at full alpha, then the
    // whole figure at half.
    if (stamina > 0.0f)
    {
        CallImage(ctx, base, figure, ImageAlpha, alpha);
        ClipFromBottom(base, quad, stamina);
        CallImage(ctx, base, figure, ImageDraw);
        memcpy(base + quad + QuadCorners, saved, sizeof(saved));
    }
    CallImage(ctx, base, figure, ImageAlpha, alpha * 0.5f);
    CallImage(ctx, base, figure, ImageDraw);
    memcpy(base + quad + QuadCorners, saved, sizeof(saved));

    // And the flash after a change of stance, as the widget's own draw.
    if (Guest::Read8(base, widget + WidgetFlash) != 0)
        if (const uint32_t flash = Guest::Read32(base, widget + WidgetFigures + 4 + stance * 8); flash != 0)
            CallImage(ctx, base, flash, ImageDraw);
}

void Sprint::Hold(bool held)
{
    // COD3_SPRINTTEST=from,to: held between those seconds from the start, for a test.
    {
        static const char* const test = getenv("COD3_SPRINTTEST");
        static const uint64_t start = GetTickCount64();
        double from = 0, to = 0;
        if (test != nullptr && sscanf(test, "%lf,%lf", &from, &to) == 2)
        {
            const double seconds = double(GetTickCount64() - start) / 1000.0;
            held = held || (seconds >= from && seconds < to);
        }
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    if (held == g_on) return;
    g_on = held;
    Kernel::QueueConsoleCommand(held ? "+sprint" : "-sprint");
}
