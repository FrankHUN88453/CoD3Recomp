// Small fixes to the multiplayer program alone, with its own addresses
// (make_mp_host.py copies this file unchanged; the single player build
// compiles it to nothing).

#ifdef COD3_MP

#include <windows.h>
#include "debug_probe.h"
#include "kernel.h"
#include "input.h"
#include "overlay.h"
#include "render.h"
#include "render_stats.h"
#include "settings.h"
#include "imgui.h"

#include <algorithm>
#include <map>
#include <atomic>
#include <mutex>
#include <vector>
#include <string>
#include <set>
#include <mutex>
#include <cmath>
#include <string>
#include <cstdio>
#include <cstring>

// The shell shock's screen blur. An explosion close by (a bazooka fired at
// the ground at one's feet) shocks the player, and the title then smears the
// screen with a blurred copy of an earlier frame that fades out over the
// shock. Here that copy comes out dark, so the shock is a black, smeared
// screen over everything, the HUD included, for a moment.
//
// This function (the title's, beside cg_shellshockblur, the variable at
// 0x82B9F680) is called once a frame with the local client's shock: r3, when
// it began (r4) and how long it lasts (r5). While the shock lasts it starts
// the blur and notes it for the client (the word at +6256 of the client's
// 6368 bytes at 0x82C7D490), and the frame is drawn through the blur; its
// answer in r3 is not used. Asked with no time left, it notes no blur and
// the frame is drawn as it is. The shock's sound, its view kick and its
// slowed aim are elsewhere and stay. The setting: "Shell shock screen blur"
// in the F11 menu (off by default).
//
// (A washed out, then a grey and doubled picture, seen in the first tests
// of this, were "Native command lists" on Merville, not this.)
extern "C" PPC_FUNC(__imp__sub_8244BB00);
PPC_FUNC(sub_8244BB00)
{
    // What the shock is, each time it changes, into the log; and with
    // debug.txt's shock_capture, F12's captures of its first frames.
    {
        static uint32_t lastStart = 0, lastLength = 0;
        static int framesSince = -1, said = 0;
        const uint32_t start = ctx.r4.u32, length = ctx.r5.u32;
        if (start != lastStart || length != lastLength)
        {
            if (said++ < 60)
            {
                printf("shock: client %08X, began %u, lasts %u\n", ctx.r3.u32, start, length);
                fflush(stdout);
            }
            if (start != 0 && length != 0) framesSince = 0;
            lastStart = start;
            lastLength = length;
        }
        if (framesSince >= 0)
        {
            framesSince++;
            if (DebugProbe::ShockCapture() && (framesSince == 1 || framesSince == 8 || framesSince == 20 || framesSince == 40))
            {
                Overlay::RequestScreenshot();
                Render::LogNextFrame();
                printf("shock: capture %d frames in\n", framesSince);
                fflush(stdout);
            }
            if (framesSince > 40) framesSince = -1;
        }
    }
    if (!Settings::Get().shellShockBlur) ctx.r5.u64 = 0;
    __imp__sub_8244BB00(ctx, base);
}

// The melee lunge, as World at War's bayonet and Modern Warfare 2's
// commando knife have it (a PC addition).
//
// Call of Duty 3 has a melee assist already: a melee swung with an enemy
// close in front (bg_meleeassistrange, 50 units, inside bg_meleeassistfov)
// takes him as its target (the entity handle at +1480 of the player's
// state) and, every move while it lasts, turns the view onto him
// (bg_meleeassistaspeed). This is that turn (sub_824A1150, with the player
// state in r3 and the move's milliseconds in r4, from Pmove for every move
// on the host and in each player's prediction alike). After it, the player
// is carried towards the target as well: the flat part of his velocity
// (+16 of the state; the origin is at +0) set at the lunge's speed straight
// at him until he is within arm's reach. And the assist takes its target
// from further away (122 units: 128 until 0.5.57, 5% shorter since), so the
// lunge has a gap to close.
//
// Movement is predicted and decided on the host, so in a match everyone
// wants the same build and setting. "Melee lunge" in the F11 menu's
// multiplayer tab (on by default).
namespace
{
    constexpr uint32_t EntityTable = 0x82BD1DC0 + 512;   // [entity, serial] by number
    constexpr uint32_t MeleeTarget = 1480, Velocity = 16;
    constexpr uint32_t EntityOrigin = 336;
    constexpr float LungeSpeed = 520.0f;       // units a second; sprinting is about 280
    constexpr float ArmsReach = 36.0f;
    constexpr uint32_t NameAssistRange = 0x82077FD8;   // "bg_meleeassistrange"
    constexpr uint32_t CvarSet = 0x8254F0C8;

    float GetFloat(uint8_t* base, uint32_t address)
    {
        const uint32_t word = Guest::Read32(base, address);
        float value;
        memcpy(&value, &word, 4);
        return value;
    }
    void PutFloat(uint8_t* base, uint32_t address, float value)
    {
        uint32_t word;
        memcpy(&word, &value, 4);
        Guest::Write32(base, address, word);
    }
}

extern "C" PPC_FUNC(__imp__sub_824A1150);
PPC_FUNC(sub_824A1150)
{
    const uint32_t ps = ctx.r3.u32;
    const bool lunge = Settings::Get().meleeLunge;
    {
        // The assist's reach, once, the first time a move is made: "122"
        // and "50" are written where the title keeps its own strings.
        static int reach = -1;
        if (reach != (lunge ? 1 : 0))
        {
            reach = lunge ? 1 : 0;
            static uint32_t text = 0;
            if (text == 0) text = Guest::AllocatePhysical(0x1000);
            strcpy(reinterpret_cast<char*>(base + text), lunge ? "122" : "50");
            PPCContext call = ctx;
            call.r1.u32 = (ctx.r1.u32 - 0x1000) & ~0xFu;
            call.r3.u32 = NameAssistRange;
            call.r4.u32 = text;
            call.r5.u32 = 1;
            call.lr = 0;
            Guest::Lookup(CvarSet)(call, base);
        }
    }
    __imp__sub_824A1150(ctx, base);
    if (!lunge || ps == 0) return;

    const uint32_t handle = Guest::Read32(base, ps + MeleeTarget);
    if (handle == 0) return;
    const uint32_t number = handle & 0x3FFF, serial = handle >> 14;
    if (number >= 4096) return;
    const uint32_t slot = EntityTable + number * 8;
    if (Guest::Read32(base, slot + 4) != serial) return;
    const uint32_t entity = Guest::Read32(base, slot);
    if (entity == 0) return;

    const float dx = GetFloat(base, entity + EntityOrigin) - GetFloat(base, ps);
    const float dy = GetFloat(base, entity + EntityOrigin + 4) - GetFloat(base, ps + 4);
    const float distance = std::sqrt(dx * dx + dy * dy);
    if (!(distance > ArmsReach) || distance > 400.0f) return;
    PutFloat(base, ps + Velocity, dx / distance * LungeSpeed);
    PutFloat(base, ps + Velocity + 4, dy / distance * LungeSpeed);
}

// A heavy landing (sub_8248ED40, the player's crash landing in his
// movement): the title stuns the player for 500 to 2000 ms (35 a per cent of
// the fall's damage), the player's pm_time (+48 of his record, the movement's
// record at +0 of 0x82B922C8's) with the land flag (0x100 of +44), and cuts his
// speed. With Quick landing the stun is a quarter of that, at most 150 ms; the
// speed cut stays.
extern "C" PPC_FUNC(__imp__sub_8248ED40);
PPC_FUNC(sub_8248ED40)
{
    __imp__sub_8248ED40(ctx, base);
    if (!Settings::Get().quickLanding) return;
    const uint32_t move = Guest::Read32(base, 0x82B922C8);
    const uint32_t record = move ? Guest::Read32(base, move) : 0;
    if (!record) return;
    const uint32_t flags = Guest::Read32(base, record + 44);
    if (!(flags & 0x100)) return;
    const int32_t stun = int32_t(Guest::Read32(base, record + 48));
    if (stun <= 0) return;
    const int32_t quick = std::min(stun / 4, 150);
    Guest::Write32(base, record + 48, uint32_t(quick));
    static int said = 0;
    if (said++ < 20) { printf("moves: landing stun %d ms cut to %d\n", stun, quick); fflush(stdout); }
}

// Sprint: the player's stamina is the float at +1168 of his record (1 full),
// and +1172 counts up while he sprints. When a sprint ends the title waits
// about a second before the stamina comes back, then refills it over another
// second or more, and sprinting again waits on that. With Quick sprint
// recovery the stamina starts coming back a quarter second after the sprint
// ends and is full a second and a half from empty.
namespace
{
    float RecordFloat(uint8_t* base, uint32_t address)
    {
        const uint32_t bits = Guest::Read32(base, address);
        float f; memcpy(&f, &bits, 4); return f;
    }
}

void MpFixesFrame(uint8_t* base)
{
    const uint32_t now = GetTickCount();
    const uint32_t entities = Guest::Read32(base, 0x82BCC7FC);
    const uint32_t self = entities ? Guest::Read32(base, entities + 4) : 0;
    const uint32_t record = self ? Guest::Read32(base, self + 512) : 0;
    if (!record) return;
    const bool playing = Guest::Read32(base, record + 1924) == 3;

    if (Settings::Get().quickSprint && playing)
    {
        static uint32_t lastRun = 0, lastCount = 0, lastFrame = 0;
        const uint32_t count = Guest::Read32(base, record + 1172);
        if (count != lastCount) { lastCount = count; lastRun = now; }
        const float seconds = std::min(float(now - lastFrame) / 1000.0f, 0.1f);
        lastFrame = now;
        const float stamina = RecordFloat(base, record + 1168);
        // Run dry: the stamina comes back to half in a third of a second,
        // sprinting or not, so a sprint pressed again right away is a real
        // sprint and not short stumbling bursts on a near-empty bar.
        static bool winded = false;
        if (stamina >= 0.0f && stamina < 0.02f) winded = true;
        if (winded)
        {
            const float more = std::min(0.5f, stamina + seconds / 0.33f * 0.5f);
            uint32_t bits; memcpy(&bits, &more, 4);
            Guest::Write32(base, record + 1168, bits);
            if (more >= 0.5f) winded = false;
        }
        else if (now - lastRun > 250 && stamina >= 0.0f && stamina < 1.0f)
        {
            const float more = std::min(1.0f, stamina + seconds / 1.5f);
            uint32_t bits; memcpy(&bits, &more, 4);
            Guest::Write32(base, record + 1168, bits);
        }
    }

    // Aiming down the sights, for finding its fields: the record's words
    // that change between the sights down and a moment after they come up
    // (the left trigger, or the right mouse button standing in for it), the
    // first ten times.
    {
        static uint32_t snapshot[600] = {};
        static bool haveSnapshot = false, reported = false;
        static uint32_t heldSince = 0;
        static int reports = 0;
        // The sights: bit 8 of the buttons in this machine's player's newest
        // command (client 0's ring of 64, 48 bytes each, at +1688 of his 6352
        // bytes at 0x82C2ECF0, the newest's number at +4760).
        const uint32_t player = 0x82C2ECF0;
        const uint32_t newest = Guest::Read32(base, player + 4760);
        const uint32_t command = player + 1688 + 48 * (newest & 63);
        const bool aiming = (Guest::Read32(base, command + 4) & 8) != 0 || Input::State().leftTrigger > 100;
        if (!aiming)
        {
            for (uint32_t i = 0; i < 600; i++) snapshot[i] = Guest::Read32(base, record + 4 * i);
            haveSnapshot = true;
            reported = false;
            heldSince = 0;
        }
        else if (haveSnapshot && !reported && reports < 10)
        {
            if (!heldSince) heldSince = now;
            if (now - heldSince > 600)
            {
                reported = true;
                reports++;
                std::string line;
                for (uint32_t i = 8; i < 600; i++)
                {
                    const uint32_t word = Guest::Read32(base, record + 4 * i);
                    if (word == snapshot[i]) continue;
                    float f; memcpy(&f, &word, 4);
                    char item[64];
                    snprintf(item, sizeof(item), " +%u=%08X(%.3f)", 4 * i, word, double(f));
                    line += item;
                }
                printf("moves: sights up, changed:%s\n", line.c_str());
                fflush(stdout);
            }
        }
    }

    // move_watch (debug.txt): the sprint's and the landing's words, ten
    // times a second, for 90 seconds of play.
    static uint32_t last = 0, began = 0;
    if (!DebugProbe::MoveWatch() || !playing) return;
    if (!began) began = now;
    if (now - began > 90000 || now - last < 100) return;
    last = now;
    static uint32_t before[64] = {};
    static const uint32_t watched[] = { 40, 44, 48, 52, 1160, 1164, 1168, 1172, 1176, 1180, 1184, 1188, 1192, 1196, 1200 };
    std::string line;
    for (size_t i = 0; i < sizeof(watched) / sizeof(watched[0]); i++)
    {
        const uint32_t word = Guest::Read32(base, record + watched[i]);
        if (word != before[i])
        {
            char item[48];
            if (watched[i] == 1168) snprintf(item, sizeof(item), " +%u=%.3f", watched[i], RecordFloat(base, record + watched[i]));
            else snprintf(item, sizeof(item), " +%u=%d", watched[i], int32_t(word));
            line += item;
            before[i] = word;
        }
    }
    if (!line.empty()) { printf("moves: t %u%s\n", now - began, line.c_str()); fflush(stdout); }
}

// A custom class's main weapon. A player's kit at his spawn (sub_82206028:
// the player, his class 0 to 6, ...) takes his weapons, then gives the
// class's main weapon (by name, from sub_821E6118 for the Americans,
// sub_821E61A8 for the Germans: heavy assault, light assault, rifleman,
// medic, support, anti-armour, scout) and his side's pistol, then the
// class's grenades and ability, and switches to the main weapon. For this
// machine's own player (entity 0) the name asked for is the one chosen in
// the settings; the rest of the kit is the class's.
extern "C" PPC_FUNC(__imp__sub_82206028);
extern "C" PPC_FUNC(__imp__sub_821E6118);
extern "C" PPC_FUNC(__imp__sub_821E61A8);
namespace
{
    bool g_ownSpawn = false;
    bool g_ownAmerican = true;       // this machine's player's side at his last kit
    char g_ownPrimary[32] = {};      // and the main weapon it gave him
    bool g_launcherDue = false;      // the launcher still to give, after his kit
    bool g_binocularsDue = false;    // and the binoculars
    std::vector<uint32_t> g_binocularsOthers;   // the other players (and bots) whose kits this machine gave: binoculars for them too
    uint32_t g_ownHandle = 0;   // this machine's player's script handle, from his last kit
    const char* const CustomWeapons[] = { "", "m1garand", "thompson", "bar", "springfield", "mg30cal", "bazooka", "shotgun",
                                          "kar98", "mp40", "mp44", "kar98_sniper", "mg34", "panzerschreck",
                                          "lee_enfield", "bren", "sten", "fg42", "fg42_scoped" };
    // (The campaign's Sten and MG42 have models in the multiplayer archives
    // but no weapon file a map loads: given one, a player gets nothing.)

    // The pistols' names the kit gives (sub_82206028): the Germans' "p38"
    // at 0x8203AFD8, the Americans' "colt" at 0x8203AFDC, each made into a
    // script string (sub_821DF378: the string at r3, the text at r4) and
    // given. For one's own spawn the text asked for is the custom class's
    // second weapon: the other side's pistol, or a main weapon.
    constexpr uint32_t GermanPistol = 0x8203AFD8, AmericanPistol = 0x8203AFDC;

    uint32_t WeaponName(uint8_t* base, int chosen);
    const char* CampaignStandIn(const char* name);   // below, with the campaign guns

    // The custom class's second weapon's name (guest text), or 0: the side's own.
    uint32_t SecondWeapon(uint8_t* base)
    {
        const Settings::Values values = Settings::Get();
        const int slot = values.activeClass - 1;
        if (slot < 0 || slot >= Settings::Values::CustomClasses) return 0;
        const int sidearm = values.classSidearm[slot];
        if (sidearm == 1) return AmericanPistol;
        if (sidearm == 2) return GermanPistol;
        if (sidearm >= 3) return WeaponName(base, sidearm - 2);
        return 0;
    }

    // A weapon of the list by its number (1 on), as guest text.
    uint32_t WeaponName(uint8_t* base, int chosen)
    {
        constexpr int count = int(sizeof(CustomWeapons) / sizeof(CustomWeapons[0]));
        if (chosen <= 0 || chosen >= count) return 0;
        static uint32_t names = 0;
        if (!names)
        {
            names = Guest::AllocatePhysical(0x400);
            uint32_t at = 0;
            for (const char* name : CustomWeapons) { memcpy(base + names + at, name, strlen(name) + 1); at += 32; }
        }
        // A campaign gun not set up on this map: the gun it is a copy of.
        if (const char* standIn = CampaignStandIn(CustomWeapons[chosen]))
            for (int i = 1; i < count; i++)
                if (strcmp(CustomWeapons[i], standIn) == 0) return names + 32 * uint32_t(i);
        return names + 32 * uint32_t(chosen);
    }

    bool CustomWeapon(uint8_t* base, PPCContext& ctx, bool american)
    {
        if (!g_ownSpawn) return false;
        (void)american;
        const Settings::Values values = Settings::Get();
        const int slot = values.activeClass - 1;
        if (slot < 0 || slot >= Settings::Values::CustomClasses) return false;
        const int chosen = values.classPrimary[slot];
        if (chosen <= 0 || chosen >= int(sizeof(CustomWeapons) / sizeof(CustomWeapons[0]))) return false;
        ctx.r3.u64 = WeaponName(base, chosen);
        static int said = 0;
        if (said++ < 20) { printf("class: own spawn with %s\n", CustomWeapons[chosen]); fflush(stdout); }
        return true;
    }
}

PPC_FUNC(sub_82206028)
{
    // A script entity handle: its index in the low 14 bits, its entry (the
    // entity, then its generation) 8 bytes each at 0x82BD1FC0 (as the
    // script's SetModel reads it, sub_824CE130). This machine's own player:
    // the entity of client 0 (0x82BCC7FC's list, +4).
    const uint32_t handle = Guest::Read32(base, ctx.r3.u32);
    const uint32_t entity = Guest::Read32(base, 0x82BD1FC0 + 8 * (handle & 0x3FFF));
    const uint32_t entities = Guest::Read32(base, 0x82BCC7FC);
    const uint32_t own = entities ? Guest::Read32(base, entities + 4) : 0;
    g_ownSpawn = entity != 0 && entity == own;
    if (g_ownSpawn) g_ownHandle = handle;
    // A custom class's base class: the kit as that class's (its items,
    // ability and grenade count).
    if (g_ownSpawn)
    {
        const Settings::Values values = Settings::Get();
        const int slot = values.activeClass - 1;
        if (slot >= 0 && slot < Settings::Values::CustomClasses && values.classBase[slot] > 0)
            ctx.r4.s64 = values.classBase[slot] - 1;
    }
    static int said = 0;
    if (said++ < 20)
    {
        printf("class: kit for handle %08X (entity %08X, ours %08X), class %d%s\n", handle, entity, own, ctx.r4.s32,
               g_ownSpawn ? ": this machine's player" : "");
        fflush(stdout);
    }
    __imp__sub_82206028(ctx, base);
    if (g_ownSpawn && Settings::Get().launcher) g_launcherDue = true;
    if (g_ownSpawn && Settings::Get().binoculars) g_binocularsDue = true;
    else if (!g_ownSpawn && Settings::Get().binoculars && g_binocularsOthers.size() < 64) g_binocularsOthers.push_back(handle);
    g_ownSpawn = false;
}

// A script string made from text (sub_821DF378: the string at r3, the text
// at r4). In one's own kit, the side's pistol's name becomes the custom
// class's second weapon.
extern "C" PPC_FUNC(__imp__sub_821DF378);
PPC_FUNC(sub_821DF378)
{
    if (g_ownSpawn && (ctx.r4.u32 == GermanPistol || ctx.r4.u32 == AmericanPistol))
    {
        if (const uint32_t second = SecondWeapon(base))
        {
            static int said = 0;
            if (said++ < 20)
            {
                char name[32] = {};
                for (int i = 0; i < 31; i++) { name[i] = char(Guest::Read8(base, second + i)); if (!name[i]) break; }
                printf("class: own spawn, second weapon %s\n", name);
                fflush(stdout);
            }
            ctx.r4.u64 = second;
        }
    }
    __imp__sub_821DF378(ctx, base);
}

namespace
{
    // The main weapon's name a kit asked for (r3, guest text), kept for the
    // launcher's way back.
    void KeepPrimary(uint8_t* base, uint32_t name, bool american)
    {
        if (!g_ownSpawn) return;
        g_ownAmerican = american;
        g_ownPrimary[0] = 0;
        for (int i = 0; name && i < 31; i++) { g_ownPrimary[i] = char(Guest::Read8(base, name + i)); g_ownPrimary[i + 1] = 0; if (!g_ownPrimary[i]) break; }
    }
}

PPC_FUNC(sub_821E6118)
{
    if (!CustomWeapon(base, ctx, true)) __imp__sub_821E6118(ctx, base);
    KeepPrimary(base, ctx.r3.u32, true);
}

PPC_FUNC(sub_821E61A8)
{
    if (!CustomWeapon(base, ctx, false)) __imp__sub_821E61A8(ctx, base);
    KeepPrimary(base, ctx.r3.u32, false);
}

// The kit's grenades: their kind by side (sub_821E6238: the name) and how
// many by class and rank (sub_821E6850: into the word at r3; one, two, three
// as the player is promoted). For one's own spawn: the custom class's kind
// and count, and with "two grenades" at least two.
extern "C" PPC_FUNC(__imp__sub_821E6238);
PPC_FUNC(sub_821E6238)
{
    if (g_ownSpawn)
    {
        const Settings::Values values = Settings::Get();
        const int slot = values.activeClass - 1;
        const int kind = slot >= 0 && slot < Settings::Values::CustomClasses ? values.classGrenadeType[slot] : 0;
        if (kind > 0)
        {
            static uint32_t names = 0;
            if (!names)
            {
                names = Guest::AllocatePhysical(0x100);
                memcpy(base + names, "fraggrenade", 12);
                memcpy(base + names + 32, "stielhandgranate", 17);
                memcpy(base + names + 64, "mills_bomb", 11);
            }
            // 3, the Mills bomb: the campaign's, or the frag where it is
            // not set up (campaign_guns off, Poissons).
            const char* const standIn = kind == 3 ? CampaignStandIn("mills_bomb") : nullptr;
            ctx.r3.u64 = names + (kind == 1 || standIn ? 0 : kind == 2 ? 32 : 64);
            return;
        }
    }
    __imp__sub_821E6238(ctx, base);
}

// How many of a slot's ammo a kit gives (sub_821F28C8: into r3, the slot by
// name in r4 (a script string: its text at +12 of what r4 points at), the
// class, the rank): "primary", "primaryb", "grenade", "smokegrenade" (the
// second grenade slot) and "special" (a mine or a satchel: one). For one's
// own spawn: the custom class's grenade count, two grenades at least, and
// four of the second slot and the special.
extern "C" PPC_FUNC(__imp__sub_821F28C8);
PPC_FUNC(sub_821F28C8)
{
    const uint32_t out = ctx.r3.u32, holder = ctx.r4.u32;
    __imp__sub_821F28C8(ctx, base);
    if (!g_ownSpawn || !out || !holder) return;
    const uint32_t string = Guest::Read32(base, holder);
    if (!string) return;
    char slot[24] = {};
    for (int i = 0; i < 23; i++) { slot[i] = char(Guest::Read8(base, string + 12 + i)); if (!slot[i]) break; }
    const Settings::Values values = Settings::Get();
    const int custom = values.activeClass - 1;
    int count = int32_t(Guest::Read32(base, out));
    const int before = count;
    if (strcmp(slot, "grenade") == 0)
    {
        const int chosen = custom >= 0 && custom < Settings::Values::CustomClasses ? values.classGrenades[custom] : 0;
        if (chosen > 0) count = chosen;
        else if (values.twoGrenades && count < 2) count = 2;
    }
    else if ((strcmp(slot, "smokegrenade") == 0 || strcmp(slot, "special") == 0) && values.fourSecondary && count > 0 && count < 4)
        count = 4;
    if (count != before) Guest::Write32(base, out, uint32_t(count));
    static int said = 0;
    if (said++ < 30) { printf("class: own spawn, %s %d (the game's %d)\n", slot, count, before); fflush(stdout); }
}

// The classes' abilities: the scout's artillery, the medic's health packs
// and the support's ammo come back on a timer (sub_821F1E28 for the
// artillery, sub_821F23E0 for the others): the time it comes back is kept
// in the player's script variable 0x14D6461A, set to the game time plus the
// wait (three minutes for the artillery). With a shorter recharge, each new
// wait is cut to its share as soon as it is set.
namespace
{
    uint32_t CallGame(PPCContext& ctx, uint8_t* base, uint32_t function, uint32_t r3 = 0, uint32_t r4 = 0)
    {
        PPCContext call = ctx;
        call.r1.u32 = (ctx.r1.u32 - 0x2000) & ~0xFu;
        call.r3.u64 = r3;
        call.r4.u64 = r4;
        call.lr = 0;
        Guest::Lookup(function)(call, base);
        return call.r3.u32;
    }
}

bool MpFixesStreakArmed(uint32_t handle);
void MpFixesAbilities(PPCContext& ctx, uint8_t* base)
{
    const int percent = Settings::Get().abilityRecharge;
    if (percent >= 100 || percent <= 0 || !g_ownHandle) return;
    if (MpFixesStreakArmed(g_ownHandle)) return;   // the killstreak's artillery keeps its own clock
    static uint32_t last = 0;
    const uint32_t ticks = GetTickCount();
    if (ticks - last < 250) return;
    last = ticks;
    const uint32_t gameTime = Guest::Read32(base, 0x82A98030 + 1400);   // the script's GetTime
    if (gameTime < 0x82000000u || gameTime >= 0x82600000u) return;
    const uint32_t vars = CallGame(ctx, base, 0x821DE680, g_ownHandle);
    if (!vars) return;
    const uint32_t slot = CallGame(ctx, base, 0x821DE958, vars, 0x14D6461A);
    if (!slot) return;
    static uint32_t adjusted = 0;
    const uint32_t ready = Guest::Read32(base, slot);
    if (ready == adjusted) return;
    const uint32_t now = CallGame(ctx, base, gameTime);
    if (int32_t(ready - now) <= 0 || int32_t(ready - now) > 600000) { adjusted = ready; return; }
    const uint32_t wait = ready - now;
    const uint32_t shorter = now + uint32_t(uint64_t(wait) * uint32_t(percent) / 100);
    Guest::Write32(base, slot, shorter);
    adjusted = shorter;
    printf("class: ability back in %u s, not %u s\n", (shorter - now) / 1000, wait / 1000);
    fflush(stdout);
}

#endif

// A rocket launcher for everyone, as equipment (the Launcher setting): at
// one's own spawn, after the kit, the side's launcher (the Americans'
// bazooka, the Germans' Panzerschreck) is given besides the class's weapons,
// with some rockets; d-pad left takes it out, and again puts it away for the
// main weapon. The script's own calls (the table at 0x82A98030): give a
// weapon at +2664, switch to one at +2692, a weapon's clip and its spare
// ammunition at +2864 and +2872 (the player, the weapon's name, how many).
#ifdef COD3_MP
namespace
{
    constexpr uint32_t Calls = 0x82A98030, MakeText = 0x821E0E48, FreeText = 0x821DD938;
    bool g_launcherOut = false;

    uint32_t Call3(PPCContext& ctx, uint8_t* base, uint32_t function, uint32_t r3, uint32_t r4, uint32_t r5)
    {
        PPCContext call = ctx;
        call.r1.u32 = (ctx.r1.u32 - 0x2000) & ~0xFu;
        call.r3.u64 = r3;
        call.r4.u64 = r4;
        call.r5.u64 = r5;
        call.lr = 0;
        Guest::Lookup(function)(call, base);
        return call.r3.u32;
    }

    // A script call on a player (his script handle) with a weapon's name (and a count).
    void OnWeaponFor(PPCContext& ctx, uint8_t* base, uint32_t handle, uint32_t slot, const char* weapon, uint32_t count = 0)
    {
        const uint32_t function = Guest::Read32(base, Calls + slot);
        if (function < 0x82000000u || function >= 0x82600000u || !handle) return;
        static uint32_t scratch = 0;
        if (!scratch) scratch = Guest::AllocatePhysical(0x100);
        memset(base + scratch, 0, 0x100);
        memcpy(base + scratch + 0x40, weapon, strlen(weapon) + 1);
        Call3(ctx, base, MakeText, scratch, scratch + 0x40, 0);
        Call3(ctx, base, function, handle, scratch, count);
        Call3(ctx, base, FreeText, scratch, 0, 0);
    }

    // A script call on this machine's player with a weapon's name (and a count).
    void OnWeapon(PPCContext& ctx, uint8_t* base, uint32_t slot, const char* weapon, uint32_t count = 0)
    {
        const uint32_t function = Guest::Read32(base, Calls + slot);
        if (function < 0x82000000u || function >= 0x82600000u || !g_ownHandle) return;
        static uint32_t scratch = 0;
        if (!scratch) scratch = Guest::AllocatePhysical(0x100);
        memset(base + scratch, 0, 0x100);
        memcpy(base + scratch + 0x40, weapon, strlen(weapon) + 1);
        Call3(ctx, base, MakeText, scratch, scratch + 0x40, 0);
        Call3(ctx, base, function, g_ownHandle, scratch, count);
        Call3(ctx, base, FreeText, scratch, 0, 0);
    }

    const char* Launcher() { return g_ownAmerican ? "bazooka" : "panzerschreck"; }
}

