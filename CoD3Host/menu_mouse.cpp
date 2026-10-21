// Pointing at the title's own menus with the mouse (a PC addition).
//
// The menus are the title's C++ front end, built for a pad: a menu system
// (FEMenuSystem: the front end's and the in-game one, their pointers at
// 0x82A8DF2C +28 and +32) keeps its menus (+4, +24 of them) and which is up
// (+12, -1 for none); a menu (FEMenu) keeps its entries (+4, +38 of them),
// the highlighted one (+34) and where it lays them out: from x, y (+12,
// +16) one row (+20) apart, less a centring offset (+24), from the first
// shown (+32). Its SetSelection (vtable +164, sub_824CE558) moves the
// highlight as the cross does. An entry (FEMenuEntry) holds the widget it
// shows (+16) and whether it is hidden (+22).
//
// Each frame the menu system updates (sub_824EE2E0, on the title's thread,
// for both systems), the entry under the pointer is found and highlighted
// through SetSelection, with the sound the cross makes; the left button is
// then A over an entry
// (input.cpp). COD3_MENULOG=1 prints the menu and the pointer every second;
// COD3_MENUCURSOR=u,v puts the pointer at that place of the picture (0..1),
// for a test without a hand on the mouse.

#include "menu_mouse.h"
#include "pc_prompts.h"
#include "video_menu.h"
#include "widescreen.h"
#include "input.h"
#include "kernel.h"
#include "window.h"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" PPC_FUNC(__imp__sub_824EE2E0);

namespace
{
    constexpr uint32_t View2D = 0x8290E9AC;     // the view the 2D drawing goes through: +560 x scale, +576 y scale, +608 offset (the log)

    // A system's fields.
    constexpr uint32_t SystemMenus = 4, SystemActive = 12, SystemCount = 24;
    // A menu's.
    constexpr uint32_t MenuEntries = 4, MenuX = 12, MenuY = 16, MenuRow = 20, MenuOffset = 24;
    constexpr uint32_t MenuFirstShown = 32, MenuSelected = 34, MenuCount = 38, MenuShown = 40, MenuFlags = 42, MenuLocked = 48;
    constexpr uint32_t MenuSetSelection = 164;
    // The cross's up and down (vtable +204, sub_824C5E50, and +208) play the
    // move sound (vtable +28, sub_824E7810: UI_Select) when the highlight
    // moves, if the menu has one (+49), then call SetSelection.
    constexpr uint32_t MenuHasMoveSound = 49, MenuPlayMoveSound = 28;
    constexpr uint32_t MainMenuVtable = 0x82071DDC;
    // An entry's.
    constexpr uint32_t EntryWidget = 16, EntryHidden = 22;

    float ReadFloat(uint8_t* base, uint32_t address)
    {
        const uint32_t v = Guest::Read32(base, address);
        float f;
        memcpy(&f, &v, 4);
        return f;
    }
    int16_t Read16s(uint8_t* base, uint32_t address) { return int16_t(Guest::Read16(base, address)); }

    // The pointer in the picture, 0..1 across and down; false when it is
    // not over the picture (or the window is not in front).
    bool PointerInPicture(float& u, float& v)
    {
        static const char* const forced = getenv("COD3_MENUCURSOR");
        if (forced != nullptr)
        {
            // "u,v", or places from a time: "12:u,v 20:u,v" (seconds of the
            // pad script, input.cpp). Wobbling by a hair each call, as a
            // hand on the mouse does.
            static unsigned calls = 0;
            const double now = Input::ScriptSeconds();
            bool found = false;
            for (const char* at = forced; *at != 0;)
            {
                float t = 0.0f, pu = 0.0f, pv = 0.0f;
                if (sscanf(at, "%f:%f,%f", &t, &pu, &pv) == 3) { if (t <= now) { u = pu; v = pv; found = true; } }
                else if (sscanf(at, "%f,%f", &pu, &pv) == 2) { u = pu; v = pv; found = true; }
                while (*at != 0 && *at != ' ') at++;
                while (*at == ' ') at++;
            }
            if (!found) return false;
            u += (calls++ & 1) ? 0.001f : 0.0f;
            return true;
        }
        if (!Window::HasFocus()) return false;
        const HWND hwnd = static_cast<HWND>(Window::Handle());
        if (hwnd == nullptr) return false;
        POINT p{};
        RECT client{};
        if (!GetCursorPos(&p) || !ScreenToClient(hwnd, &p) || !GetClientRect(hwnd, &client)) return false;
        // The picture is the largest 16:9 rectangle in the middle of the
        // window (render_d3d11.cpp, PresentLocked).
        const float cw = float(client.right), ch = float(client.bottom);
        const float aspect = Widescreen::PictureAspect();   // 16:9, or wider in a level (widescreen.cpp)
        float w = cw, h = cw / aspect;
        if (h > ch) { h = ch; w = h * aspect; }
        const float x0 = std::floor((cw - w) * 0.5f), y0 = std::floor((ch - h) * 0.5f);
        u = (float(p.x) + 0.5f - x0) / std::floor(w);
        v = (float(p.y) + 0.5f - y0) / std::floor(h);
        return u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f;
    }

