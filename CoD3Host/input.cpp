#include "input.h"
#include "window.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>

#include <Windows.h>

namespace
{
    // The Xbox button masks, which are also the XInput ones.
    constexpr uint16_t PadUp        = 0x0001;
    constexpr uint16_t PadDown      = 0x0002;
    constexpr uint16_t PadLeft      = 0x0004;
    constexpr uint16_t PadRight     = 0x0008;
    constexpr uint16_t PadStart     = 0x0010;
    constexpr uint16_t PadBack      = 0x0020;
    constexpr uint16_t PadLeftThumb  = 0x0040;
    constexpr uint16_t PadLeftShoulder  = 0x0100;
    constexpr uint16_t PadRightShoulder = 0x0200;
    constexpr uint16_t PadA         = 0x1000;
    constexpr uint16_t PadB         = 0x2000;
    constexpr uint16_t PadX         = 0x4000;
    constexpr uint16_t PadY         = 0x8000;

    constexpr int16_t StickFull = 32767;

    struct XInputGamepad
    {
        uint16_t buttons;
        uint8_t leftTrigger;
        uint8_t rightTrigger;
        int16_t thumbLX, thumbLY, thumbRX, thumbRY;
    };
    struct XInputState
    {
        uint32_t packetNumber;
        XInputGamepad gamepad;
    };

    using GetStateFunction = uint32_t (WINAPI*)(uint32_t, XInputState*);
    GetStateFunction g_getState = nullptr;
    bool g_looked = false;