// The sights in the air: the aim-down-sights flag's update (sub_824A7770,
// from the movement's PmoveSingle) puts the sights up only on the ground
// (the movement's locals at 0x82C74880, +48 the ground under the player:
// none in the air, so it went to the sights-down path). With ads_in_air
// it is told there is ground for that check alone, and set back after.
extern "C" PPC_FUNC(__imp__sub_824A7770);
PPC_FUNC(sub_824A7770)
{
    constexpr uint32_t Ground = 0x82C74880u + 48u;
    const bool lift = Settings::Get().adsInAir && Guest::Read32(base, Ground) == 0;
    if (lift) Guest::Write32(base, Ground, 1);
    __imp__sub_824A7770(ctx, base);
    if (lift) Guest::Write32(base, Ground, 0);
}

// A dying player's gun dropped (sub_82580E38: the entity in r3; it drops
// the first slot's weapon, +1065). The gun in hand is kept in the second
// slot for pick-ups (below), so the pistol fell instead of the gun held:
// the two are put back the other way first, and the gun held falls.
extern "C" PPC_FUNC(__imp__sub_82580E38);
PPC_FUNC(sub_82580E38)
{
    const uint32_t entity = ctx.r3.u32;
    const uint32_t ps = entity ? Guest::Read32(base, entity + 512) : 0;
    if (ps >= 0x40000000u && ps < 0x90000000u)
    {
        uint32_t held = Guest::Read8(base, entity + 3);
        if (held == 0) held = Guest::Read32(base, ps + 164);
        const uint8_t first = Guest::Read8(base, ps + 1065), second = Guest::Read8(base, ps + 1066);
        if (held != 0 && held == second && first != 0 && first != held)
        {
            Guest::Write8(base, ps + 1065, second);
            Guest::Write8(base, ps + 1066, first);
            static int said = 0;
            if (said++ < 20) { printf("slots: dying with %u in hand: it is the one dropped (was the slot's %u)\n", held, first); fflush(stdout); }
        }
    }
    __imp__sub_82580E38(ctx, base);
}

void MpFixesCampaignGuns(PPCContext& ctx, uint8_t* base);
namespace { std::string ReadText(uint8_t* base, uint32_t at, size_t most); }

// Binoculars for every class (the Binoculars setting). Only the scout's kit
// has them (the loadout's sub_821F3C60: "mp_binoculars_nospot", by rank
// "_rank2" or "_rank3", given as his "special"); the others' specials are
// mines, rifle grenades, revives and ammo (sub_821EE6C0, sub_821EE7C8,
// sub_821F3DF0, sub_821F3EC0, chosen by class in sub_821FB7E0). At one's own
// spawn, after the kit, the plain binoculars are given besides, every slot
// put back as it was, as the launcher is; the host does the same for every
// other player and bot whose kit it gives. D-pad up takes them out (the
// script's switch, +2692), d-pad down goes back to the main weapon (on the
// keyboard: B and N, the Binoculars out and away keys). While they are out
// aim is held for the title (they are at the eyes), the left trigger's
// presses turn an x2 zoom on and off instead, and the right trigger goes
// through only with an artillery strike ready (input.cpp).
namespace
{
    bool g_binocularsOut = false;
    constexpr const char* Binoculars = "mp_binoculars_nospot";
}

bool MpFixesOwnStrikeReady();
bool MpFixesStreakPad(PPCContext& ctx, uint8_t* base, bool down, int rights);
namespace { bool g_heldSpotter = false; }   // 0.5.86: the spotter's binoculars in this machine's player's hands
void MpFixesBinoculars(PPCContext& ctx, uint8_t* base)
{
    const uint32_t entities = Guest::Read32(base, 0x82BCC7FC);
    const uint32_t own = entities ? Guest::Read32(base, entities + 4) : 0;
    const uint32_t ps = own ? Guest::Read32(base, own + 512) : 0;
    if (g_binocularsDue)
    {
        g_binocularsDue = false;
        g_binocularsOut = false;
        Input::SetBinoculars(false, false);
        if (ps)
        {
            uint8_t slots[10];
            for (int i = 0; i < 10; i++) slots[i] = Guest::Read8(base, ps + 1064 + i);
            OnWeapon(ctx, base, 2664, Binoculars);
            for (int i = 0; i < 10; i++)
                if (Guest::Read8(base, ps + 1064 + i) != slots[i])
                {
                    printf("class: the binoculars took slot %d (weapon %u, was %u): put back\n", i, Guest::Read8(base, ps + 1064 + i), slots[i]);
                    Guest::Write8(base, ps + 1064 + i, slots[i]);
                }
            static int said = 0;
            if (said++ < 20) { printf("class: own spawn, binoculars given (main weapon %s)\n", g_ownPrimary); fflush(stdout); }
        }
    }
    // The others' (the host gives every player's kit): the same, with their
    // slots put back.
    for (const uint32_t handle : g_binocularsOthers)
    {
        const uint32_t entity = Guest::Read32(base, 0x82BD1FC0 + 8 * (handle & 0x3FFF));
        const uint32_t theirs = entity ? Guest::Read32(base, entity + 512) : 0;
        if (theirs < 0x40000000u || theirs >= 0x90000000u) continue;
        uint8_t slots[10];
        for (int i = 0; i < 10; i++) slots[i] = Guest::Read8(base, theirs + 1064 + i);
        OnWeaponFor(ctx, base, handle, 2664, Binoculars);
        for (int i = 0; i < 10; i++)
            if (Guest::Read8(base, theirs + 1064 + i) != slots[i]) Guest::Write8(base, theirs + 1064 + i, slots[i]);
        static int said = 0;
        if (said++ < 20) { printf("class: binoculars given to handle %08X\n", handle); fflush(stdout); }
    }
    g_binocularsOthers.clear();
    // Whether they are out is read from the weapon in hand (+164 of the
    // player's state) each frame, never kept apart from it: the binoculars'
    // weapon number found by name, as the script's give finds it (the
    // name's hash, sub_820E10E0, then the weapon, sub_82480428). (0.5.59's
    // first try kept its own "out" and took the weapon's kind +180 for 7;
    // the binoculars' is not, so it lost track a moment after they came out,
    // held aim on whatever was in hand and would not put them away.)
    static uint8_t binocular = 0;
    static uint32_t binocularMap = 0;
    const uint32_t list = Guest::Read32(base, 0x82BCBF9C);
    if (binocularMap != list) { binocular = 0; binocularMap = list; }
    if (!binocular && list)
    {
        static uint32_t text = 0;
        if (!text) text = Guest::AllocatePhysical(0x40);
        memset(base + text, 0, 0x40);
        memcpy(base + text, Binoculars, strlen(Binoculars) + 1);
        const uint32_t hash = Call3(ctx, base, 0x820E10E0, text, 0, 0);
        binocular = uint8_t(Call3(ctx, base, 0x82480428, hash, 0, 0));
        const uint32_t weapon = binocular ? Guest::Read32(base, list + 4 * binocular) : 0;
        printf("class: binoculars are weapon %u (kind %u, field of view word %08X)\n", binocular,
               weapon ? Guest::Read32(base, weapon + 180) : 0, weapon ? Guest::Read32(base, weapon + 1600) : 0);
        fflush(stdout);
    }
    const uint32_t held = ps ? Guest::Read32(base, ps + 164) : 0;
    const bool out = binocular != 0 && held == binocular && ps != 0;
    {
        // The spotter's binoculars (mp_binoculars, a strike armed): found by
        // name as the class's are.
        static uint8_t spotter = 0;
        static uint32_t spotterMap = 0;
        if (spotterMap != list) { spotter = 0; spotterMap = list; }
        if (!spotter && list)
        {
            static uint32_t text = 0;
            if (!text) text = Guest::AllocatePhysical(0x40);
            memset(base + text, 0, 0x40);
            memcpy(base + text, "mp_binoculars", 14);
            spotter = uint8_t(Call3(ctx, base, 0x82480428, Call3(ctx, base, 0x820E10E0, text, 0, 0), 0, 0));
        }
        g_heldSpotter = spotter != 0 && ps != 0 && held == spotter;
    }
    const uint32_t binocularWeapon = (binocular && list) ? Guest::Read32(base, list + 4 * binocular) : 0;

    // The view through them: the binoculars' own sights' field of view
    // (+1600 of their weapon, 0.5.59's first try left the game's, far too
    // close) set to a light zoom, 1.5 times the player's own field of view
    // (the F11 menu's, cg_fov), and twice that while the left trigger's zoom
    // is on. The game's own value is put back whenever they are not in hand.
    static uint32_t fovWeapon = 0, fovWas = 0;
    static bool zoomed = false;
    const auto view = [&](int level) {   // 0 the game's own, 1 light, 2 the x2 zoom
        if (level == 0)
        {
            if (fovWeapon) Guest::Write32(base, fovWeapon + 1600, fovWas);
            fovWeapon = 0;
            return;
        }
        if (!binocularWeapon) return;
        if (fovWeapon != binocularWeapon) { fovWeapon = binocularWeapon; fovWas = Guest::Read32(base, binocularWeapon + 1600); }
        const float own = float(std::clamp(Settings::Get().fov, 50, 120));
        const float magnify = level == 2 ? 3.0f : 1.5f;
        const float half = own * 0.5f * 3.14159265f / 180.0f;
        const float fov = 2.0f * atanf(tanf(half) / magnify) * 180.0f / 3.14159265f;
        uint32_t word; memcpy(&word, &fov, 4);
        if (Guest::Read32(base, binocularWeapon + 1600) != word)
        {
            Guest::Write32(base, binocularWeapon + 1600, word);
            static int said = 0;
            if (said++ < 20) { printf("class: binoculars' view %.1f degrees (x%.1f of %.0f)\n", fov, magnify, own); fflush(stdout); }
        }
    };
    if (!Settings::Get().binoculars || !g_ownHandle || g_ownPrimary[0] == 0 || !out)
    {
        view(0);
        zoomed = false;
        Input::SetBinoculars(false, false);
        Input::TakeAimPresses();
    }
    else
    {
        // In hand: at the eyes, the right trigger only with a strike ready,
        // the left one's presses turning the x2 zoom on and off.
        Input::SetBinoculars(true, MpFixesOwnStrikeReady());
        if (Input::TakeAimPresses() % 2 == 1) zoomed = !zoomed;
        view(zoomed ? 2 : 1);
    }
    g_binocularsOut = out;
    if (!Settings::Get().binoculars || !g_ownHandle || g_ownPrimary[0] == 0) return;
    const Input::Pad pad = Input::State();
    int starts = 0, ups = 0, downs = 0;
    Input::TakePresses(starts, ups, downs);
    int rights = Input::TakeRightPresses();
    // Presses from before this frame's last second (the menus, a load) are
    // stale: no game frame was there to take them.
    static uint32_t lastFrame = 0;
    const uint32_t tick = GetTickCount();
    if (tick - lastFrame > 1000) starts = ups = downs = rights = 0;
    lastFrame = tick;
    const bool up = ups > 0, down = downs > 0, start = starts > 0;
    // The pause menu: the d-pad walks it, and must not take the binoculars
    // out or put them away. The game's own flag for it: Start's command,
    // togglepaused (sub_82527280), flips a byte per local player at
    // 0x82C73024 (the one PmoveSingle stops the player's moves by), this
    // machine's player's first. (0.5.63 to 0.5.68 guessed at it from the
    // buttons and never saw it.) Presses made while it is set are dropped.
    const bool menu = Guest::Read8(base, 0x82C73024) != 0;
    static bool saidMenu = false;
    if (menu != saidMenu) { saidMenu = menu; printf("class: pause menu %s (binoculars' d-pad %s)\n", menu ? "open" : "closed", menu ? "off" : "on"); fflush(stdout); }
    (void)start;
    if (menu) return;
    if (RenderStats::Current().triangles < 20000.0f || !binocular) return;   // not in the menus
    // 0.5.82: d-pad down picks a killstreak when there is one to pick; d-pad
    // up then puts the binoculars away as well as taking them out.
    const bool streakDown = MpFixesStreakPad(ctx, base, down, rights);
    if (up && out)
    {
        zoomed = false;
        Input::SetBinoculars(false, false);
        g_launcherOut = false;
        OnWeapon(ctx, base, 2692, g_ownPrimary);
        static int said = 0;
        if (said++ < 20) { printf("class: d-pad up, binoculars away, to %s\n", g_ownPrimary); fflush(stdout); }
    }
    else if (up && !out)
    {
        OnWeapon(ctx, base, 2692, Binoculars);
        static int said = 0;
        if (said++ < 20) { printf("class: d-pad up, binoculars out (in hand %u)\n", held); fflush(stdout); }
    }
    else if (down && out && !streakDown)
    {
        zoomed = false;
        Input::SetBinoculars(false, false);
        g_launcherOut = false;
        OnWeapon(ctx, base, 2692, g_ownPrimary);
        static int said = 0;
        if (said++ < 20) { printf("class: d-pad down, binoculars away, to %s\n", g_ownPrimary); fflush(stdout); }
    }
}
// The artillery as a killstreak (the Artillery killstreak setting, on the
// host, who runs every player's game). Each player's kills in a row are
// counted where damage is dealt (G_Damage, sub_825862F8: the target r3,
// the attacker r5; the target's health at +776, a player's state at +512):
// a player whose health goes from above nothing to nothing has died, which
// ends his own run and adds one to his attacker's. Every killstreak_kills
// in a row earns a strike.
//
// The strike is the scout's own: his kit starts the script thread
// "ArtilleryDispenser" (sub_821F1E28, started by sub_821F3C60 through the
// script's thread call at +60 of 0x82A98030: the .bro's name, line 620, the
// function's name, the player and a 12-byte closure from sub_8221D2F0), which
// ends with the player's death, keeps the time the strike is ready in the
// player's variable 0x14D6461A (0: ready) and, once one is called, sets it to
// the game time plus three minutes. For a streak the thread is started if
// the player has none running, or the time set to now; once he has called
// it, it is set an hour away until his next streak, so it does not come back
// by itself. Earned strikes not yet called wait for his next life.
namespace
{
    struct Streak
    {
        uint32_t handle = 0;
        int kills = 0;        // in a row, this life
        int charges = 0;      // strikes earned, not yet called
        int typhoons = 0;     // of those, Typhoon runs
        int uavs = 0;         // UAVs earned, not yet called (0.5.82: kept until called)
        bool armed = false;   // a strike called from the killstreak box, waiting for the binoculars' mark
        bool typhoonCalled = false;   // that strike is a Typhoon run
        bool ours = false;    // the dispenser thread started by us, this life
        bool fired = false;   // 0.5.84: the armed strike's barrage came (the binoculars were fired)
        bool alive = true;
    };
    Streak g_streaks[24];
    bool g_streakBackToGun = false;   // this machine's strike was called: back to the main weapon
    bool g_streakScout[24] = {};   // the class's own dispenser (a scout): his own clock

    int ClientOf(uint8_t* base, uint32_t entity)
    {
        const uint32_t entities = Guest::Read32(base, 0x82BCC7FC);
        if (!entities || !entity) return -1;
        for (int i = 0; i < 24; i++) if (Guest::Read32(base, entities + 4 * uint32_t(i + 1)) == entity) return i;
        return -1;
    }

    uint32_t CallArgs(PPCContext& ctx, uint8_t* base, uint32_t function, uint32_t r3, uint32_t r4 = 0, uint32_t r5 = 0,
                      uint32_t r6 = 0, uint32_t r7 = 0, uint32_t r8 = 0)
    {
        PPCContext call = ctx;
        call.r1.u32 = (ctx.r1.u32 - 0x2000) & ~0xFu;
        call.r3.u64 = r3; call.r4.u64 = r4; call.r5.u64 = r5; call.r6.u64 = r6; call.r7.u64 = r7; call.r8.u64 = r8;
        call.lr = 0;
        Guest::Lookup(function)(call, base);
        return call.r3.u32;
    }

    // The player's ready time (variable 0x14D6461A), or 0 when he has none.
    uint32_t ReadySlot(PPCContext& ctx, uint8_t* base, uint32_t handle)
    {
        const uint32_t vars = CallArgs(ctx, base, 0x821DE680, handle);
        return vars ? CallArgs(ctx, base, 0x821DE958, vars, 0x14D6461A) : 0;
    }

    uint32_t GameTime(PPCContext& ctx, uint8_t* base)
    {
        const uint32_t function = Guest::Read32(base, 0x82A98030 + 1400);
        if (function < 0x82000000u || function >= 0x82600000u) return 0;
        return CallArgs(ctx, base, function, 0);
    }

    void StartDispenser(PPCContext& ctx, uint8_t* base, uint32_t handle)
    {
        const uint32_t thread = Guest::Read32(base, 0x82A98030 + 60);
        if (thread < 0x82000000u || thread >= 0x82600000u) return;
        static uint32_t word = 0;
        if (!word) word = Guest::AllocatePhysical(0x10);
        Guest::Write32(base, word, handle);
        const uint32_t closure = CallArgs(ctx, base, 0x8221D2F0, word);
        if (!closure) return;
        CallArgs(ctx, base, thread, 0x820396B4, 620, 0x8203A130, handle, closure, 0);
    }
}

// Whether this machine's player may fire his binoculars: a strike waiting
// (on the host, who counts); a joiner's machine does not count kills and
// leaves the trigger to the host's game.
namespace { bool g_damageSeen = false; }
bool MpFixesOwnStrikeReady()
{
    if (!g_damageSeen || !Settings::Get().killstreakArtillery) return true;
    const uint32_t entities = Guest::Read32(Guest::Base, 0x82BCC7FC);
    const uint32_t own = entities ? Guest::Read32(Guest::Base, entities + 4) : 0;
    const int i = ClientOf(Guest::Base, own);
    return i >= 0 && g_streaks[i].armed && g_streaks[i].charges > 0;
}

// Whether a barrage of this player's is a killstreak's (a strike we made
// ready, not a scout's own clock).
bool MpFixesStreakBarrage(uint32_t handle)
{
    for (int i = 0; i < 24; i++)
        if (g_streaks[i].handle == handle) return g_streaks[i].ours && !g_streakScout[i];
    return false;
}

bool MpFixesStreakArmed(uint32_t handle)
{
    for (const Streak& s : g_streaks) if (s.handle == handle && (s.ours || s.charges > 0)) return true;
    return false;
}

// The UAV: every enemy on the compass for a while. The compass draws the
// enemies itself (sub_825049C8 and the rest of its widgets) while the
// enemy_radar setting is 1 (its record at 0x82B2CF90, the value at +12);
// the game never sets it. It is this machine's own drawing, so the UAV
// shows on the earner's machine (the host's player, for now).
namespace
{
    uint64_t g_uavUntil = 0;
    constexpr uint32_t EnemyRadar = 0x82B2CF90;
}
void MpFixesUav(int seconds)
{
    g_uavUntil = GetTickCount64() + uint64_t(seconds) * 1000;
    Guest::Write32(Guest::Base, EnemyRadar + 12, 1);
    Overlay::Notice("UAV ONLINE  -  enemies on your compass for " + std::to_string(seconds) + " seconds", 4000);
    printf("killstreak: UAV on for %d seconds\n", seconds);
    fflush(stdout);
}
void MpFixesUavFrame()
{
    if (g_uavUntil && GetTickCount64() >= g_uavUntil)
    {
        g_uavUntil = 0;
        Guest::Write32(Guest::Base, EnemyRadar + 12, 0);
        Overlay::Notice("UAV offline", 2000);
        printf("killstreak: UAV off\n");
        fflush(stdout);
    }
}

// The artillery barrage twice as long for a killstreak's. The scout's
// barrage (sub_821F70A8: the caller's handle at r3, then two values passed
// on to each shell's thread) starts its shells' threads and returns; for a
// player whose barrage is a killstreak's, the same barrage is started again
// once the first has fallen (barrage_repeat_ms after, 8 seconds).
namespace
{
    struct Repeat { uint32_t handle = 0; uint64_t r4 = 0, r5 = 0, at = 0; };
    Repeat g_repeat;
    bool g_repeating = false;
    // The Typhoon's run (0.5.77): the barrage's target is a point passed by
    // value (r4 its x and y, the high half of r5 its z). A run is five
    // strikes one after another along the line from the caller to the spot
    // he marked, 300 units apart and centred on it, 0.9 seconds between.
    std::vector<Repeat> g_strafe;

    float BitsF(uint32_t w) { float f; memcpy(&f, &w, 4); return f; }
    uint32_t FBits(float f) { uint32_t w; memcpy(&w, &f, 4); return w; }
}
bool MpFixesStreakBarrage(uint32_t handle);
extern "C" PPC_FUNC(__imp__sub_821F70A8);
PPC_FUNC(sub_821F70A8)
{
    const uint32_t handle = ctx.r3.u32 ? Guest::Read32(base, ctx.r3.u32) : 0;
    static int said = 0;
    if (said++ < 40)
    {
        printf("killstreak: artillery barrage for handle %08X (r4 %016llX r5 %016llX: %.0f %.0f %.0f)%s\n", handle,
               (unsigned long long)ctx.r4.u64, (unsigned long long)ctx.r5.u64, BitsF(uint32_t(ctx.r4.u64 >> 32)),
               BitsF(uint32_t(ctx.r4.u64)), BitsF(uint32_t(ctx.r5.u64 >> 32)), g_repeating ? ", one we started" : "");
        fflush(stdout);
    }
    if (!g_repeating && handle)
    {
        int i = 0;
        while (i < 24 && g_streaks[i].handle != handle) i++;
        Streak* s = i < 24 ? &g_streaks[i] : nullptr;
        if (s && s->armed) s->fired = true;   // the call is spent when its barrage comes, not before
        if (s && s->typhoonCalled && s->typhoons > 0)
        {
            // A Typhoon run: the strikes along the line, no second half.
            s->typhoons--;
            s->typhoonCalled = false;
            const float tx = BitsF(uint32_t(ctx.r4.u64 >> 32)), ty = BitsF(uint32_t(ctx.r4.u64)), tz = BitsF(uint32_t(ctx.r5.u64 >> 32));
            const uint32_t entities = Guest::Read32(base, 0x82BCC7FC);
            const uint32_t entity = entities ? Guest::Read32(base, entities + 4 * uint32_t(i + 1)) : 0;
            float dx = 1, dy = 0;
            if (entity)
            {
                dx = tx - BitsF(Guest::Read32(base, entity + 336));
                dy = ty - BitsF(Guest::Read32(base, entity + 340));
                const float length = sqrtf(dx * dx + dy * dy);
                if (length > 1) { dx /= length; dy /= length; } else { dx = 1; dy = 0; }
            }
            const uint64_t now = GetTickCount64();
            for (int k = 1; k < 5; k++)
            {
                const float along = (float(k) - 2.0f) * 300.0f;
                const uint64_t r4 = (uint64_t(FBits(tx + dx * along)) << 32) | FBits(ty + dy * along);
                g_strafe.push_back({ handle, r4, ctx.r5.u64, now + uint64_t(k) * 900 });
            }
            // The first strike is this call itself, moved to the start of the line.
            ctx.r4.u64 = (uint64_t(FBits(tx - dx * 600.0f)) << 32) | FBits(ty - dy * 600.0f);
            printf("killstreak: client %d's Typhoon run at %.0f %.0f %.0f, heading %.2f %.2f\n", i, tx, ty, tz, dx, dy);
            fflush(stdout);
            const uint32_t own = entities ? Guest::Read32(base, entities + 4) : 0;
            if (own && own == entity) Overlay::Notice("TYPHOON INBOUND  -  rockets along your mark", 4000);
        }
        else if (MpFixesStreakBarrage(handle))
            g_repeat = { handle, ctx.r4.u64, ctx.r5.u64, GetTickCount64() + 8000 };
    }
    __imp__sub_821F70A8(ctx, base);
}
void MpFixesBarrageFrame(PPCContext& ctx, uint8_t* base)
{
    static uint32_t word = 0;
    if (!word) word = Guest::AllocatePhysical(0x10);
    auto start = [&](const Repeat& r)
    {
        Guest::Write32(base, word, r.handle);
        PPCContext call = ctx;
        call.r1.u32 = (ctx.r1.u32 - 0x2000) & ~0xFu;
        call.r3.u64 = word; call.r4.u64 = r.r4; call.r5.u64 = r.r5; call.lr = 0;
        g_repeating = true;
        Guest::Lookup(0x821F70A8)(call, base);
        g_repeating = false;
    };
    const uint64_t now = GetTickCount64();
    for (size_t k = 0; k < g_strafe.size();)
    {
        if (now < g_strafe[k].at) { k++; continue; }
        const Repeat r = g_strafe[k];
        g_strafe.erase(g_strafe.begin() + k);
        start(r);
        printf("killstreak: a Typhoon strike at %.0f %.0f\n", BitsF(uint32_t(r.r4 >> 32)), BitsF(uint32_t(r.r4)));
        fflush(stdout);
    }
    if (!g_repeat.handle || now < g_repeat.at) return;
    const Repeat r = g_repeat;
    g_repeat = Repeat{};
    start(r);
    printf("killstreak: the barrage's second half started\n");
    fflush(stdout);
}

extern "C" PPC_FUNC(__imp__sub_825862F8);
PPC_FUNC(sub_825862F8)
{
    const uint32_t target = ctx.r3.u32, attacker = ctx.r5.u32;
    const int32_t before = target ? int32_t(Guest::Read32(base, target + 776)) : 0;
    g_damageSeen = true;
    __imp__sub_825862F8(ctx, base);
    if (!target || before <= 0 || !Settings::Get().killstreakArtillery) return;
    const int32_t after = int32_t(Guest::Read32(base, target + 776));
    if (after > 0) return;
    const int victim = ClientOf(base, target);
    if (victim < 0) return;
    Streak& dead = g_streaks[victim];
    dead.kills = 0;
    dead.alive = false;
    dead.ours = false;   // his dispenser ends with him
    const int killer = ClientOf(base, attacker);
    if (killer < 0 || killer == victim) return;
    Streak& s = g_streaks[killer];
    s.kills++;
    const Settings::Values values = Settings::Get();
    const bool mine = killer == ClientOf(base, Guest::Read32(base, Guest::Read32(base, 0x82BCC7FC) + 4));
    printf("killstreak: client %d killed client %d, %d in a row\n", killer, victim, s.kills);
    // The ladder: the UAV, then the artillery, then the Typhoon, each once a
    // life at its count (as Call of Duty 4's).
    if (s.kills == values.killstreakUav)
    {
        s.uavs++;
        printf("killstreak: client %d earned the UAV\n", killer);
        if (mine) Overlay::Notice("UAV EARNED  -  d-pad right to call it", 3000);
    }
    if (s.kills == values.killstreakKills)
    {
        s.charges++;
        printf("killstreak: client %d earned an artillery barrage (%d waiting)\n", killer, s.charges);
        if (mine) Overlay::Notice("ARTILLERY EARNED  -  d-pad down to pick, d-pad right to call", 3000);
    }
    if (s.kills == values.killstreakTyphoon)
    {
        s.charges++;
        s.typhoons++;
        printf("killstreak: client %d earned the Typhoon (%d strikes waiting, %d of them runs)\n", killer, s.charges, s.typhoons);
        if (mine) Overlay::Notice("TYPHOON EARNED  -  d-pad down to pick, d-pad right to call", 3000);
    }
    if (mine) Overlay::ConsolePrint("killstreak: " + std::to_string(s.kills) + " in a row");
    fflush(stdout);
}

// streak_give <uav|artillery|typhoon> in the console (0.5.79, for testing on
// the host): the host's own player is given that killstreak as if earned.
namespace { std::atomic<int> g_streakAsked{0}; }
void MpFixesGiveStreak(const std::string& kind)
{
    const int what = kind == "uav" ? 1 : kind == "artillery" ? 2 : kind == "typhoon" ? 3 : 0;
    if (!what) { Overlay::ConsolePrint("streak_give: uav, artillery or typhoon"); return; }
    g_streakAsked.store(what);
}
void MpFixesGiveStreakFrame(uint8_t* base)
{
    const int what = g_streakAsked.exchange(0);
    if (!what) return;
    const uint32_t entities = Guest::Read32(base, 0x82BCC7FC);
    const int i = entities ? ClientOf(base, Guest::Read32(base, entities + 4)) : -1;
    if (i < 0) { Overlay::ConsolePrint("streak_give: you are not in a game"); return; }
    Streak& s = g_streaks[i];
    if (what == 1) s.uavs++;
    if (what >= 2) s.charges++;
    if (what == 3) s.typhoons++;
    printf("killstreak: streak_give %d for client %d (%d strikes waiting, %d runs)\n", what, i, s.charges, s.typhoons);
    fflush(stdout);
    Overlay::Notice(what == 3 ? "TYPHOON GIVEN" : what == 2 ? "ARTILLERY GIVEN" : "UAV GIVEN", 2000);
}

