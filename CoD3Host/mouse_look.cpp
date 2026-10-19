// Looking around with the mouse, the way the PC Call of Duty games do it: the
// mouse's counts turn the view by a fixed angle each, straight into the view
// angles, both axes alike (a PC addition).
//
// The console has a pad, and the title turns its view in sub_8250FF48 (its
// CL_GamepadMove): the right stick's deflection, through a dead zone and a
// curve, ramped up while it is held, times the frame's time and the
// title's m_yaw and m_pitch, added to the local client's view angles
// (0x82A57900, 7280 bytes a client: +5732 pitch, +5736 yaw). A mouse turned
// into that stick steered like a pad: the curve and the ramp in every
// movement, a top speed, and up and down slower than left and right. So
// the mouse is no longer a stick in a level: after the title's own turn
// (the pad's, when one is plugged in) the mouse's counts since the last
// call go into the angles, so many degrees a count, under the same
// conditions the title's turn is under, scaled as the title scales the
// stick while aiming down the sights (+5696). COD3_STICKMOUSE=1 makes the
// mouse a stick again.

#include "input.h"
#include "kernel.h"
#include "settings.h"

#include <Windows.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" PPC_FUNC(__imp__sub_8250FF48);

namespace
{
    constexpr uint32_t LocalClient = 0x82A2A1C8;     // which local client
    constexpr uint32_t Clients = 0x82A57900;         // 7280 bytes each
    constexpr uint32_t ClientSize = 7280;
    constexpr uint32_t ClientFlags = 76;             // 0x4000: the title turns nothing
    constexpr uint32_t ClientMode = 1304;            // 5: turned another way (sub_8250F648), left to the stick
    constexpr uint32_t ClientZoom = 5696;            // the turn's scale while aiming down the sights
    constexpr uint32_t ClientPitch = 5732, ClientYaw = 5736;
    constexpr uint32_t PlayerEntities = 0x82A2AB40;  // a table, the local client's at [client + 1]
    constexpr uint32_t LookHeld = 0x82A90A6A;        // a byte per client: no turning now
    constexpr uint32_t Profile = 0x82A8E610;         // +65 the inverted look
    constexpr uint32_t InvertOn = 0x82062730, InvertOff = 0x8207CE48;   // the factor the title takes for each

    // Degrees a count at sensitivity 1: the PC games' m_yaw (0.022) at
    // their sensitivity 3.
    constexpr double DegreesPerCount = 0.022 * 3.0;

    float ReadFloat(uint8_t* base, uint32_t address)
    {
        const uint32_t word = Guest::Read32(base, address);
        float value;
        memcpy(&value, &word, 4);
        return value;
    }
    void WriteFloat(uint8_t* base, uint32_t address, float value)
    {
        uint32_t word;
        memcpy(&word, &value, 4);
        Guest::Write32(base, address, word);
    }
}

PPC_FUNC(sub_8250FF48)
{
    __imp__sub_8250FF48(ctx, base);
    static const bool stick = getenv("COD3_STICKMOUSE") != nullptr;
    if (stick) return;

    const uint32_t index = Guest::Read32(base, LocalClient);
    const uint32_t client = Clients + index * ClientSize;
    // The other way of turning (a mounted gun or a vehicle) keeps the mouse
    // as a stick: it is not told the mouse is taken here.
    if (Guest::Read32(base, client + ClientMode) == 5) return;
    Input::LookTaken();
    long dx = 0, dy = 0;
    const bool held = Input::TakeLook(dx, dy);
    // COD3_LOOKTEST=dx,dy: so many counts more each call, for a test run
    // with no hand on the mouse.
    // COD3_LOOKTEST=dx,dy,n turns round after n calls.
    static const struct Test { long x = 0, y = 0, turn = 0; bool on = false; } test = []() {
        Test t;
        if (const char* text = getenv("COD3_LOOKTEST")) t.on = sscanf(text, "%ld,%ld,%ld", &t.x, &t.y, &t.turn) >= 2;
        return t;
    }();
    static long calls = 0;
    if (test.on)
    {
        const long sign = test.turn > 0 && ++calls > test.turn ? -1 : 1;
        dx += test.x * sign;
        dy += test.y * sign;
    }
    else if (!held) return;
    if (dx == 0 && dy == 0) return;

    // Where the title's own turn would not have turned.
    if ((Guest::Read32(base, client + ClientFlags) & 0x4000) != 0) return;
    if (base[LookHeld + index] != 0) return;
    const uint32_t entity = Guest::Read32(base, Guest::Read32(base, PlayerEntities) + (index + 1) * 4);
    if (entity != 0 && Guest::Read32(base, entity + 528) != 0)
    {
        ctx.r3.u64 = entity;
        sub_820A1170(ctx, base);
        if ((ctx.r3.u32 & 0xFF) == 0) return;
    }

    const double zoom = ReadFloat(base, client + ClientZoom);
    const double degrees = DegreesPerCount * Settings::Get().mouseSensitivity * (zoom > 0.0 ? zoom : 1.0);
    // The title's inverted look takes another factor for its pitch.
    const double normal = ReadFloat(base, InvertOff);
    const double now = ReadFloat(base, base[Profile + 65] != 0 ? InvertOn : InvertOff);
    const double invert = normal != 0.0 && now / normal < 0.0 ? -1.0 : 1.0;
    WriteFloat(base, client + ClientYaw, float(double(ReadFloat(base, client + ClientYaw)) - double(dx) * degrees));
    WriteFloat(base, client + ClientPitch, float(double(ReadFloat(base, client + ClientPitch)) + double(dy) * degrees * invert));

    // COD3_MOUSELOG=1: what the look did, once a second.
    static const bool log = getenv("COD3_MOUSELOG") != nullptr;
    if (log)
    {
        static uint64_t second = 0;
        static long sumX = 0, sumY = 0;
        sumX += dx;
        sumY += dy;
        const uint64_t t = GetTickCount64() / 1000;
        if (t != second)
        {
            second = t;
            printf("look: %ld, %ld counts a second, %.4f degrees a count (zoom %.3f, invert %d), yaw %.2f pitch %.2f\n", sumX, sumY, degrees, zoom,
                invert < 0.0 ? 1 : 0, ReadFloat(base, client + ClientYaw), ReadFloat(base, client + ClientPitch));
            fflush(stdout);
            sumX = sumY = 0;
        }
    }
}
