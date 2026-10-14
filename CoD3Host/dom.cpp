// Domination (a PC addition, multiplayer only): the title's cut mode, made
// again.
//
// What the title left: "dom" among its game mode names (the devmap and
// map_restart commands know it as mode 7, with War's score limits), a
// Domination entry on the quick match screen, "spawn_dom_allies" and
// "spawn_dom_axis", and the flag models. What it cut: the mode's script (the
// game's start, sub_82218BE8, knows dm, tdm, ctf, hq, scf and war, and says
// "Unknown gametype" to the rest). Rouen alone still has the mode's flags in
// its map: five triggers, dom_flag_trigger1 to 5, one at each corner and one
// in the middle.
//
// Here: a Team Battle on a map with flags becomes Domination when the host
// turns it on (the Game Modes page, lobby_rules.cpp). Standing at a flag
// with nobody from the other side there takes it: ten seconds alone, less
// with more of the side there (up to three times as fast), nothing while both
// sides are there. Every five seconds each side scores a point for each flag
// it holds; kills score nothing for the side. The Team Battle's own score
// limit and end of the match stand (its score is the same team score).
//
// Every machine in System Link runs its own server, so the host's machine
// alone keeps the flags and the score: it sends them over the side channel
// ("C3DS") four times a second, and a joiner shows them and puts the score
// in his own game. The flags are drawn on the overlay (top of the screen):
// each flag's letter in its holder's colour, the distance to it, and who is
// taking it.

#ifdef COD3_MP

#include "dom.h"
#include "kernel.h"
#include "net.h"
#include "overlay.h"
#include "render_stats.h"

#include "imgui.h"

#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

extern "C" PPC_FUNC(__imp__sub_824B4690);   // the script's AddTeamScore(team, points)
extern "C" PPC_FUNC(__imp__sub_824B4730);   // the script's team scores back to nothing (a match's start)

bool LobbyDomination();   // lobby_rules.cpp
bool LobbyIsHost();

namespace
{
    // The team scores the script's GetTeamScore and AddTeamScore keep
    // (sub_824B45E8, sub_824B4690): the axis at +36, the allies at +40.
    constexpr uint32_t TeamScores = 0x82BA13E4, AxisScore = 36, AlliesScore = 40;
    constexpr uint32_t EntityList = 0x82BCC7FC;   // the players' entities, by client number + 1
    constexpr uint32_t MaxClients = 24;
    constexpr uint32_t CvarFind = 0x8254A468;

    constexpr int Flags = 5;
    constexpr float Radius = 200.0f, Height = 160.0f;   // a flag's reach, around and above or below
    constexpr float CaptureSeconds = 10.0f;
    constexpr uint32_t ScoreEveryMs = 5000;

    struct Place { const char* map; float at[Flags][3]; };
    // Rouen's five dom_flag_trigger entities, 1 to 5 as A to E, at the ground
    // (the triggers stand 36 above it: a player at C stands at 28).
    const Place Places[] = {
        { "rouen", { { -1184, -1392, 36 }, { 1152, -1376, 28 }, { 0, 0, 28 }, { -1152, 1376, 28 }, { 1080, 1352, 32 } } },
    };

    struct Flag
    {
        uint8_t owner = 0;       // 0 nobody, 1 the allies, 2 the axis
        uint8_t taker = 0;       // the side taking it
        float progress = 0.0f;   // 0 to 1
    };

    // What the overlay draws, kept under the lock (it draws on the window's
    // thread).
    struct Shown
    {
        bool active = false;
        Flag flags[Flags];
        float at[Flags][3] = {};
        int allies = 0, axis = 0;
        uint32_t mySide = 0;
        float me[3] = {};
        bool meAlive = false;
        float eye = 60.0f;            // eye height above the feet
        float pitch = 0, yaw = 0;     // where he looks (degrees; pitch down positive)
        float fov = 80.0f;            // cg_fov
        std::string message;
        uint32_t messageAt = 0;
        uint32_t frameAt = 0;
        int placed = 0;          // flags in place (fewer than five: the host is still placing them)
        bool host = false;    // the last server frame: none for a while, no match
    };
    std::mutex g_lock;
    Shown g_shown;

    // The game thread's own.
    bool g_active = false;
    float g_at[Flags][3] = {};          // where the flags stand (feet level)
    bool g_ready = false;               // the map looked at for flags
    int g_placed = 0;                   // flags in place, A first (5: the match runs)
    std::string g_map;
    uint32_t g_beganAt = 0;
    uint32_t g_models[Flags] = {};      // each flag's model (an entity handle), 0 for none
    uint8_t g_modelOwner[Flags] = {};   // whose flag each model shows
    int g_remakes[Flags] = {};          // how often each flag's banner was made again
    uint32_t g_flapAt[Flags] = {};      // when to set the made-again banner flapping (Dress)
    uint32_t g_bannerAt[Flags] = {};    // when each flag's banner was made, for the second one (Dress)
    uint32_t g_banners[Flags][3] = {};  // each flag's banners, by holder (1 American, 2 German), made when first wanted
    Flag g_flags[Flags];
    uint32_t g_lastFrame = 0, g_lastScore = 0, g_lastSend = 0;

    // The host's state as it last came (a joiner).
    std::atomic<bool> g_stateNew{ false };
    std::mutex g_stateLock;
    Flag g_stateFlags[Flags];
    int g_stateAllies = 0, g_stateAxis = 0, g_statePlaced = 0;
    float g_statePlaces[Flags][3] = {};
    uint32_t g_stateAt = 0;

    uint32_t Call(PPCContext& ctx, uint8_t* base, uint32_t function, uint32_t r3, uint32_t r4 = 0, uint32_t r5 = 0,
                  uint32_t r6 = 0)
    {
        PPCContext call = ctx;
        call.r1.u32 = (ctx.r1.u32 - 0x2000) & ~0xFu;
        call.r3.u64 = r3;
        call.r4.u64 = r4;
        call.r5.u64 = r5;
        call.r6.u64 = r6;
        call.lr = 0;
        Guest::Lookup(function)(call, base);
        return call.r3.u32;
    }

    float ReadFloat(uint8_t* base, uint32_t address)
    {
        const uint32_t bits = Guest::Read32(base, address);
        float f; memcpy(&f, &bits, 4); return f;
    }