    // The picture's place in the menus' coordinates: 640 by 480 over the
    // whole picture, as the front end's 2D view has it (scale 2/640 and
    // -2/480 from the top left). The view current at the menu's update is
    // not asked: in a level it is whichever the frame drew last.
    bool PictureToMenu(uint8_t*, float u, float v, float& x, float& y)
    {
        // A picture wider than 16:9 has the 2D narrowed into its middle.
        const float k = Widescreen::Stretch();
        u = (u - 0.5f) * k + 0.5f;
        x = u * 640.0f;
        y = v * 480.0f;
        return true;
    }

    bool DrawnLately(uint32_t quad);

    void Log(uint8_t* base, uint32_t system, uint32_t menu)
    {
        static auto told = std::chrono::steady_clock::now() - std::chrono::seconds(5);
        const auto now = std::chrono::steady_clock::now();
        if (now - told < std::chrono::seconds(1)) return;
        told = now;
        const uint32_t view = Guest::Read32(base, View2D);
        {
            const auto words = [&](const char* what, uint32_t at, uint32_t n) {
                printf("menu:   %s %08X:", what, at);
                for (uint32_t i = 0; i < n; i++) printf(" %08X", Guest::Read32(base, at + i * 4));
                printf("\n");
            };
            words("manager", 0x82A8DF2C, 16);
            words("system", system, 16);
            words("menu", menu, 20);
        }
        printf("menu: system %08X menu %08X (vtable %08X) at %d,%d row %d offset %d, first %d selected %d of %d, %d shown, flags %04X locked %u\n",
            system, menu, Guest::Read32(base, menu), int32_t(Guest::Read32(base, menu + MenuX)), int32_t(Guest::Read32(base, menu + MenuY)),
            int32_t(Guest::Read32(base, menu + MenuRow)), int32_t(Guest::Read32(base, menu + MenuOffset)), Read16s(base, menu + MenuFirstShown),
            Read16s(base, menu + MenuSelected), Read16s(base, menu + MenuCount), Read16s(base, menu + MenuShown), Guest::Read16(base, menu + MenuFlags),
            Guest::Read8(base, menu + MenuLocked));
        if (view != 0)
            printf("menu:   2D view %08X: scale %g %g offset %g %g\n", view, ReadFloat(base, view + 560), ReadFloat(base, view + 580),
                ReadFloat(base, view + 608), ReadFloat(base, view + 612));
        const uint32_t entries = Guest::Read32(base, menu + MenuEntries);
        const int count = Read16s(base, menu + MenuCount);
        for (int i = 0; i < count && i < 32 && entries != 0; i++)
        {
            const uint32_t entry = Guest::Read32(base, entries + i * 4);
            if (entry == 0) continue;
            const uint32_t widget = Guest::Read32(base, entry + EntryWidget);
            printf("menu:   entry %d %08X bytes %02X %02X %02X widget %08X (vtable %08X):", i, entry, Guest::Read8(base, entry + 20), Guest::Read8(base, entry + 21),
                Guest::Read8(base, entry + EntryHidden), widget, widget != 0 ? Guest::Read32(base, widget) : 0);
            if (widget != 0)
                for (uint32_t o = 4; o < 112; o += 4)
                {
                    const uint32_t w = Guest::Read32(base, widget + o);
                    float f;
                    memcpy(&f, &w, 4);
                    if ((w & 0x7F800000u) != 0 && (w & 0x7F800000u) != 0x7F800000u && std::fabs(f) < 100000.0f && std::fabs(f) > 0.001f) printf(" +%u=%g", o, f);
                    else printf(" +%u=%08X", o, w);
                }
            printf("\n");
        }
        // The menu's own widgets (arrows, values): the words of it that point
        // at an object of a front end class.
        const auto scan = [&](const char* what, uint32_t object) {
        if (object == 0) return;
        for (uint32_t o = 4; o < 512; o += 4)
        {
            const uint32_t p = Guest::Read32(base, object + o);
            if (p < 0x40000000u || p >= 0xA0000000u) continue;
            const uint32_t vt = Guest::Read32(base, p);
            if (vt < 0x82070000u || vt >= 0x82073000u) continue;
            if (vt == 0x82070E04u)      // FEImage: its first quad's corners
            {
                const uint32_t quad = Guest::Read32(base, p + 32) != 0 ? Guest::Read32(base, Guest::Read32(base, p + 32)) : 0;
                printf("menu:   %s+%u %08X image vis %u at %g,%g quad %g,%g..%g,%g alpha %02X drawn %d\n", what, o, p, Guest::Read8(base, p + 4), ReadFloat(base, p + 20),
                    ReadFloat(base, p + 24), quad ? ReadFloat(base, quad + 16) : 0.0f, quad ? ReadFloat(base, quad + 20) : 0.0f,
                    quad ? ReadFloat(base, quad + 76) : 0.0f, quad ? ReadFloat(base, quad + 80) : 0.0f, quad ? Guest::Read32(base, quad + 32) >> 24 : 0, quad ? int(DrawnLately(quad)) : -1);
            }
            else
            {
                printf("menu:   %s+%u %08X vt %08X vis %u +8 %g +44 %g +48 %g align %u", what, o, p, vt, Guest::Read8(base, p + 4), ReadFloat(base, p + 8),
                    ReadFloat(base, p + 44), ReadFloat(base, p + 48), (Guest::Read16(base, p + 108) >> 4) & 3);
                if (vt == 0x82070B54u)
                {
                    const uint32_t text = Guest::Read32(base, p + 28);
                    printf(" text %08X:", text);
                    for (uint32_t i = 0; i < 48 && text >= 0x40000000u && text < 0xC0000000u; i += 4) printf(" %08X", Guest::Read32(base, text + i));
                }
                printf("\n");
            }
        }
        };
        scan("", menu);
        scan("system", system);
        scan("panel", Guest::Read32(base, system + 52));
        // COD3_MENULOG=2: the front end's fonts and the menu's texts in full.
        static const bool full = []() { const char* t = getenv("COD3_MENULOG"); return t != nullptr && t[0] == '2'; }();
        if (full)
        {
            for (uint32_t f = 4; f <= 16; f += 4)
            {
                const uint32_t font = Guest::Read32(base, 0x82A8DF2C + f);
                if (font == 0) continue;
                const uint32_t image = Guest::Read32(base, font + 4);
                const uint32_t texture = image != 0 ? Guest::Read32(base, image + 20) : 0;
                printf("menu:   font +%u %08X:", f, font);
                for (uint32_t o = 0; o < 64; o += 4) printf(" %08X", Guest::Read32(base, font + o));
                printf(" texture %08X fetch %08X %08X %08X\n", texture, texture ? Guest::Read32(base, texture + 32) : 0,
                    texture ? Guest::Read32(base, texture + 36) : 0, texture ? Guest::Read32(base, texture + 40) : 0);
                const uint32_t rects = Guest::Read32(base, font + 44);
                printf("menu:     glyphs:");
                for (uint32_t c = 0; c < 8 && rects != 0; c++)
                    printf(" [%g %g %g %g]", ReadFloat(base, rects + c * 16), ReadFloat(base, rects + c * 16 + 4), ReadFloat(base, rects + c * 16 + 8),
                        ReadFloat(base, rects + c * 16 + 12));
                printf("\n");
            }
            for (uint32_t o = 4; o < 512; o += 4)
            {
                const uint32_t p = Guest::Read32(base, menu + o);
                if (p < 0x40000000u || p >= 0xA0000000u) continue;
                const uint32_t vt = Guest::Read32(base, p);
                if (vt != 0x82070B54u && vt != 0x82070C84u) continue;
                printf("menu:   text +%u %08X vt %08X:", o, p, vt);
                for (uint32_t k = 0; k < 160; k += 4) printf(" %08X", Guest::Read32(base, p + k));
                printf("\n");
            }
        }
        float u, v, x, y;
        if (PointerInPicture(u, v) && PictureToMenu(base, u, v, x, y)) printf("menu:   pointer %.3f %.3f -> %.1f %.1f\n", u, v, x, y);
        fflush(stdout);
    }
}

