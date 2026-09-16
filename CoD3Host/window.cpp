// The presentation path: a real window, and whatever the guest has put in its
// front buffer.
//
// On the console the GPU renders into EDRAM and resolves the finished frame to
// a buffer in main memory, which the video scaler then scans out. Everything up
// to that resolve is missing here, so most of the time there is no frame to
// show. When that is the case the window says what the runtime is doing instead
// of pretending, because a black window and a stalled title look identical.
//
// The moment a command processor does produce a resolved frame, this is what
// puts it on screen: the buffer address arrives through Window::SetFrontBuffer
// and every following present blits it.

#include "kernel.h"
#include "window.h"
#include "d3d11_backend.h"
#include <cstdlib>
#include <cstdio>
#include "gpu.h"
#include "edram.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <Windows.h>

namespace
{
    // The window opens at 720p, which is what the console put on screen,
    // and the picture fills it at 16 by 9 whatever size it is made. The
    // frame behind it is drawn at a whole multiple of the title's 1040 by
    // 624 chosen from the window's height, so a window on a 4K screen gets
    // a frame drawn at that size rather than a small one stretched.
    // COD3_SCALE=N pins the multiple, 1 to 4.
    constexpr int Width = 1280;
    constexpr int Height = 720;

    uint32_t ScaleForHeight(int clientHeight)
    {
        static const int pinned = []() {
            const char* text = getenv("COD3_SCALE");
            return text != nullptr ? int(strtol(text, nullptr, 10)) : 0;
        }();
        if (pinned >= 1) return uint32_t(std::min(pinned, 4));
        const int scale = (clientHeight + 312) / 624;   // nearest whole multiple
        return uint32_t(std::max(1, std::min(scale, 4)));
    }

    std::atomic<bool> g_running{ false };
    std::atomic<HWND> g_window{ nullptr };
    std::thread g_thread;

    std::atomic<uint32_t> g_frontBuffer{ 0 };
    std::atomic<uint32_t> g_frontBufferWidth{ 0 };
    std::atomic<uint32_t> g_frontBufferHeight{ 0 };
    std::atomic<uint32_t> g_frontBufferPitch{ 0 };
    std::atomic<bool> g_frontBufferTiled{ false };
    std::atomic<uint64_t> g_presented{ 0 };

    std::vector<uint32_t> g_pixels;   // host side, top down BGRA

    std::atomic<bool> g_cursorHidden{ false };

    // Frames the guest finished, counted where the front buffer changes. The
    // window redraws at its own rate whether or not anything new arrived, so
    // counting redraws would report the window's speed and never the game's.
    std::atomic<uint64_t> g_guestFrames{ 0 };
    double g_framesPerSecond = 0.0;

    // The rate, worked out once a second. A shorter window makes the number
    // jump about; a longer one hides the stalls worth seeing.
    void UpdateRate()
    {
        static auto since = std::chrono::steady_clock::now();
        static uint64_t at = 0;

        const auto now = std::chrono::steady_clock::now();
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - since);
        if (elapsed < std::chrono::milliseconds(500)) return;