// Each frame on the host: strikes earned are made ready, called ones taken off.
void MpFixesKangarooTreads(uint8_t* base);
void MpFixesKillstreaks(PPCContext& ctx, uint8_t* base)
{
    MpFixesKangarooTreads(base);
    MpFixesUavFrame();
    MpFixesGiveStreakFrame(base);
    MpFixesBarrageFrame(ctx, base);
    if (!Settings::Get().killstreakArtillery) return;
    static uint32_t last = 0;
    const uint32_t ticks = GetTickCount();
    if (ticks - last < 250) return;
    last = ticks;
    const uint32_t entities = Guest::Read32(base, 0x82BCC7FC);
    if (!entities) return;
    const uint32_t now = GameTime(ctx, base);
    if (!now) return;
    for (int i = 0; i < 24; i++)
    {
        Streak& s = g_streaks[i];
        const uint32_t entity = Guest::Read32(base, entities + 4 * uint32_t(i + 1));
        const uint32_t ps = entity ? Guest::Read32(base, entity + 512) : 0;
        if (ps < 0x40000000u || ps >= 0x90000000u) { s = Streak{}; continue; }
        const uint32_t handle = Guest::Read32(base, entity + 484);
        if (handle != s.handle) { s = Streak{}; s.handle = handle; }
        if (int32_t(Guest::Read32(base, entity + 776)) > 0) s.alive = true;
        // 0.5.86: the other players' strikes (the host counts everyone's kills,
        // but only the host has the killstreak box): one earned is armed at
        // once, the spotter's binoculars given and put in his hands (every
        // other slot kept), and fired as the host's are.
        if (s.alive && !s.armed && s.charges > 0 && entity != Guest::Read32(base, entities + 4))
        {
            s.armed = true;
            s.typhoonCalled = s.typhoons > 0;
            uint8_t slots[10];
            for (int n = 0; n < 10; n++) slots[n] = Guest::Read8(base, ps + 1064 + n);
            OnWeaponFor(ctx, base, handle, 2664, "mp_binoculars");
            for (int n = 0; n < 10; n++) if (Guest::Read8(base, ps + 1064 + n) != slots[n]) Guest::Write8(base, ps + 1064 + n, slots[n]);
            OnWeaponFor(ctx, base, handle, 2692, "mp_binoculars");
            printf("killstreak: client %d's %s armed for him: the spotter's binoculars in his hands\n", i, s.typhoonCalled ? "Typhoon" : "artillery");
            fflush(stdout);
        }
        if (!s.alive || (!s.armed && !s.ours)) continue;
        uint32_t slot = ReadySlot(ctx, base, handle);
        if (s.armed && s.charges > 0 && !s.ours)
        {
            // 0.5.83: the dispenser started every time a strike is called from
            // the box. (Its ready time, the player's variable, outlives the
            // thread: 0.5.82 took a variable left from before for a scout's
            // running dispenser, started none, and the spotter's binoculars
            // came out with nothing to fire.)
            g_streakScout[i] = false;
            StartDispenser(ctx, base, handle);
            slot = ReadySlot(ctx, base, handle);
            s.ours = true;
            if (slot) Guest::Write32(base, slot, 0);
            printf("killstreak: client %d's artillery ready (%s, ready word %08X)\n", i, g_streakScout[i] ? "his own dispenser" : "dispenser started", slot);
            fflush(stdout);
        }
        if (!slot) continue;
        const uint32_t ready = Guest::Read32(base, slot);
        const bool waiting = ready != 0 && int32_t(ready - now) > 0;
        // 0.5.84: a call is spent only when its barrage comes (the barrage
        // hook sets fired). 0.5.83 took the dispenser's own first clock,
        // set as it starts, for the call: the charge went and the main
        // weapon came back before the binoculars were up.
        if (s.armed && s.fired)
        {
            s.charges--;
            s.armed = false;
            s.fired = false;
            printf("killstreak: client %d called his strike (%d more waiting)\n", i, s.charges);
            fflush(stdout);
            Guest::Write32(base, slot, now + 3600000u);
            if (entity == Guest::Read32(base, entities + 4)) g_streakBackToGun = true;
        }
        else if (s.armed && s.charges > 0)
        {
            // Armed: kept ready until fired, and a round kept in the spotter's
            // binoculars (0.5.83 set it once, as the switch began: the
            // binoculars came up with 0 and would not fire).
            if (ready != 0) Guest::Write32(base, slot, 0);
            // 0.5.85: written straight into his ammunition (the script's
            // clip call did not take for the binoculars): the weapon by its
            // name's hash (sub_820E10E0, sub_82480428), its clip's number
            // (+1468; the clips from +688 of the player's state) and its
            // ammunition's (+1460; from +320), as the launcher's rockets are.
            static uint32_t text = 0;
            static uint8_t spotter = 0;
            if (!text) text = Guest::AllocatePhysical(0x40);
            if (!spotter)
            {
                memset(base + text, 0, 0x40);
                memcpy(base + text, "mp_binoculars", 14);
                const uint32_t hash = Call3(ctx, base, 0x820E10E0, text, 0, 0);
                spotter = uint8_t(Call3(ctx, base, 0x82480428, hash, 0, 0));
                printf("killstreak: the spotter's binoculars are weapon %u\n", spotter);
                fflush(stdout);
            }
            const uint32_t list = Guest::Read32(base, 0x82BCBF9C);
            const uint32_t weapon = (spotter && list) ? Guest::Read32(base, list + 4 * spotter) : 0;
            if (weapon)
            {
                const uint32_t clip = Guest::Read32(base, weapon + 1468), ammo = Guest::Read32(base, weapon + 1460);
                if (clip > 0 && clip < 128 && Guest::Read32(base, ps + 4 * (clip + 172)) == 0) Guest::Write32(base, ps + 4 * (clip + 172), 1);
                if (ammo > 0 && ammo < 128 && Guest::Read32(base, ps + 4 * (ammo + 80)) == 0) Guest::Write32(base, ps + 4 * (ammo + 80), 1);
            }
        }
        else if (!s.armed && !g_streakScout[i] && !waiting) Guest::Write32(base, slot, now + 3600000u);
    }
}

// The killstreak box (0.5.82, as Call of Duty 4's and later): streaks earned
// are kept, not used at once. A box at the bottom right lists this machine's
// player's (the host's: the host counts), the one picked framed: d-pad down
// picks the next, d-pad right calls it. The UAV goes up at once; the
// artillery and the Typhoon take the spotter's binoculars out
// (mp_binoculars, which can mark a spot: the class binoculars,
// mp_binoculars_nospot, cannot, so a strike earned as a rifleman could not
// be called) and the strike falls where they are fired; then back to the
// main weapon.
namespace
{
    enum StreakKind { StreakUav, StreakArtillery, StreakTyphoon };
    struct StreakBox { bool active = false; int kills = 0; int counts[3] = {}; int picked = 0; bool armed = false; bool typhoon = false; };
    std::mutex g_streakBoxLock;
    StreakBox g_streakBox;
    int g_streakPicked = 0;
    constexpr const char* Spotter = "mp_binoculars";

    int StreakCount(const Streak& s, int kind)
    {
        return kind == StreakUav ? s.uavs : kind == StreakArtillery ? std::max(0, s.charges - s.typhoons) : s.typhoons;
    }

    bool StreakHudActive()
    {
        std::lock_guard<std::mutex> lock(g_streakBoxLock);
        return g_streakBox.active;
    }

    void StreakHudDraw(int width, int height)
    {
        StreakBox box;
        {
            std::lock_guard<std::mutex> lock(g_streakBoxLock);
            box = g_streakBox;
        }
        if (!box.active) return;
        ImDrawList* draw = ImGui::GetForegroundDrawList();
        ImFont* font = ImGui::GetFont();
        const float scale = height / 1080.0f;
        const float size = 22.0f * scale, small = 17.0f * scale, row = 34.0f * scale, w = 250.0f * scale;
        static const char* const names[3] = { "UAV", "ARTILLERY", "TYPHOON" };
        int rows = 0;
        for (int k = 0; k < 3; k++) if (box.counts[k] > 0) rows++;
        const float h = (rows + 1) * row + 14.0f * scale;
        const float x = width - w - 40.0f * scale, y = height - h - 230.0f * scale;
        const ImU32 back = IM_COL32(0, 0, 0, 150), edge = IM_COL32(210, 180, 110, 220), white = IM_COL32(255, 255, 255, 240),
                    dim = IM_COL32(200, 200, 200, 170), gold = IM_COL32(240, 200, 90, 255);
        draw->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), back, 6.0f * scale);
        draw->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), edge, 6.0f * scale, 0, 2.0f * scale);
        char line[64];
        snprintf(line, sizeof(line), "KILLSTREAK  %d", box.kills);
        draw->AddText(font, small, ImVec2(x + 12 * scale, y + 8 * scale), dim, line);
        float ry = y + row;
        for (int k = 0; k < 3; k++)
        {
            if (box.counts[k] <= 0) continue;
            const bool picked = k == box.picked;
            if (picked) draw->AddRect(ImVec2(x + 6 * scale, ry - 3 * scale), ImVec2(x + w - 6 * scale, ry + row - 7 * scale), gold, 4.0f * scale, 0, 2.0f * scale);
            snprintf(line, sizeof(line), "%s%s  x%d", picked ? "> " : "  ", names[k], box.counts[k]);
            draw->AddText(font, size, ImVec2(x + 14 * scale, ry), picked ? gold : white, line);
            ry += row;
        }
        const char* hint = box.armed ? (box.typhoon ? "MARK THE RUN: fire the binoculars" : "MARK THE SPOT: fire the binoculars")
                                     : "D-PAD DOWN pick   D-PAD RIGHT call";
        draw->AddText(font, small, ImVec2(x + 12 * scale, y + h + 4 * scale), dim, hint);
    }
}

// Each game frame (from the binoculars' d-pad code): the picks and calls,
// and the box's picture. True when d-pad down was the box's (a streak to
// pick from), so it does not also put the binoculars away.
bool MpFixesStreakPad(PPCContext& ctx, uint8_t* base, bool down, int rights)
{
    static bool registered = false;
    if (!registered) { registered = true; Overlay::SetStreakHud(StreakHudActive, StreakHudDraw); }
    const uint32_t entities = Guest::Read32(base, 0x82BCC7FC);
    const uint32_t own = entities ? Guest::Read32(base, entities + 4) : 0;
    const int i = own ? ClientOf(base, own) : -1;
    StreakBox box;
    bool usedDown = false;
    if (i >= 0 && Settings::Get().killstreakArtillery)
    {
        Streak& s = g_streaks[i];
        if (g_streakBackToGun)
        {
            g_streakBackToGun = false;
            OnWeapon(ctx, base, 2692, g_ownPrimary);
        }
        int total = 0;
        for (int k = 0; k < 3; k++) total += StreakCount(s, k);
        if (total > 0 && StreakCount(s, g_streakPicked) == 0)
            for (int k = 0; k < 3; k++) if (StreakCount(s, k) > 0) { g_streakPicked = k; break; }
        if (down && total > 0 && !s.armed)
        {
            usedDown = true;
            for (int step = 1; step <= 3; step++)
            {
                const int k = (g_streakPicked + step) % 3;
                if (StreakCount(s, k) > 0) { g_streakPicked = k; break; }
            }
        }
        if (rights > 0 && total > 0 && !s.armed && s.alive)
        {
            const int k = g_streakPicked;
            if (k == StreakUav && s.uavs > 0)
            {
                s.uavs--;
                MpFixesUav(Settings::Get().uavSeconds);
                printf("killstreak: own UAV called from the box\n");
            }
            else if ((k == StreakArtillery || k == StreakTyphoon) && StreakCount(s, k) > 0)
            {
                s.armed = true;
                s.typhoonCalled = k == StreakTyphoon;
                const uint32_t ps = Guest::Read32(base, own + 512);
                uint8_t slots[10];
                for (int n = 0; n < 10; n++) slots[n] = Guest::Read8(base, ps + 1064 + n);
                OnWeapon(ctx, base, 2664, Spotter);
                for (int n = 0; n < 10; n++) if (Guest::Read8(base, ps + 1064 + n) != slots[n]) Guest::Write8(base, ps + 1064 + n, slots[n]);
                OnWeapon(ctx, base, 2692, Spotter);
                OnWeapon(ctx, base, 2864, Spotter, 1);   // one round in them: the mark
                Overlay::Notice(k == StreakTyphoon ? "TYPHOON  -  fire the binoculars at the line's centre" : "ARTILLERY  -  fire the binoculars at the spot", 3000);
                printf("killstreak: own %s called from the box: the spotter's binoculars out\n", k == StreakTyphoon ? "Typhoon" : "artillery");
            }
            fflush(stdout);
        }
        box.active = total > 0 || s.armed || s.kills > 0;
        box.kills = s.kills;
        for (int k = 0; k < 3; k++) box.counts[k] = StreakCount(s, k);
        box.picked = g_streakPicked;
        box.armed = s.armed;
        box.typhoon = s.typhoonCalled;
    }
    // 0.5.86: a player who joined (his kills are counted on the host, which
    // arms his strike and puts the spotter's binoculars in his hands): while
    // he holds them, his box shows the strike waiting to be marked.
    if (!box.active && !g_damageSeen && g_heldSpotter)
    {
        box.active = true;
        box.counts[StreakArtillery] = 1;
        box.picked = StreakArtillery;
        box.armed = true;
    }
    {
        std::lock_guard<std::mutex> lock(g_streakBoxLock);
        g_streakBox = box;
    }
    return usedDown;
}

// Vehicles put down by hand (the spawnvehicle console command, the first
// step of the vehicle placement and the Kangaroo).
//
// A map's vehicles are entities of the class "script_vehicle": the entity is
// made (G_Spawn, sub_825638A0), its class name set (+548 the script string,
// sub_821DD8C8; +552 its id, sub_82548BC8), its origin (+336) and angles
// (+352), and the class's spawn function called through G_CallSpawnEntity
// (sub_8257DE38: the 53 class names' ids at 0x82BCCC68, their functions at
// 0x825FE818), as the script's spawn() does (sub_824CEAD8). The vehicle's
// (sub_82581190) reads its kind from the spawn's key/values ("vehicletype",
// level.spawnVars at 0x82C281D0: +176 the count, +180 pairs of the key's
// hash and the text) and sets it up (sub_8257F950) from the map's
// VEHICLEFILE entries: Merville and Poissons have mp_sherman(tank),
// mp_panzeriv, mp_horch, mp_wc51, mp_motorcycle_bmw and _harley. A map
// without that entry has nothing to set it up from (said in the log).
//
// The console's command is taken before the game's buffer (kernel_video.cpp)
// and done here, in the server's frame, on the host only.
namespace
{
    std::mutex g_vehicleMutex;
    bool g_inMapStart = false;   // vehicles made in the map's start: the script's own setup pass comes after

    const char* VehicleType(const std::string& asked)
    {
        static const struct { const char* name; const char* type; } names[] = {
            { "sherman", "mp_shermantank" }, { "tank", "mp_shermantank" }, { "mp_sherman", "mp_shermantank" },
            { "panzer", "mp_panzeriv" }, { "panzeriv", "mp_panzeriv" }, { "horch", "mp_horch" },
            { "wc51", "mp_wc51" }, { "jeep", "mp_wc51" }, { "truck", "mp_wc51" },
            { "bmw", "mp_motorcycle_bmw" }, { "harley", "mp_motorcycle_harley" }, { "motorcycle", "mp_motorcycle_harley" },
            { "kangaroo", "mp_kangaroo" }, { "apc", "mp_kangaroo" },
        };
        for (const auto& n : names) if (_stricmp(asked.c_str(), n.name) == 0) return n.type;
        // A vehicle file's name without its "mp_" (motorcycle_harley, wc51).
        static std::string prefixed;
        prefixed = "mp_" + asked;
        for (const auto& n : names) if (_stricmp(prefixed.c_str(), n.type) == 0) return n.type;
        return nullptr;
    }

    // The model a kind's map entity carries (Merville's script_vehicle
    // entities: their "model"). The entity needs it set before its spawn
    // function, as the map's loading does: 0.5.59's had none, and were
    // made but never seen.
    const char* VehicleModel(const std::string& type)
    {
        static const struct { const char* type; const char* model; } models[] = {
            { "mp_shermantank", "v_shermanm4a3" }, { "mp_panzeriv", "v_panzer_iv" }, { "mp_horch", "v_horch1a" },
            { "mp_wc51", "v_wc51" }, { "mp_motorcycle_bmw", "v_bmw" }, { "mp_motorcycle_harley", "v_harley" },
            { "mp_kangaroo", "v_Kangaroo" },
        };
        for (const auto& m : models) if (_stricmp(type.c_str(), m.type) == 0) return m.model;
        return nullptr;
    }

    float ReadF(uint8_t* base, uint32_t at) { const uint32_t w = Guest::Read32(base, at); float f; memcpy(&f, &w, 4); return f; }
    void WriteF(uint8_t* base, uint32_t at, float f) { uint32_t w; memcpy(&w, &f, 4); Guest::Write32(base, at, w); }

    // A vehicle of a kind (its vehicle file's name) at a place, facing yaw;
    // true when it set up (its own vehicle record).
    bool SpawnVehicleAt(PPCContext& ctx, uint8_t* base, const std::string& type, float x, float y, float z, float yaw)
    {

        // Text for the game: the class name, the kind, the key's name.
        static uint32_t scratch = 0;
        if (!scratch) scratch = Guest::AllocatePhysical(0x200);
        memset(base + scratch, 0, 0x200);
        const uint32_t classText = scratch + 0x40, typeText = scratch + 0x80, keyText = scratch + 0xC0;
        memcpy(base + classText, "script_vehicle", 15);
        memcpy(base + typeText, type.c_str(), std::min<size_t>(type.size(), 0x3E) + 1);
        memcpy(base + keyText, "vehicletype", 12);

        // Is the kind set up on this map (its vehicle file read: sub_8255A310,
        // the kind's number, -1 none; the names at 0x82B2DC90, their count
        // at 0x82B34594)? And is a vehicle record free (the map's pool,
        // level +3088 of them, 1872 bytes each at 0x82BCBDB4; one is in use
        // while its +368 is a live entity's handle)? 0.5.61's vehicles all
        // took the same record: the kind was not found, the record was
        // cleared and left free, and the entity stayed a model only.
        // The Harley's model (v_harley) is only in Poissons' archive: on
        // Merville only its wreck is, and a Harley put down there was
        // nothing to see (0.5.66).
        if (_stricmp(type.c_str(), "mp_motorcycle_harley") == 0 && Kernel::CurrentMap() != "poissons")
        {
            Overlay::ConsolePrint("spawnvehicle: the Harley's model is only on Poissons; here try bmw (the German motorcycle)");
            printf("vehicles: no Harley model on %s\n", Kernel::CurrentMap().c_str());
            fflush(stdout);
            return false;
        }
        const int32_t kind = int32_t(Call3(ctx, base, 0x8255A310, typeText, 0, 0));
        if (kind < 0)
        {
            std::string known;
            const int count = int(int16_t(Guest::Read16(base, 0x82B34594)));
            for (int i = 0; i < count && i < 32; i++)
            {
                const uint32_t name = Guest::Read32(base, 0x82B2DC90 + 4 * uint32_t(i));
                known += (known.empty() ? "" : ", ") + (name ? ReadText(base, name, 32) : std::string("?"));
            }
            printf("vehicles: spawnvehicle %s: not set up on this map (%d kinds: %s)\n", type.c_str(), count, known.c_str());
            fflush(stdout);
            Overlay::ConsolePrint("spawnvehicle: " + type + " is not on this map. Here: " + (known.empty() ? std::string("none") : known));
            return false;
        }
        {
            const uint32_t pool = Guest::Read32(base, 0x82BCBDB4);
            const int records = int(Guest::Read16(base, 0x82C281D0 + 3088));
            int used = 0;
            for (int i = 0; pool && i < records; i++)
            {
                const uint32_t handle = Guest::Read32(base, pool + 1872 * uint32_t(i) + 368);
                const uint32_t index = handle & 0x3FFF;
                if (index >= 4096) continue;
                const uint32_t entry = 0x82BD1FC0 + 8 * index;
                if ((handle >> 14) == Guest::Read32(base, entry + 4) && Guest::Read32(base, entry) != 0) used++;
            }
            if (used >= records)
            {
                printf("vehicles: spawnvehicle %s: all %d vehicle records in use\n", type.c_str(), records);
                fflush(stdout);
                Overlay::ConsolePrint("spawnvehicle: the maximum number of vehicles (" + std::to_string(records) + ") is reached");
                return false;
            }
        }

        // The spawn's key/values: only the kind, for this one spawn.
        constexpr uint32_t Vars = 0x82C281D0;
        const uint32_t savedCount = Guest::Read32(base, Vars + 176);
        const uint32_t savedKey = Guest::Read32(base, Vars + 180), savedValue = Guest::Read32(base, Vars + 184);
        const uint32_t keyHash = Call3(ctx, base, 0x820E10E0, keyText, 0, 0);
        Guest::Write32(base, Vars + 176, 1);
        Guest::Write32(base, Vars + 180, keyHash);
        Guest::Write32(base, Vars + 184, typeText);

        const uint32_t ent = Call3(ctx, base, 0x825638A0, 0xFFFFFFFFu, 0, 0);
        uint32_t made = 0;
        if (ent)
        {
            Call3(ctx, base, MakeText, scratch, classText, 0);
            Call3(ctx, base, 0x821DD8C8, ent + 548, scratch, 0);
            Call3(ctx, base, FreeText, scratch, 0, 0);
            const uint32_t id = Call3(ctx, base, 0x82548BC8, scratch + 0x10, ent + 548, 0);
            Guest::Write32(base, ent + 552, id ? Guest::Read32(base, id) : 0);
            WriteF(base, ent + 336, x); WriteF(base, ent + 340, y); WriteF(base, ent + 344, z);
            WriteF(base, ent + 352, 0.0f); WriteF(base, ent + 356, yaw); WriteF(base, ent + 360, 0.0f);
            Guest::Write32(base, ent + 616, 0);
            // Its model, through the script's SetModel (sub_824CE130: the
            // entity's script handle +484, a script string of the name).
            if (const char* model = VehicleModel(type))
            {
                const uint32_t modelText = scratch + 0x100;
                memcpy(base + modelText, model, strlen(model) + 1);
                Call3(ctx, base, MakeText, scratch + 0x20, modelText, 0);
                Call3(ctx, base, 0x824CE130, Guest::Read32(base, ent + 484), scratch + 0x20, 0);
                Call3(ctx, base, FreeText, scratch + 0x20, 0, 0);
                printf("vehicles: model %s set\n", model);
            }
            made = Call3(ctx, base, 0x8257DE38, ent, 0, 0);
        }
        Guest::Write32(base, Vars + 176, savedCount);
        Guest::Write32(base, Vars + 180, savedKey);
        Guest::Write32(base, Vars + 184, savedValue);
        const uint32_t record = ent ? Guest::Read32(base, ent + 524) : 0;
        const bool bound = record && Guest::Read32(base, record + 368) == Guest::Read32(base, ent + 484);
        printf("vehicles: spawnvehicle %s (kind %d) at %.0f %.0f %.0f (yaw %.0f): entity %08X, %s, vehicle record %08X %s\n",
               type.c_str(), kind, x, y, z, yaw, ent, made ? "spawned" : "the game refused it", record, bound ? "its own" : "NOT bound to it");
        fflush(stdout);
        // The multiplayer script's own setup of each map vehicle
        // (sub_821F2B40, from _mp_tankdrive.bro: run by sub_821FD058 over
        // getentarray("script_vehicle") at the map's start): its damage
        // effects, blowing up when flipped or under the world, and what
        // lets players get in and man its gun. A vehicle put down later
        // missed it: it stood there, not to be driven.
        // A map vehicle beside ours, word by word where they differ (the
        // entity, then the vehicle record): what a vehicle put down here
        // still lacks to be driven, manned and to sit on its wheels (0.5.70:
        // they hover and cannot be used, the map's own can).
        if (bound)
        {
            const uint32_t pool = Guest::Read32(base, 0x82BCBDB4);
            const int records = int(Guest::Read16(base, 0x82C281D0 + 3088));
            for (int i = 0; pool && i < records; i++)
            {
                const uint32_t theirs = pool + 1872 * uint32_t(i);
                if (theirs == record) continue;
                const uint32_t handle = Guest::Read32(base, theirs + 368), index = handle & 0x3FFF;
                if (index >= 4096) continue;
                const uint32_t entry = 0x82BD1FC0 + 8 * index;
                if ((handle >> 14) != Guest::Read32(base, entry + 4)) continue;
                const uint32_t other = Guest::Read32(base, entry);
                if (!other) continue;
                printf("vehicles: ours %08X (record %08X) against the map's %08X (record %08X, kind %d):\n", ent, record, other, theirs,
                       int(int16_t(Guest::Read16(base, theirs + 376))));
                int lines = 0;
                for (uint32_t w = 0; w < 1000 && lines < 120; w += 4)
                {
                    const uint32_t a = Guest::Read32(base, ent + w), b = Guest::Read32(base, other + w);
                    if (a != b) { printf("vehicles:   entity +%u ours %08X map's %08X\n", w, a, b); lines++; }
                }
                for (uint32_t w = 0; w < 1872 && lines < 260; w += 4)
                {
                    const uint32_t a = Guest::Read32(base, record + w), b = Guest::Read32(base, theirs + w);
                    if (a != b) { printf("vehicles:   record +%u ours %08X map's %08X\n", w, a, b); lines++; }
                }
                fflush(stdout);
                break;
            }
        }
        if (bound && !g_inMapStart)
        {
            static uint32_t word = 0;
            if (!word) word = Guest::AllocatePhysical(0x10);
            Guest::Write32(base, word, Guest::Read32(base, ent + 484));
            Call3(ctx, base, 0x821F2B40, word, 0, 0);
            printf("vehicles: the script's vehicle setup run for it\n");
            fflush(stdout);
        }
        Overlay::ConsolePrint("spawnvehicle: " + type + (bound ? " put down" : " was made but did not set up (see the log)"));
        return bound;
    }
}

// The vehicle placement file (vehicles.txt beside the executable): one
// vehicle a line, "<map> <kind> <x> <y> <z> <yaw>". The host puts each of a
// map's down a few seconds into every game on it; vehicle_mark <kind> puts
// one down in front of you and adds it, vehicle_clear forgets the map's,
// vehicle_list says them. Only the machine running the game (the host,
// svs's clients at 0x82B3FDE8 there) makes vehicles: the others are told by
// the game itself, as for any entity.
namespace
{
    struct VehicleOrder { int what = 0; std::string kind; };   // 1 spawn, 2 mark, 3 clear, 4 list
    std::vector<VehicleOrder> g_vehicleOrders;
    bool g_placementDue = false;

    std::string PlacementFile()
    {
        char path[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, path, MAX_PATH);
        std::string file(path);
        const size_t slash = file.find_last_of("\\/");
        return (slash == std::string::npos ? std::string() : file.substr(0, slash + 1)) + "vehicles.txt";
    }

    struct Placed { std::string map, kind; float x, y, z, yaw; };
    std::vector<Placed> ReadPlacements()
    {
        std::vector<Placed> all;
        if (FILE* file = fopen(PlacementFile().c_str(), "r"))
        {
            char line[256];
            while (fgets(line, sizeof(line), file))
            {
                char map[64] = {}, kind[64] = {};
                Placed p{};
                if (line[0] == '#') continue;
                if (sscanf(line, "%63s %63s %f %f %f %f", map, kind, &p.x, &p.y, &p.z, &p.yaw) == 6) { p.map = map; p.kind = kind; all.push_back(p); }
            }
            fclose(file);
        }
        return all;
    }

    void WritePlacements(const std::vector<Placed>& all)
    {
        if (FILE* file = fopen(PlacementFile().c_str(), "w"))
        {
            fprintf(file, "# Vehicles put down at the start of each game on a map: map kind x y z yaw (vehicle_mark <kind> in the console adds one)\n");
            for (const Placed& p : all) fprintf(file, "%s %s %.0f %.0f %.0f %.0f\n", p.map.c_str(), p.kind.c_str(), p.x, p.y, p.z, p.yaw);
            fclose(file);
        }
    }

    bool HostHere(uint8_t* base) { return Guest::Read32(base, 0x82B3FDE0 + 8) != 0; }

    std::string MapNow()
    {
        std::string map = Kernel::CurrentMap();
        for (char& c : map) c = char(tolower(static_cast<unsigned char>(c)));
        return map;
    }

    // In front of this machine's player: 220 units along his facing.
    bool InFront(uint8_t* base, float& x, float& y, float& z, float& yaw)
    {
        const uint32_t entities = Guest::Read32(base, 0x82BCC7FC);
        const uint32_t own = entities ? Guest::Read32(base, entities + 4) : 0;
        if (!own || !g_ownHandle) return false;
        // The view's yaw: the player state's +208 (as the bots aim by it).
        // (0.5.64 took the entity's +356, which stayed 0 in a devmap: every
        // vehicle went east of the player, out of sight.)
        const uint32_t ps = Guest::Read32(base, own + 512);
        yaw = ps ? ReadF(base, ps + 208) : ReadF(base, own + 356);
        printf("vehicles: facing: state +204 %.1f +208 %.1f, entity +352 %.1f +356 %.1f\n", ps ? ReadF(base, ps + 204) : 0.0f,
               yaw, ReadF(base, own + 352), ReadF(base, own + 356));
        const float rad = yaw * 3.14159265f / 180.0f;
        x = ReadF(base, own + 336) + 220.0f * cosf(rad);
        y = ReadF(base, own + 340) + 220.0f * sinf(rad);
        z = ReadF(base, own + 344) + 16.0f;
        return true;
    }

    std::string KindOf(const std::string& asked)
    {
        if (const char* known = VehicleType(asked)) return known;
        return asked;
    }
}

// Called by the console (kernel_video.cpp): spawnvehicle, vehicle_mark,
// vehicle_clear, vehicle_list.
void MpFixesPlacementDue() { g_placementDue = true; }
void MpFixesAskVehicle(const std::string& kind)
{
    std::lock_guard<std::mutex> lock(g_vehicleMutex);
    g_vehicleOrders.push_back({ 1, kind.empty() ? std::string("sherman") : kind });
}
void MpFixesVehicleOrder(int what, const std::string& kind)
{
    std::lock_guard<std::mutex> lock(g_vehicleMutex);
    g_vehicleOrders.push_back({ what, kind });
}

