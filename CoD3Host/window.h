#pragma once

// The window the finished frame goes to, and the status view that stands in
// while there is no finished frame.

#include <cstdint>

namespace Window
{
    void Open();
    void Close();
    bool IsOpen();

    // Whether the title's own window is the one in front. Keyboard input is
    // only taken when it is, so typing elsewhere is not read as a button.
    bool HasFocus();

    // The window itself, for the few things that need to talk to Windows
    // about it: pinning the pointer inside it and putting it back in the
    // middle. Handed out as a void pointer so nothing else has to include
    // Windows.h to ask.
    void* Handle();

    // Whether to draw a pointer over the window. It is hidden while the mouse
    // is steering, because a cursor pinned to the middle of the screen is only
    // something to look at.
    void SetCursorHidden(bool hidden);

    // Where the guest resolved its last frame. Everything after this is a blit.
    void SetFrontBuffer(uint32_t address, uint32_t width, uint32_t height);
    // Wheel notches turned since the last call, positive away from the user.
    int TakeWheel();

    // How that buffer is laid out. A resolved surface is wider than the part
    // that is visible, and its texels are tiled rather than in reading order,
    // so the presenter cannot walk it row by row without being told.
    void SetFrontBufferLayout(uint32_t pitch, bool tiled);
}