    // A variable's text, by its name (written once into guest memory).
    std::string CvarText(PPCContext& ctx, uint8_t* base, const char* name)
    {
        static uint32_t names = 0;
        static uint32_t used = 0;
        if (names == 0) names = Guest::AllocatePhysical(0x1000);
        const uint32_t size = uint32_t(strlen(name)) + 1;
        if (used + size > 0x1000) used = 0;
        const uint32_t at = names + used;
        memcpy(base + at, name, size);
        used += (size + 3) & ~3u;
        const uint32_t cvar = Call(ctx, base, CvarFind, at);
        const uint32_t text = cvar ? Guest::Read32(base, cvar + 4) : 0;
        return text ? std::string(reinterpret_cast<const char*>(base + text)) : std::string();
    }

    uint32_t Side(uint8_t* base, uint32_t entity)
    {
        const uint32_t team = entity ? Guest::Read32(base, entity + 520) : 0;
        return team ? Guest::Read32(base, team + 4) : 0;
    }

    const char* SideName(uint32_t side) { return side == 1 ? "ALLIES" : side == 2 ? "AXIS" : "NOBODY"; }

    void Say(const std::string& text)
    {
        printf("dom: %s\n", text.c_str()); fflush(stdout);
        std::lock_guard<std::mutex> lock(g_lock);
        g_shown.message = text;
        g_shown.messageAt = GetTickCount();
    }

    // Whether this match is a Domination: asked for, a Team Battle, on a map
    // with flags.
    bool Decide(PPCContext& ctx, uint8_t* base)
    {
        if (!LobbyDomination()) return false;
        const std::string mode = CvarText(ctx, base, "mp_gametype");
        return _stricmp(mode.c_str(), "tdm") == 0;
    }

    void Begin(PPCContext& ctx, uint8_t* base)
    {
        g_active = Decide(ctx, base);
        g_ready = false;
        g_beganAt = GetTickCount();
        memset(g_models, 0, sizeof(g_models));
        memset(g_banners, 0, sizeof(g_banners));
        memset(g_bannerAt, 0, sizeof(g_bannerAt));
        memset(g_flapAt, 0, sizeof(g_flapAt));
        memset(g_modelOwner, 0, sizeof(g_modelOwner));
        for (Flag& flag : g_flags) flag = Flag();
        g_lastScore = g_lastSend = g_lastFrame = GetTickCount();
        printf("dom: a match begins; domination %s\n", g_active ? "on" : "off"); fflush(stdout);
        std::lock_guard<std::mutex> lock(g_lock);
        g_shown = Shown();
        g_shown.active = false;   // until the flags are found
    }

    // The script engine's own: its strings, its entity lookups, spawning a
    // model and setting one (what War's flags use, sub_82211880).
    constexpr uint32_t MakeString = 0x821E0E48;      // (out, text) -> a script string in out
    constexpr uint32_t FreeString = 0x821DD938;      // (string)
    constexpr uint32_t ArrayMake = 0x821D8D80;       // (array): an empty list
    constexpr uint32_t ArrayAt = 0x8221DB70;         // (array, i) -> the i-th's address
    constexpr uint32_t FindEntities = 0x821E21D8;    // (name, key, array, 0): the entities whose key is name
    constexpr uint32_t EntityOrigin = 0x821E3670;    // (out, entity) -> its origin
    constexpr uint32_t SetModel = 0x824CE130;        // (entity, model name, 0)
    constexpr uint32_t ScriptCalls = 0x82A98030;     // the engine's table of script functions
    constexpr uint32_t SpawnSlot = 612;              // its Spawn(class name, origin, 0) -> entity
    // The two keys the game's modes look entities up by (War's war_flag,
    // SCF's flag point, CTF's flags: the first; War's flag's target: the
    // second).
    constexpr uint32_t KeyA = 0x19F9F0E8, KeyB = 0x15B1F8A7;

    uint32_t g_scratch = 0;   // guest memory for the calls' strings and lists

    uint32_t Scratch(uint8_t* base)
    {
        if (!g_scratch) g_scratch = Guest::AllocatePhysical(0x1000);
        return g_scratch;
    }

    uint32_t Text(uint8_t* base, uint32_t at, const char* text)
    {
        const size_t size = strlen(text) + 1;
        memcpy(base + at, text, size);
        return at;
    }

    // The entities named so under a key: their handles, at most 64.
    std::vector<uint32_t> Find(PPCContext& ctx, uint8_t* base, const char* name, uint32_t key)
    {
        std::vector<uint32_t> found;
        const uint32_t scratch = Scratch(base);
        const uint32_t string = scratch, list = scratch + 0x10, text = scratch + 0x100;
        memset(base + list, 0, 0x40);
        Call(ctx, base, ArrayMake, list);
        Call(ctx, base, MakeString, string, Text(base, text, name));
        Call(ctx, base, FindEntities, string, key, list, 0);
        Call(ctx, base, FreeString, string);
        const int32_t count = int32_t(Guest::Read32(base, list + 8));
        for (int32_t i = 0; i < count && i < 64; i++)
        {
            const uint32_t at = Call(ctx, base, ArrayAt, list, uint32_t(i));
            if (at) found.push_back(Guest::Read32(base, at));
        }
        return found;
    }

    bool Origin(PPCContext& ctx, uint8_t* base, uint32_t entity, float out[3])
    {
        const uint32_t scratch = Scratch(base);
        Guest::Write32(base, scratch + 0x60, entity);
        const uint32_t at = Call(ctx, base, EntityOrigin, scratch + 0x70, scratch + 0x60);
        if (!at) return false;
        for (int i = 0; i < 3; i++) out[i] = ReadFloat(base, at + 4 * i);
        return true;
    }

    const char* FlagModel(uint8_t owner)
    {
        // The names as the archive spells them: the asset lookup goes by a
        // hash of the exact name, and the American one is "_US".
        // The Capture the Flag banners: they show on every machine (the
        // Domination ones only now and then, the American never).
        return owner == 1 ? "p_mp_ctf_flag_hanging_US" : owner == 2 ? "p_mp_ctf_flag_hanging_GR" : "p_mp_dom_flag_hanging_nt";
    }

