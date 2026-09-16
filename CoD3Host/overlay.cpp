#include "overlay.h"
#include "settings.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <vector>

#include <Windows.h>
#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>

#include "imgui.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

using Microsoft::WRL::ComPtr;

namespace
{
    std::atomic<bool> g_open{ false };
    std::atomic<bool> g_screenshotWanted{ false };
    std::atomic<bool> g_ready{ false };
    HWND g_hwnd = nullptr;
    ID3D11Device* g_device = nullptr;
    ID3D11DeviceContext* g_context = nullptr;
    int g_capturing = -1;   // the action whose key is being pressed next, or -1
    Settings::Values g_edit;   // the values the menu shows and changes
    bool g_editLoaded = false;
    float g_fontScale = 1.0f;

    // The frame counter: swaps in the last second.
    std::atomic<uint64_t> g_frames{ 0 };
    uint64_t g_framesAtTick = 0;
    double g_fps = 0.0;
    std::chrono::steady_clock::time_point g_tick;

    // A key the player pressed while a binding waited for one.
    void Capture(int key)
    {
        if (g_capturing < 0 || g_capturing >= Settings::ActionCount) return;
        if (key == VK_ESCAPE) { g_capturing = -1; return; }
        g_edit.keys[g_capturing] = key;
        g_capturing = -1;
        Settings::Set(g_edit);
    }

