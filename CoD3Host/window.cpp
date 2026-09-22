// The window the frames go to.
//
// The renderer presents from the command thread, at the title's swap; this
// thread makes the window, pumps its messages, tells the renderer the
// window's size, and asks for the last frame again when the title has gone
// quiet, so a resize or the settings menu never shows a stale picture.
// The window is either a plain one at 720p or a borderless one over the
// whole screen, from the settings or Alt+Enter.

#include "kernel.h"
#include "window.h"
#include "overlay.h"
#include "settings.h"
#include "render.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include <Windows.h>

namespace
{
    // The window opens at 720p, which is what the console put on screen,
    // and the picture fills it at 16 by 9 whatever size it is made.
    constexpr int Width = 1280;
    constexpr int Height = 720;

    std::atomic<bool> g_running{ false };
    std::atomic<HWND> g_window{ nullptr };
    std::thread g_thread;

    std::atomic<uint32_t> g_frontBuffer{ 0 };
    std::atomic<bool> g_cursorHidden{ false };
    std::atomic<int> g_wheel{ 0 };   // wheel notches since the last take

    // Frames the guest finished, counted where the front buffer changes.
    std::atomic<uint64_t> g_guestFrames{ 0 };

    bool g_borderless = false;
    WINDOWPLACEMENT g_placement{};
    std::atomic<bool> g_toggleWanted{ false };   // Alt+Enter

    void ReportSize(HWND window)
    {
        RECT client{};
        GetClientRect(window, &client);
        Render::SetWindow(window, client.right - client.left, client.bottom - client.top);
    }

    // The window over the whole of its monitor with no frame, or back to
    // the plain one where it was.
    void SetBorderless(HWND window, bool borderless)
    {
        if (borderless == g_borderless) return;
        g_borderless = borderless;
        if (borderless)
        {
            g_placement.length = sizeof(g_placement);
            GetWindowPlacement(window, &g_placement);
            MONITORINFO monitor{ sizeof(monitor) };
            GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor);
            SetWindowLongPtrW(window, GWL_STYLE, WS_POPUP | WS_VISIBLE);
            SetWindowPos(window, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top,
                monitor.rcMonitor.right - monitor.rcMonitor.left, monitor.rcMonitor.bottom - monitor.rcMonitor.top,
                SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
        }
        else
        {
            SetWindowLongPtrW(window, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
            SetWindowPlacement(window, &g_placement);
            SetWindowPos(window, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER);
        }
        ReportSize(window);
    }

    LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w, LPARAM l)
    {
        // Alt+Enter: the other window mode, through the settings so it sticks.
        if (message == WM_SYSKEYDOWN && w == VK_RETURN && (l & (1 << 29)) != 0)
        {
            g_toggleWanted.store(true);
            return 0;
        }
        // The settings menu sees every message first: F11 and F12, and all
        // of the keys and the mouse while it is open.
        if (Overlay::HandleMessage(window, message, uint64_t(w), int64_t(l))) return 0;
        // Windows asks the window what the pointer should look like whenever it
        // moves over it, and answering with nothing is how a pointer is hidden
        // over one window without touching it anywhere else.
        if (message == WM_SETCURSOR && LOWORD(l) == HTCLIENT && g_cursorHidden.load())
        {
            SetCursor(nullptr);
            return TRUE;
        }
        if (message == WM_SIZE)
        {
            if (w != SIZE_MINIMIZED) ReportSize(window);
            return 0;
        }
        if (message == WM_CLOSE || message == WM_DESTROY)
        {
            g_running.store(false);
            PostQuitMessage(0);
            return 0;
        }
        if (message == WM_MOUSEWHEEL)
        {
            g_wheel.fetch_add(GET_WHEEL_DELTA_WPARAM(w) / WHEEL_DELTA);
            return 0;
        }
        // No beep for Alt combinations the menu does not have.
        if (message == WM_SYSCHAR) return 0;
        return DefWindowProcW(window, message, w, l);
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
        ReportSize(window);
        // In front and taking the keys from the start, rather than behind
        // the console it was started from. COD3_BACKGROUND=1 leaves it
        // behind, and the keyboard and mouse alone: a run driven by a
        // script while someone is using the machine.
        static const bool background = []() {
            const char* text = getenv("COD3_BACKGROUND");
            return text != nullptr && text[0] != 0 && text[0] != '0';
        }();
        if (!background)
        {
            SetForegroundWindow(window);
            SetFocus(window);
        }
        // COD3_FULLSCREEN=1 over the settings, for a run.
        static const int fullscreenOverride = []() { const char* t = getenv("COD3_FULLSCREEN"); return t ? (t[0] == '0' ? 0 : 1) : -1; }();
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

            // The window mode the settings ask for, or Alt+Enter did.
            Settings::Values settings = Settings::Get();
            if (g_toggleWanted.exchange(false))
            {
                settings.windowMode = settings.windowMode == 0 ? 1 : 0;
                Settings::Set(settings);
            }
            const bool borderless = fullscreenOverride >= 0 ? fullscreenOverride != 0 : settings.windowMode == 1;
            SetBorderless(window, borderless);
            // The window the size of the chosen resolution, when that
            // changes, and no larger than the screen it is on.
            {
                static int appliedWidth = 0, appliedHeight = 0;
                int width, height;
                Settings::Resolution(settings, width, height);
                if (!borderless && (width != appliedWidth || height != appliedHeight))
                {
                    appliedWidth = width;
                    appliedHeight = height;
                    MONITORINFO monitor{ sizeof(monitor) };
                    GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor);
                    RECT frame{ 0, 0, width, height };
                    AdjustWindowRect(&frame, WS_OVERLAPPEDWINDOW, FALSE);
                    int frameWidth = frame.right - frame.left, frameHeight = frame.bottom - frame.top;
                    const int workWidth = monitor.rcWork.right - monitor.rcWork.left, workHeight = monitor.rcWork.bottom - monitor.rcWork.top;
                    if (frameWidth > workWidth) frameWidth = workWidth;
                    if (frameHeight > workHeight) frameHeight = workHeight;
                    const int x = monitor.rcWork.left + (workWidth - frameWidth) / 2, y = monitor.rcWork.top + (workHeight - frameHeight) / 2;
                    SetWindowPos(window, nullptr, x, y, frameWidth, frameHeight, SWP_NOZORDER | SWP_NOOWNERZORDER);
                    ReportSize(window);
                }
            }

            Render::PresentIdle();
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
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

int Window::TakeWheel() { return g_wheel.exchange(0); }

void Window::SetFrontBuffer(uint32_t address, uint32_t width, uint32_t height)
{
    const uint32_t previous = g_frontBuffer.exchange(address);
    if (previous != address)
        g_guestFrames.fetch_add(1, std::memory_order_relaxed);

    // The first few: the title flips between two buffers every frame, and
    // one line a frame is a log of nothing else.
    static std::atomic<int> announced{ 0 };
    if (previous != address && announced.fetch_add(1) < 6)
    {
        printf("window: front buffer at 0x%08X, %ux%u\n", address, width, height);
        fflush(stdout);
    }
}
