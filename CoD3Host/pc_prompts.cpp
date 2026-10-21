// The button prompts for the keyboard and mouse (a PC addition).
//
// The title writes its prompts with tokens (~cross, ~menu_back, ~square,
// ~updown ...) that its front end's text turns into a character of the
// button icon font (i_button_icons_xe): sub_824C68E0 maps a token to its
// character, and the layout that meets a '~' (sub_824D7650) puts that
// character in the text, which is drawn in the icon font there. The
// characters are '"' A, '%' B, '#' X, '$' Y, '!' RT, '&' LT, '(' RB, '\''
// LB, ')' ',' '+' '*' the cross up, down, left and right, '-' Start, '1'
// Back, '/' up and down, '.' left and right, '0' all four, '2' and '3' the
// sticks.
//
// The front end's manager (0x82A8DF2C) holds its fonts, the icon font at +8:
// +4 its image (+20 the texture, whose fetch constant's second word, +32,
// holds the address), +36 the first character, +40 how many, +44 each
// one's texture rectangle (u0 v0 u1 v1). While the keyboard and mouse are
// what the player uses (Input::PadPrompts), the renderer draws that texture
// as key caps with the keys the settings give each button
// (render_prompts.cpp): the prompts then name keys, at once, in every text
// already laid out. In a menu A is Enter and B Escape (input.cpp).

#include "pc_prompts.h"
#include "input.h"
#include "kernel.h"
#include "menu_mouse.h"
#include "render.h"
#include "settings.h"

#include <Windows.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <set>
#include <string>

namespace
{
    constexpr uint32_t FrontEndManager = 0x82A8DF2C;
    constexpr uint32_t ManagerIconFont = 8;
    constexpr uint32_t FontImage = 4, FontFirst = 36, FontCount = 40, FontRectangles = 44;
    constexpr uint32_t ImageTexture = 20, TextureFetchAddress = 32;

    float ReadFloat(uint8_t* base, uint32_t address)
    {
        const uint32_t v = Guest::Read32(base, address);
        float f;
        memcpy(&f, &v, 4);
        return f;
    }

    // A key as a prompt names it: short and in capitals.
    std::string KeyLabel(int key)
    {
        switch (key)
        {
        case 0: return "";
        case VK_LBUTTON: return "LMB";
        case VK_RBUTTON: return "RMB";
        case VK_MBUTTON: return "MMB";
        case VK_XBUTTON1: return "M4";
        case VK_XBUTTON2: return "M5";
        case VK_ESCAPE: return "ESC";
        case VK_RETURN: return "ENTER";
        case VK_LSHIFT: case VK_RSHIFT: case VK_SHIFT: return "SHIFT";
        case VK_LCONTROL: case VK_RCONTROL: case VK_CONTROL: return "CTRL";
        case VK_BACK: return "BKSP";
        case VK_CAPITAL: return "CAPS";
        case VK_UP: return "\xE2\x86\x91";
        case VK_DOWN: return "\xE2\x86\x93";
        case VK_LEFT: return "\xE2\x86\x90";
        case VK_RIGHT: return "\xE2\x86\x92";
        default: break;
        }
        std::string name = Settings::KeyName(key);
        for (char& c : name) c = char(std::toupper(static_cast<unsigned char>(c)));
        return name;
    }

