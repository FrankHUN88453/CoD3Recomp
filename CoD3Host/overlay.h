// The settings menu (F11), the frame counter and the screenshot key (F12),
// drawn over the picture with Dear ImGui on the window's thread.
#pragma once

#include <cstdint>
#include <string>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct ID3D11Texture2D;

namespace Overlay
{
    // On the window's thread, once the swap chain exists.
    void Initialize(void* hwnd, ID3D11Device* device, ID3D11DeviceContext* context);

    // Before the window's Present: the menu and the counter into the back
    // buffer, and the screenshot of it when one was asked for (with a
    // backTexture). True when anything was drawn. The HDR mode draws into a
    // layer of its own (render_d3d11.cpp) and takes the screenshot itself.
    bool Render(ID3D11RenderTargetView* back, ID3D11Texture2D* backTexture, int width, int height);

    // The HDR mode's screenshot: whether F12 asked for one (the ask is
    // taken), and a PNG of the scRGB back buffer, scRgbWhite (the game's
    // white over 80 nits) at the top.
    bool TakeScreenshotRequest();
    void SaveScreenshot(ID3D11Texture2D* back, int width, int height, float scRgbWhite);

    // The window procedure hands every message here first; true when it
    // was the menu's.
    bool HandleMessage(void* hwnd, uint32_t message, uint64_t wParam, int64_t lParam);

    // The settings menu or the console is up: the keys and the mouse are
    // theirs, not the title's.
    bool IsOpen();
    void Toggle();
    void ToggleConsole();

    // A line for the console's log, from anywhere.
    void ConsolePrint(const std::string& line);
    // A message across the top of the picture for a while (a killstreak's).
    void Notice(const std::string& text, int milliseconds = 3000);
    void RequestScreenshot();

    // Frames the title finished, for the counter; called at every swap.
    void NoteFrame();

    // What the front end drew this frame (title_trace.cpp): the main
    // menu, or its options page. The settings menu opens itself when the
    // options page follows the main menu, which is the OPTIONS entry.
    enum class FrontEndText { MainMenu = 1, OptionsMenu = 2 };
    void NoteFrontEndText(FrontEndText text);

    // A game's own heads-up display drawn over the picture (Domination's
    // flags, dom.cpp): while active() says so, draw(width, height) each frame
    // between ImGui's new frame and its render.
    void SetHud(bool (*active)(), void (*draw)(int width, int height));
    // A second one, over the first (the custom class editor, class_editor.cpp).
    void SetMenuHud(bool (*active)(), void (*draw)(int width, int height));
    // A third, the killstreaks' box (mp_fixes.cpp, 0.5.82).
    void SetStreakHud(bool (*active)(), void (*draw)(int width, int height));
}