        const uint64_t frames = g_guestFrames.load();
        g_framesPerSecond = double(frames - at) * 1000.0 / double(elapsed.count());
        at = frames;
        since = now;
    }

    // Drawn onto the finished picture, before it goes to the screen, so it can
    // never flicker separately from the frame underneath it.
    void DrawRate(HDC dc)
    {
        UpdateRate();

        wchar_t text[32];
        swprintf_s(text, L"%.0f FPS", g_framesPerSecond);

        HFONT font = CreateFontW(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FF_DONTCARE, L"Consolas");
        HGDIOBJ previous = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);

        // A dark outline first: neon green on a bright frame would otherwise
        // disappear into it.
        SetTextColor(dc, RGB(0, 0, 0));
        for (int dx = -1; dx <= 1; dx++)
            for (int dy = -1; dy <= 1; dy++)
                if (dx != 0 || dy != 0)
                    TextOutW(dc, 12 + dx, 10 + dy, text, int(wcslen(text)));

        SetTextColor(dc, RGB(57, 255, 20));
        TextOutW(dc, 12, 10, text, int(wcslen(text)));

        SelectObject(dc, previous);
        DeleteObject(font);
    }

    LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w, LPARAM l)
    {
        // Windows asks the window what the pointer should look like whenever it
        // moves over it, and answering with nothing is how a pointer is hidden
        // over one window without touching it anywhere else.
        if (message == WM_SETCURSOR && LOWORD(l) == HTCLIENT &&
            g_cursorHidden.load())
        {
            SetCursor(nullptr);
            return TRUE;
        }
        if (message == WM_CLOSE || message == WM_DESTROY)
        {
            g_running.store(false);
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(window, message, w, l);
    }

    // A frame the guest resolved is big endian and tiled: its texels are stored
    // in an order that keeps a 32 by 32 block together, not row by row. The
    // resolve wrote it through Edram::TiledOffset, so it is read back through
    // the same function and the two cancel out exactly.
    // The size the pixels below were copied at is handed back with them. It
    // used to be read from the atomics again at blit time, and the guest swaps
    // buffers from another thread: when the size changed in between, the
    // bitmap was described with one height and filled with another, which slid
    // the picture up the window and wrapped the rest around to the top.
    // The size of what CopyFrontBuffer last put in g_pixels, which at a
    // resolution scale is not the size the title thinks its frame is.
    uint32_t g_copiedWidth = 0;
    uint32_t g_copiedHeight = 0;

    bool CopyFrontBuffer(uint32_t& outWidth, uint32_t& outHeight)
    {
        const uint32_t address = g_frontBuffer.load();
        if (address == 0) return false;

        // The frame at the resolution scale, when the resolve kept one.
        {
            uint32_t shadowWidth = 0, shadowHeight = 0;
            if (Edram::CopyShadow(address, shadowWidth, shadowHeight, g_pixels))
            {
                outWidth = shadowWidth;
                outHeight = shadowHeight;
                g_copiedWidth = shadowWidth;
                g_copiedHeight = shadowHeight;
                bool anything = false;
                for (uint32_t value : g_pixels) if (value != 0) { anything = true; break; }
                return anything;
            }
        }

        const uint32_t width = g_frontBufferWidth.load();
        const uint32_t height = g_frontBufferHeight.load();
        if (width == 0 || height == 0 || width > 4096 || height > 4096) return false;
        outWidth = width;
        outHeight = height;
        g_copiedWidth = width;
        g_copiedHeight = height;

        uint32_t pitch = g_frontBufferPitch.load();
        if (pitch < width) pitch = width;
        const bool tiled = g_frontBufferTiled.load();

        g_pixels.resize(size_t(width) * height);

        bool anything = false;
        for (uint32_t y = 0; y < height; y++)
        {
            for (uint32_t x = 0; x < width; x++)
            {
                const uint32_t offset = tiled
                    ? Edram::TiledOffset(x, y, pitch, 2)
                    : (y * pitch + x) * 4;
                const uint32_t guest = Guest::Read32(Guest::Base, address + offset);
                if (guest != 0) anything = true;
                // Guest pixels are ARGB stored big endian; the DIB wants BGRA
                // little endian, which is the same bytes the other way round.
                g_pixels[size_t(y) * width + x] = guest;
            }
        }
        return anything;
    }

    // The frame exactly as the guest resolved it, at its own size, written to
    // a file. Everything between the guest and the screen adds something of its
    // own; this is the one thing that does not, so a question about what is
    // actually being drawn is answered here rather than from a photograph of a
    // stretched window.
    void DumpFrame()
    {
        static const char* const path = getenv("COD3_FRAMEDUMP");
        if (path == nullptr) return;

        // Every hundred and twenty presents unless COD3_FRAMEDUMP_EVERY says
        // otherwise, numbered and cycling through forty, so the interesting
        // frame is still there when the run ends.
        static const int every = []() {
            const char* text = getenv("COD3_FRAMEDUMP_EVERY");
            const int value = text != nullptr ? int(strtol(text, nullptr, 10)) : 120;
            return value > 0 ? value : 120;
        }();
        static int counter = 0;
        if ((counter++ % every) != 0) return;

        char name[512];
        snprintf(name, sizeof(name), "%s-%02d.bmp", path, (counter / every) % 40);

        const int width = int(g_copiedWidth);
        const int height = int(g_copiedHeight);
        if (width <= 0 || height <= 0 || g_pixels.size() < size_t(width) * height) return;

        BITMAPFILEHEADER file{};
        BITMAPINFOHEADER info{};
        info.biSize = sizeof(info);
        info.biWidth = width;
        info.biHeight = -height;          // top down
        info.biPlanes = 1;
        info.biBitCount = 32;
        info.biCompression = BI_RGB;
        info.biSizeImage = DWORD(width) * DWORD(height) * 4;

        file.bfType = 0x4D42;             // "BM"
        file.bfOffBits = sizeof(file) + sizeof(info);
        file.bfSize = file.bfOffBits + info.biSizeImage;

        FILE* out = fopen(name, "wb");
        if (out == nullptr) return;
        fwrite(&file, sizeof(file), 1, out);
        fwrite(&info, sizeof(info), 1, out);
        fwrite(g_pixels.data(), 1, info.biSizeImage, out);
        fclose(out);
    }

    void DrawStatus(HDC dc, int width, int height)
    {
        RECT full{ 0, 0, width, height };
        HBRUSH background = CreateSolidBrush(RGB(16, 18, 22));
        FillRect(dc, &full, background);
        DeleteObject(background);

        SetBkMode(dc, TRANSPARENT);

        HFONT font = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FF_DONTCARE, L"Consolas");
        HGDIOBJ previous = SelectObject(dc, font);

        const Gpu::Statistics gpu = Gpu::Stats();
        const auto& stats = Kernel::Stats();

        wchar_t lines[12][160];
        int count = 0;

        SetTextColor(dc, RGB(235, 235, 235));
        swprintf_s(lines[count++], L"Call of Duty 3, recompiled");

        const Edram::Statistics resolve = Edram::Stats();

        SetTextColor(dc, RGB(160, 165, 175));
        swprintf_s(lines[count++], L"");
        swprintf_s(lines[count++], L"guest threads   %llu",
            (unsigned long long)stats.threadsCreated.load());
        swprintf_s(lines[count++], L"allocated       %.1f MB",
            stats.bytesAllocated.load() / 1048576.0);
        swprintf_s(lines[count++], L"command packets %llu",
            (unsigned long long)gpu.packets);
        swprintf_s(lines[count++], L"draw calls      %llu",
            (unsigned long long)gpu.draws);
        swprintf_s(lines[count++], L"frames resolved %llu into 0x%08X, %ux%u",
            (unsigned long long)resolve.resolves, resolve.lastDestination,
            resolve.lastWidth, resolve.lastHeight);
        swprintf_s(lines[count++], L"vertical blanks %llu",
            (unsigned long long)stats.frames.load());
        swprintf_s(lines[count++], L"");
        swprintf_s(lines[count++],
            L"Waiting for the title to resolve a frame. Everything below is");
        swprintf_s(lines[count++],
            L"live: once a frame is resolved it replaces this view.");
        swprintf_s(lines[count++],
            L"");

        int y = 40;
        for (int i = 0; i < count; i++)
        {
            if (i >= 8) SetTextColor(dc, RGB(220, 180, 120));
            TextOutW(dc, 48, y, lines[i], int(wcslen(lines[i])));
            y += 26;
        }

        SelectObject(dc, previous);
        DeleteObject(font);
    }

    void Present(HWND window)
    {
        HDC dc = GetDC(window);
        if (dc == nullptr) return;

        RECT client{};
        GetClientRect(window, &client);
        const int width = client.right - client.left;
        const int height = client.bottom - client.top;

        // The host GPU presents its own frame when it is drawing.
        if (D3D11Backend::Enabled())
        {
            D3D11Backend::Present(window, width, height);
            return;
        }

        uint32_t sourceWidth = 0;
        uint32_t sourceHeight = 0;
        if (CopyFrontBuffer(sourceWidth, sourceHeight))
        {
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = int(sourceWidth);
            info.bmiHeader.biHeight = -int(sourceHeight);   // top down
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;

            // The title renders 1040 by 624 and the console's scaler stretches
            // that to 720p, so the window does the same. What the window should
            // not add is the blockiness of a nearest neighbour stretch at a
            // fractional factor, which is what this looked like: 1.23 pixels
            // wide means every fourth column is doubled and the rest are not.
            // Halftone interpolates instead, which is what the console's scaler
            // does and costs nothing here.
            DumpFrame();

            // The console sends a 16 by 9 picture however big its render
            // target was, so the window keeps that shape and puts black either
            // side rather than stretching the image to whatever the window
            // happens to be.
            int drawWidth = width;
            int drawHeight = (width * 9 + 8) / 16;
            if (drawHeight > height)
            {
                drawHeight = height;
                drawWidth = (height * 16 + 4) / 9;
            }
            const int drawX = (width - drawWidth) / 2;
            const int drawY = (height - drawHeight) / 2;

            // The next frame is drawn at the multiple this window's height
            // asks for.
            Edram::RequestScale(ScaleForHeight(height));

            // Drawn into a bitmap of its own and put on screen in one move, so
            // nothing can ever be caught half updated.
            HDC memory = CreateCompatibleDC(dc);
            HBITMAP surface = CreateCompatibleBitmap(dc, width, height);
            HGDIOBJ previous = SelectObject(memory, surface);

            if (drawX > 0 || drawY > 0)
            {
                RECT full{ 0, 0, width, height };
                FillRect(memory, &full,
                         static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            }

            if (drawWidth == int(sourceWidth) && drawHeight == int(sourceHeight))
            {
                // One to one: a plain copy, no filtering to soften anything.
                SetDIBitsToDevice(memory, drawX, drawY, sourceWidth, sourceHeight,
                    0, 0, 0, sourceHeight, g_pixels.data(), &info, DIB_RGB_COLORS);
            }
            else
            {
                SetStretchBltMode(memory, HALFTONE);
                SetBrushOrgEx(memory, 0, 0, nullptr);
                StretchDIBits(memory, drawX, drawY, drawWidth, drawHeight,
                    0, 0, int(sourceWidth), int(sourceHeight),
                    g_pixels.data(), &info, DIB_RGB_COLORS, SRCCOPY);
            }

            DrawRate(memory);

            BitBlt(dc, 0, 0, width, height, memory, 0, 0, SRCCOPY);

            SelectObject(memory, previous);
            DeleteObject(surface);
            DeleteDC(memory);
            g_presented.fetch_add(1, std::memory_order_relaxed);
        }
        else
        {
            DrawStatus(dc, width, height);
        }

        ReleaseDC(window, dc);
    }

    void WindowThread()
    {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = WindowProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));

        // Resource 1 is the icon compiled into the executable, so the title bar
        // and the taskbar show the same thing Explorer does.
        windowClass.hIcon = LoadIconW(windowClass.hInstance, MAKEINTRESOURCEW(1));
        windowClass.hIconSm = windowClass.hIcon;
        windowClass.lpszClassName = L"CoD3Recomp";
        RegisterClassExW(&windowClass);

        RECT desired{ 0, 0, Width, Height };
        AdjustWindowRect(&desired, WS_OVERLAPPEDWINDOW, FALSE);

        HWND window = CreateWindowExW(0, L"CoD3Recomp",
            L"Call of Duty 3 - recompiled", WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT,
            desired.right - desired.left, desired.bottom - desired.top,
            nullptr, nullptr, windowClass.hInstance, nullptr);

        if (window == nullptr)
        {
            fprintf(stderr, "window: could not be created, error %lu\n", GetLastError());
            g_running.store(false);
            return;
        }

        g_window.store(window);
        ShowWindow(window, SW_SHOW);
        // In front and taking the keys from the start, rather than behind
        // the console it was started from.
        SetForegroundWindow(window);
        SetFocus(window);
        printf("window: open at %dx%d\n", Width, Height);
        fflush(stdout);

        MSG message{};
        while (g_running.load(std::memory_order_acquire))
        {
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            {
                if (message.message == WM_QUIT) { g_running.store(false); break; }
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            if (!g_running.load()) break;

            Present(window);
            std::this_thread::sleep_for(std::chrono::milliseconds(33));
        }

        g_window.store(nullptr);
        DestroyWindow(window);
    }
}

