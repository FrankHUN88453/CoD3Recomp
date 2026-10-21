// The VIDEO page of the title's own OPTIONS (a PC addition).
//
// The options menu (ProfileEditMenu, vtable 0x820729A4: GAMEPLAY, CONTROLS,
// SOUND, SCREEN) is given a fifth entry, VIDEO: its entries' array made one
// longer and the entry made by the menu's own SetTextEntry (vtable +20),
// its text copied from SCREEN's (the text's CopyFrom, +72) and drawn right
// after it, as it is no child of the menu's layout. Its big name (+72) and
// line of help (+80) are the menu's for the entry chosen, from tables of
// four, so for the fifth they are set here.
//
// Taken, it opens the CONTROLS page (the front end's menu 4,
// OptionsControlsMenu, vtable 0x82072094) as the VIDEO page: the same
// frame, eight rows of a label, a value and arrows, a big name and a line
// of help, but its words are the video settings' (the settings menu's,
// settings.h) for as long as it is up, and the pad, the keys and the mouse
// are this page's, not the title's. Its rows 2 and 3 are sliders there:
// their bars are not drawn and a value is (two copies of row 0's, the
// text's Clone, +288); its rows 0 to 3 have no arrows, so row 4's are drawn
// at them too. More settings than rows scroll. Leaving (B, Escape) puts its
// own words back and passes the B on, which takes the title back to the
// options.

#include "video_menu.h"
#include "input.h"
#include "kernel.h"
#include "settings.h"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace Mmio { void CallIndirect(PPCContext& ctx, uint8_t* base, uint32_t address); }

namespace
{
    constexpr uint32_t OptionsMenuVtable = 0x820729A4, ControlsMenuVtable = 0x82072094;
    constexpr uint32_t SystemMenus = 4, SystemActive = 12, SystemCount = 24, SystemPush = 16;
    constexpr uint32_t MenuSystem = 8, MenuEntries = 4, MenuSelected = 34, MenuCount = 38, MenuSetSelection = 164;
    constexpr uint32_t MenuSetTextEntry = 20;           // vtable +20: (index, text) an entry of its own text
    constexpr uint32_t EntryWidget = 16;
    constexpr uint32_t TitleNew = 0x820CC1D0;           // operator new
    constexpr uint32_t TextVtable = 0x82070B54, MultiLineVtable = 0x82070C84, ImageVtable = 0x82070E04;
    constexpr uint32_t TextCopyFrom = 72, TextSetText = 132, TextClone = 288;
    constexpr uint32_t TextX = 44, TextY = 48, TextString = 28;
    constexpr uint32_t ImageQuads = 32, ImageQuadCount = 40;
    constexpr uint32_t ImageFlags = 16, ImageShown = 0x04, ImageSetShown = 60;   // vtable +60: (shown)
    // A menu's panel (its layout, out of a .panel file: the options menu's
    // is SP_OP_editprofile.panel): its pictures' list and their number.
    constexpr uint32_t MenuPanel = 60, PanelImages = 0, PanelImageCount = 4;

    // The options menu's widgets: its big name and its line of help.
    constexpr uint32_t OptionsBigName = 72, OptionsHelp = 80;
    // The controls page's: its title, big name and help, the values of its
    // rows 0, 1 and 4 to 7, the arrows of rows 4 to 7 (left, right), and
    // the bars of its sliders (rows 2 and 3).
    constexpr uint32_t ControlsTitle = 64, ControlsBigName = 72, ControlsHelp = 76;
    constexpr uint32_t ControlsValues[6] = { 84, 88, 100, 104, 108, 112 };
    constexpr int ControlsValueRows[6] = { 0, 1, 4, 5, 6, 7 };
    constexpr uint32_t ControlsLeftArrow = 148, ControlsRightArrow = 152, ControlsBars[2] = { 180, 184 };
    constexpr int Rows = 8;
    constexpr float RowStep = 20.0f;

    float ReadFloat(uint8_t* base, uint32_t address)
    {
        const uint32_t v = Guest::Read32(base, address);
        float f;
        memcpy(&f, &v, 4);
        return f;
    }

    void WriteFloatAt(uint8_t* base, uint32_t address, float f)
    {
        uint32_t v;
        memcpy(&v, &f, 4);
        Guest::Write32(base, address, v);
    }

    uint32_t Call(PPCContext& ctx, uint8_t* base, uint32_t function, uint32_t r3, uint32_t r4 = 0, uint32_t r5 = 0)
    {
        PPCContext call = ctx;
        call.r3.u64 = r3;
        call.r4.u64 = r4;
        call.r5.u64 = r5;
        Mmio::CallIndirect(call, base, function);
        return call.r3.u32;
    }

