#pragma once

// The button prompts for the keyboard and mouse (pc_prompts.cpp).

#include <cstdint>

struct PPCContext;

namespace PcPrompts
{
    // Once a frame, from the title's swap (in the menus and in a level
    // alike): the icon font's texture drawn as keys or as it is, as the
    // player plays.
    void Frame(uint8_t* base);

    // COD3_EXPANDTEST=1, from the menu systems' update: a few of the
    // placeholders a hint is written with ([VERB_MELEE] ...) expanded by the
    // title, once, and printed.
    void ExpandTest(PPCContext& ctx, uint8_t* base);
}