    void SaveScreenshot(ID3D11Texture2D* back, int width, int height)
    {
        D3D11_TEXTURE2D_DESC desc{};
        back->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        ComPtr<ID3D11Texture2D> staging;
        if (FAILED(g_device->CreateTexture2D(&desc, nullptr, &staging))) return;
        g_context->CopyResource(staging.Get(), back);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(g_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return;

        // Beside the executable, named by the moment.
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::filesystem::path folder = std::filesystem::path(exe).parent_path() / "screenshots";
        std::error_code ec;
        std::filesystem::create_directories(folder, ec);
        const std::time_t now = std::time(nullptr);
        std::tm local{};
        localtime_s(&local, &now);
        char name[64];
        strftime(name, sizeof(name), "CoD3-%Y%m%d-%H%M%S.png", &local);
        const std::filesystem::path file = folder / name;

        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICBitmapEncoder> encoder;
        ComPtr<IWICBitmapFrameEncode> frame;
        ComPtr<IWICStream> stream;
        bool ok = false;
        if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
            SUCCEEDED(factory->CreateStream(&stream)) &&
            SUCCEEDED(stream->InitializeFromFilename(file.c_str(), GENERIC_WRITE)) &&
            SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
            SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
            SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) &&
            SUCCEEDED(frame->Initialize(nullptr)) &&
            SUCCEEDED(frame->SetSize(UINT(width), UINT(height))))
        {
            WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
            if (SUCCEEDED(frame->SetPixelFormat(&format)))
            {
                // The back buffer is B8G8R8A8; its alpha is whatever the
                // present left, so it is made opaque on the way out.
                std::vector<uint8_t> rows(size_t(width) * height * 4);
                for (int y = 0; y < height; y++)
                {
                    const uint8_t* from = static_cast<const uint8_t*>(mapped.pData) + size_t(y) * mapped.RowPitch;
                    uint8_t* to = rows.data() + size_t(y) * width * 4;
                    memcpy(to, from, size_t(width) * 4);
                    for (int x = 0; x < width; x++) to[x * 4 + 3] = 255;
                }
                ok = SUCCEEDED(frame->WritePixels(UINT(height), UINT(width) * 4, UINT(rows.size()), rows.data())) &&
                     SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
            }
        }
        g_context->Unmap(staging.Get(), 0);
        printf("screenshot: %s%s\n", file.string().c_str(), ok ? "" : " could not be written");
        fflush(stdout);
    }

    void DrawMenu(int width, int height)
    {
        if (!g_editLoaded) { g_edit = Settings::Get(); g_editLoaded = true; }
        bool changed = false;
        ImGui::SetNextWindowSize(ImVec2(560 * g_fontScale, 0), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(width * 0.5f, height * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
        bool open = true;
        if (ImGui::Begin("Beállítások (F11)", &open, ImGuiWindowFlags_NoCollapse))
        {
            if (ImGui::BeginTabBar("tabs"))
            {
                if (ImGui::BeginTabItem("Grafika"))
                {
                    const char* renderers[] = { "DirectX 11", "DirectX 12 (még nincs)", "Vulkan (még nincs)" };
                    int renderer = g_edit.renderer;
                    if (ImGui::Combo("Megjelenítő", &renderer, renderers, 3))
                    {
                        // Only the first exists; the others stay names for now.
                        g_edit.renderer = 0;
                        changed = true;
                    }
                    if (renderer != 0) ImGui::TextDisabled("Egyelőre csak a DirectX 11 érhető el.");
                    const char* scales[] = { "Ablak szerint (automatikus)", "1x (1040x624)", "2x (2080x1248)", "3x (3120x1872)", "4x (4160x2496)" };
                    if (ImGui::Combo("Felbontás skálázó", &g_edit.renderScale, scales, 5)) changed = true;
                    ImGui::TextDisabled("A játék 1040x624-ben rajzol; ennek a többszöröse a kép.");
                    if (ImGui::Checkbox("FPS megjelenítése", &g_edit.fpsOverlay)) changed = true;
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Irányítás"))
                {
                    if (ImGui::SliderFloat("Egér érzékenység", &g_edit.mouseSensitivity, 0.1f, 3.0f, "%.2f")) changed = true;
                    ImGui::TextDisabled("Az X és az Y tengelyre egyformán.");
                    if (ImGui::Checkbox("Aim Assist", &g_edit.aimAssist)) changed = true;
                    ImGui::SameLine();
                    ImGui::TextDisabled("(a játék saját célsegítése; újraindítás után él)");
                    if (ImGui::Checkbox("Kontroller támogatás", &g_edit.controller)) changed = true;
                    ImGui::Separator();
                    ImGui::TextUnformatted("Billentyűk");
                    if (ImGui::BeginTable("keys", 2, ImGuiTableFlags_SizingStretchProp))
                    {
                        for (int i = 0; i < Settings::ActionCount; i++)
                        {
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            ImGui::TextUnformatted(Settings::ActionName(Settings::Action(i)));
                            ImGui::TableNextColumn();
                            ImGui::PushID(i);
                            const bool waiting = g_capturing == i;
                            if (ImGui::Button(waiting ? "Nyomj egy gombot... (Esc: mégse)" : Settings::KeyName(g_edit.keys[i]), ImVec2(-1, 0)))
                                g_capturing = waiting ? -1 : i;
                            ImGui::PopID();
                        }
                        ImGui::EndTable();
                    }
                    if (ImGui::Button("Alapértelmezett billentyűk"))
                    {
                        const Settings::Values defaults = Settings::Defaults();
                        for (int i = 0; i < Settings::ActionCount; i++) g_edit.keys[i] = defaults.keys[i];
                        changed = true;
                    }
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::Separator();
            ImGui::TextDisabled("F11: menü   F12: képernyőkép a screenshots mappába   Esc: vissza a játékba");
        }
        ImGui::End();
        if (changed) Settings::Set(g_edit);
        if (!open) { g_open.store(false); g_capturing = -1; }
    }
}

void Overlay::Initialize(void* hwnd, ID3D11Device* device, ID3D11DeviceContext* context)
{
    if (g_ready.load()) return;
    g_hwnd = static_cast<HWND>(hwnd);
    g_device = device;
    g_context = context;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    // A readable size on any screen: the font grows with the window.
    RECT client{};
    GetClientRect(g_hwnd, &client);
    g_fontScale = std::max(1.0f, float(client.bottom - client.top) / 720.0f);
    static const ImWchar ranges[] = { 0x0020, 0x00FF, 0x0100, 0x017F, 0 };   // Latin with the Hungarian letters
    ImFontConfig config;
    config.OversampleH = 2;
    if (io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 20.0f * g_fontScale, &config, ranges) == nullptr)
        io.Fonts->AddFontDefault();
    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(g_fontScale);
    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(device, context);
    g_tick = std::chrono::steady_clock::now();
    g_ready.store(true);
}

void Overlay::NoteFrame() { g_frames.fetch_add(1, std::memory_order_relaxed); }

bool Overlay::IsOpen() { return g_open.load(); }

void Overlay::Toggle()
{
    g_open.store(!g_open.load());
    g_capturing = -1;
    g_editLoaded = false;
}

void Overlay::RequestScreenshot() { g_screenshotWanted.store(true); }

bool Overlay::HandleMessage(void* hwnd, uint32_t message, uint64_t wParam, int64_t lParam)
{
    if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN)
    {
        if (wParam == VK_F11) { Toggle(); return true; }
        if (wParam == VK_F12) { RequestScreenshot(); return true; }
    }
    if (!g_ready.load() || !g_open.load()) return false;
    // A binding waiting for a key takes the next key or mouse button.
    if (g_capturing >= 0)
    {
        if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) { Capture(int(wParam)); return true; }
        if (message == WM_LBUTTONDOWN) { Capture(VK_LBUTTON); return true; }
        if (message == WM_RBUTTONDOWN) { Capture(VK_RBUTTON); return true; }
        if (message == WM_MBUTTONDOWN) { Capture(VK_MBUTTON); return true; }
        if (message == WM_XBUTTONDOWN) { Capture(GET_XBUTTON_WPARAM(wParam) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2); return true; }
    }
    else if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && wParam == VK_ESCAPE)
    {
        g_open.store(false);
        return true;
    }
    ImGui_ImplWin32_WndProcHandler(static_cast<HWND>(hwnd), message, WPARAM(wParam), LPARAM(lParam));
    // While the menu is up the keys and the mouse are its.
    return message == WM_KEYDOWN || message == WM_KEYUP || message == WM_SYSKEYDOWN || message == WM_SYSKEYUP ||
           message == WM_CHAR || message == WM_MOUSEWHEEL || message == WM_LBUTTONDOWN || message == WM_LBUTTONUP ||
           message == WM_RBUTTONDOWN || message == WM_RBUTTONUP || message == WM_MBUTTONDOWN || message == WM_MBUTTONUP;
}