    uint32_t Virtual(uint8_t* base, uint32_t object, uint32_t slot) { return Guest::Read32(base, Guest::Read32(base, object) + slot); }

    // A text's string (the string object at +28: the characters' address,
    // then the capacity and the length as halfwords).
    std::string TextOf(uint8_t* base, uint32_t widget)
    {
        const uint32_t text = Guest::Read32(base, widget + TextString);
        if (text < 0x40000000u || text >= 0xC0000000u) return {};
        const uint32_t chars = Guest::Read32(base, text);
        const uint32_t length = std::min<uint32_t>(Guest::Read16(base, text + 6), 256);
        if (chars < 0x40000000u || chars >= 0xC0000000u) return {};
        std::string s(length, '\0');
        for (uint32_t i = 0; i < length; i++) s[i] = char(Guest::Read8(base, chars + i));
        return s;
    }

    // A string in guest memory for the title to copy (its texts copy what
    // they are given into string objects of their own): one buffer, made
    // once, written again for each.
    uint32_t Scratch(const std::string& text)
    {
        static const uint32_t buffer = Guest::AllocateKernelObject(512);
        if (buffer == 0) return 0;
        const size_t n = std::min<size_t>(text.size(), 511);
        memcpy(Guest::Base + buffer, text.data(), n);
        Guest::Base[buffer + n] = 0;
        return buffer;
    }

    // Sets a text's words, when they differ: an FEText's SetText (+132),
    // or a multi line one's.
    void SetText(PPCContext& ctx, uint8_t* base, uint32_t widget, const std::string& text)
    {
        if (widget == 0 || TextOf(base, widget) == text) return;
        const uint32_t at = Scratch(text);
        if (at != 0) Call(ctx, base, Virtual(base, widget, TextSetText), widget, at);
    }

    // --- the VIDEO entry of the options menu --------------------------------------------------

    // The options menus given the entry (the front end's, and the pause
    // menu's in a level): each with the VIDEO entry's text and SCREEN's,
    // the entry above it.
    struct OptionsEntry { uint32_t menu = 0, widget = 0, above = 0; };
    OptionsEntry g_entries[4];

    // An entry and the one above it still what they were: the
    // menus are made again when a level is left or begun, and freed memory
    // is used again.
    bool EntryStillThere(uint8_t* base, const OptionsEntry& e)
    {
        if (e.menu == 0 || Guest::Read32(base, e.menu) != OptionsMenuVtable) return false;
        if (int16_t(Guest::Read16(base, e.menu + MenuCount)) != 5) return false;
        const uint32_t entries = Guest::Read32(base, e.menu + MenuEntries);
        if (entries == 0) return false;
        const uint32_t fifth = Guest::Read32(base, entries + 16), fourth = Guest::Read32(base, entries + 12);
        return fifth != 0 && fourth != 0 && Guest::Read32(base, fifth + EntryWidget) == e.widget &&
               Guest::Read32(base, fourth + EntryWidget) == e.above && Guest::Read32(base, e.widget) == TextVtable;
    }

    const OptionsEntry* EntryOf(uint8_t* base, uint32_t menu)
    {
        for (const OptionsEntry& e : g_entries)
            if (e.menu == menu && menu != 0 && EntryStillThere(base, e)) return &e;
        return nullptr;
    }

    // The options menu given a fifth entry, once: its entries' array made
    // one longer (the title's own allocation, which its destructor frees)
    // and the entry made by the menu's own SetTextEntry.
    void AddEntry(PPCContext& ctx, uint8_t* base, uint32_t menu)
    {
        const int count = int16_t(Guest::Read16(base, menu + MenuCount));
        const uint32_t entries = Guest::Read32(base, menu + MenuEntries);
        if (count != 4 || entries == 0 || Guest::Read32(base, entries + 12) == 0) return;
        const uint32_t grown = Call(ctx, base, TitleNew, 5 * 4);
        if (grown == 0) return;
        for (uint32_t i = 0; i < 4; i++) Guest::Write32(base, grown + i * 4, Guest::Read32(base, entries + i * 4));
        Guest::Write32(base, grown + 16, 0);
        Guest::Write32(base, menu + MenuEntries, grown);
        Guest::Write16(base, menu + MenuCount, 5);
        Call(ctx, base, Virtual(base, menu, MenuSetTextEntry), menu, 4, Scratch("VIDEO"));
        const uint32_t entry = Guest::Read32(base, grown + 16);
        const uint32_t widget = entry != 0 ? Guest::Read32(base, entry + EntryWidget) : 0;
        const uint32_t above = Guest::Read32(base, Guest::Read32(base, entries + 12) + EntryWidget);
        printf("video: the options menu %08X has a fifth entry %08X, VIDEO\n", menu, entry);
        fflush(stdout);
        if (widget == 0 || above == 0) return;
        // Its text in the style of the entry above (SCREEN), a row below it:
        // the text's own CopyFrom (which copies what it owns, the +20 block
        // and the strings, as its destructor frees them), then its words and
        // its place.
        Call(ctx, base, Virtual(base, widget, TextCopyFrom), widget, above);
        Call(ctx, base, Virtual(base, widget, TextSetText), widget, Scratch("VIDEO"));
        WriteFloatAt(base, widget + TextY, ReadFloat(base, above + TextY) + 20.0f);
        // A place for it: a free one, or one whose menu is gone.
        OptionsEntry* slot = &g_entries[0];
        for (OptionsEntry& e : g_entries)
            if (e.menu == 0 || !EntryStillThere(base, e)) { slot = &e; break; }
        *slot = { menu, widget, above };
    }

