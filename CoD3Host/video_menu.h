#pragma once

// The VIDEO page of the title's OPTIONS (video_menu.cpp).

#include "input.h"

#include <cstdint>

struct PPCContext;

namespace VideoMenu
{
    // Each update of a front end menu system, on the title's thread: before
    // the title's own (the options menu given its VIDEO entry) and after it
    // (the page's words and presses).
    void Frame(PPCContext& ctx, uint8_t* base, uint32_t system);
    void AfterFrame(PPCContext& ctx, uint8_t* base, uint32_t system);

    // The VIDEO page is up: the pad, the keys and the mouse are its.
    bool Active();

    // The pad as read, on the input's thread: while the page is up its
    // presses are taken for it and the title is given none (and after it
    // is left with B, the B, so the title goes back too).
    void FilterPad(Input::Pad& pad);

    // The mouse wheel on the page: it scrolls the settings.
    void Wheel(int clicks);

    // The pointer at x, y of the menus' 640 by 480 on the page: the row
    // under it (-1 for none) and the arrow or value (-1 left, 1 right).
    // False when the page is not up.
    bool HitTest(float x, float y, int& row, int& direction);
}
