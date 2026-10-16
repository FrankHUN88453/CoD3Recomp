#pragma once
#include <cstdint>
namespace WeaponCycleInput
{
    constexpr uint16_t Button=0x8000;
    struct Latch { bool down=false; unsigned pending=0; };
    inline void Observe(Latch& state,uint16_t buttons)
    {
        const bool down=(buttons&Button)!=0;
        if (down && !state.down && state.pending<8) ++state.pending;
        state.down=down;
    }
    inline unsigned Drain(Latch& state) { const unsigned n=state.pending;state.pending=0;return n; }
    inline uint16_t Report(uint16_t buttons,bool enabled,bool paused)
    {
        return enabled && !paused ? uint16_t(buttons&~Button) : buttons;
    }
}