    // The options menu's panel has a fifth row too, its dark stripe and the
    // line over it, but hidden (the menu has four entries): shown when the
    // menu has the VIDEO entry. The stripe is the full-width one right
    // under SCREEN's (the one SCREEN's text stands in), the line the thin
    // one at its top.
    void ShowFifthRow(PPCContext& ctx, uint8_t* base, const OptionsEntry& e)
    {
        const uint32_t panel = Guest::Read32(base, e.menu + MenuPanel);
        if (panel < 0x40000000u || panel >= 0xA0000000u) return;
        const uint32_t list = Guest::Read32(base, panel + PanelImages);
        const uint32_t count = Guest::Read32(base, panel + PanelImageCount);
        if (list < 0x40000000u || list >= 0xA0000000u || count == 0 || count > 64) return;
        struct Picture { uint32_t image; float y0, y1; bool shown; };
        std::vector<Picture> pictures;
        for (uint32_t i = 0; i < count; i++)
        {
            const uint32_t image = Guest::Read32(base, list + i * 4);
            if (image == 0 || Guest::Read32(base, image) != ImageVtable) continue;
            const uint32_t quads = Guest::Read32(base, image + ImageQuads);
            if (quads == 0 || Guest::Read32(base, image + ImageQuadCount) == 0) continue;
            const uint32_t quad = Guest::Read32(base, quads);
            float x0 = ReadFloat(base, quad + 16), y0 = ReadFloat(base, quad + 20), x1 = ReadFloat(base, quad + 76), y1 = ReadFloat(base, quad + 80);
            if (x0 > x1) std::swap(x0, x1);
            if (y0 > y1) std::swap(y0, y1);
            // A stripe spans the screen; a line is drawn in two halves, the
            // first from the left edge.
            if (x0 > 0.0f || x1 < 300.0f) continue;
            pictures.push_back({ image, y0, y1, (Guest::Read8(base, image + ImageFlags) & ImageShown) != 0 });
        }
        const float y = ReadFloat(base, e.above + TextY);
        float top = -1.0f;
        for (const Picture& p : pictures)
            if (p.shown && p.y1 - p.y0 >= 10.0f && y >= p.y0 && y < p.y1) top = p.y1;
        if (top < 0.0f) return;
        for (const Picture& p : pictures)
        {
            const bool stripe = p.y1 - p.y0 >= 10.0f && std::fabs(p.y0 - top) <= 1.0f;
            const bool line = p.y1 - p.y0 < 5.0f && std::fabs(p.y0 + 1.0f - top) <= 1.5f;
            if (p.shown || (!stripe && !line)) continue;
            Call(ctx, base, Virtual(base, p.image, ImageSetShown), p.image, 1);
            printf("video: the options menu's fifth row shown: %s %08X at %g..%g\n", stripe ? "stripe" : "line", p.image, p.y0, p.y1);
            fflush(stdout);
        }
    }

    // --- the settings on the page --------------------------------------------------------------

    struct Row
    {
        const char* label;
        const char* help;
        std::string (*value)(const Settings::Values&);
        void (*step)(Settings::Values&, int direction);
    };

    std::string Upper(std::string s)
    {
        for (char& c : s) c = char(toupper(static_cast<unsigned char>(c)));
        return s;
    }

    // A value of a list: wrapping when cycled.
    int Cycle(int value, int count, int direction) { return ((value + direction) % count + count) % count; }

    const std::vector<Settings::Mode>& Modes() { return Settings::DisplayModes(); }