namespace Mmio { void CallIndirect(PPCContext& ctx, uint8_t* base, uint32_t address); }

namespace
{
    std::atomic<uint64_t> g_menuUpAt{ 0 };       // when a menu system last updated with a menu up (GetTickCount64)
    std::atomic<bool> g_overEntry{ false };
    std::atomic<uint32_t> g_menuVtable{ 0 };     // the menu pointed at: its class, by its vtable

    struct Box
    {
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        bool Contains(float x, float y, float margin = 0.0f) const { return x >= x0 - margin && x < x1 + margin && y >= y0 - margin && y < y1 + margin; }
        float Width() const { return x1 - x0; }
        float Height() const { return y1 - y0; }
        float CentreX() const { return (x0 + x1) * 0.5f; }
        float CentreY() const { return (y0 + y1) * 0.5f; }
    };

    // The quads the front end drew lately (sub_824BFDD8 draws one), by
    // address: a menu keeps pictures of its other pages too, shown by their
    // flags and never drawn.
    // A table of 16384 by a hash of the address, a few places tried from
    // there; written on the title's thread, read on the same.
    struct DrawnQuad { uint32_t address = 0, at = 0; };
    DrawnQuad g_drawn[16384];
    constexpr uint32_t DrawnFor = 250, Probes = 8;

    uint32_t DrawnSlot(uint32_t quad) { return (quad * 2654435761u) >> 18; }

    bool DrawnLately(uint32_t quad)
    {
        const uint32_t now = uint32_t(GetTickCount64());
        for (uint32_t i = 0, at = DrawnSlot(quad); i < Probes; i++, at = (at + 1) & 16383)
            if (g_drawn[at].address == quad) return now - g_drawn[at].at < DrawnFor;
        return false;
    }