    void FindXInput()
    {
        if (g_looked) return;
        g_looked = true;

        // Whichever version this machine has. Loading it by name keeps the
        // build from needing the library at all.
        static const wchar_t* const names[] = {
            L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll" };
        for (const wchar_t* name : names)
        {
            HMODULE library = LoadLibraryW(name);
            if (library == nullptr) continue;
            g_getState = reinterpret_cast<GetStateFunction>(
                reinterpret_cast<void*>(GetProcAddress(library, "XInputGetState")));
            if (g_getState != nullptr) return;
        }
    }

    bool Down(int key) { return (GetAsyncKeyState(key) & 0x8000) != 0; }

    // The mouse, held by the window.
    //
    // A first person game steers with the right stick, and a mouse is a
    // relative device, so the two only meet if the pointer is pinned: the
    // cursor goes back to the middle of the window after every read and the
    // distance it travelled becomes the stick's deflection. It is taken by
    // clicking in the window and given back with Escape, because a window that
    // swallows the pointer without asking cannot be left.
    std::atomic<bool> g_mouseHeld{ false };
    POINT g_centre{};

    void SetHold(bool hold)
    {
        if (g_mouseHeld.load() == hold) return;

        const HWND window = static_cast<HWND>(Window::Handle());
        if (window == nullptr) return;

        if (hold)
        {
            RECT client{};
            GetClientRect(window, &client);
            POINT topLeft{ client.left, client.top };
            POINT bottomRight{ client.right, client.bottom };
            ClientToScreen(window, &topLeft);
            ClientToScreen(window, &bottomRight);

            g_centre.x = (topLeft.x + bottomRight.x) / 2;
            g_centre.y = (topLeft.y + bottomRight.y) / 2;

            RECT screen{ topLeft.x, topLeft.y, bottomRight.x, bottomRight.y };
            ClipCursor(&screen);
            SetCursorPos(g_centre.x, g_centre.y);
            printf("input: the mouse is held by the window and steers; "
                   "Escape gives it back\n");
        }
        else
        {
            ClipCursor(nullptr);
            printf("input: the mouse is free; click in the window to steer "
                   "with it\n");
        }
        fflush(stdout);

        Window::SetCursorHidden(hold);
        g_mouseHeld.store(hold);
    }

    // How far the pointer moved since the last look, in stick units. Full
    // deflection at about twenty five pixels between reads, which at a read
    // every four milliseconds is a brisk turn rather than a twitch.
    void ReadMouse(int16_t& outX, int16_t& outY)
    {
        outX = 0;
        outY = 0;
        if (!g_mouseHeld.load()) return;

        POINT now{};
        if (!GetCursorPos(&now)) return;

        const long dx = now.x - g_centre.x;
        const long dy = now.y - g_centre.y;
        if (dx != 0 || dy != 0) SetCursorPos(g_centre.x, g_centre.y);

        constexpr long Gain = 1300;
        const long x = std::clamp(dx * Gain, -32767L, 32767L);
        // Screen coordinates grow downwards and a stick grows upwards.
        const long y = std::clamp(-dy * Gain, -32767L, 32767L);
        outX = int16_t(x);
        outY = int16_t(y);
    }

    // One step of the menu highlight for each stretch of mouse movement.
    //
    // A menu reads the pad's cross as presses, not as a position, so the
    // movement is turned into presses: travel builds up, and each time enough
    // of it has gone by in one direction the step is reported once and the
    // total is cleared. The gap keeps a single flick from running down the
    // whole list, and the press is held for two reads so the title cannot miss
    // it between polls.
    uint16_t MenuStep()
    {
        static POINT last{};
        static bool have = false;
        static long travel = 0;
        static uint16_t holding = 0;
        static int holdReads = 0;

        POINT now{};
        if (!GetCursorPos(&now)) return 0;

        if (!have) { last = now; have = true; return 0; }
        const long dy = now.y - last.y;
        last = now;

        if (holdReads > 0)
        {
            holdReads--;
            return holding;
        }
        holding = 0;

        // Movement the other way cancels what had built up, so a wobble does
        // not eventually add up to a step.
        if ((dy > 0) != (travel > 0)) travel = 0;
        travel += dy;

        constexpr long Step = 28;   // pixels of travel to one press
        if (travel >= Step)  { travel = 0; holding = PadDown; holdReads = 2; }
        if (travel <= -Step) { travel = 0; holding = PadUp;   holdReads = 2; }
        return holding;
    }

    // The keyboard and mouse stand in for a pad. Only while the title's own
    // window is in front, so typing anywhere else is not taken as input.
    Input::Pad ReadKeyboard()
    {
        Input::Pad pad{};
        if (!Window::HasFocus())
        {
            if (g_mouseHeld.load()) SetHold(false);
            return pad;
        }

        // Taking and giving back the pointer.
        //
        // The left button used to take it, which made the mouse useless for the
        // one thing a menu wants it for: clicking. F1 and the middle button
        // take it instead, Escape always gives it back, and the left button is
        // left free to mean what it means on the screen in front of you.
        if (g_mouseHeld.load() && Down(VK_ESCAPE)) SetHold(false);
        else if (!g_mouseHeld.load() && (Down(VK_F1) || Down(VK_MBUTTON)))
            SetHold(true);

        const bool held = g_mouseHeld.load();

        struct Mapping { int key; uint16_t button; };
        static const Mapping mappings[] = {
            { VK_RETURN, PadStart },
            { VK_UP, PadUp }, { VK_DOWN, PadDown },
            { VK_LEFT, PadLeft }, { VK_RIGHT, PadRight },
            { VK_SPACE, PadA }, { 'Z', PadA },
            { 'C', PadB }, { VK_CONTROL, PadB }, { 'X', PadB },
            { 'R', PadX },
            { 'F', PadY }, { VK_TAB, PadY },
            { 'Q', PadLeftShoulder }, { 'E', PadRightShoulder },
            { VK_SHIFT, PadLeftThumb },
        };

        for (const Mapping& mapping : mappings)
            if (Down(mapping.key)) pad.buttons |= mapping.button;

        // The title's first screen waits for Start, and asking a player to know
        // that is asking them to guess. While the mouse is free, which is every
        // menu, Space is Start as well as A.
        if (!held && Down(VK_SPACE)) pad.buttons |= PadStart;
        if (!held && Down(VK_ESCAPE)) pad.buttons |= PadBack;

        // Driving a menu with the mouse.
        //
        // The menus are built for a pad: a highlight that moves up and down and
        // a button that takes it. There is no way to ask the title what is
        // under the pointer, so pointing at an entry cannot select it. What can
        // be done is to make the mouse do what the pad does. Moving it up or
        // down steps the highlight, one step per movement rather than one per
        // pixel, and the left button takes the entry. It is not pointing and
        // clicking, but it is a menu driven with the mouse.
        if (!held)
        {
            if (Down(VK_LBUTTON)) pad.buttons |= PadA;
            if (Down(VK_RBUTTON)) pad.buttons |= PadBack;
            pad.buttons |= MenuStep();
        }

        // Moving. WASD is the left stick so it is analogue, and the arrows stay
        // on the pad's cross so menus that only read that still work.
        int16_t x = 0, y = 0;
        if (Down('A')) x -= StickFull;
        if (Down('D')) x += StickFull;
        if (Down('S')) y -= StickFull;
        if (Down('W')) y += StickFull;
        pad.thumbLX = x;
        pad.thumbLY = y;

        ReadMouse(pad.thumbRX, pad.thumbRY);

        // Firing and aiming, where a shooter expects them.
        if (held && Down(VK_LBUTTON)) pad.rightTrigger = 255;
        if (held && Down(VK_RBUTTON)) pad.leftTrigger = 255;

        return pad;
    }

    std::mutex g_mutex;
    Input::Pad g_pad{};
    uint32_t g_packet = 1;
    std::chrono::steady_clock::time_point g_lastRead;

    bool Same(const Input::Pad& a, const Input::Pad& b)
    {
        return a.buttons == b.buttons &&
               a.leftTrigger == b.leftTrigger && a.rightTrigger == b.rightTrigger &&
               a.thumbLX == b.thumbLX && a.thumbLY == b.thumbLY &&
               a.thumbRX == b.thumbRX && a.thumbRY == b.thumbRY;
    }

    // Asking Windows on every call would cost more than the title's own frame.
    // Once every four milliseconds is finer than any frame it draws.
    void Refresh()
    {
        const auto now = std::chrono::steady_clock::now();

        std::lock_guard<std::mutex> lock(g_mutex);
        if (now - g_lastRead < std::chrono::milliseconds(4)) return;
        g_lastRead = now;

        FindXInput();

        // A pad and the keyboard together, not one or the other. A pad that is
        // plugged in and untouched used to silence the keyboard completely,
        // which looks exactly like input not working at all.
        Input::Pad pad = ReadKeyboard();
        if (g_getState != nullptr)
        {
            XInputState state{};
            if (g_getState(0, &state) == ERROR_SUCCESS)
            {
                pad.buttons |= state.gamepad.buttons;
                pad.leftTrigger = std::max(pad.leftTrigger, state.gamepad.leftTrigger);
                pad.rightTrigger = std::max(pad.rightTrigger, state.gamepad.rightTrigger);
                if (state.gamepad.thumbLX != 0 || state.gamepad.thumbLY != 0)
                {
                    pad.thumbLX = state.gamepad.thumbLX;
                    pad.thumbLY = state.gamepad.thumbLY;
                }
                if (state.gamepad.thumbRX != 0 || state.gamepad.thumbRY != 0)
                {
                    pad.thumbRX = state.gamepad.thumbRX;
                    pad.thumbRY = state.gamepad.thumbRY;
                }
            }
        }

        if (!Same(pad, g_pad))
        {
            g_pad = pad;
            g_packet++;
        }
    }
}

Input::Pad Input::State()
{
    Refresh();
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_pad;
}

uint16_t Input::Buttons()
{
    return State().buttons;
}

bool Input::MouseHeld()
{
    return g_mouseHeld.load();
}

uint32_t Input::PacketNumber()
{
    Refresh();
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_packet;
}

const char* Input::KeyboardHelp()
{
    return "Enter or Space start, mouse moves the menu and clicks to choose, "
           "F1 steers with it, Escape frees it";
}