void Window::Open()
{
    bool expected = false;
    if (!g_running.compare_exchange_strong(expected, true)) return;
    g_thread = std::thread(WindowThread);
}

void Window::Close()
{
    if (g_running.exchange(false) && g_thread.joinable())
        g_thread.join();
}

bool Window::IsOpen() { return g_running.load(); }

void* Window::Handle() { return g_window.load(); }

void Window::SetCursorHidden(bool hidden)
{
    g_cursorHidden.store(hidden);

    // The pointer is already sitting over the window, so nothing would ask
    // again until it moved. Sending the question once makes the change show.
    const HWND window = g_window.load();
    if (window != nullptr)
        PostMessageW(window, WM_SETCURSOR, WPARAM(window), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
}

bool Window::HasFocus()
{
    const HWND window = g_window.load();
    return window != nullptr && GetForegroundWindow() == window;
}

void Window::SetFrontBuffer(uint32_t address, uint32_t width, uint32_t height)
{
    const uint32_t previous = g_frontBuffer.exchange(address);
    if (previous != address)
        g_guestFrames.fetch_add(1, std::memory_order_relaxed);
    g_frontBufferWidth.store(width);
    g_frontBufferHeight.store(height);

    // The first few: the title flips between two buffers every frame, and
    // one line a frame is a log of nothing else.
    static std::atomic<int> announced{ 0 };
    if (previous != address && announced.fetch_add(1) < 6)
    {
        printf("window: front buffer at 0x%08X, %ux%u\n", address, width, height);
        fflush(stdout);
    }
}

void Window::SetFrontBufferLayout(uint32_t pitch, bool tiled)
{
    g_frontBufferPitch.store(pitch);
    g_frontBufferTiled.store(tiled);
}