    void NoteDrawn(uint32_t quad)
    {
        const uint32_t now = uint32_t(GetTickCount64());
        uint32_t free = ~0u;
        for (uint32_t i = 0, at = DrawnSlot(quad); i < Probes; i++, at = (at + 1) & 16383)
        {
            if (g_drawn[at].address == quad) { g_drawn[at].at = now; return; }
            if (free == ~0u && (g_drawn[at].address == 0 || now - g_drawn[at].at >= DrawnFor)) free = at;
        }
        g_drawn[free != ~0u ? free : DrawnSlot(quad)] = { quad, now };
    }

    // The pictures a menu keeps (an FEImage, vtable 0x82070E04, among its
    // words) where its first quad is drawn, drawn ones only. A quad's
    // corners are 20 bytes apart from +16, x and y first, its colour's top
    // byte the alpha (sprint.cpp).
    std::vector<Box> Pictures(uint8_t* base, uint32_t menu)
    {
        std::vector<Box> pictures;
        for (uint32_t o = 4; o < 512; o += 4)
        {
            const uint32_t p = Guest::Read32(base, menu + o);
            if (p < 0x40000000u || p >= 0xA0000000u || Guest::Read32(base, p) != 0x82070E04u || Guest::Read8(base, p + 4) == 0) continue;
            const uint32_t quads = Guest::Read32(base, p + 32);
            const uint32_t quad = quads != 0 && Guest::Read32(base, p + 40) != 0 ? Guest::Read32(base, quads) : 0;
            if (quad == 0 || Guest::Read32(base, quad + 32) >> 24 == 0 || !DrawnLately(quad)) continue;
            Box box{ ReadFloat(base, quad + 16), ReadFloat(base, quad + 20), ReadFloat(base, quad + 76), ReadFloat(base, quad + 80) };
            if (box.x0 > box.x1) std::swap(box.x0, box.x1);
            if (box.y0 > box.y1) std::swap(box.y0, box.y1);
            // A slider's fill is drawn from its bar's left edge as far as
            // the value, while the picture stands at the bar's middle
            // (+20): the bar is that much either side.
            if (const float middle = ReadFloat(base, p + 20); middle > box.CentreX() + 1.0f && middle < box.x0 + 200.0f) box.x1 = 2.0f * middle - box.x0;
            if (box.Width() >= 2.0f && box.Height() >= 2.0f && box.x1 > 0.0f && box.x0 < 640.0f && box.y1 > 0.0f && box.y0 < 480.0f) pictures.push_back(box);
        }
        return pictures;
    }

    // A row with an arrow at each end: an option's (its value between, the
    // cross's left and right change it) or the chapter select's strip of
    // chapters (the one between in the middle is the one chosen). The
    // arrows are small pictures level with each other, the outermost two
    // of a row.
    struct Strip
    {
        Box left, right;
        int entry = -1;                 // the menu's entry on the row, -1 for none
        std::vector<Box> cells;         // the larger pictures between, left to right
    };
    std::vector<Strip> Strips(const std::vector<Box>& pictures)
    {
        const auto arrow = [](const Box& b) { return b.Width() >= 4.0f && b.Width() <= 28.0f && b.Height() >= 4.0f && b.Height() <= 34.0f; };
        std::vector<Strip> strips;
        std::vector<bool> used(pictures.size(), false);
        for (size_t i = 0; i < pictures.size(); i++)
        {
            if (used[i] || !arrow(pictures[i])) continue;
            Strip strip;
            strip.left = strip.right = pictures[i];
            int members = 0;
            for (size_t j = i; j < pictures.size(); j++)
                if (!used[j] && arrow(pictures[j]) && std::fabs(pictures[j].CentreY() - pictures[i].CentreY()) <= 3.0f)
                {
                    used[j] = true;
                    members++;
                    if (pictures[j].x0 < strip.left.x0) strip.left = pictures[j];
                    if (pictures[j].x0 > strip.right.x0) strip.right = pictures[j];
                }
            if (members < 2 || strip.right.x0 - strip.left.x1 < 30.0f) continue;
            for (const Box& b : pictures)
                if (b.Width() >= 30.0f && b.Height() >= 20.0f && b.x0 >= strip.left.x1 && b.x1 <= strip.right.x0 &&
                    std::fabs(b.CentreY() - strip.left.CentreY()) <= 6.0f)
                    strip.cells.push_back(b);
            std::sort(strip.cells.begin(), strip.cells.end(), [](const Box& a, const Box& b) { return a.x0 < b.x0; });
            strips.push_back(std::move(strip));
        }
        return strips;
    }

    // A text's string (an FEText's +28 points at a string object: the
    // characters' address, then the capacity and the length as halfwords).
    std::string TextOf(uint8_t* base, uint32_t widget)
    {
        const uint32_t text = Guest::Read32(base, widget + 28);
        if (text < 0x40000000u || text >= 0xC0000000u) return {};
        const uint32_t chars = Guest::Read32(base, text);
        const uint32_t length = std::min<uint32_t>(Guest::Read16(base, text + 6), 128);
        if (chars < 0x40000000u || chars >= 0xC0000000u) return {};
        std::string s(length, '\0');
        for (uint32_t i = 0; i < length; i++) s[i] = char(Guest::Read8(base, chars + i));
        return s;
    }