    std::string ResolutionText(const Settings::Values& v)
    {
        int w, h;
        Settings::Resolution(v, w, h);
        char text[48];
        snprintf(text, sizeof(text), v.resolutionWidth > 0 ? "%d X %d" : "%d X %d (DESKTOP)", w, h);
        return text;
    }
    void ResolutionStep(Settings::Values& v, int direction)
    {
        // The desktop's first, then the display's sizes, smallest first.
        const auto& modes = Modes();
        const int count = int(modes.size()) + 1;
        int at = 0;
        for (size_t i = 0; i < modes.size(); i++)
            if (modes[i].width == v.resolutionWidth && modes[i].height == v.resolutionHeight) at = int(i) + 1;
        at = Cycle(at, count, direction);
        v.resolutionWidth = at == 0 ? 0 : modes[size_t(at - 1)].width;
        v.resolutionHeight = at == 0 ? 0 : modes[size_t(at - 1)].height;
    }

    constexpr int Scales[] = { 50, 75, 100, 125, 150, 175, 200 };
    std::string ScaleText(const Settings::Values& v) { return std::to_string(v.resolutionScale) + "%"; }
    void ScaleStep(Settings::Values& v, int direction)
    {
        // The next of the steps either way; from between two (the settings
        // menu's slider), the nearer one that way.
        int at = -1;
        for (int i = 0; i < 7; i++) if (Scales[i] <= v.resolutionScale) at = i;
        const bool exact = at >= 0 && Scales[at] == v.resolutionScale;
        if (direction > 0) at = std::min(at + 1, 6);
        else at = exact ? std::max(at - 1, 0) : std::max(at, 0);
        v.resolutionScale = Scales[at];
    }

    const Row g_rows[] = {
        { "RESOLUTION", "The size of the window, or of the full screen picture.",
          ResolutionText, ResolutionStep },
        { "WINDOW MODE", "A window, or borderless full screen. Alt+Enter too.",
          [](const Settings::Values& v) { return std::string(v.windowMode == 1 ? "FULL SCREEN" : "WINDOWED"); },
          [](Settings::Values& v, int d) { v.windowMode = Cycle(v.windowMode, 2, d); } },
        { "ASPECT RATIO", "Wider than 16:9 shows more; the HUD stays centred.",
          [](const Settings::Values& v) {
              static const char* const names[] = { "16:9 (CONSOLE)", "FIT THE WINDOW", "21:9", "32:9" };
              return std::string(names[std::clamp(v.aspectRatio, 0, 3)]); },
          [](Settings::Values& v, int d) { v.aspectRatio = Cycle(v.aspectRatio, 4, d); } },
        { "RESOLUTION SCALE", "The frame is drawn at this much of the resolution.",
          ScaleText, ScaleStep },
        { "FIELD OF VIEW", "How wide the view is. 65 is the game's own.",
          [](const Settings::Values& v) { return std::to_string(v.fov); },
          [](Settings::Values& v, int d) { v.fov = std::clamp((v.fov / 5) * 5 + d * 5, 65, 100); } },
        { "ANTI-ALIASING", "Smooths edges: MSAA samples them, FXAA blurs after.",
          [](const Settings::Values& v) {
              static const char* const names[] = { "OFF", "FXAA", "MSAA 2X", "MSAA 4X", "MSAA 8X", "MSAA 4X + FXAA" };
              return std::string(names[std::clamp(v.antialiasing, 0, 5)]); },
          [](Settings::Values& v, int d) { v.antialiasing = Cycle(v.antialiasing, 6, d); } },
        { "TEXTURE FILTERING", "Anisotropic keeps textures sharp at an angle.",
          [](const Settings::Values& v) {
              static const char* const names[] = { "CONSOLE", "BILINEAR", "TRILINEAR", "ANISOTROPIC" };
              return std::string(names[std::clamp(v.textureFilter, 0, 3)]); },
          [](Settings::Values& v, int d) { v.textureFilter = Cycle(v.textureFilter, 4, d); } },
        { "ANISOTROPY", "The anisotropic filtering's samples.",
          [](const Settings::Values& v) { return std::to_string(v.anisotropy) + "X"; },
          [](Settings::Values& v, int d) {
              int level = v.anisotropy >= 16 ? 3 : v.anisotropy >= 8 ? 2 : v.anisotropy >= 4 ? 1 : 0;
              level = std::clamp(level + d, 0, 3);
              v.anisotropy = 2 << level; } },
        { "TEXTURE QUALITY", "Low and medium leave out the finest texture levels.",
          [](const Settings::Values& v) {
              static const char* const names[] = { "LOW", "MEDIUM", "HIGH" };
              return std::string(names[std::clamp(v.textureQuality, 0, 2)]); },
          [](Settings::Values& v, int d) { v.textureQuality = Cycle(v.textureQuality, 3, d); } },
        { "SHADOW QUALITY", "The shadow maps' size. The console's is 512.",
          [](const Settings::Values& v) {
              static const char* const names[] = { "AS THE FRAME", "LOW (512)", "MEDIUM (1024)", "HIGH (2048)", "ULTRA (4096)" };
              return std::string(names[std::clamp(v.shadowQuality, 0, 4)]); },
          [](Settings::Values& v, int d) { v.shadowQuality = Cycle(v.shadowQuality, 5, d); } },
        { "MODEL DETAIL", "Maximum keeps the finest models at any distance.",
          [](const Settings::Values& v) { return std::string(v.modelDetail == 1 ? "MAXIMUM" : "CONSOLE"); },
          [](Settings::Values& v, int d) { v.modelDetail = Cycle(v.modelDetail, 2, d); } },
        { "VERTICAL SYNC", "Frames wait for the screen: no tearing.",
          [](const Settings::Values& v) { return std::string(v.vsync ? "ON" : "OFF"); },
          [](Settings::Values& v, int) { v.vsync = !v.vsync; } },
        { "FRAME RATE", "Levels as fast as the machine can, or the console's 60.",
          [](const Settings::Values& v) { return std::string(v.unlockFrameRate ? "UNLIMITED" : "60 (CONSOLE)"); },
          [](Settings::Values& v, int) { v.unlockFrameRate = !v.unlockFrameRate; } },
    };
    constexpr int RowCount = int(sizeof(g_rows) / sizeof(g_rows[0]));