    // What each icon of the font is on the keyboard and mouse.
    std::string Label(char icon, bool menu, bool title, const Settings::Values& s)
    {
        switch (icon)
        {
        case '"': return menu ? "ENTER" : KeyLabel(s.keys[Settings::Jump]);
        case '%': return menu ? "ESC" : KeyLabel(s.keys[Settings::Crouch]);
        case '#': return KeyLabel(s.keys[Settings::Use]);
        case '$': return KeyLabel(s.keys[Settings::Weapon]);
        case '!': return KeyLabel(s.keys[Settings::Fire]);
        case '&': return KeyLabel(s.keys[Settings::Aim]);
        case '(': return KeyLabel(s.keys[Settings::Grenade]);
        case '\'': return KeyLabel(s.keys[Settings::Smoke]);
        case ')': return KeyLabel(VK_UP);
        case ',': return KeyLabel(VK_DOWN);
        case '+': return KeyLabel(VK_LEFT);
        case '*': return KeyLabel(VK_RIGHT);
        case '-': return menu ? (title ? "ENTER" : "ESC") : KeyLabel(s.keys[Settings::Pause] != 0 ? s.keys[Settings::Pause] : VK_ESCAPE);
        case '1': return KeyLabel(s.keys[Settings::Objectives]);
        case '/': return "\xE2\x86\x91\xE2\x86\x93";
        case '.': return "\xE2\x86\x90\xE2\x86\x92";
        case '0': return "\xE2\x86\x91\xE2\x86\x93\xE2\x86\x90\xE2\x86\x92";
        case '2':
        {
            const std::string f = KeyLabel(s.keys[Settings::Forward]), l = KeyLabel(s.keys[Settings::Left]);
            const std::string b = KeyLabel(s.keys[Settings::Backward]), r = KeyLabel(s.keys[Settings::Right]);
            if (f.size() == 1 && l.size() == 1 && b.size() == 1 && r.size() == 1) return f + l + b + r;
            return "MOVE";
        }
        case '3': return "MOUSE";
        default: return "";
        }
    }
}

// The sentences that name the pad's sticks and buttons in words, not by
// token: the title's localized strings are found by a hash (sub_82527FD0,
// table and hash in, the entry out: +8 the text), and the ones for the pad
// are answered for the keyboard and mouse while they are what the player
// uses. {MOVE} the movement keys, {Action} that action's key. A text already
// on the screen stays as it was looked up.
namespace
{
    constexpr uint32_t MarketplaceHash = 0x4D2F262F, QuitHash = 0xD98400F5;    // "XBOX LIVE MARKETPLACE", "QUIT"