    // Where each of a menu's entries is on the screen, empty for one that
    // is not shown. An entry's widget is placed at its text's anchor (+44,
    // +48) with its box's width (+8) and alignment (+108 bits 4 and 5: 0
    // centred, 1 left, 2 right); its row is as tall as the step to its
    // neighbours, from a little above the anchor (the text's top) down. A
    // row with arrows reaches to them.
    //
    // A dialog (DialogMenu, in the DialogMenuSystem) lays its entries out
    // where they are not drawn: what it shows are the four button texts of
    // its panel (the system's +52; +24 to +36, one row of 20 apart), filled
    // from its entries, so an entry is where the text that reads the same
    // as it is.
    std::vector<Box> EntryBoxes(uint8_t* base, uint32_t system, uint32_t menu, bool dialog, const std::vector<Strip>& strips)
    {
        const uint32_t entries = Guest::Read32(base, menu + MenuEntries);
        const int count = Read16s(base, menu + MenuCount);
        if (entries == 0 || count <= 0 || count > 64) return {};
        std::vector<Box> boxes(static_cast<size_t>(count));
        if (dialog)
        {
            const uint32_t panel = Guest::Read32(base, system + 52);
            if (panel == 0) return boxes;
            for (int i = 0; i < count; i++)
            {
                const uint32_t entry = Guest::Read32(base, entries + i * 4);
                if (entry == 0 || Guest::Read8(base, entry + EntryHidden) != 0) continue;
                const uint32_t widget = Guest::Read32(base, entry + EntryWidget);
                if (widget == 0) continue;
                const std::string text = TextOf(base, widget);
                if (text.empty()) continue;
                for (uint32_t o = 24; o <= 36; o += 4)
                {
                    const uint32_t label = Guest::Read32(base, panel + o);
                    if (label == 0 || Guest::Read32(base, label) != 0x82070B54u || TextOf(base, label) != text) continue;
                    const float lx = ReadFloat(base, label + 44), ly = ReadFloat(base, label + 48);
                    boxes[size_t(i)] = { lx - 90.0f, ly - 6.0f, lx + 90.0f, ly + 14.0f };
                    break;
                }
            }
            return boxes;
        }
        float rowHeight = float(int32_t(Guest::Read32(base, menu + MenuRow)));
        {
            // The rows' step as laid out, which a menu may make other than its
            // row field: the smallest gap between two shown entries.
            float last = 0.0f, smallest = 0.0f;
            bool have = false;
            for (int i = 0; i < count; i++)
            {
                const uint32_t entry = Guest::Read32(base, entries + i * 4);
                if (entry == 0 || Guest::Read8(base, entry + EntryHidden) != 0) continue;
                const uint32_t widget = Guest::Read32(base, entry + EntryWidget);
                if (widget == 0) continue;
                const float wy = ReadFloat(base, widget + 48);
                if (have && std::fabs(wy - last) > 1.0f && (smallest == 0.0f || std::fabs(wy - last) < smallest)) smallest = std::fabs(wy - last);
                last = wy;
                have = true;
            }
            if (smallest > 0.0f) rowHeight = smallest;
        }
        if (!(rowHeight > 4.0f && rowHeight < 120.0f)) rowHeight = 20.0f;
        // A list longer than it shows: only the stretch on the screen.
        int first = 0, last = count;
        if (const int shown = Read16s(base, menu + MenuShown); shown > 0 && shown < count)
        {
            first = std::clamp(int(Read16s(base, menu + MenuFirstShown)), 0, count - 1);
            last = std::min(count, first + shown);
        }
        for (int i = first; i < last; i++)
        {
            const uint32_t entry = Guest::Read32(base, entries + i * 4);
            if (entry == 0 || Guest::Read8(base, entry + EntryHidden) != 0) continue;
            const uint32_t widget = Guest::Read32(base, entry + EntryWidget);
            if (widget == 0) continue;
            const float wx = ReadFloat(base, widget + 44), wy = ReadFloat(base, widget + 48);
            float width = ReadFloat(base, widget + 8);
            if (!(width > 8.0f && width < 640.0f)) width = 240.0f;
            const uint32_t align = (Guest::Read16(base, widget + 108) >> 4) & 3;
            Box box;
            box.x0 = align == 0 ? wx - width * 0.5f : align == 2 ? wx - width : wx;
            box.x1 = box.x0 + width;
            box.y0 = wy - rowHeight * 0.3f;
            box.y1 = box.y0 + rowHeight;
            for (const Strip& strip : strips)
                if (strip.entry == i)
                {
                    box.x0 = std::min(box.x0, strip.left.x0);
                    box.x1 = std::max(box.x1, strip.right.x1);
                }
            boxes[size_t(i)] = box;
        }
        return boxes;
    }

