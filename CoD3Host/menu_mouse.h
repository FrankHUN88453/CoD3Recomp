#pragma once

// Pointing at the title's own menus with the mouse (menu_mouse.cpp).

namespace MenuMouse
{
    // A menu of the title's is up (its system updated it lately).
    bool MenuUp();

    // The pointer is over one of its entries, which is highlighted: the
    // left button takes it as A.
    bool PointerOverEntry();

    // What the left button does where the pointer is: the cross's left
    // (-1) or right (1) pressed steps times (an arrow, an option's value,
    // a chapter of the strip), or A (takes).
    struct Click
    {
        int direction = 0;
        int steps = 0;
        bool takes = false;
    };
    Click ClickUnderPointer();

    // The left button as a test script presses it (COD3_MENUCLICK).
    bool ScriptedClick();

    // The menu up is the title screen ("PRESS START"), which wants Start.
    bool TitleScreen();
}
