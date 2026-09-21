#include "settings.h"

#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

#include <Windows.h>

namespace
{
    std::mutex g_mutex;
    Settings::Values g_values;
    std::filesystem::path g_file;

    const char* const ActionNames[Settings::ActionCount] = {
        "Fire", "Aim", "Jump", "Crouch", "Use", "Reload", "Weapon", "Melee", "Sprint",
        "Grenade", "Smoke", "Objectives", "Pause", "Forward", "Backward", "Left", "Right",
    };
    const char* const ActionKeys[Settings::ActionCount] = {
        "fire", "aim", "jump", "crouch", "use", "reload", "weapon", "melee", "sprint",
        "grenade", "smoke", "objectives", "pause", "forward", "backward", "left", "right",
    };

    std::string Trim(const std::string& text)
    {
        size_t a = 0, b = text.size();
        while (a < b && isspace((unsigned char)text[a])) a++;
        while (b > a && isspace((unsigned char)text[b - 1])) b--;
        return text.substr(a, b - a);
    }
}

const char* Settings::ActionName(Action action)
{
    return action >= 0 && action < ActionCount ? ActionNames[action] : "?";
}

const char* Settings::KeyName(int key)
{
    static char name[64];
    switch (key)
    {
    case 0: return "-";
    case VK_LBUTTON: return "Mouse 1";
    case VK_RBUTTON: return "Mouse 2";
    case VK_MBUTTON: return "Mouse 3";
    case VK_XBUTTON1: return "Mouse 4";
    case VK_XBUTTON2: return "Mouse 5";
    case VK_SPACE: return "Space";
    case VK_RETURN: return "Enter";
    case VK_ESCAPE: return "Escape";
    case VK_TAB: return "Tab";
    case VK_SHIFT: return "Shift";
    case VK_LSHIFT: return "Left Shift";
    case VK_RSHIFT: return "Right Shift";
    case VK_CONTROL: return "Ctrl";
    case VK_LCONTROL: return "Left Ctrl";
    case VK_RCONTROL: return "Right Ctrl";
    case VK_MENU: return "Alt";
    case VK_UP: return "Up";
    case VK_DOWN: return "Down";
    case VK_LEFT: return "Left";
    case VK_RIGHT: return "Right";
    case VK_BACK: return "Backspace";
    case VK_CAPITAL: return "Caps Lock";
    default: break;
    }
    if ((key >= '0' && key <= '9') || (key >= 'A' && key <= 'Z')) { name[0] = char(key); name[1] = 0; return name; }
    if (key >= VK_F1 && key <= VK_F12) { snprintf(name, sizeof(name), "F%d", key - VK_F1 + 1); return name; }
    if (key >= VK_NUMPAD0 && key <= VK_NUMPAD9) { snprintf(name, sizeof(name), "Num %d", key - VK_NUMPAD0); return name; }
    // Anything else by what the keyboard layout calls it.
    const UINT scan = MapVirtualKeyW(UINT(key), MAPVK_VK_TO_VSC);
    wchar_t wide[64] = {};
    if (scan != 0 && GetKeyNameTextW(LONG(scan << 16), wide, 63) > 0)
    {
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, name, sizeof(name), nullptr, nullptr);
        return name;
    }
    snprintf(name, sizeof(name), "Key %d", key);
    return name;
}

Settings::Values Settings::Defaults()
{
    Values v;
    v.keys[Fire] = VK_LBUTTON;
    v.keys[Aim] = VK_RBUTTON;
    v.keys[Jump] = VK_SPACE;
    v.keys[Crouch] = 'C';
    v.keys[Use] = 'E';
    v.keys[Reload] = 'R';
    v.keys[Weapon] = 'F';
    v.keys[Melee] = 'V';
    v.keys[Sprint] = VK_SHIFT;
    v.keys[Grenade] = 'G';
    v.keys[Smoke] = '4';
    v.keys[Objectives] = VK_TAB;
    v.keys[Pause] = VK_ESCAPE;
    v.keys[Forward] = 'W';
    v.keys[Backward] = 'S';
    v.keys[Left] = 'A';
    v.keys[Right] = 'D';
    return v;
}

Settings::Values Settings::Get()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_values;
}

void Settings::Set(const Values& values)
{
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_values = values;
    }
    Save();
}

