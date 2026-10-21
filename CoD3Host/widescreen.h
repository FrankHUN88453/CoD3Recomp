#pragma once

// Pictures wider than 16:9 (widescreen.cpp).

#include <cstdint>

namespace Widescreen
{
    // How many times wider than 16:9 the picture is now: 1 outside a
    // level, or when the settings keep the console's 16:9.
    float Stretch();

    // The picture's aspect: 16:9 times the stretch.
    float PictureAspect();

    // The window's client size, for the aspect that fits it (the
    // renderer's present says it).
    void SetWindowSize(int width, int height);
}