    // The highlight moved to an entry as the cross moves it: the menu's move
    // sound, then SetSelection.
    void Highlight(PPCContext& ctx, uint8_t* base, uint32_t menu, int entry)
    {
        const uint32_t vtable = Guest::Read32(base, menu);
        if (Guest::Read8(base, menu + MenuHasMoveSound) != 0)
        {
            PPCContext call = ctx;
            call.r3.u64 = menu;
            Mmio::CallIndirect(call, base, Guest::Read32(base, vtable + MenuPlayMoveSound));
        }
        PPCContext call = ctx;
        call.r3.u64 = menu;
        call.r4.u64 = uint64_t(entry);
        call.r5.u64 = 0;
        Mmio::CallIndirect(call, base, Guest::Read32(base, vtable + MenuSetSelection));
    }

    // What the left button does where the pointer is (input.cpp asks).
    std::atomic<int> g_clickDirection{ 0 };     // the cross's left (-1) or right (1) it presses, 0 for none
    std::atomic<int> g_clickSteps{ 0 };         // how many times
    std::atomic<bool> g_clickTakes{ false };    // A: an entry highlighted under it, or the chapter in the middle

    // The pointer's work on the menu up: the entry under it highlighted
    // when the pointer has moved or the left button is down (so the cross
    // and the keys still move the highlight while it rests), as the menu's
    // own SetSelection does it. Over an arrow, its row is highlighted and
    // the button is the cross's left or right; over an option's value too
    // (right, the next one); over a chapter of the strip, as many presses
    // as it is from the middle one. The button is A only over the entry
    // that is highlighted, so a click never takes another one.
    void Point(PPCContext& ctx, uint8_t* base, uint32_t system, uint32_t menu, bool dialog)
    {
        static float lastU = -1.0f, lastV = -1.0f;
        float u, v, x, y;
        if (!PointerInPicture(u, v) || !PictureToMenu(base, u, v, x, y))
        {
            g_overEntry.store(false);
            g_clickDirection.store(0);
            g_clickTakes.store(false);
            lastU = lastV = -1.0f;
            return;
        }
        // The VIDEO page (video_menu.cpp) says itself where its rows are.
        if (int row = -1, direction = 0; VideoMenu::HitTest(x, y, row, direction))
        {
            const bool moved = std::fabs(u - lastU) > 0.0005f || std::fabs(v - lastV) > 0.0005f;
            const bool pressed = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 || MenuMouse::ScriptedClick();
            lastU = u;
            lastV = v;
            if (row >= 0 && (moved || pressed) && Read16s(base, menu + MenuSelected) != row) Highlight(ctx, base, menu, row);
            const bool onRow = row >= 0 && Read16s(base, menu + MenuSelected) == row;
            g_overEntry.store(onRow && direction == 0);
            g_clickDirection.store(onRow ? direction : 0);
            g_clickSteps.store(onRow && direction != 0 ? 1 : 0);
            g_clickTakes.store(onRow && direction == 0);
            return;
        }
        const std::vector<Box> pictures = dialog ? std::vector<Box>{} : Pictures(base, menu);
        std::vector<Strip> strips = Strips(pictures);
        std::vector<Box> boxes = EntryBoxes(base, system, menu, dialog, strips);
        // Each strip's entry: the one whose row it is level with.
        for (Strip& strip : strips)
        {
            float best = 0.0f;
            for (size_t i = 0; i < boxes.size(); i++)
            {
                if (boxes[i].Height() <= 0.0f) continue;
                const float d = std::fabs(boxes[i].CentreY() - strip.left.CentreY());
                if (d < boxes[i].Height() * 0.6f && (strip.entry < 0 || d < best)) { strip.entry = int(i); best = d; }
            }
        }
        // A menu of entries also keeps the arrows of its other pages, not
        // drawn: only the rows of its entries count there.
        if (!boxes.empty())
            strips.erase(std::remove_if(strips.begin(), strips.end(), [](const Strip& s) { return s.entry < 0; }), strips.end());
        boxes = EntryBoxes(base, system, menu, dialog, strips);

        int over = -1, direction = 0, steps = 0;
        bool takes = false;
        for (const Strip& strip : strips)
        {
            const float margin = 5.0f;
            if (strip.left.Contains(x, y, margin)) { direction = -1; steps = 1; over = strip.entry; break; }
            if (strip.right.Contains(x, y, margin)) { direction = 1; steps = 1; over = strip.entry; break; }
            if (strip.entry >= 0)
            {
                // An option's value, between its arrows.
                const Box value{ strip.left.x1 + margin, boxes[size_t(strip.entry)].y0, strip.right.x0 - margin, boxes[size_t(strip.entry)].y1 };
                if (value.Contains(x, y)) { direction = 1; steps = 1; over = strip.entry; break; }
            }
            else if (!strip.cells.empty())
            {
                // The strip's chapters: the middle one is the one chosen.
                const float middle = (strip.left.x1 + strip.right.x0) * 0.5f;
                size_t centre = 0;
                for (size_t c = 1; c < strip.cells.size(); c++)
                    if (std::fabs(strip.cells[c].CentreX() - middle) < std::fabs(strip.cells[centre].CentreX() - middle)) centre = c;
                for (size_t c = 0; c < strip.cells.size(); c++)
                    if (strip.cells[c].Contains(x, y))
                    {
                        const int k = int(c) - int(centre);
                        if (k == 0) takes = true;
                        else { direction = k < 0 ? -1 : 1; steps = std::abs(k); }
                        break;
                    }
                if (takes || direction != 0) break;
            }
        }
        // A slider: a bar on an entry's row that has no arrows (the
        // sensitivities, the volume). Its left half is the cross's left, its
        // right half the right, again and again while the button is held.
        if (direction == 0 && !takes)
        {
            const Box* widest = nullptr;
            int row = -1;
            for (const Box& b : pictures)
            {
                if (b.Width() < 40.0f || b.Width() > 220.0f || b.Height() < 4.0f || b.Height() > 24.0f || !b.Contains(x, y, 4.0f)) continue;
                for (size_t i = 0; i < boxes.size(); i++)
                {
                    if (boxes[i].Height() <= 0.0f || std::fabs(b.CentreY() - boxes[i].CentreY()) >= boxes[i].Height() * 0.6f) continue;
                    bool arrows = false;
                    for (const Strip& strip : strips) arrows = arrows || strip.entry == int(i);
                    if (arrows) continue;
                    if (widest == nullptr || b.Width() > widest->Width()) { widest = &b; row = int(i); }
                }
            }
            if (widest != nullptr) { direction = x < widest->CentreX() ? -1 : 1; steps = 1; over = row; }
        }
        if (direction == 0 && !takes)
            for (size_t i = 0; i < boxes.size(); i++)
                if (boxes[i].Height() > 0.0f && boxes[i].Contains(x, y)) { over = int(i); break; }
        // A menu of no entries with a strip (the chapter select): its large
        // picture, the chosen chapter's, takes it too.
        if (direction == 0 && !takes && boxes.empty() && !strips.empty())
        {
            const Box* largest = nullptr;
            for (const Box& b : pictures)
                if (b.Width() >= 120.0f && b.Height() >= 80.0f && (largest == nullptr || b.Width() * b.Height() > largest->Width() * largest->Height())) largest = &b;
            if (largest != nullptr && largest->Contains(x, y)) takes = true;
        }

        const bool moved = std::fabs(u - lastU) > 0.0005f || std::fabs(v - lastV) > 0.0005f;
        const bool pressed = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 || MenuMouse::ScriptedClick();
        lastU = u;
        lastV = v;
        if (over >= 0 && (moved || pressed) && Read16s(base, menu + MenuSelected) != over && Guest::Read8(base, menu + MenuLocked) == 0)
        {
            static const bool logging = getenv("COD3_MENULOG") != nullptr;
            if (logging) printf("menu: the pointer (%.3f %.3f) highlights entry %d\n", u, v, over);
            Highlight(ctx, base, menu, over);
        }
        const bool onEntry = over >= 0 && Read16s(base, menu + MenuSelected) == over;
        g_overEntry.store(onEntry && direction == 0);
        g_clickDirection.store(direction);
        g_clickSteps.store(steps);
        g_clickTakes.store(takes || (onEntry && direction == 0));
        static const bool logging = getenv("COD3_MENULOG") != nullptr;
        if (logging)
        {
            static uint64_t told = 0;
            if (GetTickCount64() - told >= 1000)
            {
                told = GetTickCount64();
                printf("menu: pointer at %.1f,%.1f: %zu strips, entry %d, direction %d x%d, takes %d\n", x, y, strips.size(), over, direction, steps,
                    g_clickTakes.load() ? 1 : 0);
                for (const Strip& strip : strips)
                    printf("menu:   strip at %.0f..%.0f y %.0f, entry %d, %zu cells\n", strip.left.x0, strip.right.x1, strip.left.CentreY(), strip.entry,
                        strip.cells.size());
            }
        }
    }
}