void MpFixesKangarooProbe(PPCContext& ctx, uint8_t* base);
void MpFixesKangarooWhere(uint8_t* base);
void MpFixesVehicles(PPCContext& ctx, uint8_t* base)
{
    std::vector<VehicleOrder> orders;
    {
        std::lock_guard<std::mutex> lock(g_vehicleMutex);
        orders.swap(g_vehicleOrders);
    }
    for (const VehicleOrder& order : orders)
    {
        const std::string map = MapNow();
        if (order.what == 3 || order.what == 4)
        {
            std::vector<Placed> all = ReadPlacements(), kept;
            int mine = 0;
            for (const Placed& p : all)
            {
                if (p.map == map)
                {
                    mine++;
                    if (order.what == 4)
                        Overlay::ConsolePrint("  " + p.kind + " at " + std::to_string(int(p.x)) + " " + std::to_string(int(p.y)) + " " + std::to_string(int(p.z)));
                    if (order.what == 3) continue;
                }
                kept.push_back(p);
            }
            if (order.what == 3) { WritePlacements(kept); Overlay::ConsolePrint("vehicle_clear: " + std::to_string(mine) + " placed vehicles of " + map + " forgotten (from its next game)"); }
            else Overlay::ConsolePrint("vehicle_list: " + std::to_string(mine) + " placed on " + map + " (" + PlacementFile() + ")");
            continue;
        }
        if (!HostHere(base))
        {
            Overlay::ConsolePrint("spawnvehicle: only the host can put vehicles down");
            continue;
        }
        float x, y, z, yaw;
        if (!InFront(base, x, y, z, yaw))
        {
            Overlay::ConsolePrint("spawnvehicle: you are not in a game");
            continue;
        }
        const std::string kind = KindOf(order.kind);
        const bool made = SpawnVehicleAt(ctx, base, kind, x, y, z, yaw);
        if (order.what == 2 && made)
        {
            std::vector<Placed> all = ReadPlacements();
            all.push_back({ map, kind, x, y, z, yaw });
            WritePlacements(all);
            Overlay::ConsolePrint("vehicle_mark: " + kind + " saved for " + map + ". Type map_restart: the saved ones are made with the map's own, drivable");
        }
    }
    // (The map's placed vehicles are put down in the map's start itself,
    // sub_82564520 below.)
    MpFixesKangarooProbe(ctx, base);
    MpFixesKangarooWhere(base);
}

void MpFixesLauncher(PPCContext& ctx, uint8_t* base)
{
    MpFixesVehicles(ctx, base);
    MpFixesKillstreaks(ctx, base);
    MpFixesCampaignGuns(ctx, base);
    // Picking up a gun puts down the weapon in the second slot ("primaryb",
    // +1066: sub_82502850 and the pick-up itself), whatever is in hand; a
    // custom class's second weapon (the pistol, a main weapon kind here)
    // sits there, so the pistol always went. The weapon in hand (+164 of
    // the player's state) is kept in that slot instead: whenever it is the
    // first slot's (+1065), the two swap places. What is picked up then
    // takes the place of the weapon held, for every player on this machine.
    {
        const uint32_t entities = Guest::Read32(base, 0x82BCC7FC);
        for (uint32_t client = 0; entities && client < 24; client++)
        {
            const uint32_t entity = Guest::Read32(base, entities + 4 * (client + 1));
            const uint32_t ps = entity ? Guest::Read32(base, entity + 512) : 0;
            // (The players' states are in the title's own data, 0x82..., not
            // the heap: 0.5.37 skipped them all.)
            if (ps < 0x40000000u || ps >= 0x90000000u) continue;
            // In hand: the entity's byte +3 (the host's own carries it there,
            // bots.cpp), else its state's +164 (0.5.36 used only that, and
            // nothing ever swapped).
            uint32_t held = Guest::Read8(base, entity + 3);
            if (held == 0) held = Guest::Read32(base, ps + 164);
            {
                static uint32_t lastHeld[24] = {}, lastState[24] = {};
                const uint32_t state = Guest::Read32(base, ps + 164);
                static int said = 0;
                if ((held != lastHeld[client] || state != lastState[client]) && said < 80)
                {
                    said++;
                    lastHeld[client] = held; lastState[client] = state;
                    printf("slots: client %u in hand %u (entity +3 %u, state +164 %u, +196 %u), slots %u and %u\n", client, held,
                           Guest::Read8(base, entity + 3), state, Guest::Read32(base, ps + 196), Guest::Read8(base, ps + 1065), Guest::Read8(base, ps + 1066));
                    fflush(stdout);
                }
            }
            const uint8_t first = Guest::Read8(base, ps + 1065), second = Guest::Read8(base, ps + 1066);
            if (held != 0 && held < 128 && first == held && second != 0 && second < 128 && second != held)
            {
                Guest::Write8(base, ps + 1065, second);
                Guest::Write8(base, ps + 1066, first);
            }
        }
    }
    // The player's weapon slots (+1064: none, primary, primaryb, pistol,
    // ...) whenever they change, each weapon with its own slot kind (+180):
    // to see why picking up a Sten put down the pistol and not the FG42.
    {
        const uint32_t entities = Guest::Read32(base, 0x82BCC7FC);
        const uint32_t own = entities ? Guest::Read32(base, entities + 4) : 0;
        const uint32_t ps = own ? Guest::Read32(base, own + 512) : 0;
        static uint8_t last[8] = {};
        static int said = 0;
        if (ps && said < 60)
        {
            uint8_t now[8];
            for (int i = 0; i < 8; i++) now[i] = Guest::Read8(base, ps + 1064 + i);
            if (memcmp(now, last, 8) != 0)
            {
                memcpy(last, now, 8);
                said++;
                const uint32_t list = Guest::Read32(base, 0x82BCBF9C);
                printf("slots:");
                for (int i = 0; i < 8; i++)
                {
                    const uint32_t weapon = (now[i] && list) ? Guest::Read32(base, list + 4 * now[i]) : 0;
                    printf(" [%d] %u %s (kind %u)", i, now[i], weapon ? ReadText(base, Guest::Read32(base, weapon + 8), 24).c_str() : "-",
                           weapon ? Guest::Read32(base, weapon + 180) : 0);
                }
                printf("\n");
                fflush(stdout);
            }
        }
    }
    if (g_launcherDue)
    {
        g_launcherDue = false;
        g_launcherOut = false;
        // Not again for an anti-armour class, whose main weapon it is.
        const uint32_t entities = Guest::Read32(base, 0x82BCC7FC);
        const uint32_t own = entities ? Guest::Read32(base, entities + 4) : 0;
        const uint32_t ps = own ? Guest::Read32(base, own + 512) : 0;
        if (ps && strcmp(g_ownPrimary, Launcher()) != 0)
        {
            // The player's weapon by slot: a byte each at +1064 (slot 1 the
            // "primary", the table of slot names at 0x825D7804). Giving the
            // launcher puts it in the "primary" slot; the main weapon is put
            // back there, so the launcher is carried besides, in no slot.
            // Every slot as it was before (the launcher's own slot may be
            // the pistol's: given, it took the pistol's place).
            uint8_t slots[10];
            for (int i = 0; i < 10; i++) slots[i] = Guest::Read8(base, ps + 1064 + i);
            const uint8_t mainInSlot = slots[1];
            OnWeapon(ctx, base, 2664, Launcher());
            for (int i = 0; i < 10; i++)
                if (Guest::Read8(base, ps + 1064 + i) != slots[i])
                {
                    printf("class: the launcher took slot %d (weapon %u, was %u): put back\n", i, Guest::Read8(base, ps + 1064 + i), slots[i]);
                    Guest::Write8(base, ps + 1064 + i, slots[i]);
                }
            // The launcher's weapon number by its name, as the script's give
            // finds it (sub_824BA800): the name's hash (sub_820E10E0), then
            // the weapon with that hash (sub_82480428).
            static uint32_t text = 0;
            if (!text) text = Guest::AllocatePhysical(0x40);
            memset(base + text, 0, 0x40);
            memcpy(base + text, Launcher(), strlen(Launcher()) + 1);
            const uint32_t hash = Call3(ctx, base, 0x820E10E0, text, 0, 0);
            const uint8_t launcher = uint8_t(Call3(ctx, base, 0x82480428, hash, 0, 0));
            // Its rockets, written as the script's ammunition calls do
            // (sub_824B8A88, sub_824B8CC8) but for the launcher itself: its
            // weapon (the list at 0x82BCBF9C) gives its clip's number (+1468)
            // and its ammunition's (+1460); the clips from +688 of the
            // player's state, the ammunition from +320, each up to its most
            // (0x82B93680, 0x82B8C988).
            const uint32_t list = Guest::Read32(base, 0x82BCBF9C);
            const uint32_t weapon = (launcher && list) ? Guest::Read32(base, list + 4 * launcher) : 0;
            int clipGiven = -1, spareGiven = -1;
            if (weapon && launcher != mainInSlot)
            {
                const uint32_t clip = Guest::Read32(base, weapon + 1468), ammo = Guest::Read32(base, weapon + 1460);
                if (clip > 0 && clip < 128)
                {
                    clipGiven = std::min<int>(int32_t(Guest::Read32(base, 0x82B93680 + 4 * clip)), 1);
                    Guest::Write32(base, ps + 4 * (clip + 172), uint32_t(clipGiven));
                }
                if (Guest::Read32(base, weapon + 1820) != 0 && clip > 0 && clip < 128)
                {
                    // A weapon whose rockets are all in its "clip" (+1820 set:
                    // the spare call writes the clip, up to 0x82B8C988 +27896).
                    spareGiven = std::min<int>(int32_t(Guest::Read32(base, 0x82B8C988 + 27896 + 4 * clip)), 5);
                    Guest::Write32(base, ps + 4 * (clip + 172), uint32_t(spareGiven));
                }
                else if (ammo > 0 && ammo < 128)
                {
                    spareGiven = std::min<int>(int32_t(Guest::Read32(base, 0x82B8C988 + 4 * ammo)), 4);
                    Guest::Write32(base, ps + 4 * (ammo + 80), uint32_t(spareGiven));
                }
            }
            static int said = 0;
            if (said++ < 20)
            {
                printf("class: own spawn, %s given as equipment (main weapon %s, slot %u kept, launcher %u, clip %d, spare %d)\n",
                       Launcher(), g_ownPrimary, mainInSlot, launcher, clipGiven, spareGiven);
                fflush(stdout);
            }
        }
    }
    MpFixesBinoculars(ctx, base);
    // D-pad left: the launcher out, or back to the main weapon.
    static uint16_t last = 0;
    const uint16_t buttons = Input::Buttons();
    const bool pressed = (buttons & 0x0004) && !(last & 0x0004);
    last = buttons;
    if (!pressed || !Settings::Get().launcher || !g_ownHandle || g_ownPrimary[0] == 0) return;
    if (RenderStats::Current().triangles < 20000.0f) return;   // not in the menus
    g_launcherOut = !g_launcherOut;
    OnWeapon(ctx, base, 2692, g_launcherOut ? Launcher() : g_ownPrimary);
    static int said = 0;
    if (said++ < 20) { printf("class: d-pad left, to %s\n", g_launcherOut ? Launcher() : g_ownPrimary); fflush(stdout); }
}
#endif

// The campaign's guns in multiplayer.
//
// A weapon is set up from its WEAPONFILE entry in the map's archive
// (sub_8249A380: its name, the entry), into a record that names its models
// and animations as text: the view model at +20, the arms' animations from
// +72 to +164, the world model at +1432, the HUD icon at +1440. A campaign
// gun is made as a copy of a like multiplayer one (the Lee-Enfield of the
// Kar98, set up again under its own name from the Kar98's entry), with the
// campaign's model and animation names put in. Their assets come from a
// campaign level's weapons archive, loaded with the multiplayer's own
// (sub_8247DD38, below).
#ifdef COD3_MP
extern "C" PPC_FUNC(__imp__sub_8249A380);
namespace
{
    // Guest text, kept for good.
    uint32_t GuestText(uint8_t* base, const std::string& text)
    {
        static uint32_t pool = 0, used = 0;
        if (!pool || used + text.size() + 1 > 0x4000) { pool = Guest::AllocatePhysical(0x4000); used = 0; }
        const uint32_t at = pool + used;
        memcpy(base + at, text.c_str(), text.size() + 1);
        used += uint32_t(text.size() + 1 + 3) & ~3u;
        return at;
    }

    std::string ReadText(uint8_t* base, uint32_t at, size_t most = 64)
    {
        std::string text;
        for (size_t i = 0; at && i < most; i++) { const char c = char(Guest::Read8(base, at + uint32_t(i))); if (!c) break; text += c; }
        return text;
    }

    struct CampaignGun
    {
        const char* name;       // the new weapon's
        const char* from;       // the multiplayer weapon it is a copy of
        const char* title;      // its name's text key
        const char* viewModel;
        const char* worldModel;
        const char* hud;
        const char* armsFrom;   // in the copy's animation names, this
        const char* armsTo;     // becomes this
        int clipSize;           // its own clip, if not the copy's (0)
        int reloadAdd;          // with a reload in parts: rounds put in by each
        float reloadStart, reloadEach, reloadEnd;   // the parts' animations' lengths, in seconds
        float reloadWhole;      // a reload in one: its animation's length, in seconds (0: the copy's time)
        int dropPercent;        // the copy's time to put it away, in percent (100: as it is)
        int raisePercent;       // the copy's time to take it out, in percent
        int archive;            // which campaign weapons archive has it (CampaignArchives)
        const char* shot = nullptr; // its gunshot sounds' prefix in the campaign banks (<shot>_FIRE_1ST, _3RD)
        const char* scopeFrom = nullptr; // a scoped gun whose scope (its overlay and zoom) it takes
    };
    // The campaign weapons archives the guns come from: St Lambert's (the
    // Lee-Enfield, the Bren) and Fuel Plant's (the Lee-Enfield, the Sten, the
    // FG42; 11 banks of 3 MB, only with big_memory).
    const char* const CampaignArchives[] = { "sp\\mace2\\weapons.cod", "sp\\fuelplnt\\weapons.cod" };
    constexpr int ArchiveCount = int(sizeof(CampaignArchives) / sizeof(CampaignArchives[0]));
    const CampaignGun Guns[] = {
        { "lee_enfield", "kar98", "WEAPON_LEE_ENFIELD", "lee_enfield", "w_lee_Enfield_WORLD", "hud_Lee_enfield", "geRFL", "brRFL",
          // Ten rounds, put in as two stripper clips of five: the bolt opened
          // (w_brRFL_reload_start2, 1.3 s), a clip pushed in (w_brRFL_reload,
          // 0.8 s) for each, the bolt closed (w_brRFL_reload_end2, 1.13 s).
          // Its drop hides the rifle early and its raise shows it late: 30%
          // and 70% of the Kar98's times.
          10, 5, 1.3f, 0.8f, 1.1333f, 0.0f, 30, 70, 0, "LEE_ENFIELD" },
        // The Bren: a copy of the BAR (automatic, the same handling), its
        // animations the campaign's "bren" family (the BAR's are "usBAR",
        // the same moves, no bolt), a 30-round magazine. Its reload takes as
        // long as its campaign sound (GR_BREN in stbert.wbk, 3.73 s; the
        // game's reloads match their sounds: the BAR's 4.0 s to GR_BAR's
        // 3.9, the Thompson's 2.3 to GR_THOMPSON's 2.33): 3.8 s, its
        // animation (3.53 s) a little slower.
        { "bren", "bar", "WEAPON_BREN", "w_bren", "w_bren_WORLD", "hud_bren", "usBAR", "bren",
          30, 0, 0.0f, 0.0f, 0.0f, 3.8f, 30, 70, 0, "BREN" },
        // The Sten: a copy of the Thompson (the "usSMG" family; the Sten's is
        // "brsmg"), a 32-round magazine. Its reload 3.4 s: GR_STEN
        // (fuelplnt.wbk, 2.53 s) has the magazine out at 0.95 s, in at 1.6 s
        // and the bolt at 2.8 s from the reload's start; the animation's are
        // spaced differently (at 3.2 s, 0.5.33's video: 0.7, 1.7 and 2.3 s),
        // so no length fits all three. 3.4 s keeps each within about 0.35 s
        // (2.6 s had the bolt 0.7 s early, 3.7 s ran long after the sound).
        { "sten", "thompson", "WEAPON_STEN", "w_brSMG", "w_sten_WORLD", "hud_sten_smg", "usSMG", "brsmg",
          32, 0, 0.0f, 0.0f, 0.0f, 3.4f, 30, 70, 1, "STEN" },
        // The FG42: a copy of the BAR (automatic), the "FG42" family, a
        // 20-round magazine. Its reload 2.8 s, between GR_fg42's 2.33 s
        // and its animation's 3.27 s (as the Sten's, which was out of step
        // at its sound's length). Fuel Plant's.
        { "fg42", "bar", "WEAPON_FG42", "w_FG42", "w_fg42_WORLD_noscope", "hud_fg_42", "usBAR", "FG42",
          20, 0, 0.0f, 0.0f, 0.0f, 2.8f, 30, 70, 1, "fg42" },
        // The FG42 (scoped): the same gun with the German scope view the
        // scoped Kar98 has (its overlay, reticle and zoom; the campaign
        // FG42's scope is the same German kind, w_ge_scope). The campaign
        // has no scoped FG42 model (w_fg42_world holds only the noscope one),
        // so its models are the FG42's; its name is its own, "FG42 Scoped"
        // (WEAPON_FG42_SCOPED, below). In the class list beside the FG42.
        // (0.5.54: Mace 2's, the campaign's scoped FG42: its scoped world
        // model w_fg42_WORLD, its icon, its own shots fg42sniper_FIRE.)
        { "fg42_scoped", "bar", "WEAPON_FG42_SCOPED", "w_FG42", "w_fg42_WORLD", "hud_fg_42_scoped", "usBAR", "FG42",
          20, 0, 0.0f, 0.0f, 0.0f, 2.8f, 30, 70, 0, "fg42sniper", "kar98_sniper" },
        // The Mills bomb, the British grenade (Fuel Plant's world model): a
        // copy of the American frag, its own model when thrown and lying.
        // Its view model and moves are the frag's (the campaign has none of
        // its own); a custom class's grenade kind 3.
        { "mills_bomb", "fraggrenade", "WEAPON_MILLS_BOMB", nullptr, "w_mills_bomb_WORLD", "hud_us_grenade", nullptr, nullptr,
          0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 100, 100, 1, nullptr, nullptr },
    };

    int32_t g_campaignWeapon[sizeof(Guns) / sizeof(Guns[0])] = {};   // each campaign gun's weapon number, once made

    // Shooters' sound handles and the campaign gun each last fired (none
    // for any other gun), for the gunshot sounds (sub_82554BA0).
    std::map<uint32_t, const CampaignGun*> g_shooters;
    std::mutex g_shootersLock;
    void RememberShooter(uint32_t handle, const CampaignGun* gun)
    {
        std::lock_guard<std::mutex> guard(g_shootersLock);
        if (g_shooters.size() > 256) g_shooters.clear();
        g_shooters[handle] = gun;
    }
    const CampaignGun* ShooterGun(uint32_t handle)
    {
        std::lock_guard<std::mutex> guard(g_shootersLock);
        const auto found = g_shooters.find(handle);
        return found == g_shooters.end() ? nullptr : found->second;
    }

    // A campaign gun's name: the gun it is a copy of when it is not set up on
    // this map (campaign_guns off, or a map without room for its archive);
    // else, or for any other name, null.
    const char* CampaignStandIn(const char* name)
    {
        for (size_t i = 0; i < sizeof(Guns) / sizeof(Guns[0]); i++)
            if (strcmp(name, Guns[i].name) == 0) return g_campaignWeapon[i] > 0 ? nullptr : Guns[i].from;
        return nullptr;
    }

    // Maps whose memory is too full for the campaign weapons archive (about
    // 27 MB): with it, part of Poissons (its zn01) is never loaded. On these
    // the campaign guns are left out, and a class with one gets the gun it
    // is a copy of.
    const char* const NoCampaignGuns[] = { "poissons" };
    std::string g_mapName;   // the map's, from its archive's path (D:\mp\<map>\<map>.cod)

    // A weapon's reload settings (the weapon file's fields, by the field
    // table at 0x825D7D64): +1464 clipName and +1468 its number, +1476
    // clipSize, +1548 reloadTime, +1552 reloadEmptyTime, +1556 reloadAddTime,
    // +1560 reloadStartTime, +1564 reloadStartAddTime, +1568 reloadEndTime,
    // +1872 segmentedReload, +1876 reloadAmmoAdd, +1880 reloadStartAdd. The
    // clips are numbered after every weapon is set up (sub_82486890), by
    // clipName, each with its clipSize as its most: a clip named for the gun
    // gets a number, and a most, of its own. The animations play stretched to
    // these times, so they are set to the animations' own lengths.
    void Reload(uint8_t* base, const CampaignGun& gun, uint32_t weapon)
    {
        const uint32_t was = Guest::Read32(base, weapon + 1548);
        // The times are kept in milliseconds; should they be floats in
        // seconds instead (a float's bits are far above any time), as that.
        const bool floats = was > 0x10000000u;
        auto time = [&](uint32_t at, float seconds)
        {
            if (floats) { uint32_t bits; memcpy(&bits, &seconds, 4); Guest::Write32(base, weapon + at, bits); }
            else Guest::Write32(base, weapon + at, uint32_t(seconds * 1000.0f + 0.5f));
        };
        printf("weapons: %s's copy had clip %s of %u, reload %08X (empty %08X, add %08X, start %08X, start add %08X, end %08X), in parts %u, add %u, start add %u\n",
               gun.name, ReadText(base, Guest::Read32(base, weapon + 1464)).c_str(), Guest::Read32(base, weapon + 1476), was,
               Guest::Read32(base, weapon + 1552), Guest::Read32(base, weapon + 1556), Guest::Read32(base, weapon + 1560),
               Guest::Read32(base, weapon + 1564), Guest::Read32(base, weapon + 1568), Guest::Read32(base, weapon + 1872),
               Guest::Read32(base, weapon + 1876), Guest::Read32(base, weapon + 1880));
        Guest::Write32(base, weapon + 1464, GuestText(base, gun.name));
        Guest::Write32(base, weapon + 1476, uint32_t(gun.clipSize));
        if (gun.reloadAdd > 0)
        {
            Guest::Write32(base, weapon + 1872, 1);
            Guest::Write32(base, weapon + 1876, uint32_t(gun.reloadAdd));
            Guest::Write32(base, weapon + 1880, 0);
            time(1560, gun.reloadStart);
            time(1564, gun.reloadStart);
            time(1548, gun.reloadEach);
            time(1552, gun.reloadEach);
            time(1556, gun.reloadEach * 0.75f);
            time(1568, gun.reloadEnd);
            // The start's and end's animations' names (+112, +116), after
            // the reload's (+104).
            const std::string reload = ReadText(base, Guest::Read32(base, weapon + 104));
            if (!reload.empty())
            {
                Guest::Write32(base, weapon + 112, GuestText(base, reload + "_start2"));
                Guest::Write32(base, weapon + 116, GuestText(base, reload + "_end2"));
            }
        }
        // A reload in one animation: its times set to the animation's length
        // (the ammunition put in at 80% of it).
        // The ini's sten_reload, fg42_reload and bren_reload, when set, in
        // place of the times here (tuning without a build).
        float whole = gun.reloadWhole;
        {
            const Settings::Values& v = Settings::Get();
            const float asked = strcmp(gun.name, "sten") == 0 ? v.stenReload : strcmp(gun.name, "fg42") == 0 ? v.fg42Reload
                              : strcmp(gun.name, "bren") == 0 ? v.brenReload : 0.0f;
            if (asked > 0.3f) { whole = asked; printf("weapons: %s's reload %.2f s, from the ini\n", gun.name, asked); }
        }
        // The sights' field of view (+1600, adsZoomFov, a float): the copy's
        // (the BAR's for the FG42), or the ini's (fg42_ads_fov, ...).
        {
            const Settings::Values& v = Settings::Get();
            const float fov = strcmp(gun.name, "fg42") == 0 ? v.fg42AdsFov : strcmp(gun.name, "sten") == 0 ? v.stenAdsFov
                            : strcmp(gun.name, "bren") == 0 ? v.brenAdsFov : strcmp(gun.name, "lee_enfield") == 0 ? v.enfieldAdsFov : 0.0f;
            const uint32_t bits = Guest::Read32(base, weapon + 1600);
            float was; memcpy(&was, &bits, 4);
            if (fov >= 10.0f) { uint32_t put; memcpy(&put, &fov, 4); Guest::Write32(base, weapon + 1600, put); }
            printf("weapons: %s's sights field of view %.1f (its copy's %.1f)\n", gun.name, fov >= 10.0f ? fov : was, was);
        }
        if (gun.reloadAdd <= 0 && whole > 0.0f)
        {
            time(1548, whole);
            time(1552, whole);
            time(1556, whole * 0.8f);
        }
        // Putting it away (+1572 dropTime): the Lee-Enfield's own drop
        // (w_brRFL_drop) takes the rifle out of sight in its first part and
        // shows nothing after, and its raise (w_brRFL_raise) shows it only
        // late: at the Kar98's times (400 and 500 ms) a switch sat on empty
        // hands for about half a second either way (video of 0.5.17: 60%
        // of the drop still left 0.4 s). dropPercent and raisePercent of
        // the copy's: 30% and 70%.
        {
            const uint32_t drop = Guest::Read32(base, weapon + 1572), raise = Guest::Read32(base, weapon + 1576);
            if (!floats && drop > 0 && drop < 5000 && gun.dropPercent != 100) Guest::Write32(base, weapon + 1572, drop * uint32_t(gun.dropPercent) / 100);
            if (!floats && raise > 0 && raise < 5000 && gun.raisePercent != 100) Guest::Write32(base, weapon + 1576, raise * uint32_t(gun.raisePercent) / 100);
            printf("weapons: %s's drop time %u ms (was %u), raise time %u ms (was %u)\n", gun.name, Guest::Read32(base, weapon + 1572), drop,
                   Guest::Read32(base, weapon + 1576), raise);
        }
        printf("weapons: %s gets a clip of %d%s (times in %s)\n", gun.name, gun.clipSize,
               gun.reloadAdd > 0 ? ", reloaded in parts" : gun.reloadWhole > 0.0f ? ", its own reload time" : "", floats ? "seconds" : "milliseconds");
        fflush(stdout);
    }

