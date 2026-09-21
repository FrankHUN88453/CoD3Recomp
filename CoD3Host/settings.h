// The player's settings: what the F11 menu shows and changes, kept in
// CoD3Recomp.ini beside the executable. Read from any thread through a copy;
// written by the menu.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace Settings
{
    // The keys the menu binds, each standing in for one of the pad's
    // controls. The order is the order the menu lists them in.
    enum Action
    {
        Fire, Aim, Jump, Crouch, Use, Reload, Weapon, Melee, Sprint, Grenade, Smoke,
        Objectives, Pause, Forward, Backward, Left, Right,
        ActionCount
    };

    const char* ActionName(Action action);   // "Fire", "Aim", ...

    // A key, as a Windows virtual key code, or 0 for none.
    const char* KeyName(int virtualKey);

    struct Values
    {
        int renderer = 0;             // 0 Direct3D 11; 1 and 2 are names only
        int renderScale = 0;          // 0 the window's height, 1..4 a fixed multiple of 1040x624
        int textureFilter = 3;        // 0 the title's own, 1 bilinear, 2 trilinear, 3 anisotropic
        int anisotropy = 16;          // 2..16, with textureFilter 3
        bool vsync = true;
        int antialiasing = 1;         // 0 none, 1 FXAA
        int windowMode = 0;           // 0 windowed, 1 borderless full screen
        bool fpsOverlay = false;
        bool statsOverlay = false;    // the renderer's counters over the picture
        bool aimAssist = true;        // the title's own, through its config
        bool controller = true;       // read a pad when one is there
        float mouseSensitivity = 1.0f;   // both axes, 0.1 .. 3
        int keys[ActionCount] = {};
    };

    Values Defaults();

    // The current values, and setting them (which also saves).
    Values Get();
    void Set(const Values& values);

    // Reads the file beside the executable, or starts from the defaults.
    void Load(const std::filesystem::path& exeDirectory);
    void Save();

    // The console commands the settings ask of the title, for the config
    // the file layer appends to default.cfg: the aim assist switches.
    std::string TitleConfigLines();
}