    void Model(PPCContext& ctx, uint8_t* base, uint32_t entity, const char* model)
    {
        const uint32_t scratch = Scratch(base);
        Call(ctx, base, MakeString, scratch + 0x20, Text(base, scratch + 0x200, model));
        Call(ctx, base, SetModel, entity, scratch + 0x20, 0);
        Call(ctx, base, FreeString, scratch + 0x20);
        // A banner is cloth on bones: War sets it flapping as it sets its
        // model (sub_82211880, the script's PlayAnim at +184 of the table).
        if (strstr(model, "hanging"))
        {
            const uint32_t play = Guest::Read32(base, ScriptCalls + 184);
            if (play >= 0x82000000u && play < 0x82600000u)
            {
                Call(ctx, base, MakeString, scratch + 0x40, Text(base, scratch + 0x280, "FLAG_FLAPPING"));
                Call(ctx, base, play, entity, scratch + 0x40, 0, 0);
                static int said = 0;
                if (said++ < 12) { printf("dom: %s on %08X, flapping (%08X)\n", model, entity, play); fflush(stdout); }
                Call(ctx, base, FreeString, scratch + 0x40);
            }
        }
    }

    // A flag: the maps' own Domination pole at the ground (Rouen, Verdun and
    // Mayenne have it), and the banner hung from its top (a hanging banner's
    // model hangs down from its origin, as War's do from a wall).
    constexpr float PoleHeight = 150.0f;
    constexpr float PoleSink = 30.0f;   // Rouen's C is indoors: its finial went a foot through the ceiling
    uint32_t Plant(PPCContext& ctx, uint8_t* base, const float at[3], const char* model);
    uint32_t PlantFlag(PPCContext& ctx, uint8_t* base, const float at[3])
    {
        const float foot[3] = { at[0], at[1], at[2] - PoleSink };
        const uint32_t pole = Plant(ctx, base, foot, "p_mp_dom_flag_pole");
        // The banner hangs down from its origin on its own short staff: at
        // the pole's top, beside it.
        // The banner comes when the flag is first taken (Dress): a banner
        // made as the match starts never shows, one made during it does (the
        // F9 test), and the neutral one draws nothing anyway (its cloth is
        // not loaded in Team Battle), so an untaken flag is a bare pole.
        printf("dom: pole %08X at %.0f %.0f %.0f\n", pole, at[0], at[1], at[2]); fflush(stdout);
        return pole ? pole : 1;
    }

    uint32_t Plant(PPCContext& ctx, uint8_t* base, const float at[3], const char* model)
    {
        const uint32_t spawn = Guest::Read32(base, ScriptCalls + SpawnSlot);
        if (spawn < 0x82000000u || spawn >= 0x82600000u) return 0;
        const uint32_t scratch = Scratch(base);
        for (int i = 0; i < 3; i++) { uint32_t bits; memcpy(&bits, &at[i], 4); Guest::Write32(base, scratch + 0x80 + 4 * i, bits); }
        Call(ctx, base, MakeString, scratch + 0x30, Text(base, scratch + 0x300, "script_model"));
        const uint32_t entity = Call(ctx, base, spawn, scratch + 0x30, scratch + 0x80, 0);
        Call(ctx, base, FreeString, scratch + 0x30);
        if (entity) Model(ctx, base, entity, model);
        return entity;
    }

    // The flags' places kept for each map (dom_flags.txt beside the
    // executable): a map's name, then A to E's x y z.
    std::string FlagFile()
    {
        char path[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, path, MAX_PATH);
        std::string file(path);
        const size_t slash = file.find_last_of("\\/");
        return (slash == std::string::npos ? std::string() : file.substr(0, slash + 1)) + "dom_flags.txt";
    }