    void MakeGun(PPCContext& ctx, uint8_t* base, const CampaignGun& gun, uint32_t entry)
    {
        // Set up under the copied gun's own name, so that what the set-up
        // finds by the weapon's name (its effects on each surface, "kar98_
        // METAL" and the rest, by sub_82490220) is found; then renamed: its
        // name at +8 and that name's hash at +4.
        const uint32_t name = GuestText(base, gun.name);
        PPCContext call = ctx;
        call.r3.u64 = GuestText(base, gun.from);
        call.r4.u64 = entry;
        __imp__sub_8249A380(call, base);
        const int32_t count = int32_t(Guest::Read32(base, 0x82BCBF94));
        const uint32_t list = Guest::Read32(base, 0x82BCBF9C);
        const uint32_t weapon = list && count > 0 ? Guest::Read32(base, list + 4 * uint32_t(count)) : 0;
        if (weapon)
        {
            Guest::Write32(base, weapon + 8, name);
            PPCContext hash = ctx;
            hash.r1.u32 = (ctx.r1.u32 - 0x2000) & ~0xFu;
            hash.r3.u64 = name;
            hash.lr = 0;
            Guest::Lookup(0x820E10E0)(hash, base);
            Guest::Write32(base, weapon + 4, hash.r3.u32);
        }
        if (!weapon || ReadText(base, Guest::Read32(base, weapon + 8)) != gun.name)
        {
            printf("weapons: %s could not be made (weapon %d %08X)\n", gun.name, count, weapon);
            fflush(stdout);
            return;
        }
        Guest::Write32(base, weapon + 12, GuestText(base, gun.title));
        const int test = Settings::Get().campaignTest;
        if (test != 2 && gun.viewModel) Guest::Write32(base, weapon + 20, GuestText(base, gun.viewModel));
        // The world models: +1432 the one in a player's hands, +1436 the one
        // lying on the ground when dropped (sub_82486BF8, the items: +1436
        // when it is set, else +1432). The BAR's and Kar98's are set to their
        // own (the Sten's copy, the Thompson, has none, so the Sten was the
        // only one dropped right): both are the campaign gun's world model.
        printf("weapons: %s's copy had item %s, world models %s and %s, +1444 %s, +1448 %s\n", gun.name,
               ReadText(base, Guest::Read32(base, weapon + 1428)).c_str(), ReadText(base, Guest::Read32(base, weapon + 1432)).c_str(),
               ReadText(base, Guest::Read32(base, weapon + 1436)).c_str(), ReadText(base, Guest::Read32(base, weapon + 1444)).c_str(),
               ReadText(base, Guest::Read32(base, weapon + 1448)).c_str());
        // A grenade (no animations of its own): its view model, when it is
        // the copy's world model (the frag's first-person grenade is its
        // world model in the hands, w_us_nade_world), is its own world
        // model too: the Mills bomb in the hand.
        if (!gun.armsFrom)
        {
            const std::string view = ReadText(base, Guest::Read32(base, weapon + 20));
            const std::string world = ReadText(base, Guest::Read32(base, weapon + 1432));
            auto lower = [](std::string t) { for (char& c : t) c = char(tolower(static_cast<unsigned char>(c))); return t; };
            const bool same = !view.empty() && lower(view) == lower(world);
            printf("weapons: %s's copy's view model %s, world model %s%s\n", gun.name, view.c_str(), world.c_str(),
                   same ? ": its own world model in the hands too" : "");
            if (same) Guest::Write32(base, weapon + 20, GuestText(base, gun.worldModel));
        }
        Guest::Write32(base, weapon + 1432, GuestText(base, gun.worldModel));
        // Its item's own name (+1428, hashed for the items list,
        // sub_82486BF8): the copy's ("weapon_thompson", "weapon_bar") was
        // shared, so a dropped campaign gun's item was the copy's too.
        Guest::Write32(base, weapon + 1428, GuestText(base, std::string("weapon_") + gun.name));
        if (Guest::Read32(base, weapon + 1436) != 0) Guest::Write32(base, weapon + 1436, GuestText(base, gun.worldModel));
        // A grenade's (no animations of its own): its thrown model too
        // (+1932, projectileModel), when it has one.
        if (!gun.armsFrom && !ReadText(base, Guest::Read32(base, weapon + 1932)).empty())
        {
            printf("weapons: %s's thrown model %s -> %s\n", gun.name, ReadText(base, Guest::Read32(base, weapon + 1932)).c_str(), gun.worldModel);
            Guest::Write32(base, weapon + 1932, GuestText(base, gun.worldModel));
        }
        // The HUD icon (+1440, the pick-up prompt's): the campaign's own are
        // found (0.5.35) but drew as a white box (the Sten's) or a hand
        // (the others'); the copy's own icon is kept until they draw.
        if (Settings::Get().campaignIcons) Guest::Write32(base, weapon + 1440, GuestText(base, gun.hud));
        // Its gunshots: the copy's sound names (THOMPSON_FIRE_1ST and _3RD,
        // ...) in the weapon's fields become the campaign gun's own, which
        // the campaign banks loaded beside the map's have (STEN_FIRE_1ST in
        // fuelplnt.wbk, BREN_FIRE_1ST in stbert.wbk, ...). The shots' events
        // keep the copy's name, so its muzzle flash stays (0.5.31 renamed
        // the events, and lost the flashes).
        if (Settings::Get().campaignSounds && Settings::Get().campaignShotSounds && gun.shot)
        {
            std::string from = gun.from;
            for (char& c : from) c = char(toupper(static_cast<unsigned char>(c)));
            from += "_FIRE_";
            int changed = 0;
            for (uint32_t at = 0; at < 2400; at += 4)
            {
                const uint32_t text = Guest::Read32(base, weapon + at);
                if (text < 0x40000000u || text >= 0x90000000u) continue;
                std::string value = ReadText(base, text, 48), upper = value;
                for (char& c : upper) c = char(toupper(static_cast<unsigned char>(c)));
                if (upper != from + "1ST" && upper != from + "3RD") continue;
                const std::string own = std::string(gun.shot) + "_FIRE_" + upper.substr(from.size());
                Guest::Write32(base, weapon + at, GuestText(base, own));
                printf("weapons: %s's gunshot +%u: %s -> %s\n", gun.name, at, value.c_str(), own.c_str());
                changed++;
            }
            if (!changed) printf("weapons: %s: no %s1ST/3RD sound field found\n", gun.name, from.c_str());
        }
        int renamed = 0;
        for (uint32_t at = 72; at <= 164 && test != 1 && gun.armsFrom; at += 4)
        {
            std::string anim = ReadText(base, Guest::Read32(base, weapon + at));
            printf("weapons:   %s field +%u: %s\n", gun.name, at, anim.c_str());
            const size_t found = anim.find(gun.armsFrom);
            if (found == std::string::npos) continue;
            anim.replace(found, strlen(gun.armsFrom), gun.armsTo);
            // The Kar98's reload's start and end are the Lee-Enfield's "2".
            if (anim.size() > 6 && (anim.compare(anim.size() - 6, 6, "_start") == 0 || anim.compare(anim.size() - 4, 4, "_end") == 0)) anim += "2";
            Guest::Write32(base, weapon + at, GuestText(base, anim));
            renamed++;
        }
        if (gun.clipSize > 0 && test != 1) Reload(base, gun, weapon);
        g_campaignWeapon[&gun - Guns] = count;
        printf("weapons: %s made as weapon %d (%08X) from %s, %d animations renamed (test %d)\n", gun.name, count, weapon, gun.from, renamed, test);
        fflush(stdout);
    }
}

// Each weapon's set-up: the entries of the guns the campaign ones are
// copies of are kept, and the copies made after the last weapon is set up
// (sub_8249E970, below), so they come at the end of the list and no
// multiplayer weapon's number moves.
namespace { uint32_t g_copyFrom[sizeof(Guns) / sizeof(Guns[0])] = {}; }
namespace
{
    // Each campaign weapons archive: its number for this map (-1: not used
    // here), and the number and object it was loaded as (kept: archives are
    // never let go, and are used again by the maps after).
    int32_t g_archiveActive[ArchiveCount] = { -1, -1 };
    // Merville's archive, loaded beside a map without vehicles of its own
    // (the console's vehicles_everywhere 1, before the map): its vehicle
    // files, models and effects. -1: not on this map.
    int32_t g_vehicleArchive = -1;
    int32_t g_mapArchive = -1;   // the map's own archive's number (kind 3), 0.5.85
    bool g_kangarooWaiting = false;   // its archive put off till the map's is in
    bool g_kangarooPendingMap() { return g_kangarooWaiting; }
    int32_t g_kangarooArchive = -1;   // the Kangaroo's (sp\mace2\kangaroo.cod), when asked for
    int32_t g_archiveLoaded[ArchiveCount] = { -1, -1 };
    uint32_t g_archiveObject[ArchiveCount] = {};
}
PPC_FUNC(sub_8249A380)
{
    const std::string name = ReadText(base, ctx.r3.u32, 40);
    const uint32_t entry = ctx.r4.u32;
    __imp__sub_8249A380(ctx, base);
    for (size_t i = 0; i < sizeof(Guns) / sizeof(Guns[0]); i++)
        if (name == Guns[i].from) g_copyFrom[i] = entry;
}

// The weapons set up from the map's WEAPONFILE entries (sub_8249E970), then
// the campaign guns after them.
extern "C" PPC_FUNC(__imp__sub_8249E970);
PPC_FUNC(sub_8249E970)
{
    memset(g_copyFrom, 0, sizeof(g_copyFrom));
    memset(g_campaignWeapon, 0, sizeof(g_campaignWeapon));
    PPCContext saved = ctx;
    __imp__sub_8249E970(ctx, base);
    const int32_t before = int32_t(Guest::Read32(base, 0x82BCBF94));
    if (Settings::Get().campaignGuns)
        for (size_t i = 0; i < sizeof(Guns) / sizeof(Guns[0]); i++)
            if (g_copyFrom[i] && g_archiveActive[Guns[i].archive] >= 0) MakeGun(saved, base, Guns[i], g_copyFrom[i]);
    printf("weapons: %d set up from the map, %d with the campaign's\n", before, int32_t(Guest::Read32(base, 0x82BCBF94)));
    fflush(stdout);
}


// The archives an asset is looked for in (sub_82474CB8: a bank's number,
// then a list of archive numbers filled in, their count at +128, at most
// 32): the bank's own and what it needs, then the two global ones. The
// script's SetModel, and the rest, find a model only in these (sub_820D71E8),
// so the campaign weapons archive is put at the end of every list.
extern "C" PPC_FUNC(__imp__sub_82474CB8);
PPC_FUNC(sub_82474CB8)
{
    const uint32_t list = ctx.r4.u32;
    __imp__sub_82474CB8(ctx, base);
    if (!list) return;
    // 0.5.85: and the map's own archive last, for the Kangaroo's archive:
    // its treads and wheels are drawn with v_tracked_tread and
    // v_tracked_wheel, which Merville's and Poissons' archives have and its
    // own does not (Mace 2 has them in its vehicles.cod). Not in its list,
    // they were never found, and the treads and wheels were not drawn.
    int32_t extra[ArchiveCount + 3];
    for (int i = 0; i < ArchiveCount; i++) extra[i] = g_archiveActive[i];
    extra[ArchiveCount] = g_vehicleArchive;
    extra[ArchiveCount + 1] = g_kangarooArchive;
    extra[ArchiveCount + 2] = g_kangarooArchive >= 0 || g_kangarooPendingMap() ? g_mapArchive : -1;
    for (const int32_t archive : extra)
    {
        if (archive < 0) continue;
        const int32_t count = int32_t(Guest::Read32(base, list + 128));
        if (count < 0 || count >= 32) return;
        bool there = false;
        for (int32_t i = 0; i < count; i++)
            if (int32_t(Guest::Read32(base, list + 4 * uint32_t(i))) == archive) there = true;
        if (there) continue;
        Guest::Write32(base, list + 4 * uint32_t(count), uint32_t(archive));
        Guest::Write32(base, list + 128, uint32_t(count + 1));
    }
}

// Vehicles on any map (the console's vehicles_everywhere 1, typed before the
// map is loaded). Only Merville and Poissons carry vehicle files; on another
// map Merville's archive is loaded beside the map's, as the campaign weapons
// archives are (with the weapons', below), and its VEHICLEFILE and
// VEHICLEPHYSICSFILE entries read the way the map's own are (sub_8256CE08:
// sub_8254B318 over an archive's entries with a name's prefix, the
// vehicle's reader sub_825683D8 and the physics' sub_8255F3A8).
namespace { std::atomic<bool> g_vehiclesEverywhere{false}; }
void MpFixesVehiclesEverywhere(bool on) { g_vehiclesEverywhere.store(on); }

namespace
{
    // An archive loaded beside the map's, with the weapons' (as the campaign
    // weapons archives are): kept loaded for the maps after, used again.
    struct ExtraArchive { const char* path; const char* label; int32_t active = -1, loaded = -1; uint32_t object = 0, guestPath = 0; };
    ExtraArchive g_merville{ "mp\\mp_merv\\mp_merv.cod", "Merville's archive" };
    ExtraArchive g_kangarooFile{ "sp\\mace2\\kangaroo.cod", "the Kangaroo's archive" };
    std::atomic<int> g_kangarooType{1};   // the vehicle type the Kangaroo is given: 1 four wheel, 2 tank
    std::atomic<bool> g_kangaroo{true};   // on unless the console says kangaroo 0: its archive is small (5 MB)

    void LoadExtra(PPCContext& saved, uint8_t* base, uint32_t manager, uint32_t bank, ExtraArchive& archive, const std::string& map)
    {
        archive.active = -1;
        if (archive.object && archive.loaded >= 0 && Guest::Read32(base, manager + 4 * uint32_t(archive.loaded + 11)) == archive.object)
        {
            archive.active = archive.loaded;
            printf("vehicles: %s still loaded as %d\n", archive.label, archive.active);
            fflush(stdout);
            return;
        }
        if (!archive.guestPath) archive.guestPath = GuestText(base, archive.path);
        PPCContext call = saved;
        call.r1.u32 = (saved.r1.u32 - 0x2000) & ~0xFu;
        call.r3.u64 = manager;
        call.r4.u64 = Guest::Read32(base, bank + 0);
        call.r5.u64 = archive.guestPath;
        for (int i = 0; i < 4; i++)
        {
            const uint64_t value = (uint64_t(Guest::Read32(base, bank + 12 + 8 * i)) << 32) | Guest::Read32(base, bank + 16 + 8 * i);
            (i == 0 ? call.r6 : i == 1 ? call.r7 : i == 2 ? call.r8 : call.r9).u64 = value;
        }
        if (Settings::Get().bigMemory) call.r8.u64 = (uint64_t(0xBF800000u) << 32) | (call.r8.u64 & 0xFFFFFFFFull);
        call.lr = 0;
        Guest::Lookup(0x82477B50)(call, base);
        const int32_t index = int32_t(call.r3.u32);
        printf("vehicles: %s loading as %d beside %s\n", archive.label, index, map.c_str());
        fflush(stdout);
        if (index == -1) return;
        for (int spins = 0; spins < 100000 && Guest::Read32(base, manager) == 0; spins++)
        {
            PPCContext pump = call;
            pump.r3.u64 = manager; pump.r4.u64 = 0;
            Guest::Lookup(0x8247D700)(pump, base);
            Guest::Lookup(0x820F9CA8)(pump, base);
            Guest::Lookup(0x820F9D00)(pump, base);
        }
        archive.active = archive.loaded = index;
        archive.object = Guest::Read32(base, manager + 4 * uint32_t(index + 11));
        printf("vehicles: %s loaded (%08X)\n", archive.label, archive.object);
        fflush(stdout);
    }
}
void MpFixesKangaroo(bool on) { g_kangaroo.store(on); }
void MpFixesKangarooType(int type) { g_kangarooType.store(type == 2 ? 2 : 1); printf("kangaroo: type %d from the next map start\n", type == 2 ? 2 : 1); fflush(stdout); }

namespace { uint32_t g_kangarooManager = 0, g_kangarooBank = 0; bool& g_kangarooPending = g_kangarooWaiting; }
void MpFixesLoadVehicleArchive(PPCContext& saved, uint8_t* base, uint32_t manager, uint32_t bank)
{
    const std::string map = Kernel::CurrentMap();
    const bool ownVehicles = map == "mp_merv" || map == "poissons";
    // Merville's, for vehicles on a map without (vehicles_everywhere 1: big,
    // 133 MB). The Kangaroo needs a WC51 to be copied from: on Merville and
    // Poissons, or with vehicles_everywhere.
    g_merville.active = -1;
    if (!map.empty() && !ownVehicles && g_vehiclesEverywhere.load()) LoadExtra(saved, base, manager, bank, g_merville, map);
    g_kangarooFile.active = -1;
    // 0.5.85: on Merville and Poissons the Kangaroo's archive waits for the
    // map's own (MpFixesLoadKangarooAfterMap): its treads and wheels are
    // drawn by v_tracked_tread and v_tracked_wheel, which only the map's
    // archive has (Mace 2 loads them from its vehicles.cod). Loaded before
    // the map, the Kangaroo found neither and drew without both.
    if (!map.empty() && g_kangaroo.load() && !ownVehicles && g_merville.active >= 0) LoadExtra(saved, base, manager, bank, g_kangarooFile, map);
    g_kangarooPending = false;
    if (!map.empty() && g_kangaroo.load() && ownVehicles)
    {
        if (!g_kangarooBank) g_kangarooBank = Guest::AllocatePhysical(0x40);
        memcpy(base + g_kangarooBank, base + bank, 0x30);
        g_kangarooManager = manager;
        g_kangarooPending = true;
    }
    g_vehicleArchive = g_merville.active;
    g_kangarooArchive = g_kangarooFile.active;
}

// The Kangaroo's archive on Merville and Poissons, put off till the map's
// own archive is in (from the vehicles' setup, sub_82561838, which comes
// after it): with the weapons' bank's manager and a copy of that bank (its
// name word and bounds), kept from then.
void MpFixesLoadKangarooAfterMap(PPCContext& ctx, uint8_t* base)
{
    if (!g_kangarooPending) return;
    const std::string map = Kernel::CurrentMap();
    printf("vehicles: %s's own archive (%d) is in: the Kangaroo's after it, looking in the map's too (the treads' and wheels' shaders)\n", map.c_str(), g_mapArchive);
    fflush(stdout);
    LoadExtra(ctx, base, g_kangarooManager, g_kangarooBank, g_kangarooFile, map);
    g_kangarooArchive = g_kangarooFile.active;
    g_kangarooPending = false;
}

// The Kangaroo (the campaign's Mace 2 APC, v_Kangaroo in sp\mace2\kangaroo.cod,
// with a driver, a gunner and six passengers' tags) as a vehicle kind of its
// own, "mp_kangaroo": a copy of the WC51 truck's vehicle file (784 bytes,
// set up by sub_825683D8: its name first, the rest its fields; the kinds'
// table at 0x82B2DC90, their count at 0x82B34594), under the new name. Its
// model is set when it is put down.
namespace
{
    void MakeKangarooKind(PPCContext& ctx, uint8_t* base)
    {
        const int count = int(int16_t(Guest::Read16(base, 0x82B34594)));
        uint32_t truck = 0;
        for (int i = 0; i < count; i++)
        {
            const uint32_t def = Guest::Read32(base, 0x82B2DC90 + 4 * uint32_t(i));
            const std::string name = def ? ReadText(base, def, 32) : std::string();
            if (_stricmp(name.c_str(), "mp_kangaroo") == 0) return;
            if (_stricmp(name.c_str(), "mp_horch") == 0 || (!truck && _stricmp(name.c_str(), "mp_wc51") == 0)) truck = def;
        }
        if (!truck || count >= 32)
        {
            printf("vehicles: no WC51 to make the Kangaroo from (%d kinds)\n", count);
            fflush(stdout);
            return;
        }
        PPCContext call = ctx;
        call.r1.u32 = (ctx.r1.u32 - 0x1000) & ~0xFu;
        call.r3.u64 = 16; call.r4.u64 = 784; call.lr = 0;
        Guest::Lookup(0x820DE750)(call, base);
        const uint32_t kangaroo = call.r3.u32;
        if (!kangaroo) return;
        memcpy(base + kangaroo, base + truck, 784);
        memset(base + kangaroo, 0, 32);
        memcpy(base + kangaroo, "mp_kangaroo", 12);
        Guest::Write32(base, 0x82B2DC90 + 4 * uint32_t(count), kangaroo);
        Guest::Write16(base, 0x82B34594, uint16_t(count + 1));
        printf("vehicles: the Kangaroo made as kind %d (from the %s's vehicle file)\n", count, ReadText(base, truck, 32).c_str());
        fflush(stdout);
    }
}

namespace
{
    // 0.5.82: the Kangaroo its own kind (mp_kangaroo, a copy of the Horch's
    // vehicle file made with the map's files, so the Horch's seats' animations
    // stay), not the map's real Horch's: its driving block (+80 to +127: the
    // Horch's speeds, too fast for a tank's hull, Justin) taken from the
    // Sherman's file, and its seats (+72: the Horch 3, the WC51 2) as many as
    // its model has places: the driver, the gunner and six passengers.
    void KangarooOwnKind(uint8_t* base, uint32_t record)
    {
        // 0.5.83: the vehicle file's fields by the game's own table of them
        // (0x825FE3C8: name, offset, kind): type +32, subtype +34 (the
        // game's subtypes: Jeep 1, Motorcycle, Truck, Wagon, Halftrack,
        // Horch, Sherman, Kangaroo 8), numSeats +68 (0.5.82 wrote 8 to +72,
        // which is hudIndex), texureScroll +40 and its scale +76, maxSpeed
        // +80 to boundsLength +124, health +128, the cameras +432 to +499.
        // The campaign's "Kangaroo" file (its archive's, read in the map's
        // start) gives those to mp_kangaroo when it is there; else the
        // Sherman's driving and bounds.
        const int count = int(int16_t(Guest::Read16(base, 0x82B34594)));
        int own = -1;
        uint32_t ownDef = 0, sherman = 0, campaign = 0;
        for (int i = 0; i < count && i < 32; i++)
        {
            const uint32_t def = Guest::Read32(base, 0x82B2DC90 + 4 * uint32_t(i));
            const std::string name = def ? ReadText(base, def, 32) : std::string();
            if (_stricmp(name.c_str(), "mp_kangaroo") == 0) { own = i; ownDef = def; }
            else if (_stricmp(name.c_str(), "mp_shermanTank") == 0) sherman = def;
            else if (name.find("angaroo") != std::string::npos) campaign = def;
        }
        if (own < 0) { printf("kangaroo: no kind of its own on this map\n"); return; }
        const uint32_t from = campaign ? campaign : sherman;
        if (from)
        {
            memcpy(base + ownDef + 80, base + from + 80, 52);     // maxSpeed .. boundsLength, health
            if (campaign)
            {
                memcpy(base + ownDef + 40, base + from + 40, 4);   // texureScroll
                memcpy(base + ownDef + 76, base + from + 76, 4);   // texureScrollScale
                memcpy(base + ownDef + 68, base + from + 68, 4);   // numSeats
                memcpy(base + ownDef + 392, base + from + 392, 4); // engineSndSpeed
                memcpy(base + ownDef + 432, base + from + 432, 68); // the cameras
            }
        }
        if (!campaign) Guest::Write32(base, ownDef + 68, 8);       // numSeats: driver, gunner, six passengers
        // Its subtype the game's own Kangaroo (8), its driving curve the
        // Kangaroo's (vehicleCurveFile +508: Kangaroo.bvce is in mp\global.cod),
        // and its type 4 wheel (1) or tank (2: tracked, the treads' scroll)
        // as the kangaroo_type console command last said.
        Guest::Write16(base, ownDef + 34, 8);
        memset(base + ownDef + 508, 0, 32);
        memcpy(base + ownDef + 508, "Kangaroo", 9);
        Guest::Write16(base, ownDef + 32, uint16_t(g_kangarooType.load()));
        Guest::Write16(base, record + 376, uint16_t(own));
        printf("kangaroo: its own kind %d (type %d, subtype 8, curve Kangaroo) from %s: seats %d, max speed %.0f, bounds %.0f %.0f %.0f, tread scroll %.2f x %.2f\n", own, g_kangarooType.load(),
               campaign ? "the campaign's Kangaroo file" : sherman ? "the Sherman's" : "the Horch's", int(Guest::Read32(base, ownDef + 68)),
               ReadF(base, ownDef + 80), ReadF(base, ownDef + 116), ReadF(base, ownDef + 120), ReadF(base, ownDef + 124),
               ReadF(base, ownDef + 40), ReadF(base, ownDef + 76));
        fflush(stdout);
    }