    // --- the page ------------------------------------------------------------------------------

    std::atomic<bool> g_wanted{ false };     // VIDEO taken: the page is opening
    std::atomic<bool> g_open{ false };       // the controls page is the VIDEO page
    std::atomic<bool> g_leave{ false };      // B: put it back
    uint32_t g_from = 0;                     // the options menu the page was opened from
    bool g_returning = false;                // left with B: VIDEO is chosen again there
    std::atomic<uint64_t> g_passBUntil{ 0 }; // B passed on to the title until then (GetTickCount64)
    // Presses for the page, from the input's thread.
    std::atomic<int> g_up{ 0 }, g_down{ 0 }, g_left{ 0 }, g_right{ 0 }, g_wheel{ 0 };

    uint32_t g_controls = 0;                 // the controls page (OptionsControlsMenu)
    uint32_t g_extraValues[2] = {};          // values for rows 2 and 3, clones of row 0's
    int g_top = 0;                           // the setting on the first row
    // The page's own words, to put back.
    struct Saved { uint32_t widget; std::string text; };
    std::vector<Saved> g_saved;

    uint32_t ControlsWidget(uint8_t* base, uint32_t offset) { return g_controls != 0 ? Guest::Read32(base, g_controls + offset) : 0; }
    uint32_t RowLabel(uint8_t* base, int row)
    {
        const uint32_t entries = Guest::Read32(base, g_controls + MenuEntries);
        const uint32_t entry = entries != 0 ? Guest::Read32(base, entries + uint32_t(row) * 4) : 0;
        return entry != 0 ? Guest::Read32(base, entry + EntryWidget) : 0;
    }
    uint32_t RowValue(uint8_t* base, int row)
    {
        for (int i = 0; i < 6; i++) if (ControlsValueRows[i] == row) return ControlsWidget(base, ControlsValues[i]);
        return row == 2 ? g_extraValues[0] : row == 3 ? g_extraValues[1] : 0;
    }

    bool ControlsStillThere(uint8_t* base)
    {
        return g_controls != 0 && Guest::Read32(base, g_controls) == ControlsMenuVtable && int16_t(Guest::Read16(base, g_controls + MenuCount)) == Rows;
    }

    void Save(uint8_t* base, uint32_t widget)
    {
        if (widget == 0) return;
        for (const Saved& s : g_saved) if (s.widget == widget) return;
        g_saved.push_back({ widget, TextOf(base, widget) });
    }

    // The words on the rows, the big name and the help, for the setting
    // chosen; each set only when it differs (the page's own updates may
    // have put theirs back).
    void Show(PPCContext& ctx, uint8_t* base)
    {
        const Settings::Values v = Settings::Get();
        SetText(ctx, base, ControlsWidget(base, ControlsTitle), "OPTIONS / VIDEO");
        for (int row = 0; row < Rows; row++)
        {
            const int setting = g_top + row;
            SetText(ctx, base, RowLabel(base, row), setting < RowCount ? g_rows[setting].label : "");
            SetText(ctx, base, RowValue(base, row), setting < RowCount ? g_rows[setting].value(v) : "");
        }
        const int selected = int16_t(Guest::Read16(base, g_controls + MenuSelected));
        const int setting = std::clamp(g_top + std::max(selected, 0), 0, RowCount - 1);
        SetText(ctx, base, ControlsWidget(base, ControlsBigName), g_rows[setting].label);
        SetText(ctx, base, ControlsWidget(base, ControlsHelp), g_rows[setting].help);
    }