    struct Sentence { uint32_t hash; const char* text; };
    const Sentence Sentences[] = {
        // Moving and looking.
        { 0x81E1068C, "Use the mouse to look around and {MOVE} to move." },
        { 0x45D8EB25, "Use the mouse to look around and {MOVE} to move." },
        { 0x8D776D04, "Use {MOVE}" }, { 0x1850C584, "Use {MOVE}" }, { 0x66401F58, "Use {MOVE}" },
        { 0x6792ABB7, "Move the mouse" }, { 0xD5FA4857, "Move the mouse" }, { 0xE1D4DCAB, "Move the mouse" },
        { 0xD5193159, "Use the mouse and {MOVE}" }, { 0x60EC40B9, "Use the mouse and {MOVE}" }, { 0x6CC6D50D, "Use the mouse and {MOVE}" },
        // The objectives.
        { 0xDEEA4A0B, "Press and hold {Objectives} to check your objectives." },
        { 0x7FFC5AFF, "Press and hold {Objectives} to check your objectives." },
        // The right stick pressed: the view.
        { 0xFB4C8B2B, "Press {Melee} to toggle camera view." }, { 0x141D1F64, "Press {Melee} to toggle camera view." },
        { 0x83BC603F, "Press {Melee} to toggle camera view." },
        { 0x7272BFE1, "Press {Melee} to toggle view" }, { 0x1E6AA05A, "Press {Melee} to toggle view" }, { 0xF3F3D635, "Press {Melee} to toggle view" },
        // A hint's [VERB_x] (sub_824DD9A8): the verb, "INGAME_PRESS", _PULL
        // or _CLICK, and _AND_HOLD, then the button, a token or for a stick
        // "INGAME_L3_XENON" or _R3_; each looked up by name with "_XBOX360"
        // after it first (sub_8252CB98), the hash the name's, lower case,
        // h * 33 + c (sub_820A0118). Clicking a stick and pulling a trigger
        // are pressing a key.
        { 0x00A692B6, "Press" },                    // INGAME_CLICK
        { 0x7F34BAC7, "Press and hold" },           // INGAME_CLICK_AND_HOLD_XBOX360
        { 0xCC24CEA6, "Press" },                    // INGAME_PULL_XBOX360
        { 0x0C5B2D7E, "Press and hold" },           // INGAME_PULL_AND_HOLD_XBOX360
        { 0x6C191896, "{Binoculars}" },             // INGAME_L3_XENON
        { 0xF5A415DC, "{Melee}" },                  // INGAME_R3_XENON
        // The sticks pressed and held: a weapon set up.
        { 0x266B4FFC, "Press and hold {Binoculars} to set up your weapon" }, { 0x266B5002, "Press and hold {Melee} to set up your weapon" },
        { 0xCB5D7C3A, "Press and hold {Binoculars}" }, { 0xE926CD8D, "Press and hold {Melee}" },
        { 0xF3C2A68F, "{Binoculars}" }, { 0xC1A01315, "{Melee}" }, { 0xCFD83048, "{Binoculars}" }, { 0xE817970E, "{Melee}" },
        { 0xC48997AE, "Press and hold" },
        { 0x2E341C2F, "{Binoculars}" }, { 0x2E341CF5, "{Melee}" }, { 0x1C6145BB, "Press {Binoculars}" }, { 0x1C614681, "Press {Melee}" },
        // Triggers are pressed, not pulled.
        { 0xC284B070, "Press ~r2" }, { 0xC284AFAA, "Press ~l2" }, { 0x97404365, "Press and hold" },
        { 0x6C0B09A2, "Press ~r1 and ~l1 alternately as fast as you can" },
        // The right stick turned: the mouse.
        { 0x90DE938C, "Move the mouse in circles, clockwise." }, { 0x9D075825, "Move the mouse in circles, clockwise." },
        { 0xAF0E6080, "Move the mouse in circles, clockwise." }, { 0x84EEFC5B, "Move the mouse in circles, clockwise" },
        { 0xDD3D6B6F, "Move the mouse in circles, clockwise" }, { 0x10904294, "Move the mouse in circles, clockwise" },
        { 0x8E75818F, "Move the mouse in circles, counterclockwise." }, { 0x3D612B48, "Move the mouse in circles, counterclockwise." },
        { 0x78AE1A23, "Move the mouse in circles, counterclockwise." },
        { 0xD754EC4B, "Move the mouse up and down to move the crowbar." }, { 0x08676C84, "Move the mouse up and down to move the crowbar." },
        { 0xA1AD1D5F, "Move the mouse up and down to move the crowbar." },
        { 0x19456AB0, "Move the mouse left and right." }, { 0x89A68CC9, "Move the mouse left and right." }, { 0x84A4E324, "Move the mouse left and right." },
        { 0xDD2D2529, "Turn {MOVE} round to move the barrel left and right" }, { 0x82D2B27D, "Turn {MOVE} round to move the barrel left and right" },
        { 0x7E6E60A2, "Turn {MOVE} round to move the barrel left and right" },
        { 0xDF824DCA, "Move the mouse in circles to raise and lower the barrel" }, { 0x82E4CAFE, "Move the mouse in circles to raise and lower the barrel" },
        { 0xF2EAD1A3, "Move the mouse in circles to raise and lower the barrel" },
        // The title screen.
        { 0x53BCE3DA, "PRESS ENTER" }, { 0x01BFAEB6, "Press ENTER" }, { 0x501CDF0E, "Press ENTER" },
    };
    constexpr uint32_t SentenceCount = sizeof(Sentences) / sizeof(Sentences[0]);
    constexpr uint32_t SlotBytes = 160;          // a sentence and its entry (16) in guest memory