    bool LoadFlags(const std::string& map)
    {
        FILE* file = fopen(FlagFile().c_str(), "r");
        if (!file) return false;
        char line[512];
        bool found = false;
        while (fgets(line, sizeof(line), file))
        {
            char name[64] = {};
            float v[15] = {};
            if (sscanf(line, "%63s %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f", name, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5],
                       &v[6], &v[7], &v[8], &v[9], &v[10], &v[11], &v[12], &v[13], &v[14]) == 16 && map == name)
            {
                memcpy(g_at, v, sizeof(g_at));
                found = true;
            }
        }
        fclose(file);
        return found;
    }

    void SaveFlags(const std::string& map)
    {
        std::vector<std::string> kept;
        if (FILE* file = fopen(FlagFile().c_str(), "r"))
        {
            char line[512];
            while (fgets(line, sizeof(line), file))
            {
                char name[64] = {};
                if (sscanf(line, "%63s", name) == 1 && map != name) kept.push_back(line);
            }
            fclose(file);
        }
        if (FILE* file = fopen(FlagFile().c_str(), "w"))
        {
            for (const std::string& line : kept) fputs(line.c_str(), file);
            fprintf(file, "%s", map.c_str());
            for (int i = 0; i < Flags; i++) fprintf(file, " %.0f %.0f %.0f", g_at[i][0], g_at[i][1], g_at[i][2]);
            fprintf(file, "\n");
            fclose(file);
            printf("dom: flags for %s saved in %s\n", map.c_str(), FlagFile().c_str());
            fflush(stdout);
        }
    }

    // Where this map's flags go: placed before (the file), or Rouen's own.
    bool FindFlags(PPCContext& ctx, uint8_t* base)
    {
        g_map = CvarText(ctx, base, "mapname");
        for (char& c : g_map) c = char(tolower(static_cast<unsigned char>(c)));
        const size_t slash = g_map.find_last_of("\\/");
        if (slash != std::string::npos) g_map = g_map.substr(slash + 1);
        if (LoadFlags(g_map)) return true;
        for (const Place& place : Places)
            if (g_map.find(place.map) != std::string::npos)
            {
                memcpy(g_at, place.at, sizeof(g_at));
                return true;
            }
        return false;
    }

    void Setup(PPCContext& ctx, uint8_t* base)
    {
        g_ready = true;
        g_placed = 0;
        if (!FindFlags(ctx, base))
        {
            printf("dom: no flags for %s yet\n", g_map.c_str()); fflush(stdout);
            std::lock_guard<std::mutex> lock(g_lock);
            g_shown.active = true;
            g_shown.placed = 0;
            return;
        }
        g_placed = Flags;
        for (int i = 0; i < Flags; i++)
        {
            printf("dom: flag %c at %.0f %.0f %.0f\n", 'A' + i, g_at[i][0], g_at[i][1], g_at[i][2]);
            g_models[i] = PlantFlag(ctx, base, g_at[i]);
            g_modelOwner[i] = 0;
        }
        printf("dom: flags planted (models %08X %08X %08X %08X %08X)\n", g_models[0], g_models[1], g_models[2], g_models[3],
               g_models[4]);
        fflush(stdout);
        std::lock_guard<std::mutex> lock(g_lock);
        g_shown.active = true;
        g_shown.placed = g_placed;
        for (int i = 0; i < Flags; i++) memcpy(g_shown.at[i], g_at[i], sizeof(g_shown.at[i]));
    }

    // A test of the banners (F9): twenty of them in a row in front of the
    // player at head height, and the draws counted before and after, to
    // tell a banner the renderer draws wrongly from one it is never asked to
    // draw.
    void BannerTest(PPCContext& ctx, uint8_t* base)
    {
        static bool wasDown = false;
        static uint32_t testedAt = 0;
        static float drawsBefore = 0;
        const bool down = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
        const bool pressed = down && !wasDown;
        wasDown = down;
        const uint32_t now = GetTickCount();
        if (testedAt && now - testedAt > 3000)
        {
            const RenderStats::Snapshot after = RenderStats::Current();
            printf("dom: banner test: %.0f draws a frame before, %.0f after (%.0f k triangles)\n", drawsBefore, after.draws,
                   after.triangles / 1000.0f);
            fflush(stdout);
            Say("BANNER TEST: " + std::to_string(int(drawsBefore)) + " DRAWS BEFORE, " + std::to_string(int(after.draws)) + " AFTER");
            testedAt = 0;
        }
        if (!pressed) return;
        const uint32_t entities = Guest::Read32(base, EntityList);
        const uint32_t self = entities ? Guest::Read32(base, entities + 4) : 0;
        const uint32_t record = self ? Guest::Read32(base, self + 512) : 0;
        if (!record) return;
        drawsBefore = RenderStats::Current().draws;
        const float x = ReadFloat(base, record), y = ReadFloat(base, record + 4), z = ReadFloat(base, record + 8);
        const float yaw = ReadFloat(base, record + 208) * 3.14159265f / 180.0f;
        // The German and neutral ones, every spelling the archives have,
        // left to right: which of them draw.
        const char* models[] = { "p_mp_dom_flag_hanging_gr", "p_mp_dom_flag_hanging_GR", "p_mp_ctf_flag_hanging_GR",
                                 "p_mp_ctf_flag_hanging_gr", "w_flag3dper_gr", "p_mp_dom_flag_hanging_nt", "p_mp_dom_flag_hanging_NT",
                                 "p_mp_dom_flag_hanging_US", "w_flag3dper", "p_mp_ctf_flag_hanging_US" };
        for (int i = 0; i < 10; i++)
        {
            const float ahead = 160.0f, side = (i - 4.5f) * 40.0f;
            const float at[3] = { x + std::cos(yaw) * ahead + std::sin(yaw) * side, y + std::sin(yaw) * ahead - std::cos(yaw) * side,
                                  z + 100.0f };
            const uint32_t entity = Plant(ctx, base, at, models[i]);
            printf("dom: banner test %d (from the left) %s: %08X\n", i + 1, models[i], entity); fflush(stdout);
        }
        testedAt = now;
        Say("BANNER TEST: TEN BANNERS, LEFT TO RIGHT: 5 GERMAN, 2 NEUTRAL, 3 AMERICAN");
    }

    // A test of the campaign guns' models (F8): five models in a row in
    // front of the player, at chest height, left to right: the Kar98's world
    // model (one that shows, to compare), the Lee-Enfield's world and view
    // models, the Bren's world and view models. Which of them show tells
    // whether the campaign archive's models can be found by name at all.
    void FlagBannerTest(PPCContext& ctx, uint8_t* base)
    {
        static bool wasDown = false;
        const bool down = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
        const bool pressed = down && !wasDown;
        wasDown = down;
        if (!pressed) return;
        const uint32_t entities = Guest::Read32(base, EntityList);
        const uint32_t self = entities ? Guest::Read32(base, entities + 4) : 0;
        const uint32_t record = self ? Guest::Read32(base, self + 512) : 0;
        if (!record) return;
        const float x = ReadFloat(base, record), y = ReadFloat(base, record + 4), z = ReadFloat(base, record + 8);
        const float yaw = ReadFloat(base, record + 208) * 3.14159265f / 180.0f;
        const char* models[] = { "w_ge_RFL_WORLD", "w_lee_Enfield_WORLD", "lee_enfield", "w_bren_WORLD", "w_bren" };
        for (int i = 0; i < 5; i++)
        {
            const float ahead = 100.0f, side = (i - 2) * 30.0f;
            const float at[3] = { x + std::cos(yaw) * ahead + std::sin(yaw) * side, y + std::sin(yaw) * ahead - std::cos(yaw) * side,
                                  z + 50.0f };
            const uint32_t entity = Plant(ctx, base, at, models[i]);
            printf("dom: model test %d (from the left) %s: %08X\n", i + 1, models[i], entity);
        }
        fflush(stdout);
        Say("MODEL TEST: KAR98, LEE-ENFIELD WORLD, LEE-ENFIELD VIEW, BREN WORLD, BREN VIEW");
    }

    // The host, on a map with no flags yet: F7 puts the next flag where he
    // stands; with the fifth the map's flags are kept and the match runs.
    void PlaceFlags(PPCContext& ctx, uint8_t* base)
    {
        static bool wasDown = false;
        const bool down = (GetAsyncKeyState(VK_F7) & 0x8000) != 0;
        const bool pressed = down && !wasDown;
        wasDown = down;
        if (!pressed || g_placed >= Flags) return;
        const uint32_t entities = Guest::Read32(base, EntityList);
        const uint32_t self = entities ? Guest::Read32(base, entities + 4) : 0;
        const uint32_t record = self ? Guest::Read32(base, self + 512) : 0;
        if (!record || int32_t(Guest::Read32(base, record + 304)) <= 0) return;
        const int i = g_placed;
        g_at[i][0] = ReadFloat(base, record); g_at[i][1] = ReadFloat(base, record + 4); g_at[i][2] = ReadFloat(base, record + 8);
        g_models[i] = PlantFlag(ctx, base, g_at[i]);
        g_modelOwner[i] = 0;
        g_placed++;
        printf("dom: flag %c placed at %.0f %.0f %.0f\n", 'A' + i, g_at[i][0], g_at[i][1], g_at[i][2]); fflush(stdout);
        if (g_placed == Flags)
        {
            SaveFlags(g_map);
            Say("ALL FIVE FLAGS PLACED: DOMINATION IS ON");
        }
        else Say(std::string("FLAG ") + char('A' + i) + " PLACED");
        std::lock_guard<std::mutex> lock(g_lock);
        g_shown.placed = g_placed;
        memcpy(g_shown.at, g_at, sizeof(g_at));
    }

    float g_dressDraws = 0;
    uint32_t g_dressedAt = 0;
    // Each flag's model in its holder's colours.
    // A model that draws nothing (the American Domination banner: seen never
    // to show): how a banner is put away. (The neutral one shows a bare
    // staff, the German CTF one a German flag.)
    constexpr const char* Hidden = "p_mp_dom_flag_hanging_US";
    // The banner's cloth sits at the foot of its own short staff (seen at
    // the bottom of the pole), so it is raised to hang near the pole's top.
    constexpr float BannerLift = 60.0f;
    void Dress(PPCContext& ctx, uint8_t* base)
    {
        const uint32_t now = GetTickCount();
        for (int i = 0; i < Flags; i++)
        {
            const uint8_t owner = g_flags[i].owner;
            if (g_models[i] && g_modelOwner[i] != owner && owner <= 2)
            {
                // The old holder's banner put away, a new one made for the
                // new holder.
                if (g_banners[i][0]) Model(ctx, base, g_banners[i][0], Hidden);
                g_banners[i][0] = 0;
                g_modelOwner[i] = owner;
                g_bannerAt[i] = 0;
                if (owner)
                {
                    // A hanging banner is a whole flag on its own staff, its
                    // origin at the staff's foot, so it stands where the pole does.
                    const float foot[3] = { g_at[i][0], g_at[i][1], g_at[i][2] + BannerLift };
                    g_banners[i][0] = Plant(ctx, base, foot, FlagModel(owner));
                    g_bannerAt[i] = now;
                    g_remakes[i] = 0;
                    printf("dom: flag %c shows %s (%08X)\n", 'A' + i, FlagModel(owner), g_banners[i][0]); fflush(stdout);
                    g_dressDraws = RenderStats::Current().draws;
                    g_dressedAt = now;
                }
            }
            // The banners made first never show, ones made a little later
            // do (the F8 and F9 tests: the first made in a match stayed
            // unseen wherever they stood): two seconds on, the banner is
            // made again and the first put away.
            // Made again four times (two, four, eight and sixteen seconds
            // on): a slower machine (a joiner's) may not have the model ready
            // by the first ones.
            if (g_bannerAt[i] && now - g_bannerAt[i] > (2000u << (g_remakes[i] > 0 ? g_remakes[i] - 1 : 0)) && g_modelOwner[i])
            {
                g_remakes[i]++;
                g_bannerAt[i] = g_remakes[i] < 4 ? now : 0;
                const float foot[3] = { g_at[i][0], g_at[i][1], g_at[i][2] + BannerLift };
                const uint32_t again = Plant(ctx, base, foot, FlagModel(g_modelOwner[i]));
                if (again)
                {
                    if (g_banners[i][0]) Model(ctx, base, g_banners[i][0], Hidden);
                    g_banners[i][0] = again;
                    g_flapAt[i] = now + 1000;
                }
                printf("dom: flag %c's banner made again (%08X)\n", 'A' + i, again); fflush(stdout);
            }
        }
        // The made-again banner set flapping once more, a second on (the
        // first time, as it was made, it stayed still).
        for (int i = 0; i < Flags; i++)
            if (g_flapAt[i] && int32_t(now - g_flapAt[i]) >= 0)
            {
                g_flapAt[i] = 0;
                const uint32_t play = Guest::Read32(base, ScriptCalls + 184);
                if (g_banners[i][0] && play >= 0x82000000u && play < 0x82600000u)
                {
                    const uint32_t scratch = Scratch(base);
                    Call(ctx, base, MakeString, scratch + 0x40, Text(base, scratch + 0x280, "FLAG_FLAPPING"));
                    Call(ctx, base, play, g_banners[i][0], scratch + 0x40, 0, 0);
                    Call(ctx, base, FreeString, scratch + 0x40);
                    printf("dom: flag %c's banner set flapping again\n", 'A' + i); fflush(stdout);
                }
            }
        // Whether the renderer is asked to draw more once a banner is up
        // (some seconds on, the draws a frame against those before).
        if (g_dressedAt && GetTickCount() - g_dressedAt > 3000)
        {
            const RenderStats::Snapshot after = RenderStats::Current();
            printf("dom: after the banner, %.0f draws a frame (before %.0f), %.0f k triangles\n", after.draws, g_dressDraws,
                   after.triangles / 1000.0f);
            fflush(stdout);
            g_dressedAt = 0;
        }
    }

    void SendState(uint8_t* base);

    // The host: who stands at each flag, the flags taken, the points.
    void Run(uint8_t* base, float seconds, uint32_t now)
    {
        int present[Flags][3] = {};
        const uint32_t entities = Guest::Read32(base, EntityList);
        for (uint32_t client = 0; entities && client < MaxClients; client++)
        {
            const uint32_t entity = Guest::Read32(base, entities + 4 * (client + 1));
            const uint32_t record = entity ? Guest::Read32(base, entity + 512) : 0;
            if (!record) continue;
            if (int32_t(Guest::Read32(base, record + 304)) <= 0) continue;   // dead
            if (Guest::Read32(base, record + 1924) != 3) continue;           // not in the game
            const uint32_t side = Side(base, entity);
            if (side != 1 && side != 2) continue;
            const float x = ReadFloat(base, record), y = ReadFloat(base, record + 4), z = ReadFloat(base, record + 8);
            for (int i = 0; i < Flags; i++)
            {
                const float dx = x - g_at[i][0], dy = y - g_at[i][1], dz = z - g_at[i][2];
                if (dx * dx + dy * dy <= Radius * Radius && std::fabs(dz) <= Height) present[i][side]++;
            }
        }
        for (int i = 0; i < Flags; i++)
        {
            Flag& flag = g_flags[i];
            const int allies = present[i][1], axis = present[i][2];
            if (allies > 0 && axis > 0) continue;   // fought over: nothing moves
            const uint32_t side = allies > 0 ? 1 : axis > 0 ? 2 : 0;
            const int count = allies + axis;
            if (side == 0 || side == flag.owner)
            {
                // Nobody taking it: what was taken goes back.
                flag.progress -= seconds / CaptureSeconds;
                if (flag.progress <= 0.0f) { flag.progress = 0.0f; flag.taker = 0; }
                continue;
            }
            if (flag.taker != side) { flag.taker = uint8_t(side); flag.progress = 0.0f; }
            flag.progress += seconds * float(count < 3 ? count : 3) / CaptureSeconds;
            if (flag.progress >= 1.0f)
            {
                flag.owner = uint8_t(side);
                flag.taker = 0;
                flag.progress = 0.0f;
                Say(std::string(SideName(side)) + " TOOK FLAG " + char('A' + i));
            }
        }
        if (now - g_lastScore >= ScoreEveryMs)
        {
            g_lastScore += ScoreEveryMs;
            if (now - g_lastScore > ScoreEveryMs) g_lastScore = now;
            int allies = 0, axis = 0;
            for (const Flag& flag : g_flags) { if (flag.owner == 1) allies++; else if (flag.owner == 2) axis++; }
            if (allies) Guest::Write32(base, TeamScores + AlliesScore, Guest::Read32(base, TeamScores + AlliesScore) + allies);
            if (axis) Guest::Write32(base, TeamScores + AxisScore, Guest::Read32(base, TeamScores + AxisScore) + axis);
        }
        if (now - g_lastSend >= 250)
        {
            g_lastSend = now;
            SendState(base);
        }
    }

    // The host's state for the joiners: "C3DS", 2, the scores, how many
    // flags are placed, each flag's holder, taker and progress, and where
    // each stands.
    constexpr int PacketSize = 4 + 1 + 8 + 1 + 3 * Flags + 12 * Flags;
    void SendState(uint8_t* base)
    {
        uint8_t packet[PacketSize] = { 'C', '3', 'D', 'S', 2 };
        const uint32_t allies = Guest::Read32(base, TeamScores + AlliesScore), axis = Guest::Read32(base, TeamScores + AxisScore);
        memcpy(packet + 5, &allies, 4);
        memcpy(packet + 9, &axis, 4);
        packet[13] = uint8_t(g_placed);
        for (int i = 0; i < Flags; i++)
        {
            packet[14 + 3 * i] = g_flags[i].owner;
            packet[15 + 3 * i] = g_flags[i].taker;
            packet[16 + 3 * i] = uint8_t(g_flags[i].progress * 255.0f);
        }
        memcpy(packet + 14 + 3 * Flags, g_at, sizeof(g_at));
        Net::SideSend(packet, sizeof(packet));
    }

    PPCContext* g_ctx = nullptr;   // this frame's, for Follow's calls

    // A joiner: the host's flags and score, as they last came.
    void Follow(uint8_t* base)
    {
        if (!g_stateNew.exchange(false)) return;
        Flag flags[Flags];
        int allies, axis, placed;
        float at[Flags][3];
        {
            std::lock_guard<std::mutex> lock(g_stateLock);
            memcpy(flags, g_stateFlags, sizeof(flags));
            allies = g_stateAllies; axis = g_stateAxis; placed = g_statePlaced;
            memcpy(at, g_statePlaces, sizeof(at));
        }
        // The host's flags where he has them, each planted here as it comes.
        for (int i = 0; i < placed; i++)
        {
            const bool moved = std::fabs(at[i][0] - g_at[i][0]) > 1.0f || std::fabs(at[i][1] - g_at[i][1]) > 1.0f;
            memcpy(g_at[i], at[i], sizeof(g_at[i]));
            if (i >= g_placed || (moved && !g_models[i]))
            {
                g_models[i] = PlantFlag(*g_ctx, base, g_at[i]);
                g_modelOwner[i] = 0;
            }
        }
        if (placed != g_placed)
        {
            g_placed = placed;
            std::lock_guard<std::mutex> lock(g_lock);
            g_shown.placed = g_placed;
            memcpy(g_shown.at, g_at, sizeof(g_at));
        }
        for (int i = 0; i < Flags; i++)
        {
            if (flags[i].owner != g_flags[i].owner && flags[i].owner != 0)
                Say(std::string(SideName(flags[i].owner)) + " TOOK FLAG " + char('A' + i));
            g_flags[i] = flags[i];
        }
        Guest::Write32(base, TeamScores + AlliesScore, uint32_t(allies));
        Guest::Write32(base, TeamScores + AxisScore, uint32_t(axis));
    }

    // The overlay's part: the flags across the top of the screen.
    bool HudActive()
    {
        // Not over the menus between matches (the server still runs there):
        // a menu's picture has next to no triangles.
        if (RenderStats::Current().triangles < 20000.0f) return false;
        std::lock_guard<std::mutex> lock(g_lock);
        return g_shown.active && GetTickCount() - g_shown.frameAt < 1000;
    }

    void HudDraw(int width, int height)
    {
        Shown shown;
        {
            std::lock_guard<std::mutex> lock(g_lock);
            shown = g_shown;
        }
        if (!shown.active) return;
        ImDrawList* draw = ImGui::GetForegroundDrawList();
        ImFont* font = ImGui::GetFont();
        const float scale = height / 1080.0f;
        const float box = 46.0f * scale, gap = 14.0f * scale, big = 30.0f * scale, small = 18.0f * scale;
        const float total = Flags * box + (Flags - 1) * gap;
        float x = (width - total) * 0.5f;
        const float y = 54.0f * scale;
        const ImU32 friendly = IM_COL32(70, 140, 255, 235), enemy = IM_COL32(230, 70, 60, 235),
                    neutral = IM_COL32(150, 150, 150, 200), white = IM_COL32(255, 255, 255, 255),
                    shadow = IM_COL32(0, 0, 0, 170);
        auto colour = [&](uint32_t side) { return side == 0 ? neutral : (shown.mySide == 0 || side == shown.mySide) ? friendly : enemy; };

        static bool said = false;
        if (!said) { said = true; printf("dom: the flags drawn on a %dx%d picture\n", width, height); fflush(stdout); }

        // Each flag where it stands, over the picture: its letter in its
        // holder's colour and how far, at the edge of the screen when it is
        // out of sight.
        if (shown.meAlive)
        {
            const float rad = 3.14159265f / 180.0f;
            const float cp = std::cos(shown.pitch * rad), sp = std::sin(shown.pitch * rad);
            const float cy = std::cos(shown.yaw * rad), sy = std::sin(shown.yaw * rad);
            const float forward[3] = { cp * cy, cp * sy, -sp };
            const float right[3] = { sy, -cy, 0.0f };
            const float up[3] = { sp * cy, sp * sy, cp };
            const float aspect = float(width) / float(height);
            // cg_fov is the width's at 4:3; a wider picture sees more.
            const float tanX = std::tan(shown.fov * 0.5f * rad) * aspect / (4.0f / 3.0f);
            const float tanY = tanX / aspect;
            const float eye[3] = { shown.me[0], shown.me[1], shown.me[2] + shown.eye };
            for (int i = 0; i < shown.placed; i++)
            {
                const float d[3] = { shown.at[i][0] - eye[0], shown.at[i][1] - eye[1], shown.at[i][2] + 70.0f - eye[2] };
                const float fz = d[0] * forward[0] + d[1] * forward[1] + d[2] * forward[2];
                const float fx = d[0] * right[0] + d[1] * right[1] + d[2] * right[2];
                const float fy = d[0] * up[0] + d[1] * up[1] + d[2] * up[2];
                float sx, syy;
                bool edge = false;
                if (fz > 1.0f)
                {
                    sx = width * 0.5f * (1.0f + fx / (fz * tanX));
                    syy = height * 0.5f * (1.0f - fy / (fz * tanY));
                }
                else
                {
                    // Behind: toward the side it is on, at the bottom.
                    sx = fx >= 0 ? float(width) : 0.0f;
                    syy = height * 0.75f;
                    edge = true;
                }
                const float margin = 40.0f * scale;
                if (sx < margin) { sx = margin; edge = true; }
                if (sx > width - margin) { sx = width - margin; edge = true; }
                if (syy < margin + 120.0f * scale) { syy = margin + 120.0f * scale; edge = true; }
                if (syy > height - margin) { syy = height - margin; edge = true; }
                const Flag& flag = shown.flags[i];
                const float r = (edge ? 14.0f : 18.0f) * scale;
                const ImVec2 c(sx, syy);
                draw->AddQuadFilled(ImVec2(c.x, c.y - r - 2 * scale), ImVec2(c.x + r + 2 * scale, c.y), ImVec2(c.x, c.y + r + 2 * scale),
                                    ImVec2(c.x - r - 2 * scale, c.y), shadow);
                draw->AddQuadFilled(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y),
                                    colour(flag.owner));
                if (flag.taker != 0 && flag.progress > 0.0f)
                    draw->AddRectFilled(ImVec2(c.x - r, c.y + r + 4 * scale), ImVec2(c.x - r + 2 * r * flag.progress, c.y + r + 8 * scale),
                                        colour(flag.taker));
                const char letter[2] = { char('A' + i), 0 };
                const float letterSize = (edge ? 20.0f : 26.0f) * scale;
                const ImVec2 size = font->CalcTextSizeA(letterSize, FLT_MAX, 0.0f, letter);
                draw->AddText(font, letterSize, ImVec2(c.x - size.x * 0.5f, c.y - size.y * 0.5f), white, letter);
                char metres[16];
                snprintf(metres, sizeof(metres), "%.0fm", std::sqrt(d[0] * d[0] + d[1] * d[1]) * 0.0254f);
                const ImVec2 mSize = font->CalcTextSizeA(small, FLT_MAX, 0.0f, metres);
                draw->AddText(font, small, ImVec2(c.x - mSize.x * 0.5f + 1, c.y + r + 10 * scale + 1), shadow, metres);
                draw->AddText(font, small, ImVec2(c.x - mSize.x * 0.5f, c.y + r + 10 * scale), white, metres);
            }
        }

        // The score: ours first.
        char score[96];
        const uint32_t mine = shown.mySide == 2 ? 2 : 1;
        snprintf(score, sizeof(score), "DOMINATION   %s %d  -  %d %s", SideName(mine), mine == 1 ? shown.allies : shown.axis,
                 mine == 1 ? shown.axis : shown.allies, SideName(mine == 1 ? 2 : 1));
        const ImVec2 scoreSize = font->CalcTextSizeA(small, FLT_MAX, 0.0f, score);
        draw->AddText(font, small, ImVec2((width - scoreSize.x) * 0.5f + 1, y - small - 6 * scale + 1), shadow, score);
        draw->AddText(font, small, ImVec2((width - scoreSize.x) * 0.5f, y - small - 6 * scale), white, score);

        int standingAt = -1;
        for (int i = 0; i < Flags; i++, x += box + gap)
        {
            if (i >= shown.placed)
            {
                // Not placed yet: an empty outline.
                draw->AddRect(ImVec2(x, y), ImVec2(x + box, y + box), neutral, 6 * scale, 0, 2 * scale);
                continue;
            }
            const Flag& flag = shown.flags[i];
            draw->AddRectFilled(ImVec2(x, y), ImVec2(x + box, y + box), shadow, 6 * scale);
            draw->AddRectFilled(ImVec2(x + 3 * scale, y + 3 * scale), ImVec2(x + box - 3 * scale, y + box - 3 * scale),
                                colour(flag.owner), 5 * scale);
            if (flag.taker != 0 && flag.progress > 0.0f)
            {
                const float barY = y + box + 4 * scale;
                draw->AddRectFilled(ImVec2(x, barY), ImVec2(x + box, barY + 6 * scale), shadow);
                draw->AddRectFilled(ImVec2(x, barY), ImVec2(x + box * flag.progress, barY + 6 * scale), colour(flag.taker));
            }
            const char letter[2] = { char('A' + i), 0 };
            const ImVec2 size = font->CalcTextSizeA(big, FLT_MAX, 0.0f, letter);
            draw->AddText(font, big, ImVec2(x + (box - size.x) * 0.5f, y + (box - size.y) * 0.5f), white, letter);
            // How far, in metres (a unit is an inch).
            const float dx = shown.at[i][0] - shown.me[0], dy = shown.at[i][1] - shown.me[1], dz = shown.at[i][2] - shown.me[2];
            const float metres = std::sqrt(dx * dx + dy * dy) * 0.0254f;
            if (shown.meAlive && dx * dx + dy * dy <= Radius * Radius && std::fabs(dz) <= Height) standingAt = i;
            char distance[16];
            snprintf(distance, sizeof(distance), "%.0fm", metres);
            const ImVec2 farSize = font->CalcTextSizeA(small, FLT_MAX, 0.0f, distance);
            draw->AddText(font, small, ImVec2(x + (box - farSize.x) * 0.5f, y + box + 12 * scale), white, distance);
        }

        std::string line;
        if (standingAt >= 0)
        {
            const Flag& flag = shown.flags[standingAt];
            const char letter = char('A' + standingAt);
            if (flag.owner == shown.mySide) line = std::string("HOLDING FLAG ") + letter;
            else if (flag.taker == shown.mySide && flag.progress > 0.0f)
                line = std::string("TAKING FLAG ") + letter + " " + std::to_string(int(flag.progress * 100)) + "%";
            else line = std::string("AT FLAG ") + letter;
        }
        if (shown.placed < Flags)
        {
            line = shown.host ? std::string("NO FLAGS ON THIS MAP YET: STAND WHERE FLAG ") + char('A' + shown.placed) +
                                    " GOES AND PRESS F7"
                              : std::string("WAITING FOR THE HOST TO PLACE THE FLAGS");
        }
        if (!shown.message.empty() && GetTickCount() - shown.messageAt < 4000)
            line = line.empty() ? shown.message : shown.message + "     " + line;
        if (!line.empty())
        {
            const ImVec2 size = font->CalcTextSizeA(small * 1.2f, FLT_MAX, 0.0f, line.c_str());
            const ImVec2 at((width - size.x) * 0.5f, y + box + 40 * scale);
            draw->AddText(font, small * 1.2f, ImVec2(at.x + 1, at.y + 1), shadow, line.c_str());
            draw->AddText(font, small * 1.2f, at, white, line.c_str());
        }
    }
}

