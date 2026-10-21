#pragma once

// Sprinting, a PC addition (sprint.cpp).

namespace Sprint
{
    // The sprint key is held (in a level, the mouse held, no menu up) or not:
    // from the input thread, as often as it reads.
    void Hold(bool held);
}
