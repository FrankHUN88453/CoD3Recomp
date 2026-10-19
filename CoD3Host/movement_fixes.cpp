// Movement as the later games of the engine have it (a PC addition).
//
// Bounces. Call of Duty 4 and World at War let a player who jumps onto a
// slope at the right angle bounce off it. That comes from one line of their
// PM_StepSlideMove: when a jump's step up leaves the player above a slope
// and the trace down finds it, the velocity is projected onto the slope
// with its speed kept (PM_ProjectVelocity), so speed into a steep slope
// becomes speed up it. Call of Duty 3 has the same function, from the same
// source, but clips the velocity there instead (PM_ClipVelocity: the speed
// into the slope is thrown away), and the jump dies on the slope: close to
// a bounce, never one.
//
// The clip at that one call is made the projection, as Call of Duty 4 has
// it (from the reconstructed source, kisakcod's bg_pmove.cpp). A slope
// steeper than Call of Duty 4 lets bounce (a normal under 0.3 up) keeps
// the clip. Every other caller of the clip is left as it is.
//
// Movement is predicted on each player's machine and decided on the host,
// so in multiplayer everyone in the match wants the same setting, or a
// bounce is taken back as it happens. The setting: "CoD4 bounces" in the
// F11 menu (bounces in the ini).

#include "kernel.h"
#include "settings.h"
#include "input.h"

#include <cmath>
#include <cstring>

namespace
{
    // The step down's clip in PM_StepSlideMove: its return address.
    constexpr uint32_t StepDownClipCall = 0x8248FB70;

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

// PM_ClipVelocity(const float* in, const float* normal, float* out)
extern "C" PPC_FUNC(__imp__sub_8246B138);
PPC_FUNC(sub_8246B138)
{
    if (uint32_t(ctx.lr) == StepDownClipCall && Settings::Get().bounces)
    {
        const uint32_t in = ctx.r3.u32, normalAt = ctx.r4.u32, out = ctx.r5.u32;
        const float v[3] = { ReadFloat(base, in), ReadFloat(base, in + 4), ReadFloat(base, in + 8) };
        const float n[3] = { ReadFloat(base, normalAt), ReadFloat(base, normalAt + 4), ReadFloat(base, normalAt + 8) };
        if (n[2] >= 0.3f)
        {
            // PM_ProjectVelocity, as Call of Duty 4 has it.
            const float lengthSq2D = v[0] * v[0] + v[1] * v[1];
            float result[3] = { v[0], v[1], v[2] };
            if (std::fabs(n[2]) >= 0.001f && lengthSq2D != 0.0f)
            {
                const float newZ = -(n[0] * v[0] + n[1] * v[1]) / n[2];
                const float lengthScale = std::sqrt((v[2] * v[2] + lengthSq2D) / (newZ * newZ + lengthSq2D));
                if (lengthScale < 1.0f || newZ < 0.0f || v[2] > 0.0f)
                {
                    result[0] = v[0] * lengthScale;
                    result[1] = v[1] * lengthScale;
                    result[2] = newZ * lengthScale;
                }
            }
            WriteFloat(base, out, result[0]);
            WriteFloat(base, out + 4, result[1]);
            WriteFloat(base, out + 8, result[2]);
            return;
        }
    }
    __imp__sub_8246B138(ctx, base);
}

#ifndef COD3_MP
// The campaign's binoculars coming up while running. The +binoculars
// command (sub_8250CDB0: the player's +1340 bit 1, or 0x82A578F4's bit 2
// to raise them) was reaching the game in the middle of a run (the PC's
// tap-to-sprint keeps the sprint button held for the title, input.cpp),
// and the binoculars then stopped the run. They are not taken out while
// running; standing or walking, they come up as before.
extern "C" PPC_FUNC(__imp__sub_8250CDB0);
PPC_FUNC(sub_8250CDB0)
{
    const Input::Pad pad = Input::State();
    static int said = 0;
    if ((said < 40 && said++ < 40))
    {
        printf("binoculars: +binoculars with buttons 0x%04X, left stick %d,%d%s\n", pad.buttons, pad.thumbLX, pad.thumbLY,
               Input::Running() ? ": running, not taken out" : "");
        fflush(stdout);
    }
    if (Input::Running()) return;
    __imp__sub_8250CDB0(ctx, base);
}
#endif