void Domination::Frame(PPCContext& ctx, uint8_t* base)
{
    static bool registered = false;
    if (!registered) { registered = true; Overlay::SetHud(HudActive, HudDraw); }
    if (!g_active)
    {
        // In case the match began without the scores put back (a joiner, say).
        static uint32_t lastLook = 0;
        const uint32_t now = GetTickCount();
        if (now - lastLook < 2000) return;
        lastLook = now;
        if (!Decide(ctx, base)) return;
        Begin(ctx, base);
        if (!g_active) return;
    }
    const uint32_t now = GetTickCount();
    if (!g_ready)
    {
        // The map's entities are all there a moment after the start.
        if (now - g_beganAt < 2500) return;
        Setup(ctx, base);
        if (!g_active) return;
        g_lastFrame = g_lastScore = g_lastSend = now;
    }
    const float seconds = std::fmin(float(now - g_lastFrame) / 1000.0f, 0.5f);
    g_lastFrame = now;
    g_ctx = &ctx;
    BannerTest(ctx, base);
    FlagBannerTest(ctx, base);
    if (LobbyIsHost())
    {
        if (g_placed < Flags)
        {
            PlaceFlags(ctx, base);
            if (now - g_lastSend >= 500) { g_lastSend = now; SendState(base); }
        }
        else Run(base, seconds, now);
    }
    else Follow(base);
    Dress(ctx, base);

    // For the overlay: this machine's player and the flags.
    const uint32_t entities = Guest::Read32(base, EntityList);
    const uint32_t self = entities ? Guest::Read32(base, entities + 4) : 0;
    const uint32_t record = self ? Guest::Read32(base, self + 512) : 0;
    std::lock_guard<std::mutex> lock(g_lock);
    g_shown.frameAt = GetTickCount();
    g_shown.host = LobbyIsHost();
    memcpy(g_shown.flags, g_flags, sizeof(g_flags));
    g_shown.allies = int32_t(Guest::Read32(base, TeamScores + AlliesScore));
    g_shown.axis = int32_t(Guest::Read32(base, TeamScores + AxisScore));
    g_shown.mySide = Side(base, self);
    g_shown.meAlive = record && int32_t(Guest::Read32(base, record + 304)) > 0;
    if (record)
    {
        g_shown.me[0] = ReadFloat(base, record); g_shown.me[1] = ReadFloat(base, record + 4); g_shown.me[2] = ReadFloat(base, record + 8);
        const float eye = ReadFloat(base, record + 220);
        g_shown.eye = eye > 10.0f && eye < 100.0f ? eye : 60.0f;
        g_shown.pitch = ReadFloat(base, record + 204);
        g_shown.yaw = ReadFloat(base, record + 208);
    }
    static uint32_t lastFov = 0, lastLog = 0;
    // (now: this frame's, above)
    if (now - lastFov > 3000)
    {
        lastFov = now;
        const float fov = float(atof(CvarText(ctx, base, "cg_fov").c_str()));
        g_shown.fov = fov > 20.0f && fov < 140.0f ? fov : 80.0f;
    }
    if (record && now - lastLog > 5000)
    {
        lastLog = now;
        printf("dom: me at %.0f %.0f %.0f, eye %.0f, view %.1f %.1f (+2096: %.1f %.1f), fov %.0f\n", g_shown.me[0], g_shown.me[1],
               g_shown.me[2], g_shown.eye, g_shown.pitch, g_shown.yaw, ReadFloat(base, record + 2096), ReadFloat(base, record + 2100), g_shown.fov);
        fflush(stdout);
    }
}