MenuMouse::Click MenuMouse::ClickUnderPointer()
{
    Click click;
    if (!MenuUp()) return click;
    click.direction = g_clickDirection.load(std::memory_order_relaxed);
    click.steps = g_clickSteps.load(std::memory_order_relaxed);
    click.takes = g_clickTakes.load(std::memory_order_relaxed);
    return click;
}

// A quad of the front end drawn (an FEImage's draw calls this for each of
// its quads): noted, for the pictures the pointer can find.
extern "C" PPC_FUNC(__imp__sub_824BFDD8);
PPC_FUNC(sub_824BFDD8)
{
    NoteDrawn(ctx.r3.u32);
    // COD3_QUADLOG=1 (a test): every quad drawn in a burst every 3 s.
    static const bool quadLog = getenv("COD3_QUADLOG") != nullptr;
    if (quadLog)
    {
        static uint64_t burst = 0;
        const uint64_t now = GetTickCount64();
        if (now - burst >= 3000) burst = now;
        if (now - burst < 18)
        {
            const uint32_t q = ctx.r3.u32;
            printf("quad: %08X %g,%g %g,%g %g,%g %g,%g argb %08X uv %g,%g..%g,%g\n", q, ReadFloat(base, q + 16), ReadFloat(base, q + 20),
                ReadFloat(base, q + 36), ReadFloat(base, q + 40), ReadFloat(base, q + 56), ReadFloat(base, q + 60), ReadFloat(base, q + 76),
                ReadFloat(base, q + 80), Guest::Read32(base, q + 32), ReadFloat(base, q + 24), ReadFloat(base, q + 28), ReadFloat(base, q + 84),
                ReadFloat(base, q + 88));
        }
    }
    __imp__sub_824BFDD8(ctx, base);
}

