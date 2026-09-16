// The settings menu (F11), the frame counter and the screenshot key (F12),
// drawn over the picture with Dear ImGui on the window's thread.
#pragma once

#include <cstdint>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct ID3D11Texture2D;

namespace Overlay
{
    // On the window's thread, once the swap chain exists.
    void Initialize(void* hwnd, ID3D11Device* device, ID3D11DeviceContext* context);

    // Before the window's Present: the menu and the counter into the back
    // buffer, and the screenshot of it when one was asked for.
    void Render(ID3D11RenderTargetView* back, ID3D11Texture2D* backTexture, int width, int height);

    // The window procedure hands every message here first; true when it
    // was the menu's.
    bool HandleMessage(void* hwnd, uint32_t message, uint64_t wParam, int64_t lParam);

    bool IsOpen();
    void Toggle();
    void RequestScreenshot();

    // Frames the title finished, for the counter; called at every swap.
    void NoteFrame();
}