void Settings::Load(const std::filesystem::path& exeDirectory)
{
    g_file = exeDirectory / "CoD3Recomp.ini";
    Values v = Defaults();
    if (FILE* file = _wfopen(g_file.c_str(), L"rb"))
    {
        char line[512];
        while (fgets(line, sizeof(line), file))
        {
            const std::string text = Trim(line);
            if (text.empty() || text[0] == '#' || text[0] == ';' || text[0] == '[') continue;
            const size_t eq = text.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = Trim(text.substr(0, eq)), value = Trim(text.substr(eq + 1));
            if (key == "renderer") v.renderer = atoi(value.c_str());
            else if (key == "render_scale") v.renderScale = atoi(value.c_str());
            else if (key == "texture_filter") v.textureFilter = atoi(value.c_str());
            else if (key == "anisotropy") v.anisotropy = atoi(value.c_str());
            else if (key == "vsync") v.vsync = atoi(value.c_str()) != 0;
            else if (key == "antialiasing") v.antialiasing = atoi(value.c_str());
            else if (key == "window_mode") v.windowMode = atoi(value.c_str());
            else if (key == "fps_overlay") v.fpsOverlay = atoi(value.c_str()) != 0;
            else if (key == "stats_overlay") v.statsOverlay = atoi(value.c_str()) != 0;
            else if (key == "aim_assist") v.aimAssist = atoi(value.c_str()) != 0;
            else if (key == "controller") v.controller = atoi(value.c_str()) != 0;
            else if (key == "mouse_sensitivity") v.mouseSensitivity = float(atof(value.c_str()));
            else if (key.compare(0, 4, "key_") == 0)
            {
                for (int i = 0; i < ActionCount; i++)
                    if (key.compare(4, std::string::npos, ActionKeys[i]) == 0) v.keys[i] = atoi(value.c_str());
            }
        }
        fclose(file);
    }
    if (v.renderer < 0 || v.renderer > 2) v.renderer = 0;
    if (v.renderScale < 0 || v.renderScale > 4) v.renderScale = 0;
    if (v.textureFilter < 0 || v.textureFilter > 3) v.textureFilter = 3;
    if (v.anisotropy < 2 || v.anisotropy > 16) v.anisotropy = 16;
    if (v.antialiasing < 0 || v.antialiasing > 1) v.antialiasing = 1;
    if (v.windowMode < 0 || v.windowMode > 1) v.windowMode = 0;
    if (!(v.mouseSensitivity >= 0.1f && v.mouseSensitivity <= 3.0f)) v.mouseSensitivity = 1.0f;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_values = v;
}

void Settings::Save()
{
    Values v = Get();
    if (g_file.empty()) return;
    if (FILE* file = _wfopen(g_file.c_str(), L"wb"))
    {
        fprintf(file, "# Call of Duty 3 recompiled: the settings menu (F11) keeps its values here.\n");
        fprintf(file, "[graphics]\nrenderer = %d\nrender_scale = %d\ntexture_filter = %d\nanisotropy = %d\nvsync = %d\nantialiasing = %d\nwindow_mode = %d\nfps_overlay = %d\nstats_overlay = %d\n",
            v.renderer, v.renderScale, v.textureFilter, v.anisotropy, v.vsync ? 1 : 0, v.antialiasing, v.windowMode, v.fpsOverlay ? 1 : 0, v.statsOverlay ? 1 : 0);
        fprintf(file, "[game]\naim_assist = %d\ncontroller = %d\nmouse_sensitivity = %.2f\n", v.aimAssist ? 1 : 0, v.controller ? 1 : 0, v.mouseSensitivity);
        fprintf(file, "[keys]\n");
        for (int i = 0; i < ActionCount; i++) fprintf(file, "key_%s = %d\n", ActionKeys[i], v.keys[i]);
        fclose(file);
    }
}

std::string Settings::TitleConfigLines()
{
    const Values v = Get();
    // The title's own aim assist is three switches of its console. Off is
    // said outright; on is the title's default, so nothing is said.
    if (v.aimAssist) return {};
    return "seta aim_slowdown_enabled \"0\"\nseta aim_lockon_enabled \"0\"\nseta aim_autoaim_enabled \"0\"\n";
}