    // The sentence with its keys in, as the settings name them.
    std::string Fill(const char* text, const Settings::Values& s)
    {
        std::string out;
        for (const char* p = text; *p != 0; p++)
        {
            if (*p != '{') { out += *p; continue; }
            const char* end = strchr(p, '}');
            if (end == nullptr) { out += p; break; }
            const std::string name(p + 1, end);
            p = end;
            if (name == "MOVE")
            {
                const std::string f = KeyLabel(s.keys[Settings::Forward]), l = KeyLabel(s.keys[Settings::Left]);
                const std::string b = KeyLabel(s.keys[Settings::Backward]), r = KeyLabel(s.keys[Settings::Right]);
                out += (f.size() == 1 && l.size() == 1 && b.size() == 1 && r.size() == 1) ? f + " " + l + " " + b + " " + r : std::string("the movement keys");
                continue;
            }
            for (int a = 0; a < Settings::ActionCount; a++)
                if (name == Settings::ActionName(Settings::Action(a)))
                {
                    const std::string key = KeyLabel(s.keys[a]);
                    out += "[" + (key.empty() ? std::string("?") : key) + "]";
                    break;
                }
        }
        return out;
    }

    std::mutex g_sentenceMutex;
    uint32_t g_sentenceSlots = 0;                // guest memory: one slot a sentence
}

extern "C" PPC_FUNC(__imp__sub_82527FD0);
PPC_FUNC(sub_82527FD0)
{
    // The main menu's XBOX LIVE MARKETPLACE is Quit on a PC (menu_mouse.cpp
    // ends the game when it is taken): the title's own QUIT, the pause
    // menu's, in the title's language.
    if (ctx.r4.u32 == MarketplaceHash) ctx.r4.u64 = QuitHash;
    const uint32_t hash = ctx.r4.u32;
    __imp__sub_82527FD0(ctx, base);
    const uint32_t entry = ctx.r3.u32;
    // COD3_STRINGLOG=1: each string the title looks up, once.
    static const bool strings = getenv("COD3_STRINGLOG") != nullptr;
    if (strings && entry != 0)
    {
        static std::mutex mutex;
        static std::set<uint32_t> told;
        std::lock_guard<std::mutex> lock(mutex);
        if (told.insert(hash).second)
        {
            std::string text;
            for (uint32_t at = Guest::Read32(base, entry + 8), k = 0; at != 0 && k < 120; k++)
            {
                const uint8_t c = Guest::Read8(base, at + k);
                if (c == 0) break;
                text += char(c);
            }
            printf("strings: %08X \"%s\"\n", hash, text.c_str());
            fflush(stdout);
        }
    }
    if (entry == 0 || Input::PadPrompts()) return;
    uint32_t i = 0;
    while (i < SentenceCount && Sentences[i].hash != hash) i++;
    if (i == SentenceCount) return;
    static const bool logging = getenv("COD3_PROMPTLOG") != nullptr;
    std::lock_guard<std::mutex> lock(g_sentenceMutex);
    if (g_sentenceSlots == 0) g_sentenceSlots = Guest::AllocateKernelObject(SentenceCount * SlotBytes);
    if (g_sentenceSlots == 0) return;
    // The slot: the entry (the title's own words, the text at +8), then the text.
    const uint32_t slot = g_sentenceSlots + i * SlotBytes, text = slot + 16;
    std::string filled = Fill(Sentences[i].text, Settings::Get());
    if (filled.size() > SlotBytes - 17) filled.resize(SlotBytes - 17);
    for (uint32_t k = 0; k < 16; k += 4) Guest::Write32(base, slot + k, Guest::Read32(base, entry + k));
    Guest::Write32(base, slot + 8, text);
    for (size_t k = 0; k <= filled.size(); k++) Guest::Write8(base, text + uint32_t(k), k < filled.size() ? uint8_t(filled[k]) : 0);
    if (logging)
    {
        std::string was;
        for (uint32_t at = Guest::Read32(base, entry + 8), k = 0; at != 0 && k < 120; k++)
        {
            const uint8_t c = Guest::Read8(base, at + k);
            if (c == 0) break;
            was += char(c);
        }
        printf("prompts: %08X \"%s\" -> \"%s\"\n", hash, was.c_str(), filled.c_str());
        fflush(stdout);
    }
    ctx.r3.u64 = slot;
}

// The title's expansion of a placeholder of a hint: "[VERB_MELEE]" in, the
// words for it out (sub_824DD9A8; the keys it names are looked up through
// sub_8252CB98, by name, which comes to sub_82527FD0 above).
constexpr uint32_t ExpandPlaceholder = 0x824DD9A8;
namespace Mmio { void CallIndirect(PPCContext& ctx, uint8_t* base, uint32_t address); }