    // One of the map's own vehicles moved to a saved place, in the map's
    // start (its physics not yet begun): every copy of its origin and yaw
    // the comparison found (the entity's +32, +304, +336, +832 and the
    // yaw at +76 and +356; the record's +192, +208, +448 and its yaw at
    // +228, +244, +468). It keeps everything that makes it drivable.
    // The map's vehicles made Kangaroos at the map's start (their entities).
    std::set<uint32_t> g_kangarooEntities;
    struct Moved { float x, y, z; uint32_t entity, record; std::string kind; };
    std::vector<Moved> g_movedFrom;
    void ScanForPlace(uint8_t* base, const Moved& m)
    {
        int hits = 0;
        static const struct { uint32_t from, to; } ranges[] = {
            { 0x40000000u, 0x70000000u }, { 0x82000000u, 0x84000000u }, { 0xA0000000u, 0xC0000000u } };
        for (const auto& r : ranges)
        {
            for (uint64_t at = r.from; at < r.to && hits < 80;)
            {
                MEMORY_BASIC_INFORMATION info{};
                if (!VirtualQuery(base + at, &info, sizeof(info))) break;
                const uint64_t regionEnd = std::min<uint64_t>(r.to, uint64_t(reinterpret_cast<uint8_t*>(info.BaseAddress) - base) + info.RegionSize);
                if (info.State == MEM_COMMIT && !(info.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
                {
                    for (uint64_t a = at & ~3ull; a + 12 <= regionEnd && hits < 80; a += 4)
                    {
                        const uint32_t g = uint32_t(a);
                        const float x = ReadF(base, g);
                        if (!(fabsf(x - m.x) < 200.0f)) continue;
                        const float y = ReadF(base, g + 4);
                        if (!(fabsf(y - m.y) < 200.0f)) continue;
                        const float z = ReadF(base, g + 8);
                        if (!(fabsf(z - m.z) < 250.0f)) continue;
                        if ((g >= m.entity && g < m.entity + 876) || (g >= m.record && g < m.record + 1872)) continue;
                        hits++;
                        std::string around;
                        char word[12];
                        for (int k = -8; k < 8; k++) { snprintf(word, sizeof(word), " %08X", Guest::Read32(base, g + 4 * k)); around += word; }
                        printf("vehicles: %s's old place still at %08X: %.1f %.1f %.1f |%s\n", m.kind.c_str(), g, x, y, z, around.c_str());
                    }
                }
                at = regionEnd;
            }
        }
        printf("vehicles: %s's old place (%.0f %.0f %.0f): %d copies left in memory\n", m.kind.c_str(), m.x, m.y, m.z, hits);
        fflush(stdout);
    }
    int g_mapStarts = 0;   // the map starts seen (sub_82564520), for the probe below

    void MoveVehicle(uint8_t* base, uint32_t entity, uint32_t record, float x, float y, float z, float yaw)
    {
        // 0.5.80: back to 0.5.73's copies only (the entity's +32, +304, +336,
        // +832 and its yaw at +76, +356; the record's +192, +208, +448 and its
        // yaw at +228, +244, +468). 0.5.76 carried every position near the old
        // place in both, turned: after it the moved jeep could not be got
        // into at all (Justin's test, 0.5.79).
        const float ox = ReadF(base, entity + 336), oy = ReadF(base, entity + 340), oz = ReadF(base, entity + 344);
        auto put = [&](uint32_t at) { WriteF(base, at, x); WriteF(base, at + 4, y); WriteF(base, at + 8, z); };
        for (const uint32_t at : { 32u, 304u, 336u, 832u }) put(entity + at);
        for (const uint32_t at : { 192u, 208u, 448u }) put(record + at);
        WriteF(base, entity + 76, yaw); WriteF(base, entity + 356, yaw);
        WriteF(base, record + 228, yaw); WriteF(base, record + 244, yaw); WriteF(base, record + 468, yaw);
        printf("vehicles: moved from %.0f %.0f %.0f to %.0f %.0f %.0f (0.5.73's copies)\n", ox, oy, oz, x, y, z);
    }
    // A vehicle kind's file (its def, 0x82B2DC90 by the record's +376): every
    // word, and the text a word points at when it is text: its seats, their
    // tags and animations, to give the Kangaroo its six passengers.
    void DumpVehicleKind(uint8_t* base, uint32_t record)
    {
        const int kind = int(int16_t(Guest::Read16(base, record + 376)));
        const uint32_t def = kind >= 0 && kind < 32 ? Guest::Read32(base, 0x82B2DC90 + 4 * uint32_t(kind)) : 0;
        if (!def) return;
        std::string line;
        char word[64];
        for (uint32_t at = 0; at < 784; at += 4)
        {
            const uint32_t w = Guest::Read32(base, def + at);
            if (!w) continue;
            float f; memcpy(&f, &w, 4);
            std::string text;
            if (w >= 0x40000000u && w < 0x90000000u)
            {
                text = ReadText(base, w, 32);
                bool printable = text.size() >= 3;
                for (char c : text) if (c < 0x20 || c > 0x7E) printable = false;
                if (!printable) text.clear();
            }
            if (!text.empty()) snprintf(word, sizeof(word), " +%u='%s'", at, text.c_str());
            else if (std::isfinite(f) && fabsf(f) > 0.001f && fabsf(f) < 100000.0f) snprintf(word, sizeof(word), " +%u=%.2f", at, f);
            else snprintf(word, sizeof(word), " +%u=%08X", at, w);
            line += word;
        }
        printf("vehicles: kind %d's file (%s):%s\n", kind, ReadText(base, def, 32).c_str(), line.c_str());
    }
    // The entity's and the record's words, for finding the box a vehicle is
    // hit by (its mins and maxs) next to the Horch's and the WC51's.
    void DumpEntity(uint8_t* base, uint32_t entity, uint32_t record, const std::string& kind)
    {
        std::string line;
        char word[24];
        for (uint32_t at = 0; at < 900; at += 4)
        {
            const uint32_t w = Guest::Read32(base, entity + at);
            float f; memcpy(&f, &w, 4);
            if (w == 0) continue;
            if (std::isfinite(f) && fabsf(f) > 0.5f && fabsf(f) < 20000.0f) snprintf(word, sizeof(word), " +%u=%.1f", at, f);
            else snprintf(word, sizeof(word), " +%u=%08X", at, w);
            line += word;
        }
        printf("vehicles: %s's entity:%s\n", kind.c_str(), line.c_str());
        (void)record;
    }
}

// The map's saved vehicles (vehicles.txt), in the map's start: its last
// pass over the vehicles (sub_82564520: each one's entity unlinked and
// linked again) runs right after the map's own are made, before the game
// begins. 0.5.72 made new ones here: made as the map's are, they still
// could not be driven (they lack what the map's vehicles are given from
// the map itself, their collision brush at +878 among it). So a saved
// vehicle is now one of the map's own of that kind, moved to its place
// (the Kangaroo: a WC51 given the Kangaroo's model); a new one is made only
// when the map has none of that kind left, and says so.
extern "C" PPC_FUNC(__imp__sub_82564520);
PPC_FUNC(sub_82564520)
{
    if (Guest::Read32(base, 0x82B3FDE0 + 8) != 0)
    {
        const std::string map = MapNow();
        const uint32_t pool = Guest::Read32(base, 0x82BCBDB4);
        const int records = int(Guest::Read16(base, 0x82C281D0 + 3088));
        // The map's vehicles: entity, record, kind's name.
        struct Own { uint32_t entity, record; std::string kind; bool taken; };
        std::vector<Own> own;
        for (int i = 0; pool && i < records; i++)
        {
            const uint32_t record = pool + 1872 * uint32_t(i);
            const uint32_t handle = Guest::Read32(base, record + 368), index = handle & 0x3FFF;
            if (index >= 4096) continue;
            const uint32_t entry = 0x82BD1FC0 + 8 * index;
            if ((handle >> 14) != Guest::Read32(base, entry + 4)) continue;
            const uint32_t entity = Guest::Read32(base, entry);
            if (!entity) continue;
            const int kind = int(int16_t(Guest::Read16(base, record + 376)));
            const uint32_t def = kind >= 0 && kind < 32 ? Guest::Read32(base, 0x82B2DC90 + 4 * uint32_t(kind)) : 0;
            own.push_back({ entity, record, def ? ReadText(base, def, 32) : std::string("?"), false });
        }
        std::string have;
        for (const Own& o : own) have += (have.empty() ? "" : ", ") + o.kind;
        printf("vehicles: %s's own at its start: %s\n", map.c_str(), have.empty() ? "none" : have.c_str());
        int moved = 0, made = 0, wanted = 0;
        g_kangarooEntities.clear();
        g_mapStarts++;
        g_inMapStart = true;
        for (const Placed& p : ReadPlacements())
        {
            if (p.map != map) continue;
            wanted++;
            const bool kangaroo = _stricmp(p.kind.c_str(), "mp_kangaroo") == 0;
            // Which of the map's vehicles can stand in, best first: the same
            // kind; for the Kangaroo the Sherman (the Kangaroo is a Sherman's
            // hull: tracks, its size, its collision), then a WC51, then a
            // Horch; for the WC51 a Horch. One that is not the kind asked for
            // takes the asked for one's model.
            std::vector<std::string> kinds;
            if (kangaroo) kinds = { "mp_horch", "mp_wc51" };
            else if (_stricmp(p.kind.c_str(), "mp_wc51") == 0) kinds = { "mp_wc51", "mp_horch" };
            else kinds = { p.kind };
            Own* use = nullptr;
            std::string kind = kinds.front();
            for (const std::string& k : kinds)
            {
                for (Own& o : own) if (!o.taken && _stricmp(o.kind.c_str(), k.c_str()) == 0) { use = &o; break; }
                if (use) { kind = k; break; }
            }
            if (use)
            {
                use->taken = true;
                g_movedFrom.push_back({ ReadF(base, use->entity + 336), ReadF(base, use->entity + 340), ReadF(base, use->entity + 344),
                                        use->entity, use->record, p.kind });
                MoveVehicle(base, use->entity, use->record, p.x, p.y, p.z, p.yaw);
                DumpEntity(base, use->entity, use->record, kind);
                DumpVehicleKind(base, use->record);
                const char* model = kangaroo ? "v_Kangaroo" : (_stricmp(kind.c_str(), p.kind.c_str()) != 0 ? "v_wc51" : nullptr);
                if (kangaroo) g_kangarooEntities.insert(use->entity);
                if (model)
                {
                    static uint32_t scratch = 0;
                    if (!scratch) scratch = Guest::AllocatePhysical(0x80);
                    memset(base + scratch, 0, 0x80);
                    memcpy(base + scratch + 0x40, model, strlen(model) + 1);
                    Call3(ctx, base, MakeText, scratch, scratch + 0x40, 0);
                    Call3(ctx, base, 0x824CE130, Guest::Read32(base, use->entity + 484), scratch, 0);
                    Call3(ctx, base, FreeText, scratch, 0, 0);
                }
                if (kangaroo)
                {
                    // 0.5.82: the Kangaroo's own box, the size of its hull (a
                    // Sherman's: about 250 long, 112 wide, 95 high), set after
                    // its model (setting the model sets the box from the model:
                    // 0.5.81 set it before, and Justin still walked through
                    // parts of it). The entity's mins at +240, maxs at +256.
                    const float mins[3] = { -125.0f, -56.0f, 0.0f }, maxs[3] = { 125.0f, 56.0f, 95.0f };
                    for (int k = 0; k < 3; k++) { WriteF(base, use->entity + 240 + 4 * k, mins[k]); WriteF(base, use->entity + 256 + 4 * k, maxs[k]); }
                    KangarooOwnKind(base, use->record);
                }
                moved++;
                printf("vehicles: the map's %s moved to %.0f %.0f %.0f for a saved %s\n", kind.c_str(), p.x, p.y, p.z, p.kind.c_str());
            }
            else if (SpawnVehicleAt(ctx, base, p.kind, p.x, p.y, p.z, p.yaw))
            {
                made++;
                printf("vehicles: no %s of the map's left: a new one made (not drivable yet)\n", kind.c_str());
            }
        }
        g_inMapStart = false;
        if (wanted)
        {
            printf("vehicles: %d saved: %d of the map's own moved there, %d made new\n", wanted, moved, made);
            Overlay::ConsolePrint("vehicles: " + std::to_string(moved) + " saved vehicles are the map's own, moved (drivable); " +
                                  std::to_string(made) + " made new. This map has: " + (have.empty() ? std::string("none") : have));
        }
        fflush(stdout);
    }
    g_placementDue = false;
    __imp__sub_82564520(ctx, base);
    // 0.5.79: what still stands where a moved vehicle stood (its collision
    // stays there: walking into the Kangaroo goes through it, and at the
    // Sherman's old place something unseen blocks the way). Every copy of
    // the old place left in memory, outside the entity and the record
    // already carried, is logged with the words around it.
    for (const Moved& m : g_movedFrom) ScanForPlace(base, m);
    // (Scanned again a few seconds in, by the probe, once the game is going.)
}

// 0.5.76: a tank's tags each frame (sub_82570558, its entity in r3): its
// body, turret, barrel, gunner's turret and barrel, hatches and recoil, found
// on its model by their names' hashes, hashed once into 0x82CA1AC8.. (the
// flags at +6888). Where they are is where the tank is got into (the
// hatches) and aimed from. The Kangaroo (a Sherman given the Kangaroo's
// model) has none of a tank's turret, barrel or hatches, so it could not be
// got into. For a Kangaroo the names asked for are its own for the while:
// the hatches its driver's and gunner's places, the turret and barrel its
// gun's mount and barrel.
extern "C" PPC_FUNC(__imp__sub_82570558);
PPC_FUNC(sub_82570558)
{
    constexpr uint32_t Hashes = 0x82CA0000u, Flags = Hashes + 6888;
    const bool kangaroo = !g_kangarooEntities.empty() && g_kangarooEntities.count(ctx.r3.u32) != 0 &&
                          (Guest::Read32(base, Flags) & 0xFF) == 0xFF;
    if (!kangaroo) { __imp__sub_82570558(ctx, base); return; }
    struct Swap { uint32_t at; const char* name; };
    static const Swap swaps[] = {
        { 6880, "tag_gunner_turret" }, { 6876, "tag_gunner_barrel" },
        { 6864, "tag_gunner" }, { 6860, "tag_driver" }, { 6856, "tag_gunner_barrel" },
    };
    static uint32_t hashes[5] = {};
    if (!hashes[0])
        for (int i = 0; i < 5; i++) hashes[i] = Call3(ctx, base, 0x820E10E0, GuestText(base, swaps[i].name), 0, 0);
    uint32_t saved[5];
    for (int i = 0; i < 5; i++) { saved[i] = Guest::Read32(base, Hashes + swaps[i].at); Guest::Write32(base, Hashes + swaps[i].at, hashes[i]); }
    __imp__sub_82570558(ctx, base);
    for (int i = 0; i < 5; i++) Guest::Write32(base, Hashes + swaps[i].at, saved[i]);
}

// 0.5.77 (finding what the Kangaroo lacks): every tag the game looks up on
// a model (sub_82435668: the DObj r3, the name's hash r4; -1 when the model
// has none) is logged once per tag and caller when the model is a
// Kangaroo's, with the name when it is one of the names below. And once a
// map, a few seconds in, every vehicle's tags are listed (the DObj's models
// at +128, their count at +170; a model's skeleton is its first non-zero
// word from +36, its bone table at +8: the count at +16, 12-byte entries
// from +20 with the name's hash at +4), and the names the tank's own tag
// code hashes (sub_82570558's, from 0x820655BC on) printed.
namespace
{
    const char* const KnownTags[] = {
        "tag_origin", "tag_body", "tag_body2", "tag_turret", "tag_turret2", "tag_barrel", "tag_barrel2", "tag_flash", "tag_flash2",
        "tag_recoil", "tag_driver", "tag_passenger", "tag_passenger1", "tag_passenger2", "tag_passenger3", "tag_passenger4",
        "tag_passenger5", "tag_passenger6", "tag_gunner", "tag_gunner1", "tag_gunner2", "tag_gunner_turret", "tag_gunner_barrel",
        "tag_gunner_flash", "tag_gunner_recoil", "tag_hatch", "tag_hatch1", "tag_hatch2", "tag_hatch_open", "tag_driver_hatch",
        "tag_gunner_hatch", "tag_commander", "tag_commander_hatch", "tag_loader", "tag_enter_driver", "tag_enter_gunner",
        "tag_enter_passenger", "tag_enter_back", "tag_enter", "tag_exit", "tag_tread_left", "tag_tread_right", "tag_gear_left",
        "tag_gear_right", "tag_wheel_front_left", "tag_wheel_front_right", "tag_wheel_back_left", "tag_wheel_back_right",
        "tag_wheel_middle_left", "tag_wheel_middle_right", "tag_wheel_left", "tag_wheel_right", "tag_wheel1", "tag_wheel2",
        "tag_engine", "tag_engine_left", "tag_engine_right", "tag_detach", "tag_player", "tag_camera", "tag_flag", "tag_mg",
        "tag_mg_flash", "tag_mg_turret", "tag_mg_barrel", "tag_antenna", "tag_headlight_left", "tag_headlight_right",
        "tag_exhaust", "tag_smoke", "tag_fire", "tag_death_fx", "tag_seat", "tag_seat1", "tag_seat2", "tag_gun", "gun_mount",
        "gun_mount1", "tag_gun_mount", "tag_steering", "tag_steeringwheel", "tag_steering_wheel", "tag_wheel_steering",
        "tag_cargo", "tag_door_left", "tag_door_right", "tag_light", "tag_sideplate", "tag_sideplate_left", "tag_sideplate_right",
        "tag_handlebars", "tag_handlebar", "tag_rider", "tag_sidecar", "tag_sidecar_gunner", "tag_fender", "tag_spare",
        "tag_window", "tag_glass", "tag_windshield", "tag_hood", "tag_trunk", "tag_left", "tag_right", "tag_front", "tag_back",
        "tag_rear", "tag_top", "tag_bottom", "tag_hull", "tag_chassis", "tag_tracks", "tag_track_left", "tag_track_right",
        "tag_treads", "tag_tread", "tag_wheels", "tag_bogie", "tag_shield", "tag_ammo", "tag_weapon", "tag_weapon_right",
        "tag_weapon_left", "tag_inhand", "tag_eye", "tag_view", "tag_aim", "tag_aim_animated", "tag_pivot", "tag_mount",
        "tag_mantle", "tag_propeller", "tag_prop", "tag_canopy", "tag_wing_left", "tag_wing_right", "tag_rocket", "tag_rocket1",
        "tag_rocket2", "tag_rocket3", "tag_rocket4", "tag_bomb", "tag_pilot", "tag_tail", "j_wheel", "j_body", "j_turret",
        "j_barrel", "ng_tread", "ng_wheel1", "ng_wheel2", "ng_sideplate", "ng_body", "ng_body3", "ng_main", "tread", "body",
        "turret", "barrel", "hatch", "driver", "gunner", "passenger", "wheel", "origin",
        "tag_enter_left", "tag_enter_right", "tag_enter_back_left", "tag_enter_back_right", "tag_enter_turret",
        "tag_hatch_left", "tag_hatch_right", "tag_gunner_player", "tag_flash_2", "tag_flash_11", "tag_flash_22",
        "tag_gunhull", "tag_hand_left", "tag_hand_right", "tag_gunner_hand_left", "tag_gunner_hand_right",
    };
    std::map<uint32_t, std::string> g_tagNames;
    std::set<uint64_t> g_tagAsked;
    std::string g_probedMap;
    uint64_t g_probeAt = 0;

    uint32_t TagHash(const char* text)
    {
        uint32_t h = 0;
        for (const char* p = text; *p; p++) h = h * 33 + uint32_t(tolower(uint8_t(*p)));
        return h;
    }
    std::string TagName(uint32_t hash)
    {
        if (g_tagNames.empty()) for (const char* n : KnownTags) g_tagNames[TagHash(n)] = n;
        const auto it = g_tagNames.find(hash);
        char text[16];
        snprintf(text, sizeof(text), "%08X", hash);
        return it != g_tagNames.end() ? it->second : std::string("?") + text;
    }
    bool IsKangarooDObj(uint8_t* base, uint32_t dobj)
    {
        if (!dobj) return false;
        for (const uint32_t entity : g_kangarooEntities) if (Guest::Read32(base, entity + 492) == dobj) return true;
        return false;
    }
    std::string BoneList(uint8_t* base, uint32_t dobj)
    {
        std::string out;
        if (!dobj) return "no model";
        const int models = base[dobj + 170];
        for (int m = 0; m < models && m < 8; m++)
        {
            const uint32_t model = Guest::Read32(base, dobj + 128 + 4 * uint32_t(m));
            if (!model) continue;
            uint32_t skeleton = 0;
            for (int k = 0; k < 8 && !skeleton; k++) skeleton = Guest::Read32(base, model + 36 + 4 * uint32_t(k));
            const uint32_t table = skeleton ? Guest::Read32(base, skeleton + 8) : 0;
            const uint32_t count = table ? Guest::Read32(base, table + 16) : 0;
            const uint32_t entries = table ? Guest::Read32(base, table + 20) : 0;
            out += "[model " + std::to_string(m) + ": " + std::to_string(count) + " tags]";
            for (uint32_t i = 0; entries && i < count && i < 160; i++) out += " " + TagName(Guest::Read32(base, entries + 12 * i + 4));
        }
        return out;
    }
}

// 0.5.78: the tags a vehicle is got into by, which the Kangaroo's model
// lacks (0.5.77's log, the names found from their hashes): the Panzer, the
// Horch, the BMW and the WC51 all have tag_enter_left and tag_enter_right
// (the WC51 and the Horch tag_enter_back_left/right too), the tanks
// tag_hatch_left/right, tag_enter_turret and tag_gunhull. The Kangaroo has
// tag_enter_back, its six wheels, tag_driver, tag_gunner and its gun's
// mount. Asked on a Kangaroo, each missing one is answered with the nearest
// it has. This is the lookup under every tag lookup (sub_82434E60: the
// DObj r3, the name's hash r4; the tag's index, or -1).
namespace
{
    struct TagSwap { const char* missing; const char* instead; };
    const TagSwap KangarooTags[] = {
        { "tag_enter_left", "tag_wheel_middle_left" }, { "tag_enter_right", "tag_wheel_middle_right" },
        { "tag_enter_back_left", "tag_enter_back" }, { "tag_enter_back_right", "tag_enter_back" },
        { "tag_hatch_left", "tag_driver" }, { "tag_hatch_right", "tag_gunner" },
        { "tag_enter_turret", "tag_gunner_turret" }, { "tag_gunhull", "gun_mount" },
        { "tag_gunner_player", "tag_gunner" }, { "tag_steeringwheel", "tag_driver" },
        { "tag_recoil", "tag_barrel" },
    };
    uint32_t KangarooInstead(uint32_t hash)
    {
        static std::map<uint32_t, uint32_t> swaps;
        TagName(0);   // the known names first
        if (swaps.empty())
            for (const TagSwap& s : KangarooTags)
            {
                swaps[TagHash(s.missing)] = TagHash(s.instead);
                g_tagNames[TagHash(s.missing)] = s.missing;
            }
        const auto it = swaps.find(hash);
        return it != swaps.end() ? it->second : 0;
    }
}

extern "C" PPC_FUNC(__imp__sub_82434E60);
PPC_FUNC(sub_82434E60)
{
    const uint32_t dobj = ctx.r3.u32, hash = ctx.r4.u32, from = uint32_t(ctx.lr);
    PPCContext saved = ctx;
    __imp__sub_82434E60(ctx, base);
    if (g_kangarooEntities.empty() || !IsKangarooDObj(base, dobj)) return;
    const int32_t found = int32_t(ctx.r3.u32);
    int32_t instead = -1;
    if (found < 0)
        if (const uint32_t other = KangarooInstead(hash))
        {
            PPCContext again = saved;
            again.r4.u64 = other;
            __imp__sub_82434E60(again, base);
            instead = int32_t(again.r3.u32);
            if (instead >= 0) ctx.r3.u64 = again.r3.u64;
        }
    (void)found;
    if (instead >= 0 && g_tagAsked.size() < 600 && g_tagAsked.insert((uint64_t(hash) << 32) | from).second)
    {
        printf("kangaroo: tag %s asked: none, its stand-in's %d\n", TagName(hash).c_str(), instead);
        fflush(stdout);
    }
}

// 0.5.78: who asks which tag of which vehicle (sub_82435668, the caller
// its return address), every vehicle's model, not only the Kangaroo's: with
// Use pressed next to the jeep and next to the Kangaroo, what getting in
// asks shows. The wheels' and the turret's every-frame asks are left out.
namespace { std::map<uint32_t, std::string> g_vehicleDObjs; std::set<uint64_t> g_vehicleAsked; }
extern "C" PPC_FUNC(__imp__sub_82435668);
PPC_FUNC(sub_82435668)
{
    const uint32_t dobj = ctx.r3.u32, hash = ctx.r4.u32, from = uint32_t(ctx.lr);
    __imp__sub_82435668(ctx, base);
    if (g_vehicleDObjs.empty() || from == 0x8258D72C || from == 0x8256FD30) return;
    const auto it = g_vehicleDObjs.find(dobj);
    if (it == g_vehicleDObjs.end()) return;
    if (g_vehicleAsked.size() < 800 && g_vehicleAsked.insert((uint64_t(hash) << 32) ^ from ^ (uint64_t(dobj) << 8)).second)
    {
        printf("vehicles: %s's tag %s asked from %08X: %d\n", it->second.c_str(), TagName(hash).c_str(), from, int32_t(ctx.r3.u32));
        fflush(stdout);
    }
}

// 0.5.81: where each Kangaroo is, every five seconds while it moves.
void MpFixesKangarooWhere(uint8_t* base)
{
    static uint64_t next = 0;
    static float last[3] = {};
    if (g_kangarooEntities.empty() || GetTickCount64() < next) return;
    next = GetTickCount64() + 5000;
    for (const uint32_t entity : g_kangarooEntities)
    {
        const float x = ReadF(base, entity + 336), y = ReadF(base, entity + 340), z = ReadF(base, entity + 344);
        if (fabsf(x - last[0]) + fabsf(y - last[1]) + fabsf(z - last[2]) < 2.0f) continue;
        last[0] = x; last[1] = y; last[2] = z;
        printf("kangaroo: at %.0f %.0f %.0f, facing %.0f\n", x, y, z, ReadF(base, entity + 356));
        fflush(stdout);
    }
}

void MpFixesKangarooProbe(PPCContext& ctx, uint8_t* base)
{
    const std::string map = MapNow() + "#" + std::to_string(g_mapStarts);
    if (MapNow().empty() || map == g_probedMap) return;
    if (!g_probeAt) { g_probeAt = GetTickCount64() + 5000; g_vehicleDObjs.clear(); g_vehicleAsked.clear(); return; }
    if (GetTickCount64() < g_probeAt) return;
    g_probeAt = 0;
    g_probedMap = map;
    printf("vehicles: a few seconds in:\n");
    for (const Moved& m : g_movedFrom) ScanForPlace(base, m);
    g_movedFrom.clear();
    printf("kangaroo: tag hash check: tag_origin %08X here, %08X the game's\n", TagHash("tag_origin"),
           Call3(ctx, base, 0x820E10E0, GuestText(base, "tag_origin"), 0, 0));
    static const uint32_t names[] = { 0x820655BC, 0x820655D0, 0x820655EC, 0x820655F8, 0x82065604, 0x82065614, 0x82065624, 0x8206544C, 0x820676BC };
    for (const uint32_t at : names) printf("kangaroo: the tank code's name at %08X: %s\n", at, ReadText(base, at, 40).c_str());
    const uint32_t pool = Guest::Read32(base, 0x82BCBDB4);
    const int records = int(Guest::Read16(base, 0x82C281D0 + 3088));
    std::set<std::string> listed;
    for (int i = 0; pool && i < records; i++)
    {
        const uint32_t record = pool + 1872 * uint32_t(i);
        const uint32_t handle = Guest::Read32(base, record + 368), index = handle & 0x3FFF;
        if (index >= 4096) continue;
        const uint32_t entry = 0x82BD1FC0 + 8 * index;
        if ((handle >> 14) != Guest::Read32(base, entry + 4)) continue;
        const uint32_t entity = Guest::Read32(base, entry);
        if (!entity) continue;
        const int kind = int(int16_t(Guest::Read16(base, record + 376)));
        const uint32_t def = kind >= 0 && kind < 32 ? Guest::Read32(base, 0x82B2DC90 + 4 * uint32_t(kind)) : 0;
        std::string name = def ? ReadText(base, def, 32) : std::string("?");
        const bool kangaroo = g_kangarooEntities.count(entity) != 0;
        if (kangaroo) name += " (the Kangaroo)";
        g_vehicleDObjs[Guest::Read32(base, entity + 492)] = name + " " + std::to_string(i);
        if (!listed.insert(name).second) continue;
        printf("kangaroo: %s's tags: %s\n", name.c_str(), BoneList(base, Guest::Read32(base, entity + 492)).c_str());
        // 0.5.82: its box as linked (mins +240, maxs +256, the world's +272 and
        // +284), its model object's first 256 bytes (which parts are hidden:
        // the Kangaroo's treads and wheels are never drawn) and its kind's file.
        if (kangaroo || name.rfind("mp_panzeriv", 0) == 0 || name.rfind("mp_shermanTank", 0) == 0)
        {
            printf("kangaroo: %s's box %.0f %.0f %.0f to %.0f %.0f %.0f, in the world %.0f %.0f %.0f to %.0f %.0f %.0f\n", name.c_str(),
                   ReadF(base, entity + 240), ReadF(base, entity + 244), ReadF(base, entity + 248), ReadF(base, entity + 256),
                   ReadF(base, entity + 260), ReadF(base, entity + 264), ReadF(base, entity + 272), ReadF(base, entity + 276),
                   ReadF(base, entity + 280), ReadF(base, entity + 284), ReadF(base, entity + 288), ReadF(base, entity + 292));
            const uint32_t dobj = Guest::Read32(base, entity + 492);
            std::string words;
            char word[12];
            for (uint32_t at = 0; dobj && at < 256; at += 4) { snprintf(word, sizeof(word), " %08X", Guest::Read32(base, dobj + at)); words += word; }
            printf("kangaroo: %s's model object:%s\n", name.c_str(), words.c_str());
            DumpVehicleKind(base, record);
        }
    }
    fflush(stdout);
}

// Room for more vehicles: the map's vehicle records (sub_82561838: how
// many, 1872 bytes each, the ones in use copied over and their entities'
// +524 pointed at the copies) are as many as the map asked for (Merville
// in a devmap: 5, all taken by its own vehicles, so spawnvehicle had none).
// Sixteen more each time, for vehicles put down by hand: 30 KB.
void MpFixesPlacementDue();
extern "C" PPC_FUNC(__imp__sub_82561838);
PPC_FUNC(sub_82561838)
{
    const uint32_t asked = ctx.r3.u32 & 0xFFFF;
    // The map's vehicle files are read just before (in the map's start,
    // sub_82593FA8, which then makes the records here): Merville's are read
    // after them when its archive is here, then the Kangaroo made.
    // (0.5.62 waited for sub_8256CE08 to do it, which this map start does
    // not call.)
    if (g_vehicleArchive >= 0)
    {
        const uint32_t archives = Guest::Read32(base, 0x82BCBE1C);
        PPCContext call = ctx;
        call.r1.u32 = (ctx.r1.u32 - 0x1000) & ~0xFu;
        call.r3.u64 = archives; call.r4.u64 = uint32_t(g_vehicleArchive); call.r5.u64 = 0x820658F0; call.r6.u64 = 0x825683D8; call.lr = 0;
        Guest::Lookup(0x8254B318)(call, base);
        call = ctx;
        call.r1.u32 = (ctx.r1.u32 - 0x1000) & ~0xFu;
        call.r3.u64 = archives; call.r4.u64 = uint32_t(g_vehicleArchive); call.r5.u64 = 0x820658FC; call.r6.u64 = 0x8255F3A8; call.lr = 0;
        Guest::Lookup(0x8254B318)(call, base);
        printf("vehicles: Merville's vehicle files read (%d kinds now)\n", int(int16_t(Guest::Read16(base, 0x82B34594))));
        fflush(stdout);
    }
    MpFixesLoadKangarooAfterMap(ctx, base);
    // 0.5.83: the Kangaroo's own archive's vehicle files too (the campaign's
    // "Kangaroo": its speed, seats, bounds, cameras and treads' scroll), read
    // as Merville's are; then mp_kangaroo is made (from the Horch's).
    if (g_kangarooArchive >= 0)
    {
        const uint32_t archives = Guest::Read32(base, 0x82BCBE1C);
        for (const auto& reader : { std::pair<uint32_t, uint32_t>{ 0x820658F0, 0x825683D8 }, std::pair<uint32_t, uint32_t>{ 0x820658FC, 0x8255F3A8 } })
        {
            PPCContext call = ctx;
            call.r1.u32 = (ctx.r1.u32 - 0x1000) & ~0xFu;
            call.r3.u64 = archives; call.r4.u64 = uint32_t(g_kangarooArchive); call.r5.u64 = reader.first; call.r6.u64 = reader.second; call.lr = 0;
            Guest::Lookup(0x8254B318)(call, base);
        }
        printf("vehicles: the Kangaroo's archive's vehicle files read (%d kinds now)\n", int(int16_t(Guest::Read16(base, 0x82B34594))));
        fflush(stdout);
    }
    if (g_kangarooArchive >= 0) MakeKangarooKind(ctx, base);
    ctx.r3.u64 = std::min<uint32_t>(asked + 16, 64);
    printf("vehicles: room for %u vehicles (the map asked for %u)\n", ctx.r3.u32, asked);
    fflush(stdout);
    __imp__sub_82561838(ctx, base);
    MpFixesPlacementDue();
}


// A bank (an archive) loaded with what it needs first (sub_8247DD38: the
// manager, the bank: its name at +0, its path at +8, its bounds from +12,
// its archive's number at +180), by sub_82477B50 (the manager, the name, the
// path, the bounds), then waited for. With the multiplayer's weapons
// (mp\weapusge.cod), a campaign level's weapons archive is loaded too, the
// same way: the campaign guns' models and animations.
extern "C" PPC_FUNC(__imp__sub_8247DD38);
PPC_FUNC(sub_8247DD38)
{
    const uint32_t manager = ctx.r3.u32, bank = ctx.r4.u32;
    PPCContext saved = ctx;
    std::string path = bank ? ReadText(base, Guest::Read32(base, bank + 8)) : std::string();
    for (char& c : path) c = char(tolower(static_cast<unsigned char>(c)));
    const bool weapons = path.find("weapusge") != std::string::npos;
    // (Letting the campaign archive of the map before go here, by
    // sub_8247D820, did free the room, but the next map then read the
    // game's tables of that archive after it was gone, and crashed: the
    // game's own banks are let go by more than that. Left loaded for now:
    // a map the guns are skipped on is best played first, and the archive
    // is loaded once and used again by the maps after.)
    __imp__sub_8247DD38(ctx, base);
    const uint32_t result = ctx.r3.u32;
    if (weapons) MpFixesLoadVehicleArchive(saved, base, manager, bank);
    ctx.r3.u64 = result;
    if (!weapons || !Settings::Get().campaignGuns) return;
    // The map: its own archive is opened before the weapons' (kernel_file.cpp).
    g_mapName = Kernel::CurrentMap();
    const bool big = Settings::Get().bigMemory;
    for (int which = 0; which < ArchiveCount; which++)
    {
        g_archiveActive[which] = -1;
        // Without big_memory: only the first, and not on the maps too full
        // for it. (With it, the banks are enough, sub_82479448 above.)
        if (which > 0 && !big) continue;
        bool skip = false;
        for (const char* map : NoCampaignGuns)
            if (g_mapName == map && !big) skip = true;
        if (skip)
        {
            printf("weapons: no campaign guns on %s (not enough memory for their archive)\n", g_mapName.c_str());
            fflush(stdout);
            continue;
        }
        // Still loaded from a map before (its slot, manager + 4 * (number +
        // 11), holding the same archive): used again, not loaded again.
        const int32_t loaded = g_archiveLoaded[which];
        if (g_archiveObject[which] && loaded >= 0 && Guest::Read32(base, manager + 4 * uint32_t(loaded + 11)) == g_archiveObject[which])
        {
            g_archiveActive[which] = loaded;
            printf("weapons: campaign weapons archive %s still loaded as %d\n", CampaignArchives[which], loaded);
            fflush(stdout);
            continue;
        }
        static uint32_t paths[ArchiveCount] = {};
        if (!paths[which]) paths[which] = GuestText(base, CampaignArchives[which]);
        PPCContext call = saved;
        call.r1.u32 = (saved.r1.u32 - 0x2000) & ~0xFu;
        call.r3.u64 = manager;
        call.r4.u64 = Guest::Read32(base, bank + 0);
        call.r5.u64 = paths[which];
        for (int i = 0; i < 4; i++)
        {
            const uint64_t value = (uint64_t(Guest::Read32(base, bank + 12 + 8 * i)) << 32) | Guest::Read32(base, bank + 16 + 8 * i);
            (i == 0 ? call.r6 : i == 1 ? call.r7 : i == 2 ? call.r8 : call.r9).u64 = value;
        }
        // No banks: the third bound's high word (the bank's +28) is how many
        // banks the archive reserves from the pool when it is made
        // (sub_82476870, through sub_824731E0). With none reserved, its load
        // (sub_82475E18) skips the planning and takes every block from heap
        // 0 instead (the map block, 480 MB with big_memory), marked so
        // (0x20000) to be given back there. The map's own zones keep all 96
        // banks: Poissons loads whole beside both campaign archives.
        if (big) call.r8.u64 = (uint64_t(0xBF800000u) << 32) | (call.r8.u64 & 0xFFFFFFFFull);
        call.lr = 0;
        Guest::Lookup(0x82477B50)(call, base);
        const int32_t index = int32_t(call.r3.u32);
        printf("weapons: campaign weapons archive %s loading as %d (beside %s, bank %08X)\n", CampaignArchives[which], index, path.c_str(), bank);
        fflush(stdout);
        // Waited for as the bank's own is (sub_8247D700 then the two pumps,
        // till the manager's first word is set).
        if (index == -1) continue;
        for (int spins = 0; spins < 100000 && Guest::Read32(base, manager) == 0; spins++)
        {
            PPCContext pump = call;
            pump.r3.u64 = manager; pump.r4.u64 = 0;
            Guest::Lookup(0x8247D700)(pump, base);
            Guest::Lookup(0x820F9CA8)(pump, base);
            Guest::Lookup(0x820F9D00)(pump, base);
        }
        g_archiveActive[which] = index;
        g_archiveLoaded[which] = index;
        g_archiveObject[which] = Guest::Read32(base, manager + 4 * uint32_t(index + 11));
        printf("weapons: campaign weapons archive loaded (%08X)\n", g_archiveObject[which]);
        fflush(stdout);
    }
    ctx.r3.u64 = result;
}
#endif

// Diagnostics for Poissons' missing zone (zn01) while the campaign guns'
// archive is loaded, which more memory (big_memory) did not cure: every
// archive opened (sub_82477B50: the manager, a kind, the path; the number it
// gets) and every streaming allocation that fails (sub_8246EA50: the size;
// null when the stream heap has no room).
#ifdef COD3_MP
namespace { uint32_t g_banks = 0; }   // the bank pool's object (sub_82479448's, below), for counting
extern "C" PPC_FUNC(__imp__sub_82477B50);
PPC_FUNC(sub_82477B50)
{
    const uint32_t kind = ctx.r4.u32, path = ctx.r5.u32;
    __imp__sub_82477B50(ctx, base);
    if (kind == 3) g_mapArchive = int32_t(ctx.r3.u32);   // the map's own archive (kind 3)
    static int said = 0;
    if (said++ < 200)
    {
        // The banks' bitmaps (three words each, at +0 and +12): their set
        // bits counted, to see how full the pool is.
        int first = 0, second = 0;
        for (uint32_t w = 0; g_banks && w < 3; w++)
        {
            first += __builtin_popcount(Guest::Read32(base, g_banks + 4 * w));
            second += __builtin_popcount(Guest::Read32(base, g_banks + 12 + 4 * w));
        }
        printf("archives: %s opened as %d (kind %u); bank bitmaps %d and %d set\n", ReadText(base, path, 80).c_str(), int32_t(ctx.r3.u32), kind, first, second);
        fflush(stdout);
    }
}
extern "C" PPC_FUNC(__imp__sub_8246EA50);
PPC_FUNC(sub_8246EA50)
{
    const uint32_t size = ctx.r3.u32;
    __imp__sub_8246EA50(ctx, base);
    static int said = 0;
    if (ctx.r3.u32 == 0 && size != 0 && said++ < 50)
    {
        printf("archives: stream allocation of %.1f MB failed (no room in the stream heap)\n", size / 1048576.0);
        fflush(stdout);
    }
}
#endif

// The archive manager's banks (sub_82479448, its set-up): every archive
// loaded takes one bank of 3 MB a block, from a pool of num_banks banks made
// once from the map block (num_banks * 3 MB). num_banks is a setting whose
// default, 88, is the text of the double at 0x82085908 (used only there); its
// two bitmaps hold at most 96. On Poissons the map, its zones and the
// weapons fill the 88; the campaign guns' archive (9 banks) leaves no room
// for its zone 1 (10). With big_memory the map block has the room, so the
// pool is made of all 96.
#ifdef COD3_MP
extern "C" PPC_FUNC(__imp__sub_82479448);
PPC_FUNC(sub_82479448)
{
    const uint32_t manager = ctx.r3.u32;
    g_banks = manager;
    if (Settings::Get().bigMemory)
    {
        const double banks = 96.0;
        uint64_t bits; memcpy(&bits, &banks, 8);
        Guest::Write32(base, 0x82085908, uint32_t(bits >> 32));
        Guest::Write32(base, 0x8208590C, uint32_t(bits));
    }
    __imp__sub_82479448(ctx, base);
    const uint32_t count = Guest::Read32(base, manager + 24);
    float banks; memcpy(&banks, &count, 4);
    printf("archives: %.0f banks of %.1f MB for archives (%s)\n", banks, Guest::Read32(base, manager + 28) / 1048576.0,
           Settings::Get().bigMemory ? "big_memory: the most there can be" : "the game's own");
    fflush(stdout);
}
#endif

// The game's own memory (sub_820E0158, at start): a block of 0x171F0000
// bytes (369.9 MB) for maps and everything loaded with them, and one of
// 0x900000, both physical memory (sub_823142B0: size, highest address,
// alignment, protection); the block's start, end and size kept at
// 0x82A8665C (+0, +4, +8), the second's at +12, +16, +20. Graphics memory
// is 511 MB and full at about 492 MB with a big map, so with the campaign
// guns (their archive is about 27 MB more) the map block is made a little
// bigger out of the rest: extra_map_memory MB, 16 by default.
#ifdef COD3_MP
extern "C" PPC_FUNC(__imp__sub_820E0158);
PPC_FUNC(sub_820E0158)
{
    const Settings::Values& settings = Settings::Get();
    const bool big = settings.bigMemory;
    if (!big && (!settings.campaignGuns || settings.extraMapMemory <= 0)) { __imp__sub_820E0158(ctx, base); return; }
    const uint32_t state = 0x82A8665C;
    // With big_memory: the map block in the extended window (kernel.h), 480
    // MB of it instead of 370, and the 370 MB of the 512 left to the
    // graphics library's own textures and buffers.
    const uint32_t size = big ? 0x1E000000u : 0x171F0000u + uint32_t(settings.extraMapMemory) * 0x100000u;
    PPCContext call = ctx;
    call.r1.u32 = (ctx.r1.u32 - 0x2000) & ~0xFu;
    call.lr = 0;
    auto allocate = [&](uint32_t bytes, uint32_t protect)
    {
        call.r3.u64 = bytes; call.r4.u64 = 0xFFFFFFFFu; call.r5.u64 = 0x10000; call.r6.u64 = protect;
        Guest::Lookup(0x823142B0)(call, base);
        return call.r3.u32;
    };
    Guest::Write32(base, state + 8, size);
    uint32_t at = big ? Guest::AllocateExtended(size) : allocate(size, 0x20000004u);
    uint32_t got = size;
    if (!at)
    {
        // Not that much left: the game's own size.
        got = 0x171F0000u;
        Guest::Write32(base, state + 8, got);
        at = allocate(got, 0x20000004u);
    }
    Guest::Write32(base, state + 0, at);
    Guest::Write32(base, state + 4, at + got);
    Guest::Write32(base, state + 20, 0x900000u);
    const uint32_t second = allocate(0x900000u, 0x20000404u);
    Guest::Write32(base, state + 12, second);
    Guest::Write32(base, state + 16, second + 0x900000u);
    printf("memory: the game's map block is %.1f MB (%d MB more than the game's own) at %08X%s\n", got / 1048576.0, int((got - 0x171F0000u) / 0x100000u), at,
           Guest::IsExtendedCpu(at) ? ", past the 512 MB of graphics memory (big_memory)" : "");
    fflush(stdout);
}
#endif

// The drawing side's set-up of a weapon (sub_8245E720: its number): its
// models, animations, name and crosshair, kept at 0x82C83810, 192 bytes a
// weapon, +124 set once done. The game does it for the weapons it knows of
// as the level starts; a campaign gun, made after, is set up here once the
// player is in the game.
#ifdef COD3_MP
void MpFixesCampaignGuns(PPCContext& ctx, uint8_t* base)
{
    const uint32_t entities = Guest::Read32(base, 0x82BCC7FC);
    const uint32_t own = entities ? Guest::Read32(base, entities + 4) : 0;
    if (!own || !Guest::Read32(base, own + 512)) return;
    const int32_t count = int32_t(Guest::Read32(base, 0x82BCBF94));
    for (int32_t weapon : g_campaignWeapon)
    {
        if (weapon <= 0 || weapon > count || weapon >= 64) continue;
        const uint32_t info = 0x82C83810 + 192 * uint32_t(weapon);
        if (Guest::Read8(base, info + 124)) continue;
        PPCContext call = ctx;
        call.r1.u32 = (ctx.r1.u32 - 0x2000) & ~0xFu;
        call.r3.u64 = uint32_t(weapon);
        call.lr = 0;
        Guest::Lookup(0x8245E720)(call, base);
        printf("weapons: weapon %d set up for drawing (%s)\n", weapon, Guest::Read8(base, info + 124) ? "done" : "not done");
        fflush(stdout);
    }
}
#endif

// A table looked up by a weapon's name (sub_820D1320: the table, the name;
// a list of entries, each named at +0, compared as text): the drawing
// set-up (sub_8245E720) finds its weapon's entry there, and with none it
// stops early, leaving the weapon without models, name or crosshair. A
// campaign gun has no entry of its own, so it takes the entry of the gun it
// is a copy of.
#ifdef COD3_MP
// The game's hash of a name (sub_820B00C0): h * 33 + the lowercased
// character, from 0.
static uint32_t NameHash(const std::string& name)
{
    uint32_t h = 0;
    for (char c : name) h = h * 33 + uint32_t(uint8_t(tolower(static_cast<unsigned char>(c))));
    return h;
}

// The borrowed entry lists the copied gun's animations by their names'
// hashes (+4 the count, +8 the items, 28 bytes each, the hash first): a copy
// of it is made with each animation of the copied gun's family ("geRFL")
// swapped for the campaign gun's ("brRFL"), so its own arms' moves play.
static uint32_t CampaignAnimEntry(uint8_t* base, const CampaignGun& gun, const std::string& key, uint32_t entry)
{
    if (!entry) return entry;
    static std::map<std::string, std::pair<uint32_t, uint32_t>> made;   // the gun's: the entry copied, the copy
    auto& slot = made[key];
    if (slot.first == entry && slot.second) return slot.second;
    const uint32_t count = Guest::Read32(base, entry + 4), items = Guest::Read32(base, entry + 8);
    if (!items || count == 0 || count > 256) return entry;
    // The family's moves, each with the campaign gun's name for it.
    static const char* const Moves[] = { "idle_a", "idle_ads_a", "fire", "fireADS", "rechamber", "ADSrechamber", "melee", "reload",
                                         "raise", "drop", "ads_up", "ads_down", "reload_start", "reload_end" };
    std::map<uint32_t, uint32_t> renamed;
    for (const char* move : Moves)
        for (const char* prefix : { "w_arms_", "w_" })
        {
            std::string to = move;
            if (to == "reload_start" || to == "reload_end") to += "2";
            renamed[NameHash(std::string(prefix) + gun.armsFrom + "_" + move)] = NameHash(std::string(prefix) + gun.armsTo + "_" + to);
        }
    const uint32_t copy = Guest::AllocatePhysical(16 + 28 * count);
    memcpy(base + copy, base + entry, 12);
    memcpy(base + copy + 16, base + items, 28 * count);
    Guest::Write32(base, copy + 8, copy + 16);
    // A reload in parts: the entry's items are the weapon's animations by
    // slot (9 the reload, 11 its start, 12 its end; the copied gun has its
    // idle in 11 and 12): the start's and end's made from the reload's item,
    // named for the copied family's start and end, swapped below.
    if (gun.reloadAdd > 0 && count > 12)
        for (uint32_t i : { 11u, 12u })
        {
            memcpy(base + copy + 16 + 28 * i, base + copy + 16 + 28 * 9, 28);
            for (const char* prefix : { "w_arms_", "w_" })
            {
                const uint32_t reload = NameHash(std::string(prefix) + gun.armsFrom + "_reload");
                const uint32_t part = NameHash(std::string(prefix) + gun.armsFrom + (i == 11 ? "_reload_start" : "_reload_end"));
                for (uint32_t word = 0; word < 7; word++)
                    if (Guest::Read32(base, copy + 16 + 28 * i + 4 * word) == reload) Guest::Write32(base, copy + 16 + 28 * i + 4 * word, part);
            }
        }
    int swapped = 0;
    // Every word of an item is looked at: besides the arms' animation (the
    // first), an item can name the gun's own (the "w_" family: the rifle's
    // moves, which carry it up to the hands while the bolt is worked).
    int gunSwapped = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        std::string words;
        for (uint32_t word = 0; word < 7; word++)
        {
            const uint32_t at = copy + 16 + 28 * i + 4 * word, hash = Guest::Read32(base, at);
            char text[12]; snprintf(text, sizeof(text), " %08X", hash); words += text;
            const auto found = renamed.find(hash);
            if (found == renamed.end()) continue;
            Guest::Write32(base, at, found->second);
            if (word == 0) swapped++; else gunSwapped++;
        }
        printf("weapons:   %s item %u:%s\n", key.c_str(), i, words.c_str());
    }
    printf("weapons: %s's other words: %d swapped\n", key.c_str(), gunSwapped);
    printf("weapons: %s's animations: %d of %u swapped for its own\n", key.c_str(), swapped, count);
    fflush(stdout);
    slot = { entry, copy };
    return copy;
}

extern "C" PPC_FUNC(__imp__sub_820D1320);
PPC_FUNC(sub_820D1320)
{
    const uint32_t table = ctx.r3.u32, name = ctx.r4.u32;
    PPCContext saved = ctx;
    __imp__sub_820D1320(ctx, base);
    if (ctx.r3.u32 || !name) return;
    const std::string text = ReadText(base, name, 40);
    // Two entries a weapon: by its name, and by its name with "_arms" (the
    // drawing set-up, sub_82465920, makes that one with "%s%s"); each taken
    // from the copied gun's, under the same ending.
    for (const CampaignGun& gun : Guns)
        if (text == gun.name || text == std::string(gun.name) + "_arms")
        {
            const bool arms = text != gun.name;
            static uint32_t from[sizeof(Guns) / sizeof(Guns[0])][2] = {};
            uint32_t& alias = from[&gun - Guns][arms];
            if (!alias) alias = GuestText(base, arms ? std::string(gun.from) + "_arms" : std::string(gun.from));
            PPCContext again = saved;
            again.r3.u64 = table;
            again.r4.u64 = alias;
            __imp__sub_820D1320(again, base);
            static int said = 0;
            if (said++ < 10)
            {
                printf("weapons: %s has no entry of its own in table %08X; %s's (%08X) taken\n", text.c_str(), table, ReadText(base, alias).c_str(), again.r3.u32);
                // What the entry lists (+4 a count, +8 the items, 28 bytes
                // each, the first word of each an item's name or handle).
                const uint32_t entry = again.r3.u32;
                const uint32_t items = entry ? Guest::Read32(base, entry + 4) : 0, at = entry ? Guest::Read32(base, entry + 8) : 0;
                for (uint32_t i = 0; i < items && i < 40 && at; i++)
                {
                    const uint32_t first = Guest::Read32(base, at + 28 * i);
                    const std::string text = (first >= 0x40000000u && first < 0xC0000000u) ? ReadText(base, first, 48) : std::string();
                    printf("weapons:   item %u: %08X %s\n", i, first, text.c_str());
                }
                fflush(stdout);
            }
            ctx.r3.u64 = gun.armsFrom ? CampaignAnimEntry(base, gun, text, again.r3.u32) : again.r3.u32;
            return;
        }
}
#endif

// A weapon's effect events (its shot's sound and flash, the reload's, ...)
// are played by the weapon's name (sub_82556180, sub_82556508 and
// sub_825563D0: the name in r5, the event in r6; CG_FireWeapon, sub_8246A228,
// passes its weapon's +8). A campaign gun has no events of its own, so it
// plays the ones of the gun it is a copy of.
#ifdef COD3_MP
namespace
{
    void CampaignEventName(uint8_t* base, PPCContext& ctx, uint32_t function)
    {
        if (!ctx.r5.u32) return;
        const std::string text = ReadText(base, ctx.r5.u32, 40);
        // Every weapon event but a shot's (1, 3, 7, 53: thousands), with the
        // time in milliseconds: when a reload's sounds start against the
        // reload itself (the Sten's still feels out of step, 0.5.31).
        {
            const bool shot = (function == 0x82556180u || function == 0x82556508u) &&
                              (ctx.r6.u32 == 1 || ctx.r6.u32 == 3 || ctx.r6.u32 == 7 || ctx.r6.u32 == 53);
            static int said = 0;
            if (!shot && said++ < 1500)
            {
                printf("weapons: event %u for %s (sub_%08X, from %08X) at %llu ms\n", ctx.r6.u32, text.c_str(), function, uint32_t(ctx.lr),
                       (unsigned long long)GetTickCount64());
                fflush(stdout);
            }
        }
        bool any = false;
        for (int32_t weapon : g_campaignWeapon) any = any || weapon > 0;
        if (!any) return;
        // Who fired what (0.5.49): a shot's events reach the sound code
        // (sub_82554BA0) later, as messages carrying the shooter's sound
        // handle (+484 of his entity, the message's +496). Each shot event
        // keeps the handle's gun: a campaign gun's, or none.
        if ((function == 0x82556180u || function == 0x82556508u) && ctx.r4.u32 >= 0x40000000u && ctx.r4.u32 < 0x90000000u &&
            (ctx.r6.u32 == 1 || ctx.r6.u32 == 3 || ctx.r6.u32 == 7 || ctx.r6.u32 == 53))
        {
            const CampaignGun* fired = nullptr;
            for (const CampaignGun& gun : Guns) if (text == gun.name) fired = &gun;
            RememberShooter(Guest::Read32(base, ctx.r4.u32 + 484), fired);
        }
        // Its own gunshot (0.5.46): the copy's shot sound comes with one of
        // events 3, 7 and 53, which keep the campaign name below (they find
        // no sound by it: silent), and the muzzle flash with event 1, which
        // keeps the copy's. With event 1 the campaign gun's own shot is
        // played by name (sub_82556868, the way the game plays a sound at an
        // entity), from the campaign banks: <SHOT>_FIRE_1ST for this
        // machine's player, _3RD for the others.
        if (false && Settings::Get().campaignSounds && Settings::Get().campaignShotSounds && ctx.r6.u32 == 1 &&
            (function == 0x82556180u || function == 0x82556508u))
            for (const CampaignGun& gun : Guns)
                if (text == gun.name && gun.shot)
                {
                    const uint32_t entities = Guest::Read32(base, 0x82BCC7FC);
                    const uint32_t self = entities ? Guest::Read32(base, entities + 4) : 0;
                    const bool mine = self != 0 && ctx.r4.u32 == self;
                    static uint32_t names[sizeof(Guns) / sizeof(Guns[0])][2] = {};
                    uint32_t& name = names[&gun - Guns][mine ? 0 : 1];
                    if (!name) name = GuestText(base, std::string(gun.shot) + (mine ? "_FIRE_1ST" : "_FIRE_3RD"));
                    PPCContext call = ctx;
                    call.r1.u32 = (ctx.r1.u32 - 0x400) & ~0xFu;
                    const uint32_t out = call.r1.u32 + 0x200;
                    Guest::Write32(base, out, 0);
                    call.r3.u64 = out;
                    call.r4.u64 = ctx.r4.u32;
                    call.r5.u64 = name;
                    call.r6.u64 = 0;
                    call.r7.u64 = uint64_t(int64_t(-1));
                    call.r8.u64 = 0;
                    call.lr = 0;
                    Guest::Lookup(0x82556868)(call, base);
                    static int said = 0;
                    if (said++ < 12) { printf("weapons: %s's own shot %s\n", gun.name, ReadText(base, name).c_str()); fflush(stdout); }
                    break;
                }
        // With the campaign levels' sound banks loaded (campaign_sounds,
        // sub_824964E8 below), the sounds an animation plays (sub_82555E90
        // and sub_82555F90, from its markers: the reloads) keep the gun's
        // own name, for the campaign bank's own sounds (GR_STEN, ...).
        // A gun with no campaign sounds of its own (the Mills bomb) is the
        // copy's for these too: its explosion is one (silent and unseen
        // under its own name, 0.5.54).
        if (Settings::Get().campaignSounds && (function == 0x82555E90u || function == 0x82555F90u))
        {
            bool own = true;
            for (const CampaignGun& gun : Guns) if (text == gun.name && !gun.shot && !gun.armsFrom) own = false;
            if (own) return;
        }
        // And the shots (sub_82556180 and sub_82556508: events 1, 3, 7 and
        // 53 a shot), for the campaign banks' gunshots (STEN_FIRE_1ST,
        // fg42_FIRE_1ST, BREN_FIRE_1ST, LEE_ENFIELD_FIRE_1ST):
        // campaign_gunshots, 0 for the copy's.
        if (Settings::Get().campaignSounds && (function == 0x82556180u || function == 0x82556508u))
        {
            // campaign_shot_events: which shot events keep the campaign
            // gun's own name (1: event 1, 2: event 3, 4: event 7, 8: event
            // 53; campaign_gunshots: all four), to find which carries the
            // sound and which the muzzle flash.
            const uint32_t event = ctx.r6.u32;
            const int mask = Settings::Get().campaignGunshots ? 15 : Settings::Get().campaignShotEvents;
            if ((event == 1 && (mask & 1)) || (event == 3 && (mask & 2)) || (event == 7 && (mask & 4)) || (event == 53 && (mask & 8))) return;
        }
        for (const CampaignGun& gun : Guns)
            if (text == gun.name)
            {
                static uint32_t from[sizeof(Guns) / sizeof(Guns[0])] = {};
                uint32_t& alias = from[&gun - Guns];
                if (!alias) alias = GuestText(base, gun.from);
                ctx.r5.u64 = alias;
                return;
            }
    }
}
extern "C" PPC_FUNC(__imp__sub_82556180);
PPC_FUNC(sub_82556180) { CampaignEventName(base, ctx, 0x82556180); __imp__sub_82556180(ctx, base); }
extern "C" PPC_FUNC(__imp__sub_82556508);
PPC_FUNC(sub_82556508) { CampaignEventName(base, ctx, 0x82556508); __imp__sub_82556508(ctx, base); }
extern "C" PPC_FUNC(__imp__sub_825563D0);
PPC_FUNC(sub_825563D0) { CampaignEventName(base, ctx, 0x825563D0); __imp__sub_825563D0(ctx, base); }
// The other senders that take a weapon's name (r5), the same way.
extern "C" PPC_FUNC(__imp__sub_82555C00);
PPC_FUNC(sub_82555C00) { CampaignEventName(base, ctx, 0x82555C00); __imp__sub_82555C00(ctx, base); }
extern "C" PPC_FUNC(__imp__sub_82555D80);
PPC_FUNC(sub_82555D80) { CampaignEventName(base, ctx, 0x82555D80); __imp__sub_82555D80(ctx, base); }
extern "C" PPC_FUNC(__imp__sub_82555E90);
PPC_FUNC(sub_82555E90) { CampaignEventName(base, ctx, 0x82555E90); __imp__sub_82555E90(ctx, base); }
extern "C" PPC_FUNC(__imp__sub_82555F90);
PPC_FUNC(sub_82555F90) { CampaignEventName(base, ctx, 0x82555F90); __imp__sub_82555F90(ctx, base); }
extern "C" PPC_FUNC(__imp__sub_82556298);
PPC_FUNC(sub_82556298) { CampaignEventName(base, ctx, 0x82556298); __imp__sub_82556298(ctx, base); }

// The campaign levels' sound banks (sp\<level>\<level>.wbk: the campaign
// guns' own reloads and shots, GR_STEN, STEN_FIRE_1ST, ...), loaded beside
// the map's. A map's archive names its bank; sub_824964E8 (the audio bank
// manager, a key: the bank's name made by sub_820B5170, the path, the kind,
// the archive's +136) loads it, once for each key. Each time a map's bank
// is loaded (its path has \mp\ in it), the campaign archives' levels'
// banks are loaded the same way after it.
namespace { const char* const CampaignBanks[] = { "mace2" }; }   // Mace 2's bank has every campaign gun's sounds (0.5.54)
extern "C" PPC_FUNC(__imp__sub_824964E8);
PPC_FUNC(sub_824964E8)
{
    PPCContext saved = ctx;
    const std::string path = ctx.r5.u32 ? ReadText(base, ctx.r5.u32, 200) : std::string();
    __imp__sub_824964E8(ctx, base);
    const uint32_t result = ctx.r3.u32;
    {
        static int said = 0;
        if (said++ < 40)
        {
            printf("sounds: bank %s registered (kind %u, archive %08X) -> %08X\n", path.c_str(), saved.r6.u32, saved.r7.u32, result);
            fflush(stdout);
        }
    }
    std::string lower = path;
    for (char& c : lower) c = char(tolower(static_cast<unsigned char>(c)));
    const size_t at = lower.find("mp\\");
    if (!Settings::Get().campaignGuns || !Settings::Get().campaignSounds || at == std::string::npos || lower.find("global") != std::string::npos ||
        lower.size() < 4 || lower.compare(lower.size() - 4, 4, ".wbk") != 0)
    {
        ctx.r3.u64 = result;
        return;
    }
    for (int which = 0; which < ArchiveCount && which < int(sizeof(CampaignBanks) / sizeof(CampaignBanks[0])); which++)
    {
        if (g_archiveActive[which] < 0) continue;
        const std::string level = CampaignBanks[which];
        const std::string name = level + ".wbk";
        // (A DLC map's bank is on its own drive, dlcs:\mp\...: the
        // campaign's are always on the game's, D:.)
        std::string prefix = path.substr(0, at);
        if (prefix.find(':') != std::string::npos && lower.compare(0, 2, "d:") != 0) prefix = "D:\\";
        const std::string full = prefix + "sp\\" + level + "\\" + name;
        // The key, as the game makes it: the name in a 128-byte buffer (its
        // length in the last byte), through sub_820B5170.
        PPCContext call = saved;
        call.r1.u32 = (saved.r1.u32 - 0x800) & ~0xFu;
        const uint32_t nameBuffer = call.r1.u32 + 0x200, keyBuffer = call.r1.u32 + 0x300, pathBuffer = call.r1.u32 + 0x400;
        for (uint32_t i = 0; i < 0x300; i += 4) Guest::Write32(base, nameBuffer + i, 0);
        for (size_t i = 0; i < name.size() && i < 120; i++) Guest::Write8(base, nameBuffer + uint32_t(i), uint8_t(name[i]));
        Guest::Write8(base, nameBuffer + 127, uint8_t(name.size()));
        for (size_t i = 0; i <= full.size() && i < 250; i++) Guest::Write8(base, pathBuffer + uint32_t(i), i < full.size() ? uint8_t(full[i]) : 0);
        call.r3.u64 = keyBuffer; call.r4.u64 = nameBuffer; call.r5.u64 = 1; call.lr = 0;
        Guest::Lookup(0x820B5170)(call, base);
        call.r3.u64 = saved.r3.u64; call.r4.u64 = keyBuffer; call.r5.u64 = pathBuffer; call.r6.u64 = saved.r6.u64; call.r7.u64 = saved.r7.u64; call.lr = 0;
        __imp__sub_824964E8(call, base);
        printf("sounds: campaign bank %s loaded beside %s -> %08X\n", full.c_str(), path.c_str(), call.r3.u32);
        fflush(stdout);
    }
    ctx.r3.u64 = result;
}

// A campaign gun's names (its world model, HUD icon), for the look-ups below.
namespace
{
    bool CampaignName(const std::string& name)
    {
        std::string lower = name;
        for (char& c : lower) c = char(tolower(static_cast<unsigned char>(c)));
        for (const CampaignGun& gun : Guns)
        {
            std::string world = gun.worldModel, hud = gun.hud;
            for (char& c : world) c = char(tolower(static_cast<unsigned char>(c)));
            for (char& c : hud) c = char(tolower(static_cast<unsigned char>(c)));
            if (lower == world || lower == hud) return true;
        }
        return false;
    }
}

// Materials asked for by name (sub_824408E0: the name in r3; looked for in
// one archive, the bank's or the global one, then the level's, else the
// default material at *0x82A8C398, drawn as a white box). A campaign gun's
// HUD icon (the pick-up prompt's) is only in its campaign archive: looked for
// there too (sub_82477E70: an archive's number, the name's key made by
// sub_820B00C0).
// A material's first words, and the first words of what each points at
// (its textures, their fetch constants: formats and addresses), logged to
// compare the campaign icons with the multiplayer ones.
static void DumpMaterial(uint8_t* base, const std::string& name, uint32_t material)
{
    if (material < 0x40000000u || material >= 0x90000000u) return;
    auto words = [&](uint32_t at, int count)
    {
        std::string line;
        char text[16];
        for (int i = 0; i < count; i++) { snprintf(text, sizeof(text), " %08X", Guest::Read32(base, at + 4u * uint32_t(i))); line += text; }
        return line;
    };
    printf("materials:   %s at %08X:%s\n", name.c_str(), material, words(material, 24).c_str());
    for (int i = 0; i < 24; i++)
    {
        const uint32_t to = Guest::Read32(base, material + 4u * uint32_t(i));
        if (to < 0x40000000u || to >= 0x90000000u || to == material) continue;
        printf("materials:     +%d -> %08X:%s \"%s\"\n", i * 4, to, words(to, 16).c_str(), ReadText(base, to, 24).c_str());
    }
}
extern "C" PPC_FUNC(__imp__sub_824408E0);
PPC_FUNC(sub_824408E0)
{
    const std::string name = ctx.r3.u32 ? ReadText(base, ctx.r3.u32, 64) : std::string();
    PPCContext saved = ctx;
    __imp__sub_824408E0(ctx, base);
    if (name == "hud_thompson" || name == "hud_bar_fullauto" || name == "hud_kar98")
    {
        static std::set<std::string> dumped;
        if (dumped.insert(name).second) { printf("materials: %s (a copy's, to compare) -> %08X\n", name.c_str(), ctx.r3.u32); DumpMaterial(base, name, ctx.r3.u32); fflush(stdout); }
    }
    if (name.empty() || !CampaignName(name)) return;
    const uint32_t fallback = Guest::Read32(base, 0x82A8C398);
    const uint32_t first = ctx.r3.u32;
    if (ctx.r3.u32 == fallback || ctx.r3.u32 == 0)
        for (const int32_t archive : g_archiveActive)
        {
            if (archive < 0) continue;
            PPCContext call = saved;
            call.r1.u32 = (saved.r1.u32 - 0x800) & ~0xFu;
            const uint32_t nameBuffer = call.r1.u32 + 0x200, key = call.r1.u32 + 0x300;
            for (uint32_t i = 0; i < 0x200; i += 4) Guest::Write32(base, nameBuffer + i, 0);
            for (size_t i = 0; i < name.size() && i < 120; i++) Guest::Write8(base, nameBuffer + uint32_t(i), uint8_t(name[i]));
            call.r3.u64 = key; call.r4.u64 = nameBuffer; call.lr = 0;
            Guest::Lookup(0x820B00C0)(call, base);
            call.r3.u64 = uint32_t(archive); call.r4.u64 = key; call.lr = 0;
            Guest::Lookup(0x82477E70)(call, base);
            if (call.r3.u32 != 0) { ctx.r3.u64 = call.r3.u32; break; }
        }
    static int said = 0;
    if (said++ < 60)
    {
        printf("materials: %s -> %08X (first %08X, the default %08X)%s\n", name.c_str(), ctx.r3.u32, first, fallback,
               ctx.r3.u32 == fallback ? " NOT FOUND" : "");
        DumpMaterial(base, name, ctx.r3.u32);
        fflush(stdout);
    }
}

// Models asked for by name (sub_82446EA0: the result's holder r3, the name
// r6, looked for in the archives of the bank number r5's list). A campaign
// gun's world model not found that way: looked for with each campaign
// archive's own list too. Logged either way.
extern "C" PPC_FUNC(__imp__sub_82446EA0);
PPC_FUNC(sub_82446EA0)
{
    const std::string name = ctx.r6.u32 ? ReadText(base, ctx.r6.u32, 64) : std::string();
    PPCContext saved = ctx;
    const uint32_t holder = ctx.r3.u32;
    __imp__sub_82446EA0(ctx, base);
    if (name.empty() || !CampaignName(name) || !holder) return;
    const uint32_t first = Guest::Read32(base, holder);
    if (first == 0)
        for (const int32_t archive : g_archiveActive)
        {
            if (archive < 0) continue;
            PPCContext call = saved;
            call.r5.u64 = uint32_t(archive);
            call.lr = 0;
            __imp__sub_82446EA0(call, base);
            if (Guest::Read32(base, holder) != 0) break;
        }
    static int said = 0;
    if (said++ < 60)
    {
        printf("models: %s (bank %d) -> %08X (first %08X)\n", name.c_str(), saved.r5.s32, Guest::Read32(base, holder), first);
        fflush(stdout);
    }
    ctx.r3.u64 = holder;
}

// The items list (sub_82486BF8, 52 bytes each from 0x82C75A48 + 52, item i
// for weapon i: the name's hash, the name, the model, the HUD icon, ...):
// the campaign guns' items logged once it is made.
extern "C" PPC_FUNC(__imp__sub_82486BF8);
void ResolveCampaignIconsLater(PPCContext& ctx, uint8_t* base);
PPC_FUNC(sub_82486BF8)
{
    __imp__sub_82486BF8(ctx, base);
    ResolveCampaignIconsLater(ctx, base);
    // Scoped campaign guns: the scope of the gun named (its sights' zoom
    // +1600, zoom in/out fractions +1608/+1612, overlay +1616, reticle
    // +1620, overlay width and height +1624/+1628), once every weapon is set up.
    {
        const int32_t count = int32_t(Guest::Read32(base, 0x82BCBF94));
        const uint32_t list = Guest::Read32(base, 0x82BCBF9C);
        for (size_t i = 0; i < sizeof(Guns) / sizeof(Guns[0]); i++)
        {
            const int32_t weapon = g_campaignWeapon[i];
            if (weapon <= 0 || !Guns[i].scopeFrom || !list || weapon > count) continue;
            const uint32_t own = Guest::Read32(base, list + 4 * uint32_t(weapon));
            uint32_t scope = 0;
            for (int32_t k = 1; k <= count && !scope; k++)
            {
                const uint32_t def = Guest::Read32(base, list + 4 * uint32_t(k));
                if (def && ReadText(base, Guest::Read32(base, def + 8)) == Guns[i].scopeFrom) scope = def;
            }
            if (!own || !scope) { printf("weapons: %s: no %s to take the scope of\n", Guns[i].name, Guns[i].scopeFrom); continue; }
            for (uint32_t at = 1600; at <= 1628; at += 4)
                if (at != 1604) Guest::Write32(base, own + at, Guest::Read32(base, scope + at));
            printf("weapons: %s takes %s's scope (overlay %s)\n", Guns[i].name, Guns[i].scopeFrom, ReadText(base, Guest::Read32(base, own + 1616)).c_str());
        }
        fflush(stdout);
    }
    for (size_t i = 0; i < sizeof(Guns) / sizeof(Guns[0]); i++)
    {
        const int32_t weapon = g_campaignWeapon[i];
        if (weapon <= 0) continue;
        const uint32_t item = 0x82C75A48u + 52u * uint32_t(weapon);
        printf("items: %d %s: hash %08X name %s model %s icon %s weapon %u\n", weapon, Guns[i].name, Guest::Read32(base, item),
               ReadText(base, Guest::Read32(base, item + 4)).c_str(), ReadText(base, Guest::Read32(base, item + 12)).c_str(),
               ReadText(base, Guest::Read32(base, item + 20)).c_str(), Guest::Read32(base, item + 40));
    }
    fflush(stdout);
}

// Sounds played by name (sub_82556868: the sound's name in r5, from the
// map's sound bank, mp\<map>\<map>.wbk). The campaign guns' reload
// animations ask for their campaign sounds (GR_STEN, GR_fg42, ...), which
// are only in the campaign levels' banks: those play the reload sound of the
// gun each is a copy of instead. Every name asked for is logged once.
namespace
{
    struct SoundAlias { const char* campaign; const char* multiplayer; };
    const SoundAlias SoundAliases[] = {
        { "gr_sten", "GR_THOMPSON" },
        { "gr_fg42", "GR_BAR" },
        { "gr_bren", "GR_BAR" },
        { "gr_enfield_reload", "GR_KAR98" },
        { "gr_enfield_rechamber", "GF_KAR98_Rechamber" },
        { "sten_fire_1st", "THOMPSON_FIRE_1ST" },
        { "sten_fire_3rd", "THOMPSON_FIRE_3RD" },
        { "fg42_fire_1st", "BAR_FIRE_1ST" },
        { "fg42_fire_3rd", "BAR_FIRE_3RD" },
        { "bren_fire_1st", "BAR_FIRE_1ST" },
        { "bren_fire_3rd", "BAR_FIRE_3RD" },
        { "lee_enfield_fire_1st", "KAR98_FIRE_1ST" },
        { "lee_enfield_fire_3rd", "KAR98_FIRE_3RD" },
    };
}
extern "C" PPC_FUNC(__imp__sub_82556868);
PPC_FUNC(sub_82556868)
{
    if (ctx.r5.u32)
    {
        std::string text = ReadText(base, ctx.r5.u32, 64);
        static std::set<std::string> seen;
        static std::mutex lock;
        {
            std::lock_guard<std::mutex> guard(lock);
            if (seen.size() < 400 && seen.insert(text).second)
            {
                printf("sounds: %s asked for (from %08X)\n", text.c_str(), uint32_t(ctx.lr));
                fflush(stdout);
            }
        }
        for (char& c : text) c = char(tolower(static_cast<unsigned char>(c)));
        const bool ownShots = Settings::Get().campaignSounds && Settings::Get().campaignShotSounds;
        for (const SoundAlias& alias : SoundAliases)
            if (text == alias.campaign && !(ownShots && strstr(alias.campaign, "_fire_")))
            {
                static uint32_t names[sizeof(SoundAliases) / sizeof(SoundAliases[0])] = {};
                uint32_t& name = names[&alias - SoundAliases];
                if (!name) name = GuestText(base, alias.multiplayer);
                ctx.r5.u64 = name;
                break;
            }
    }
    __imp__sub_82556868(ctx, base);
}

// Diagnostics (0.5.44): every asset name looked up by name (sub_820B00C0
// makes an archive key from a name: models, effects, materials, ...), once
// each, and the HUD's image lookups (sub_824EE308: a list of the menus'
// image objects searched by name; not found, the default's), with the
// objects found for hud_ names described.
extern "C" PPC_FUNC(__imp__sub_820B00C0);
PPC_FUNC(sub_820B00C0)
{
    if (ctx.r4.u32)
    {
        static std::set<std::string> seen;
        static std::mutex lock;
        const std::string name = ReadText(base, ctx.r4.u32, 64);
        std::lock_guard<std::mutex> guard(lock);
        if (seen.size() < 4000 && seen.insert(name).second) printf("assets: %s (from %08X)\n", name.c_str(), uint32_t(ctx.lr));
    }
    __imp__sub_820B00C0(ctx, base);
}

extern "C" PPC_FUNC(__imp__sub_824EE308);
PPC_FUNC(sub_824EE308)
{
    const uint32_t list = ctx.r3.u32;
    const std::string name = ctx.r4.u32 ? ReadText(base, ctx.r4.u32, 64) : std::string();
    __imp__sub_824EE308(ctx, base);
    static std::set<std::string> seen;
    static std::mutex lock;
    std::lock_guard<std::mutex> guard(lock);
    if (seen.size() > 400 || !seen.insert(name + "@" + std::to_string(list)).second) return;
    const uint32_t found = ctx.r3.u32;
    const uint32_t fallback = Guest::Read32(base, 0x82C6B1ECu + 32u);
    printf("hud images: %s in list %08X (%u images) -> %08X%s\n", name.c_str(), list, list ? Guest::Read32(base, list + 8) : 0, found,
           found == fallback ? " (the default)" : "");
    if (name.rfind("hud_", 0) == 0 && found >= 0x40000000u && found < 0x90000000u)
    {
        auto words = [&](uint32_t at, int count)
        {
            std::string line; char text[16];
            for (int i = 0; i < count; i++) { snprintf(text, sizeof(text), " %08X", Guest::Read32(base, at + 4u * uint32_t(i))); line += text; }
            return line;
        };
        printf("hud images:   object%s\n", words(found, 48).c_str());
        const uint32_t record = Guest::Read32(base, found + 72);
        if (record >= 0x40000000u && record < 0x90000000u)
            printf("hud images:   +72 record %08X%s \"%s\"\n", record, words(record, 40).c_str(), ReadText(base, record + 12, 40).c_str());
        for (int i = 0; i < 48; i++)
        {
            const uint32_t to = Guest::Read32(base, found + 4u * uint32_t(i));
            if (i == 18 || to < 0x40000000u || to >= 0x90000000u || to == found) continue;
            printf("hud images:   +%d -> %08X%s\n", i * 4, to, words(to, 16).c_str());
        }
    }
    fflush(stdout);
}

// The HUD's images (sub_824784B8: an archive key r3, the asset's type r4,
// "TEX" 0x00584554 for an image): looked for in one archive only (the
// current one, sub_8246F1A0), else the default image. The pick-up prompt's
// gun icons are images by the weapon's icon name (sub_825026A8): the
// campaign guns' (hud_sten_smg, ...) are in the campaign weapons archives,
// so they came back as the default (a hand, or a white box). Not found,
// an image is looked for in the campaign archives too.
extern "C" PPC_FUNC(__imp__sub_824784B8);
PPC_FUNC(sub_824784B8)
{
    PPCContext saved = ctx;
    __imp__sub_824784B8(ctx, base);
    if (saved.r4.u32 != 0x00584554u || !Settings::Get().campaignIcons) return;
    const uint32_t fallback = Guest::Read32(base, 0x82A8C390);
    if (ctx.r3.u32 != fallback && ctx.r3.u32 != 0) return;
    for (const int32_t archive : g_archiveActive)
    {
        if (archive < 0) continue;
        PPCContext call = saved;
        call.r1.u32 = (saved.r1.u32 - 0x400) & ~0xFu;
        call.r3.u64 = uint32_t(archive);
        call.r4.u64 = saved.r3.u32;
        call.lr = 0;
        Guest::Lookup(0x82477E70)(call, base);
        if (call.r3.u32 != 0)
        {
            static int said = 0;
            if (said++ < 40)
            {
                std::string key;
                for (uint32_t i = 4; i < 32; i++) { const char c = char(Guest::Read8(base, saved.r3.u32 + i)); if (!c) break; key += c; }
                printf("hud images: image %s found in campaign archive %d -> %08X\n", key.c_str(), archive, call.r3.u32);
                fflush(stdout);
            }
            ctx.r3.u64 = call.r3.u32;
            return;
        }
    }
}

// The HUD's images made from the menus (sub_82503EB8: an image entry r30,
// its name at +4, its image at +0 from sub_825026A8). These are made at
// the front end, before the campaign archives are loaded: the Sten's
// (hud_sten_smg, in the list) got the default image (the white box). Each
// entry is logged once; the campaign icons' are kept, and given their
// images once the campaign archives are in (after the items are made).
namespace
{
    std::vector<std::pair<uint32_t, std::string>> g_iconEntries;
    std::mutex g_iconLock;
    std::string Lower(std::string text) { for (char& c : text) c = char(tolower(static_cast<unsigned char>(c))); return text; }
    bool IsCampaignIcon(const std::string& name)
    {
        std::string bare = Lower(name);
        if (bare.size() > 4 && bare.compare(bare.size() - 4, 4, ".tga") == 0) bare.resize(bare.size() - 4);
        for (const CampaignGun& gun : Guns) if (bare == Lower(gun.hud)) return true;
        return false;
    }
    void ResolveCampaignIcons(PPCContext& ctx, uint8_t* base)
    {
        std::lock_guard<std::mutex> guard(g_iconLock);
        for (const auto& [entry, name] : g_iconEntries)
        {
            std::string bare = name;
            if (bare.size() > 4 && Lower(bare).compare(bare.size() - 4, 4, ".tga") == 0) bare.resize(bare.size() - 4);
            for (const int32_t archive : g_archiveActive)
            {
                if (archive < 0) continue;
                PPCContext call = ctx;
                call.r1.u32 = (ctx.r1.u32 - 0x800) & ~0xFu;
                const uint32_t text = call.r1.u32 + 0x200, key = call.r1.u32 + 0x300;
                for (uint32_t i = 0; i < 0x100; i += 4) Guest::Write32(base, text + i, 0);
                for (size_t i = 0; i < bare.size() && i < 120; i++) Guest::Write8(base, text + uint32_t(i), uint8_t(bare[i]));
                call.r3.u64 = key; call.r4.u64 = text; call.lr = 0;
                Guest::Lookup(0x820B00C0)(call, base);
                call.r3.u64 = uint32_t(archive); call.r4.u64 = key; call.lr = 0;
                Guest::Lookup(0x82477E70)(call, base);
                if (call.r3.u32 != 0)
                {
                    printf("hud images: %s's entry %08X: image %08X (was %08X), campaign archive %d\n", name.c_str(), entry, call.r3.u32,
                           Guest::Read32(base, entry), archive);
                    Guest::Write32(base, entry, call.r3.u32);
                    break;
                }
            }
        }
        fflush(stdout);
    }
}
extern "C" PPC_FUNC(__imp__sub_82503EB8);
PPC_FUNC(sub_82503EB8)
{
    const uint32_t entry = ctx.r4.u32;
    __imp__sub_82503EB8(ctx, base);
    const uint32_t namePointer = Guest::Read32(base, entry + 4);
    if (namePointer < 0x40000000u || namePointer >= 0x90000000u) return;
    const std::string name = ReadText(base, namePointer, 64);
    static std::set<std::string> seen;
    std::lock_guard<std::mutex> guard(g_iconLock);
    if (seen.size() < 600 && seen.insert(name).second && name.rfind("hud_", 0) == 0)
    { printf("hud images: menu image %s entry %08X image %08X\n", name.c_str(), entry, Guest::Read32(base, entry)); fflush(stdout); }
    if (IsCampaignIcon(name)) g_iconEntries.emplace_back(entry, name);
}

// The HUD's weapon icons (sub_824EF868: per weapon i, its icon's image
// object from the menus' image lists at +52 + 4i of r3, 0 when none): the
// campaign guns' logged.
extern "C" PPC_FUNC(__imp__sub_824EF868);
extern "C" PPC_FUNC(__imp__sub_824EE308);
// The HUD's weapon icons (sub_824EF868: per weapon w, the image object of
// its icon from the menus' image lists, at +52 + 4w of r3; looked for by
// name in r5's list, then by name.tga in r4's; none, 0: the hand). The
// menus' list has the Sten's object (hud_sten_smg, with no image: the white
// box) but none for the others. Each campaign gun with no object gets one
// of the list's objects no multiplayer gun uses (the M1 Carbine's, the
// G43's, the MG34's (ge_), the Thompson's full auto), and each gets its own
// image (the campaign archives' hud_..., set on the object's parts at +100,
// as the object's own image setting does, sub_824E13F0).
PPC_FUNC(sub_824EF868)
{
    PPCContext saved = ctx;
    const uint32_t table = ctx.r3.u32, nameList = ctx.r5.u32, tgaList = ctx.r4.u32;
    __imp__sub_824EF868(ctx, base);
    if (!Settings::Get().campaignIcons) return;
    const uint32_t fallback = Guest::Read32(base, 0x82C6B1ECu + 32u);
    auto find = [&](uint32_t list, const std::string& name) -> uint32_t
    {
        if (!list) return 0;
        PPCContext call = saved;
        call.r1.u32 = (saved.r1.u32 - 0x800) & ~0xFu;
        const uint32_t text = call.r1.u32 + 0x200;
        for (uint32_t i = 0; i < 0x80; i += 4) Guest::Write32(base, text + i, 0);
        for (size_t i = 0; i < name.size() && i < 120; i++) Guest::Write8(base, text + uint32_t(i), uint8_t(name[i]));
        call.r3.u64 = list; call.r4.u64 = text; call.lr = 0;
        __imp__sub_824EE308(call, base);
        return call.r3.u32 == fallback ? 0 : call.r3.u32;
    };
    auto image = [&](const std::string& name) -> uint32_t
    {
        for (const int32_t archive : g_archiveActive)
        {
            if (archive < 0) continue;
            PPCContext call = saved;
            call.r1.u32 = (saved.r1.u32 - 0x800) & ~0xFu;
            const uint32_t text = call.r1.u32 + 0x200, key = call.r1.u32 + 0x300;
            for (uint32_t i = 0; i < 0x80; i += 4) Guest::Write32(base, text + i, 0);
            for (size_t i = 0; i < name.size() && i < 120; i++) Guest::Write8(base, text + uint32_t(i), uint8_t(name[i]));
            call.r3.u64 = key; call.r4.u64 = text; call.lr = 0;
            Guest::Lookup(0x820B00C0)(call, base);
            call.r3.u64 = uint32_t(archive); call.r4.u64 = key; call.lr = 0;
            Guest::Lookup(0x82477E70)(call, base);
            if (call.r3.u32) return call.r3.u32;
        }
        return 0;
    };
    // The Sten's icon object (hud_sten_smg, in the menus' list) is the
    // pattern: a gun with none gets its own copy of it (the object, its
    // parts list and its parts, 104 bytes each, from the game's allocator
    // sub_820DE6D8), with its own image on its parts. Not found, the list
    // gives its default object (the hand), which is never changed.
    const uint32_t hand = find(nameList, "\x01none");
    auto allocate = [&](uint32_t bytes) -> uint32_t
    {
        PPCContext call = saved;
        call.r1.u32 = (saved.r1.u32 - 0x800) & ~0xFu;
        call.r3.u64 = bytes; call.lr = 0;
        Guest::Lookup(0x820DE6D8)(call, base);
        return call.r3.u32;
    };
    uint32_t pattern = 0;
    for (size_t i = 0; i < sizeof(Guns) / sizeof(Guns[0]); i++)
    {
        const int32_t weapon = g_campaignWeapon[i];
        const uint32_t object = weapon > 0 ? Guest::Read32(base, table + 52 + 4 * uint32_t(weapon)) : 0;
        if (object && object != hand && Guest::Read32(base, object + 40) > 0) { pattern = object; break; }
    }
    for (size_t i = 0; i < sizeof(Guns) / sizeof(Guns[0]); i++)
    {
        const int32_t weapon = g_campaignWeapon[i];
        if (weapon <= 0) continue;
        const uint32_t slot = table + 52 + 4 * uint32_t(weapon);
        uint32_t object = Guest::Read32(base, slot);
        const uint32_t picture = image(Guns[i].hud);
        const char* given = "its own";
        if ((!object || object == hand) && pattern && picture)
        {
            const uint32_t count = std::min<uint32_t>(Guest::Read32(base, pattern + 40), 8);
            const uint32_t copy = allocate(0x100), list = allocate(4 * count + 4);
            if (copy && list)
            {
                for (uint32_t k = 0; k < 0x100; k += 4) Guest::Write32(base, copy + k, Guest::Read32(base, pattern + k));
                const uint32_t from = Guest::Read32(base, pattern + 32);
                for (uint32_t k = 0; k < count; k++)
                {
                    const uint32_t part = allocate(104), was = Guest::Read32(base, from + 4 * k);
                    if (part && was) for (uint32_t b = 0; b < 104; b += 4) Guest::Write32(base, part + b, Guest::Read32(base, was + b));
                    Guest::Write32(base, list + 4 * k, part);
                }
                Guest::Write32(base, copy + 32, list);
                object = copy;
                Guest::Write32(base, slot, object);
                given = "a copy of the Sten's";
            }
        }
        uint32_t parts = 0;
        if (object && object != hand && picture)
        {
            parts = Guest::Read32(base, object + 40);
            const uint32_t array = Guest::Read32(base, object + 32);
            for (uint32_t k = 0; k < parts && k < 8 && array; k++)
            {
                const uint32_t part = Guest::Read32(base, array + 4 * k);
                if (part) Guest::Write32(base, part + 100, picture);
            }
        }
        printf("hud images: weapon %d %s: icon object %08X (%s; the hand %08X), image %s %08X on %u part(s)\n", weapon, Guns[i].name, object,
               given, hand, Guns[i].hud, picture, parts);
    }
    fflush(stdout);
}
void ResolveCampaignIconsLater(PPCContext& ctx, uint8_t* base) { if (Settings::Get().campaignIcons) ResolveCampaignIcons(ctx, base); }

namespace
{   // the campaign gun whose event is being made (sub_82554BA0, below)
    thread_local const CampaignGun* t_eventGun = nullptr;
}
// The sound engine's cue lookups (0.5.47 diagnostics): by name in every
// bank (sub_8212B268, the name r3), by hash (sub_8212B300, r3), by a hash
// at r3 (sub_8212AF78): each asked-for one once, with who asked.
namespace
{
    void LogCue(const char* how, const std::string& what, uint32_t from, int32_t result)
    {
        static std::set<std::string> seen;
        static std::mutex lock;
        std::lock_guard<std::mutex> guard(lock);
        if (seen.size() < 1500 && seen.insert(what).second)
        { printf("cues: %s %s (from %08X) -> %d\n", how, what.c_str(), from, result); fflush(stdout); }
    }
}
extern "C" PPC_FUNC(__imp__sub_8212B268);
PPC_FUNC(sub_8212B268)
{
    if (t_eventGun && ctx.r3.u32)
    {
        const CampaignGun& gun = *t_eventGun;
        std::string copy = gun.from;
        for (char& c : copy) c = char(toupper(static_cast<unsigned char>(c)));
        const std::string cue = ReadText(base, ctx.r3.u32, 64);
        std::string upper = cue;
        for (char& c : upper) c = char(toupper(static_cast<unsigned char>(c)));
        const std::string prefix = copy + "_FIRE_";
        if (upper.rfind(prefix, 0) == 0)
        {
            static std::map<std::string, uint32_t> swapped;
            static std::mutex lock;
            const std::string own = std::string(gun.shot) + "_FIRE_" + upper.substr(prefix.size());
            uint32_t text;
            {
                std::lock_guard<std::mutex> guard(lock);
                uint32_t& t = swapped[own];
                if (!t) t = GuestText(base, own);
                text = t;
            }
            static int said = 0;
            if (said++ < 30) { printf("cues: %s's shot %s -> %s\n", gun.name, cue.c_str(), own.c_str()); fflush(stdout); }
            ctx.r3.u64 = text;
        }
    }
    const std::string name = ctx.r3.u32 ? ReadText(base, ctx.r3.u32, 64) : std::string();
    const uint32_t from = uint32_t(ctx.lr);
    __imp__sub_8212B268(ctx, base);
    LogCue("name", name, from, int32_t(ctx.r3.u32));
}
extern "C" PPC_FUNC(__imp__sub_8212B300);
PPC_FUNC(sub_8212B300)
{
    char text[16]; snprintf(text, sizeof(text), "%08X", ctx.r3.u32);
    const uint32_t from = uint32_t(ctx.lr);
    __imp__sub_8212B300(ctx, base);
    LogCue("hash", text, from, int32_t(ctx.r3.u32));
}
extern "C" PPC_FUNC(__imp__sub_8212AF78);
PPC_FUNC(sub_8212AF78)
{
    char text[16]; snprintf(text, sizeof(text), "%08X", ctx.r3.u32 ? Guest::Read32(base, ctx.r3.u32) : 0);
    const uint32_t from = uint32_t(ctx.lr);
    __imp__sub_8212AF78(ctx, base);
    LogCue("key", text, from, int32_t(ctx.r3.u32));
}

// The sounds of a weapon's events (sub_82554BA0: the event's name r4,
// looked up in the map archive's "event" table (sub_82490220), whose
// entries give the cue names, THOMPSON_FIRE_1ST, ...; the cue then found
// by name in the banks, sub_8212B268). The table has only the multiplayer
// guns: a campaign gun's event (its shots keep its own name, events 3, 7
// and 53) found nothing, silent. Its name is looked up as the copy's
// (sten as thompson), and while that event is made the copy's gunshot
// cues become the campaign gun's own (THOMPSON_FIRE_1ST: STEN_FIRE_1ST, in
// the campaign banks).
extern "C" PPC_FUNC(__imp__sub_82554BA0);
PPC_FUNC(sub_82554BA0)
{
    // The event's name r4 (the copy's: shots keep it, for the table and
    // the muzzle flash), the message r5 (+496 the shooter's sound handle).
    const CampaignGun* gun = nullptr;
    if (ctx.r4.u32 && ctx.r5.u32 && Settings::Get().campaignSounds && Settings::Get().campaignShotSounds)
    {
        const CampaignGun* fired = ShooterGun(Guest::Read32(base, ctx.r5.u32 + 496));
        if (fired)
        {
            const std::string name = Lower(ReadText(base, ctx.r4.u32, 64));
            const std::string copy = fired->from;
            if (name.rfind(copy, 0) == 0 && (name.size() == copy.size() || !isalnum(static_cast<unsigned char>(name[copy.size()]))))
                gun = fired;
            static std::set<std::string> seen;
            static std::mutex lock;
            std::lock_guard<std::mutex> guard(lock);
            if (seen.size() < 60 && seen.insert(name).second) { printf("cues: event %s by a %s shooter%s\n", name.c_str(), fired->name, gun ? "" : " (not its gun's)"); fflush(stdout); }
        }
    }
    const CampaignGun* outer = t_eventGun;
    t_eventGun = gun;
    __imp__sub_82554BA0(ctx, base);
    t_eventGun = outer;
}

// Text keys the game has no text for (sub_8254D180: the string table r3, the
// key r4, the text back): the campaign guns' own, given here.
extern "C" PPC_FUNC(__imp__sub_8254D180);
PPC_FUNC(sub_8254D180)
{
    if (ctx.r4.u32)
    {
        static const std::pair<const char*, const char*> Texts[] = { { "WEAPON_FG42_SCOPED", "FG42 Scoped" }, { "WEAPON_MILLS_BOMB", "Mills Bomb" } };
        const std::string key = ReadText(base, ctx.r4.u32, 40);
        static uint32_t made[sizeof(Texts) / sizeof(Texts[0])] = {};
        for (size_t i = 0; i < sizeof(Texts) / sizeof(Texts[0]); i++)
            if (key == Texts[i].first)
            {
                uint32_t& guest = made[i];
                if (!guest) guest = GuestText(base, Texts[i].second);
                ctx.r3.u64 = guest;
                return;
            }
    }
    __imp__sub_8254D180(ctx, base);
}
// 0.5.86: the Kangaroo's treads in line. The tread bones are moved by how
// far each side has gone (sub_82434918, from the model's drawing: the left
// tread by the vehicle record's +1312, the right by its +1308, times 0.0045;
// only while it moves). The Kangaroo's record is the Horch's, and a four
// wheeler keeps something else at +1308: the right tread stood out of
// place. Each frame its right side's distance is kept the left side's.
void MpFixesKangarooTreads(uint8_t* base)
{
    for (const uint32_t entity : g_kangarooEntities)
    {
        const uint32_t record = Guest::Read32(base, entity + 524);
        if (record < 0x40000000u || record >= 0x90000000u) continue;
        const uint32_t left = Guest::Read32(base, record + 1312);
        if (Guest::Read32(base, record + 1308) != left)
        {
            static int said = 0;
            if (said++ < 5) { uint32_t w = Guest::Read32(base, record + 1308); float r, l; memcpy(&r, &w, 4); memcpy(&l, &left, 4); printf("kangaroo: right tread's distance %.1f set to the left's %.1f\n", r, l); fflush(stdout); }
            Guest::Write32(base, record + 1308, left);
        }
    }
}
#endif