    void Enter(PPCContext& ctx, uint8_t* base)
    {
        g_saved.clear();
        Save(base, ControlsWidget(base, ControlsTitle));
        Save(base, ControlsWidget(base, ControlsBigName));
        Save(base, ControlsWidget(base, ControlsHelp));
        for (int row = 0; row < Rows; row++)
        {
            Save(base, RowLabel(base, row));
            if (row != 2 && row != 3) Save(base, RowValue(base, row));
        }
        // Values for the slider rows: clones of row 0's, a row and two
        // below it; made once a page.
        const uint32_t model = RowValue(base, 0);
        for (int i = 0; i < 2 && model != 0; i++)
        {
            if (g_extraValues[i] == 0 || Guest::Read32(base, g_extraValues[i]) != TextVtable)
                g_extraValues[i] = Call(ctx, base, Virtual(base, model, TextClone), model);
            if (g_extraValues[i] != 0) WriteFloatAt(base, g_extraValues[i] + TextY, ReadFloat(base, model + TextY) + RowStep * float(2 + i));
        }
        g_top = 0;
        g_up = g_down = g_left = g_right = g_wheel = 0;
        g_leave = false;
        // The first row chosen.
        Call(ctx, base, Virtual(base, g_controls, MenuSetSelection), g_controls, 0, 0);
        g_open = true;
        Show(ctx, base);
        printf("video: the VIDEO page is up\n");
        fflush(stdout);
    }

    void Leave(PPCContext& ctx, uint8_t* base, bool passB)
    {
        g_open = false;
        if (ControlsStillThere(base))
            for (const Saved& s : g_saved) SetText(ctx, base, s.widget, s.text);
        g_saved.clear();
        if (passB)
        {
            g_passBUntil = GetTickCount64() + 150;
            g_returning = true;
        }
        printf("video: the VIDEO page is left\n");
        fflush(stdout);
    }

    void Apply(const Settings::Values& v) { Settings::Set(v); }

    // The page's presses, on the title's thread.
    void Navigate(PPCContext& ctx, uint8_t* base)
    {
        int selected = int16_t(Guest::Read16(base, g_controls + MenuSelected));
        if (selected < 0 || selected >= Rows) selected = 0;
        int row = selected;
        int up = g_up.exchange(0), down = g_down.exchange(0);
        const int wheel = g_wheel.exchange(0);
        static const bool log = getenv("COD3_MENULOG") != nullptr;
        if (log && (up | down | wheel) != 0) printf("video: up %d down %d wheel %d at row %d, top %d\n", up, down, wheel, selected, g_top);
        const int maxTop = std::max(0, RowCount - Rows);
        if (wheel != 0) g_top = std::clamp(g_top - wheel, 0, maxTop);
        for (; up > 0; up--)
        {
            if (row > 0) row--;
            else if (g_top > 0) g_top--;
        }
        for (; down > 0; down--)
        {
            if (row < Rows - 1 && g_top + row + 1 < RowCount) row++;
            else if (g_top < maxTop) g_top++;
        }
        if (row != selected)
            Call(ctx, base, Virtual(base, g_controls, MenuSetSelection), g_controls, uint32_t(row), 0);
        const int steps = g_right.exchange(0) - g_left.exchange(0);
        const int setting = g_top + row;
        if (steps != 0 && setting < RowCount)
        {
            Settings::Values v = Settings::Get();
            for (int i = 0; i < std::abs(steps); i++) g_rows[setting].step(v, steps > 0 ? 1 : -1);
            Apply(v);
        }
    }

    // --- drawing --------------------------------------------------------------------------------

    // A slider's bar or the frame round it (pictures of the page's layout,
    // not all of them the page's own words): what is drawn where rows 2
    // and 3 have their sliders, between the arrows' columns.
    bool IsSliderPart(uint8_t* base, uint32_t image)
    {
        if (!ControlsStillThere(base)) return false;
        if (image == ControlsWidget(base, ControlsBars[0]) || image == ControlsWidget(base, ControlsBars[1])) return true;
        const uint32_t quads = Guest::Read32(base, image + ImageQuads);
        if (quads == 0 || Guest::Read32(base, image + ImageQuadCount) == 0) return false;
        const uint32_t quad = Guest::Read32(base, quads);
        float x0 = ReadFloat(base, quad + 16), y0 = ReadFloat(base, quad + 20), x1 = ReadFloat(base, quad + 76), y1 = ReadFloat(base, quad + 80);
        if (x0 > x1) std::swap(x0, x1);
        if (y0 > y1) std::swap(y0, y1);
        return x0 >= 360.0f && x1 <= 500.0f && y0 >= 146.0f && y1 <= 198.0f && x1 - x0 >= 20.0f;
    }
}