// COD3_MENUCLICK="20 24.5": the left button pressed for a fifth of a second
// at those seconds of the pad script, for a test.
bool MenuMouse::ScriptedClick()
{
    static const char* const clicks = getenv("COD3_MENUCLICK");
    if (clicks == nullptr) return false;
    const double now = Input::ScriptSeconds();
    for (const char* at = clicks; *at != 0;)
    {
        const double t = atof(at);
        if (now >= t && now < t + 0.2) return true;
        while (*at != 0 && *at != ' ') at++;
        while (*at == ' ') at++;
    }
    return false;
}

bool MenuMouse::MenuUp() { return GetTickCount64() - g_menuUpAt.load(std::memory_order_relaxed) < 200; }
bool MenuMouse::PointerOverEntry() { return MenuUp() && g_overEntry.load(std::memory_order_relaxed); }
bool MenuMouse::TitleScreen() { return MenuUp() && g_menuVtable.load(std::memory_order_relaxed) == 0x82071724u; }   // TitleMenu

PPC_FUNC(sub_824EE2E0)
{
    static const bool logging = getenv("COD3_MENULOG") != nullptr;
    static const bool off = getenv("COD3_NOMENUMOUSE") != nullptr;
    // A dialog (DialogMenuSystem, whose update comes here too) is in front
    // of the menu it was opened over, which goes on updating: while one is
    // up, only its buttons are pointed at.
    constexpr uint32_t DialogSystemVtable = 0x820719D4;
    static uint64_t dialogUpAt = 0;
    const uint32_t system = ctx.r3.u32;
    PcPrompts::ExpandTest(ctx, base);
    // COD3_POSLOG=1: the player's origin twice a second (a test of movement).
    static const bool positions = getenv("COD3_POSLOG") != nullptr;
    if (positions)
    {
        static uint64_t told = 0;
        const uint64_t now = GetTickCount64();
        const uint32_t player = Guest::Read32(base, 0x82A2AB44);
        if (now - told >= 500 && player != 0)
        {
            told = now;
            printf("position: %llu %.1f %.1f %.1f\n", (unsigned long long)now, ReadFloat(base, player + 336), ReadFloat(base, player + 340), ReadFloat(base, player + 344));
            fflush(stdout);
        }
    }
    // Quit, in the main menu's last place (pc_prompts.cpp names it): taken
    // with A, the window is closed, which ends the game; the title does not
    // see the A (its Marketplace would open over the closing).
    static bool quitting = false;
    if (system != 0 && !quitting)
    {
        // A pressed lately, and not yet taken here.
        static uint64_t taken = 0;
        const uint64_t pressedAt = Input::PressedAt(0x1000);
        const bool a = pressedAt != 0 && pressedAt != taken && GetTickCount64() - pressedAt < 200;
        const int32_t active = int32_t(Guest::Read32(base, system + SystemActive));
        const uint32_t menus = Guest::Read32(base, system + SystemMenus);
        const uint32_t menu = active >= 0 && active < int32_t(Guest::Read32(base, system + SystemCount)) && menus != 0
            ? Guest::Read32(base, menus + uint32_t(active) * 4) : 0;
        if (menu != 0 && Guest::Read32(base, menu) == MainMenuVtable && a)
        {
            taken = pressedAt;
            const int count = Read16s(base, menu + MenuCount);
            if (count == 5 && Read16s(base, menu + MenuSelected) == count - 1 && Guest::Read8(base, menu + MenuLocked) == 0)
            {
                printf("menu: Quit taken\n");
                quitting = true;
                Window::RequestClose();
            }
        }
    }
    if (quitting) return;
    VideoMenu::Frame(ctx, base, system);
    if (system != 0 && !off)
    {
        const int32_t active = int32_t(Guest::Read32(base, system + SystemActive));
        const int32_t count = int32_t(Guest::Read32(base, system + SystemCount));
        const uint32_t menus = Guest::Read32(base, system + SystemMenus);
        if (active >= 0 && active < count && menus != 0)
            if (const uint32_t menu = Guest::Read32(base, menus + uint32_t(active) * 4); menu != 0)
            {
                const uint64_t now = GetTickCount64();
                const bool dialog = Guest::Read32(base, system) == DialogSystemVtable;
                if (dialog) dialogUpAt = now;
                g_menuUpAt.store(now, std::memory_order_relaxed);
                if (logging) Log(base, system, menu);
                if (!dialog && now - dialogUpAt < 200) {}
                else
                {
                    g_menuVtable.store(Guest::Read32(base, menu), std::memory_order_relaxed);
                    if (!Input::MouseHeld()) Point(ctx, base, system, menu, dialog);
                    else g_overEntry.store(false);
                }
            }
    }
    PPCContext before = ctx;
    __imp__sub_824EE2E0(ctx, base);
    VideoMenu::AfterFrame(before, base, system);
}