void PcPrompts::ExpandTest(PPCContext& ctx, uint8_t* base)
{
    static const bool wanted = getenv("COD3_EXPANDTEST") != nullptr;
    static bool done = false;
    static const uint64_t start = GetTickCount64();
    if (!wanted || done || GetTickCount64() - start < 20000) return;
    done = true;
    const char* const placeholders[] = { "[VERB_MELEE]", "[VERB_STANCE_DOWN]", "[VERB_STANCE_UP]", "[VERB_GRENADE]", "[PRESS_MELEE]", "[MELEE]" };
    const uint32_t text = Guest::AllocateKernelObject(64);
    for (const char* placeholder : placeholders)
    {
        for (size_t k = 0; k <= strlen(placeholder); k++) Guest::Write8(base, text + uint32_t(k), uint8_t(placeholder[k]));
        printf("expand: %s, pad prompts %d\n", placeholder, Input::PadPrompts() ? 1 : 0);
        fflush(stdout);
        PPCContext call = ctx;
        call.r3.u64 = text;
        Mmio::CallIndirect(call, base, ExpandPlaceholder);
        std::string out;
        for (uint32_t at = call.r3.u32, k = 0; at != 0 && k < 120; k++)
        {
            const uint8_t c = Guest::Read8(base, at + k);
            if (c == 0) break;
            out += char(c);
        }
        printf("expand: %s -> \"%s\"\n", placeholder, out.c_str());
        fflush(stdout);
    }
}

void PcPrompts::Frame(uint8_t* base)
{
    static uint64_t last = 0;
    const uint64_t now = GetTickCount64();
    if (now - last < 100) return;
    last = now;

    const uint32_t font = Guest::Read32(base, FrontEndManager + ManagerIconFont);
    const uint32_t first = font != 0 ? Guest::Read32(base, font + FontFirst) : 0;
    const uint32_t count = font != 0 ? Guest::Read32(base, font + FontCount) : 0;
    const uint32_t rectangles = font != 0 ? Guest::Read32(base, font + FontRectangles) : 0;
    const uint32_t image = font != 0 ? Guest::Read32(base, font + FontImage) : 0;
    const uint32_t texture = image != 0 ? Guest::Read32(base, image + ImageTexture) : 0;
    const uint32_t address = texture != 0 ? Guest::Read32(base, texture + TextureFetchAddress) & 0xFFFFF000u : 0;
    // The icon font: a few dozen characters from the space on.
    if (Input::PadPrompts() || address == 0 || rectangles == 0 || first != 0x20 || count < 20 || count > 64)
    {
        Render::SetPromptKeys(0, nullptr, 0);
        return;
    }

    const Settings::Values settings = Settings::Get();
    const bool menu = MenuMouse::MenuUp(), title = MenuMouse::TitleScreen();
    static const char icons[] = "\"%#$!&('),+*-1/.023";
    Render::PromptCell cells[sizeof(icons)] = {};
    uint32_t n = 0;
    for (const char* icon = icons; *icon != 0; icon++)
    {
        const uint32_t index = uint32_t(uint8_t(*icon)) - first;
        if (index >= count) continue;
        Render::PromptCell& cell = cells[n];
        cell.u0 = ReadFloat(base, rectangles + index * 16);
        cell.v0 = ReadFloat(base, rectangles + index * 16 + 4);
        cell.u1 = ReadFloat(base, rectangles + index * 16 + 8);
        cell.v1 = ReadFloat(base, rectangles + index * 16 + 12);
        if (!(cell.u1 > cell.u0 && cell.v1 > cell.v0 && cell.u0 >= 0.0f && cell.v1 <= 1.0f)) continue;
        const std::string label = Label(*icon, menu, title, settings);
        strncpy(cell.label, label.c_str(), sizeof(cell.label) - 1);
        n++;
    }
    Render::SetPromptKeys(address, cells, n);
}