void Domination::OnState(const uint8_t* packet, int size, unsigned int)
{
    if (size < PacketSize || packet[4] != 2 || LobbyIsHost()) return;
    std::lock_guard<std::mutex> lock(g_stateLock);
    memcpy(&g_stateAllies, packet + 5, 4);
    memcpy(&g_stateAxis, packet + 9, 4);
    g_statePlaced = packet[13] <= Flags ? packet[13] : 0;
    for (int i = 0; i < Flags; i++)
    {
        g_stateFlags[i].owner = packet[14 + 3 * i] <= 2 ? packet[14 + 3 * i] : 0;
        g_stateFlags[i].taker = packet[15 + 3 * i] <= 2 ? packet[15 + 3 * i] : 0;
        g_stateFlags[i].progress = packet[16 + 3 * i] / 255.0f;
    }
    memcpy(g_statePlaces, packet + 14 + 3 * Flags, sizeof(g_statePlaces));
    g_stateAt = GetTickCount();
    g_stateNew = true;
}

// The script's team scores back to nothing: a match begins.
PPC_FUNC(sub_824B4730)
{
    __imp__sub_824B4730(ctx, base);
    Begin(ctx, base);
}

// The script's AddTeamScore: in a Domination the sides score by the flags
// alone, not by kills.
PPC_FUNC(sub_824B4690)
{
    if (g_active) return;
    __imp__sub_824B4690(ctx, base);
}

#endif