// The options menu's entry chosen (ProfileEditMenu, vtable +76, the entry's
// number in r4): the fifth opens the VIDEO page on the controls page.
extern "C" PPC_FUNC(__imp__sub_824BC070);
PPC_FUNC(sub_824BC070)
{
    if (ctx.r4.u32 == 4 && EntryOf(base, ctx.r3.u32) != nullptr)
    {
        // The controls page of the same menu system (the front end's is its
        // menu 4; the pause menu's system is looked through).
        const uint32_t system = Guest::Read32(base, ctx.r3.u32 + MenuSystem);
        const uint32_t menus = system != 0 ? Guest::Read32(base, system + SystemMenus) : 0;
        const int32_t count = system != 0 ? int32_t(Guest::Read32(base, system + SystemCount)) : 0;
        for (int32_t i = 0; menus != 0 && i < count && i < 64; i++)
        {
            const uint32_t menu = Guest::Read32(base, menus + uint32_t(i) * 4);
            if (menu == 0 || Guest::Read32(base, menu) != ControlsMenuVtable) continue;
            g_wanted = true;
            g_from = ctx.r3.u32;
            Call(ctx, base, Virtual(base, system, SystemPush), system, uint32_t(i), 0);
            break;
        }
        return;
    }
    __imp__sub_824BC070(ctx, base);
}

// An FEText drawn (vtable +4 comes here with r4 = 0; the menu draws its
// entries' texts itself, the chosen one with r4 = 1, VIDEO's too). The
// VIDEO page's values of its slider rows are no one's, so they are drawn
// here, after row 0's, in the same state.
extern "C" PPC_FUNC(__imp__sub_824CFD58);
PPC_FUNC(sub_824CFD58)
{
    const uint32_t widget = ctx.r3.u32;
    PPCContext saved = ctx;
    __imp__sub_824CFD58(ctx, base);
    if (g_open.load() && widget != 0 && ControlsStillThere(base) && widget == RowValue(base, 0))
        for (uint32_t extra : g_extraValues)
            if (extra != 0 && Guest::Read32(base, extra) == TextVtable)
            {
                PPCContext again = saved;
                again.r3.u64 = extra;
                __imp__sub_824CFD58(again, base);
            }
}

// An FEImage drawn (its vtable +4). On the VIDEO page the slider bars are
// not drawn, and row 4's arrows are drawn at rows 0 to 3 too: their quad
// moved up a row at a time and put back.
extern "C" PPC_FUNC(__imp__sub_824C80D0);
PPC_FUNC(sub_824C80D0)
{
    const uint32_t image = ctx.r3.u32;
    if (!g_open.load() || image == 0)
    {
        __imp__sub_824C80D0(ctx, base);
        return;
    }
    if (IsSliderPart(base, image)) return;
    PPCContext saved = ctx;
    __imp__sub_824C80D0(ctx, base);
    if (!ControlsStillThere(base) || (image != ControlsWidget(base, ControlsLeftArrow) && image != ControlsWidget(base, ControlsRightArrow))) return;
    const uint32_t quads = Guest::Read32(base, image + ImageQuads);
    if (quads == 0 || Guest::Read32(base, image + ImageQuadCount) == 0) return;
    const uint32_t quad = Guest::Read32(base, quads);
    uint8_t original[80];
    memcpy(original, base + quad + 16, sizeof(original));
    for (int row = 0; row < 4; row++)
    {
        const float dy = -RowStep * float(4 - row);
        for (uint32_t corner = 0; corner < 4; corner++)
        {
            const uint32_t y = quad + 16 + corner * 20 + 4;
            uint32_t raw;
            memcpy(&raw, original + corner * 20 + 4, 4);
            raw = __builtin_bswap32(raw);
            float f;
            memcpy(&f, &raw, 4);
            WriteFloatAt(base, y, f + dy);
        }
        PPCContext again = saved;
        __imp__sub_824C80D0(again, base);
    }
    memcpy(base + quad + 16, original, sizeof(original));
}

void VideoMenu::Frame(PPCContext& ctx, uint8_t* base, uint32_t system)
{
    if (system == 0) return;
    const uint32_t menus = Guest::Read32(base, system + SystemMenus);
    const int32_t count = int32_t(Guest::Read32(base, system + SystemCount));
    if (menus == 0 || count <= 0 || count > 64) return;
    for (int32_t i = 0; i < count; i++)
    {
        const uint32_t menu = Guest::Read32(base, menus + uint32_t(i) * 4);
        if (menu == 0 || Guest::Read32(base, menu) != OptionsMenuVtable) continue;
        AddEntry(ctx, base, menu);
        if (const OptionsEntry* e = EntryOf(base, menu)) ShowFifthRow(ctx, base, *e);
    }
}