void Overlay::Render(ID3D11RenderTargetView* back, ID3D11Texture2D* backTexture, int width, int height)
{
    if (!g_ready.load()) return;
    // The counter, once a second.
    {
        const auto now = std::chrono::steady_clock::now();
        const double seconds = std::chrono::duration<double>(now - g_tick).count();
        if (seconds >= 1.0)
        {
            const uint64_t frames = g_frames.load(std::memory_order_relaxed);
            g_fps = double(frames - g_framesAtTick) / seconds;
            g_framesAtTick = frames;
            g_tick = now;
        }
    }
    // The screenshot first, of the picture alone.
    if (g_screenshotWanted.exchange(false) && backTexture != nullptr) SaveScreenshot(backTexture, width, height);

    const Settings::Values values = Settings::Get();
    const bool open = g_open.load();
    if (!open && !values.fpsOverlay) return;

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    if (values.fpsOverlay)
    {
        ImGui::SetNextWindowPos(ImVec2(8, 8));
        ImGui::SetNextWindowBgAlpha(0.45f);
        if (ImGui::Begin("fps", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing))
            ImGui::Text("%.0f fps", g_fps);
        ImGui::End();
    }
    if (open) DrawMenu(width, height);
    ImGui::Render();
    ID3D11RenderTargetView* targets[1] = { back };
    g_context->OMSetRenderTargets(1, targets, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}