void VideoMenu::AfterFrame(PPCContext& ctx, uint8_t* base, uint32_t system)
{
    if (system == 0) return;
    const uint32_t menus = Guest::Read32(base, system + SystemMenus);
    const int32_t count = int32_t(Guest::Read32(base, system + SystemCount));
    const int32_t active = int32_t(Guest::Read32(base, system + SystemActive));
    if (menus == 0 || count <= 0 || count > 64) return;
    const uint32_t up = active >= 0 && active < count ? Guest::Read32(base, menus + uint32_t(active) * 4) : 0;

    // The options menu with VIDEO chosen: its big name and help.
    // Back from the page: VIDEO chosen again.
    if (up != 0 && up == g_from && g_returning && EntryOf(base, up) != nullptr)
    {
        g_returning = false;
        if (int16_t(Guest::Read16(base, up + MenuSelected)) != 4) Call(ctx, base, Virtual(base, up, MenuSetSelection), up, 4, 0);
    }
    if (up != 0 && EntryOf(base, up) != nullptr && int16_t(Guest::Read16(base, up + MenuSelected)) == 4)
    {
        SetText(ctx, base, Guest::Read32(base, up + OptionsBigName), "VIDEO");
        SetText(ctx, base, Guest::Read32(base, up + OptionsHelp), "Configure the picture: resolution, scale, anti-aliasing, textures and shadows.");
    }

    const bool controlsUp = up != 0 && Guest::Read32(base, up) == ControlsMenuVtable;
    if (g_wanted.load() && controlsUp)
    {
        g_wanted = false;
        g_controls = up;
        if (ControlsStillThere(base)) Enter(ctx, base);
    }
    if (!g_open.load()) return;
    if (!controlsUp || up != g_controls || !ControlsStillThere(base))
    {
        // The page went by some other way.
        Leave(ctx, base, false);
        return;
    }
    if (g_leave.exchange(false))
    {
        Leave(ctx, base, true);
        return;
    }
    Navigate(ctx, base);
    Show(ctx, base);
}

bool VideoMenu::Active() { return g_open.load(); }

void VideoMenu::FilterPad(Input::Pad& pad)
{
    const uint64_t now = GetTickCount64();
    if (!g_open.load())
    {
        // B passed on after the page was left, so the title goes back too.
        if (now < g_passBUntil.load()) pad.buttons |= 0x2000;   // B
        return;
    }
    // The cross (and the left stick pushed well over), each press once and
    // again while held, as a menu repeats; A is the next value, B leaves.
    struct Held { uint64_t since = 0, last = 0; bool down = false; };
    static Held held[4];
    const bool directions[4] = {
        (pad.buttons & 0x0001) != 0 || pad.thumbLY > 20000,     // up
        (pad.buttons & 0x0002) != 0 || pad.thumbLY < -20000,    // down
        (pad.buttons & 0x0004) != 0 || pad.thumbLX < -20000,    // left
        (pad.buttons & 0x0008) != 0 || pad.thumbLX > 20000,     // right
    };
    std::atomic<int>* const counters[4] = { &g_up, &g_down, &g_left, &g_right };
    for (int i = 0; i < 4; i++)
    {
        Held& h = held[i];
        if (directions[i] && !h.down) { h.down = true; h.since = h.last = now; counters[i]->fetch_add(1); }
        else if (directions[i] && now - h.since >= 400 && now - h.last >= 120) { h.last = now; counters[i]->fetch_add(1); }
        else if (!directions[i]) h.down = false;
    }
    static bool aWas = false, bWas = false;
    const bool a = (pad.buttons & 0x1000) != 0, b = (pad.buttons & 0x2000) != 0;
    if (a && !aWas) g_right.fetch_add(1);
    if (b && !bWas) g_leave = true;
    aWas = a;
    bWas = b;
    pad = {};
}

void VideoMenu::Wheel(int clicks) { g_wheel.fetch_add(clicks); }

bool VideoMenu::HitTest(float x, float y, int& row, int& direction)
{
    row = -1;
    direction = 0;
    if (!g_open.load()) return false;
    // The controls page's rows: labels from x 117.5, one row 20 down from
    // 118, the value between arrows at 341.75 and 521.75.
    for (int r = 0; r < Rows; r++)
    {
        const float top = 118.0f + RowStep * float(r) - 6.0f;
        if (y < top || y >= top + RowStep) continue;
        if (x < 110.0f || x > 536.0f) return true;
        row = r;
        if (x >= 330.0f && x < 354.0f) direction = -1;
        else if (x >= 510.0f && x <= 536.0f) direction = 1;
        else if (x >= 354.0f && x < 510.0f) direction = 1;
        return true;
    }
    return true;
}
