// Bots: players on the host with no one behind them (a PC addition,
// multiplayer only). The first step: a bot that joins and stands.
//
// How a player is added (from the title's own join, sub_824046D8, called by
// the session layer's HandlePlayerSetup for a player who has joined through
// the network): a free slot of the server's 24 clients (4992 bytes each, the
// array's address at +8 of svs, 0x82B3FDE0; the slot's state at +0, zero is
// free) is taken and set up (sub_82532540: its player entity), the player is
// connected into the world (sub_82593608, with where to stand and which way
// to face), the state made 1, the player's game record cleared in the places
// the title clears it, and the player begun (sub_8257A6C8). A player who
// joined over the network is then sent the game state (sub_82530720); a bot
// has no one to send it to, and nothing is ever sent to its slot
// (SV_SendClientSnapshot, sub_825320D0, is skipped for it).
//
// How a player is moved: every frame the game runs each player
// (ClientThink, sub_82593518) on the latest command in that player's ring
// of 64 (48 bytes each, at +1688 of the player's 6352 bytes at 0x82C2ECF0,
// the newest's number at +4760), which SV_GetUsercmd (sub_82530270) reads.
// A bot's commands will be written there by its brain; for now it stands.
//
// For trying: debug.txt "bot_add <n>" adds bots on the host until there are
// n. See docs/bots-notes.md.

#ifdef COD3_MP

#include <algorithm>
#include <atomic>
#include <cstdio>
// The console's "bot_add <n>" (kernel_video.cpp): bots added until there are
// n, as debug.txt's bot_add does (the larger of the two counts).
static std::atomic<int> g_consoleBots{0};
void BotsAsk(int count) { g_consoleBots.store(count < 0 ? 0 : count > 20 ? 20 : count); }
// 0.5.92: the console's bot_setup N, for finding what zooms the host's view
// when bots are added: bit 1 leaves out the kit menu's callback, bit 2 the
// side switch. 1 (the kit left out) to begin with.
static std::atomic<int> g_botSetup{1};
static std::atomic<bool> g_botCamera{true};
static std::atomic<bool> g_botTactics{true};
// 0.5.130: each bot given a session player once added (bot_session 0/1).
static std::atomic<bool> g_botAutoSession{true};
// 0.5.135: a bot's session player with its own machine object (bot_remote 0/1).
static std::atomic<bool> g_botRemote{true};   // 0.5.145: on again (any number of bots), drawn by the hooks at the end
// 0.5.137: a bot takes a free local place of the session (bot_local 0/1).
static std::atomic<bool> g_botLocal{false};   // 0.5.145: off (local places are only 3)
// 0.5.163: the bot's own machine object marked "not on this machine" (+0x38 = 0)
// as 0.5.135-0.5.162 did; off: marked local as the host's.
static std::atomic<bool> g_botRemoteFlag{false};
void BotsLocal(int enabled) { g_botLocal.store(enabled != 0); printf("bots: local places for new bots %s\n", enabled ? "on" : "off"); fflush(stdout); }   // 0.5.136: off (Justin: a remote bot is invisible again)
void BotsRemote(int enabled) { g_botRemote.store(enabled != 0); printf("bots: own machine object for new bots %s\n", enabled ? "on" : "off"); fflush(stdout); }
void BotsAutoSession(int enabled) { g_botAutoSession.store(enabled != 0); printf("bots: automatic session players %s\n", enabled ? "on" : "off"); fflush(stdout); }
void BotsTactics(int enabled) { g_botTactics.store(enabled != 0); printf("bots: cover/head aim/collision routing %s\n", enabled ? "on" : "off"); fflush(stdout); }
void BotsCamera(int enabled) { g_botCamera.store(enabled != 0); printf("bots: server-only setup and isolated context %s\n", enabled ? "on" : "off"); fflush(stdout); }
static uint32_t g_viewWatchFrom = 0;   // when the host's view is watched from (0.5.91)
void BotsSetup(int bits) { g_botSetup.store(bits); printf("bots: setup %d (kit %s, side %s)\n", bits, (bits & 1) ? "left out" : "given", (bits & 2) ? "left out" : "switched"); fflush(stdout); }

#include "debug_probe.h"
#include "dom.h"
#include "settings.h"
#include "kernel.h"
#include "bot_navigation.h"
#include "bot_weapon_selection.h"
#include "bot_team_assignment.h"

#include <windows.h>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>

extern "C" PPC_FUNC(__imp__sub_82535810);   // SV_Frame
void LobbyRulesTick(PPCContext& ctx, uint8_t* base);   // lobby_rules.cpp
void MpFixesFrame(uint8_t* base);                      // mp_fixes.cpp
void MpFixesAbilities(PPCContext& ctx, uint8_t* base);  // mp_fixes.cpp
void MpFixesLauncher(PPCContext& ctx, uint8_t* base);   // mp_fixes.cpp
extern "C" PPC_FUNC(__imp__sub_825320D0);   // SV_SendClientSnapshot(client)
extern "C" PPC_FUNC(__imp__sub_824D0AF0);
extern "C" PPC_FUNC(__imp__sub_82487718);
extern "C" PPC_FUNC(__imp__sub_82483190);
extern "C" PPC_FUNC(__imp__sub_824B4270);   // Pmove(pm)
extern "C" PPC_FUNC(__imp__sub_8256B8E0);   // a player's muzzle point (entity, out)   // is an entity where it may move   // the move's scale (a command)   // spawn a player (handle, origin, angles, side)

namespace
{
    constexpr uint32_t Svs = 0x82B3FDE0;
    constexpr uint32_t ClientSize = 4992, MaxClients = 24;
    constexpr uint32_t EntityList = 0x82BCC7FC;   // a pointer to the players' entities, by client number + 1

    constexpr uint32_t SetUpSlot = 0x82532540;
    constexpr uint32_t ConnectIntoWorld = 0x82593608;
    constexpr uint32_t BeginPlayer = 0x8257A6C8;

    bool g_bot[MaxClients] = {};
    uint32_t g_botSide[MaxClients] = {};
    // 0.5.139: each bot's local place (its byte's address) and his session
    // player's number, kept so his spawn can be done without it.
    uint32_t g_placeAt[MaxClients] = {};
    uint8_t g_placeNumber[MaxClients] = {};
    // 0.5.142: each bot's own local index (1 to 3), kept from his creation,
    // 0 for none. The game's current client (0x82BCBE9C) is a LOCAL client
    // index: its arrays have 4 entries. The bot's slot (4, 5, ...) written
    // there went past them and wrecked other data (the zoom, the crashes).
    uint32_t g_placeIndex[MaxClients] = {};
    bool g_inSpawn = false;
    uint32_t g_thinkSlot = MaxClients;   // 0.5.143: the bot whose think runs
    // 0.5.151: War's flag in play (WatchWar), the bots' objective there.
    struct WarGoal { bool valid = false; float at[3] = {}; uint32_t index = 0, when = 0; };
    WarGoal g_war;   // Respawn running: places left as it sets them
    uint32_t ReservePlace(uint32_t slot)
    {
        if (g_placeIndex[slot] != 0) return g_placeIndex[slot];
        // 0.5.145: bots are remote players now, with no local place: all
        // of them share local index 3 (unused: one host, no split screen)
        // as their current client, so the game's 4-entry lists stay in
        // bounds however many bots there are.
        if (!g_botLocal.load())
        {
            g_placeIndex[slot] = 3;
            printf("bots: slot %u's local index 3 (shared scratch, remote bot)\n", slot);
            fflush(stdout);
            return 3;
        }
        for (uint32_t k = 1; k < 4; k++)
        {
            bool taken = false;
            for (uint32_t o = 0; o < MaxClients; o++) if (o != slot && g_bot[o] && g_placeIndex[o] == k) taken = true;
            if (!taken) { g_placeIndex[slot] = k; break; }
        }
        printf("bots: slot %u's local index %u%s\n", slot, g_placeIndex[slot], g_placeIndex[slot] ? "" : " (none free: his slot is used, as before)");
        fflush(stdout);
        return g_placeIndex[slot];
    }
    bool g_firing[MaxClients] = {};   // the bot wants his weapon to fire this frame
    float g_wantYaw[MaxClients] = {}, g_wantPitch[MaxClients] = {};   // where his brain wants to look
    int g_age[MaxClients] = {};   // frames since the bot was added
    float g_yaw[MaxClients] = {};
    int g_bots = 0;
    // 0.5.168: Justin wants a mix of bots. Each bot is given a role when
    // added: Objective (plays War's / Domination's flag), Rusher (runs at
    // the enemy, sprinting), Flanker (roams the map's outskirts), Camper
    // (finds a spot and holds it, crouched, moving on now and then); and a
    // class of his own (0-6: the game's seven), so their weapons differ.
    enum BotRole : uint8_t { RoleObjective, RoleRusher, RoleFlanker, RoleCamper };
    uint8_t g_role[MaxClients] = {};
    const char* RoleName(uint8_t r) { return r == RoleObjective ? "objective" : r == RoleRusher ? "rusher" : r == RoleFlanker ? "flanker" : "camper"; }

    // The players' command rings: 6352 bytes a player at 0x82C2ECF0, 64
    // commands of 48 bytes at +1688, the newest's number at +4760. A command:
    // +0 the server time, +4 the buttons (a bit each), +8 the weapon, +12/16/20
    // the view angles as shorts (360 degrees in 65536), +24/25/26 forward,
    // right and up (-127 .. 127), five floats from +28 (CL_FinishMove,
    // sub_82522B10; the moves from sub_82525338).
    constexpr uint32_t Players = 0x82C2ECF0, PlayerSize = 6352;
    constexpr uint32_t Ring = 1688, Newest = 4760, CommandSize = 48;

    // 0.5.159: THE ROOT CAUSE of the host's gun, controls and crashes with
    // bots. This array holds only this machine's FOUR local players (entry 4
    // at 0x82C34E30 is already the client's per-local-player input state at
    // 0x82C35030, 0x804 each; entry 5 reaches the gun view pointers at
    // 0x82C37070). The bots' commands were written into entries 4, 5...:
    // a bot in slot 4 wrote over the host's input state (prone standing up,
    // "someone else has control", gun glitches), a bot in slot 5 over his gun
    // view pointers (0.5.158's log: changed in FeedBots, every frame). Each
    // bot's commands now go in a block of his own, laid out the same, and the
    // server's SV_GetUsercmd (sub_82530270) takes a bot's from there.
    uint32_t g_commandBlock[MaxClients] = {};
    uint32_t CommandPlayer(uint8_t* base, uint32_t slot)
    {
        if (slot < 4 || slot >= MaxClients) return Players + slot * PlayerSize;
        if (g_commandBlock[slot] == 0)
        {
            g_commandBlock[slot] = Guest::AllocatePhysical(PlayerSize);
            if (g_commandBlock[slot]) memset(base + g_commandBlock[slot], 0, PlayerSize);
            printf("bots: slot %u's own command ring at %08X (not the local players' array)\n", slot, g_commandBlock[slot]);
            fflush(stdout);
        }
        return g_commandBlock[slot] ? g_commandBlock[slot] : Players + slot * PlayerSize;
    }
    uint32_t NewestCommand(uint8_t* base, uint32_t slot)
    {
        const uint32_t player = CommandPlayer(base, slot);
        return player + Ring + (Guest::Read32(base, player + Newest) & 63) * CommandSize;
    }

    uint32_t Clients(uint8_t* base) { return Guest::Read32(base, Svs + 8); }

    uint32_t Call(PPCContext& ctx, uint8_t* base, uint32_t function,
                  uint32_t r3, uint32_t r4 = 0, uint32_t r5 = 0, uint32_t r6 = 0, uint32_t r7 = 0)
    {
        PPCContext call = ctx;
        call.r1.u32 = (ctx.r1.u32 - 0x3000) & ~0xFu;
        call.r3.u32 = r3; call.r4.u32 = r4; call.r5.u32 = r5; call.r6.u32 = r6; call.r7.u32 = r7;
        call.lr = 0;
        Guest::Lookup(function)(call, base);
        return call.r3.u32;
    }

    float ReadFloat(uint8_t* base, uint32_t address)
    {
        const uint32_t bits = Guest::Read32(base, address);
        float f; memcpy(&f, &bits, 4); return f;
    }

    // 0.5.144: the base field of view the game's view (sub_82453810) starts
    // from, a float at 0x82BA2BC0, clamped to at least 1: the host's view
    // came out at 1 (zoomed all the way in) once a second bot was added.
    // Kept while it is sane; if a bot's step wrecks it, put back, and the
    // step named in the log.
    constexpr uint32_t BaseFov = 0x82BA2BC0;
    float g_goodFov = 0.0f;
    void CheckFov(uint8_t* base, const char* where, uint32_t slot)
    {
        const float v = ReadFloat(base, BaseFov);
        if (v >= 10.0f && v <= 170.0f) { g_goodFov = v; return; }
        if (g_goodFov <= 0.0f) return;
        static int told = 0;
        if ((told < 40 && told++ < 40))
        {
            printf("bots: the base field of view went %.2f (bits %08X) at %s, slot %u: put back to %.2f\n",
                   v, Guest::Read32(base, BaseFov), where, slot, g_goodFov);
            fflush(stdout);
        }
        uint32_t bits; memcpy(&bits, &g_goodFov, 4);
        Guest::Write32(base, BaseFov, bits);
    }
    // 0.5.146: found by 0.5.145's log: during a bot's think, his weapon's
    // view code (sub_82519178, from sub_8251A950 / ClientThink) puts back a
    // field of view it saved in his own weapon state (+0x74) into the global
    // one; a bot's is 0, so the host's view went to 0 -> zoomed in. That is
    // a local player's thing; for any bot the global value is kept exactly
    // as it was across his think and his set-up steps.
    // 0.5.151: the host's first-person weapon view object (0x82C37070[0])
    // is 0: its fields live at guest addresses 0x0 and up, and the bots'
    // runs of that same code (their objects are 0 too) wrote over them: the
    // host's gun vanished, vehicles stuck. The first 0x800 bytes are put back
    // as they were after every bot's think and set-up step.
    constexpr uint32_t NullObjectBytes = 0x800;
    struct KeepFov
    {
        uint8_t* base; uint32_t bits;
        uint8_t low[NullObjectBytes];
        // 0.5.174: CL's stance latch is separate from kbuttons and HostInput.
        // 82525338 turns it into command bits; 82571930 clears all four
        // latches even when invoked by bot setup. Preserve only host slot0.
        uint32_t hostStance;
        uint8_t stanceHold[12]; // CL slot0 +1608: hold flag, initial stance, time
        // 0.5.156: 0.5.155's log: the gun view pointers are real (static
        // objects 0x400 apart: 829D8810 the host's, then 1-3), never
        // emptied; the host's OBJECT itself gets wrecked (its +4's table
        // called through garbage). It is kept as it was too.
        uint32_t hostView = 0;
        uint8_t view[0x400];
        explicit KeepFov(uint8_t* b) : base(b), bits(Guest::Read32(b, BaseFov))
        {
            hostStance = Guest::Read32(base, 0x82B57804);
            memcpy(stanceHold, base + Players + 1608, sizeof(stanceHold));
            memcpy(low, base, NullObjectBytes);
            hostView = Guest::Read32(base, 0x82C37070);
            if (hostView < 0x40000000u || hostView >= 0xA0000000u) hostView = 0;
            if (hostView) memcpy(view, base + hostView, sizeof(view));
        }
        ~KeepFov()
        {
            const uint32_t afterStance = Guest::Read32(base, 0x82B57804);
            if (afterStance != hostStance || memcmp(stanceHold, base + Players + 1608, sizeof(stanceHold)))
            {
                static int toldStance = 0;
                if ((toldStance < 40 && toldStance++ < 40)) { printf("bots: isolated step changed host stance latch %u -> %u or hold state: restored\n", hostStance, afterStance); fflush(stdout); }
                Guest::Write32(base, 0x82B57804, hostStance);
                memcpy(base + Players + 1608, stanceHold, sizeof(stanceHold));
            }
            if (hostView && Guest::Read32(base, 0x82C37070) == hostView && memcmp(view, base + hostView, sizeof(view)) != 0)
            {
                static int toldView = 0;
                if ((toldView < 20 && toldView++ < 20))
                {
                    printf("bots: a bot's step wrote the host's gun view object %08X, put back:", hostView);
                    int shown = 0;
                    for (uint32_t w = 0; w < sizeof(view) && shown < 12; w += 4)
                    {
                        const uint32_t was = (uint32_t(view[w]) << 24) | (uint32_t(view[w + 1]) << 16) | (uint32_t(view[w + 2]) << 8) | view[w + 3];
                        const uint32_t now = Guest::Read32(base, hostView + w);
                        if (was == now) continue;
                        printf(" +%03X %08X->%08X", w, was, now);
                        shown++;
                    }
                    printf("\n");
                    fflush(stdout);
                }
                memcpy(base + hostView, view, sizeof(view));
            }
            if (Guest::Read32(base, BaseFov) != bits) Guest::Write32(base, BaseFov, bits);
            if (memcmp(low, base, NullObjectBytes) != 0)
            {
                static int told = 0;
                if ((told < 20 && told++ < 20))
                {
                    // 0.5.154: every changed word (up to 12), old and new.
                    printf("bots: a bot's step wrote the host's weapon view, put back:");
                    int shown = 0;
                    for (uint32_t w = 0; w < NullObjectBytes && shown < 12; w += 4)
                    {
                        const uint32_t was = (uint32_t(low[w]) << 24) | (uint32_t(low[w + 1]) << 16) | (uint32_t(low[w + 2]) << 8) | low[w + 3];
                        const uint32_t now = Guest::Read32(base, w);
                        if (was == now) continue;
                        printf(" +%03X %08X->%08X", w, was, now);
                        shown++;
                    }
                    printf("\n");
                    fflush(stdout);
                }
                memcpy(base, low, NullObjectBytes);
            }
        }
    };

    // 0.5.109: setup must run with the same current-client context as
    // ClientThink. A bot has no local camera; never borrow host client 0.
    constexpr uint32_t CurrentClient = 0x82BCBE9C;
    uint32_t BotSlot(uint8_t* base, uint32_t entity)
    {
        const uint32_t entities = Guest::Read32(base, EntityList);
        for (uint32_t slot = 4; entities && slot < MaxClients; ++slot)
            if (g_bot[slot] && Guest::Read32(base, entities + 4 * (slot + 1)) == entity) return slot;
        return MaxClients;
    }
    // 0.5.153: each bot's own first-person gun view memory. The game finds
    // a local player's gun view at 0x82C37070[current client]; the host's
    // entry is 0 (his lives at guest 0x0 on), and the bots' (index 3) was 0
    // too, so their runs of that code wrote into the host's. While any step
    // of a bot runs (ours, or the game's own spawn and think of his player),
    // entry 3 points at a block of his own, and the current client is 3.
    constexpr uint32_t GunViews = 0x82C37070, BotGunView = 3, GunViewBytes = 0x2000;
    uint32_t g_gunView[MaxClients] = {};
    uint32_t BotGunViewBlock(uint8_t* base, uint32_t slot)
    {
        if (slot >= MaxClients) return 0;
        if (g_gunView[slot] == 0)
        {
            g_gunView[slot] = Guest::AllocatePhysical(GunViewBytes);
            if (g_gunView[slot]) memset(base + g_gunView[slot], 0, GunViewBytes);
            printf("bots: slot %u's own gun view memory at %08X\n", slot, g_gunView[slot]);
            fflush(stdout);
        }
        return g_gunView[slot];
    }
    struct BotGunViewScope
    {
        uint8_t* base; uint32_t savedEntry, savedCurrent; bool on;
        BotGunViewScope(uint8_t* b, uint32_t slot, bool setCurrent)
            : base(b), savedEntry(Guest::Read32(b, GunViews + 4 * BotGunView)), savedCurrent(Guest::Read32(b, CurrentClient)), on(false)
        {
            const uint32_t block = BotGunViewBlock(b, slot);
            if (!block) return;
            on = true;
            Guest::Write32(base, GunViews + 4 * BotGunView, block);
            if (setCurrent) Guest::Write32(base, CurrentClient, BotGunView);
        }
        ~BotGunViewScope()
        {
            if (!on) return;
            Guest::Write32(base, GunViews + 4 * BotGunView, savedEntry);
            Guest::Write32(base, CurrentClient, savedCurrent);
        }
    };

    // 0.5.158: where the gun view pointers (0x82C37070, 4 words) change.
    // 0.5.157's hardware watch on them never fired, though they were wiped
    // when bot 2 came in: so they are compared at each step instead, and
    // the first step after which they differ is named.
    uint32_t g_gunPointers[4] = {};
    bool g_gunPointersKnown = false;
    void GunPointersCheck(uint8_t* base, const char* where, uint32_t slot = 0)
    {
        uint32_t now[4];
        for (int k = 0; k < 4; k++) now[k] = Guest::Read32(base, 0x82C37070 + 4 * k);
        if (g_gunPointersKnown && memcmp(now, g_gunPointers, sizeof(now)) != 0)
        {
            static int told = 0;
            if ((told < 60 && told++ < 60))
            {
                printf("bots: GUN VIEW POINTERS CHANGED at %s (slot %u): %08X %08X %08X %08X -> %08X %08X %08X %08X\n", where, slot,
                       g_gunPointers[0], g_gunPointers[1], g_gunPointers[2], g_gunPointers[3], now[0], now[1], now[2], now[3]);
                fflush(stdout);
            }
        }
        memcpy(g_gunPointers, now, sizeof(now));
        g_gunPointersKnown = true;
    }

    // 0.5.147: this machine's 4 local clients (0x82C7A270, 0xc88 each);
    // +0xb8 is 2 for one in play. The server frame runs a local client's
    // view and weapon code (sub_82534DB0 -> sub_82593D50) for every one at
    // 2: with two bots, local client 3 (the bots' shared current client)
    // came up at 2, and that code then ran for an empty local player: the
    // host's view went to 0, his gun vanished and he froze.
    constexpr uint32_t LocalClients = 0x82C7A270, LocalClientSize = 0xc88;
    uint32_t LocalState(uint32_t k) { return LocalClients + 0xb8 + k * LocalClientSize; }
    uint32_t g_localStateBefore[4] = {};
    bool g_localStateKnown = false;

    class BotClientScope
    {
        uint8_t* base;
        uint32_t saved, slot, hostRecord = 0, sights = 0, weapon = 0, fovBits = 0, index = 0, state = 0, gunEntry = 0;
        bool gunOn = false;
        KeepFov keep;   // 0.5.151: declared first: restores last
        const char* stage;
        bool active;
    public:
        BotClientScope(uint8_t* memory, uint32_t botSlot, const char* name)
            : base(memory), saved(Guest::Read32(memory, CurrentClient)), slot(botSlot), keep(memory), stage(name),
              active(g_botCamera.load() && botSlot >= 4 && botSlot < MaxClients)
        {
            if (!active) return;
            const uint32_t entities = Guest::Read32(base, EntityList);
            const uint32_t host = entities ? Guest::Read32(base, entities + 4) : 0;
            hostRecord = host ? Guest::Read32(base, host + 512) : 0;
            if (hostRecord) { sights = Guest::Read32(base, hostRecord + 172); weapon = Guest::Read32(base, hostRecord + 164); }
            CheckFov(base, "before", slot);
            fovBits = Guest::Read32(base, BaseFov);   // 0.5.146
            index = g_placeIndex[slot] ? g_placeIndex[slot] : slot;
            if (index < 4) state = Guest::Read32(base, LocalState(index));   // 0.5.147
            Guest::Write32(base, CurrentClient, index);
            // 0.5.153: his own gun view memory while the step runs.
            if (index == BotGunView && !g_botLocal.load())
            {
                const uint32_t block = BotGunViewBlock(base, slot);
                if (block) { gunEntry = Guest::Read32(base, GunViews + 4 * BotGunView); Guest::Write32(base, GunViews + 4 * BotGunView, block); gunOn = true; }
            }
        }
        ~BotClientScope()
        {
            if (!active) return;
            if (gunOn) Guest::Write32(base, GunViews + 4 * BotGunView, gunEntry);   // 0.5.153
            GunPointersCheck(base, stage, slot);   // 0.5.158
            Guest::Write32(base, CurrentClient, saved);
            if (index >= 1 && index < 4 && !g_botLocal.load() && Guest::Read32(base, LocalState(index)) != state)
            {
                printf("bots: %s of slot %u set local client %u's state %u -> %u: put back\n", stage, slot, index,
                       state, Guest::Read32(base, LocalState(index)));
                Guest::Write32(base, LocalState(index), state);
            }
            if (fovBits && Guest::Read32(base, BaseFov) != fovBits) Guest::Write32(base, BaseFov, fovBits);   // 0.5.146
            CheckFov(base, stage, slot);
            printf("bots: setup context %s slot %u restored %u; host ADS %08X -> %08X, weapon %u -> %u\n",
                   stage, slot, saved, sights, hostRecord ? Guest::Read32(base, hostRecord + 172) : 0,
                   weapon, hostRecord ? Guest::Read32(base, hostRecord + 164) : 0);
            fflush(stdout);
        }
        BotClientScope(const BotClientScope&) = delete;
        BotClientScope& operator=(const BotClientScope&) = delete;
    };

    // A player's side: +4 of the team record his entity's +520 points to
    // (1 the allies, 2 the axis, as the side menus set it).
    uint32_t Side(uint8_t* base, uint32_t entity)
    {
        const uint32_t team = entity ? Guest::Read32(base, entity + 520) : 0;
        return team ? Guest::Read32(base, team + 4) : 0;
    }

    // Each bot owns a stable side. New bots join the smaller team,
    // including connected humans; respawns never derive it from the host.
    uint32_t BotSide(uint8_t* base, uint32_t slot)
    {
        if (slot >= MaxClients) return 0;
        if (g_botSide[slot] == 1 || g_botSide[slot] == 2) return g_botSide[slot];
        const uint32_t entities = Guest::Read32(base, EntityList);
        const uint32_t clients = Clients(base);
        uint32_t allies = 0, axis = 0;
        for (uint32_t other = 0; entities && clients && other < MaxClients; ++other)
        {
            if (other == slot || Guest::Read32(base, clients + other * ClientSize) == 0) continue;
            const uint32_t entity = Guest::Read32(base, entities + 4 * (other + 1));
            const uint32_t side = g_bot[other] ? g_botSide[other] : Side(base, entity);
            if (side == 1) ++allies;
            if (side == 2) ++axis;
        }
        const uint32_t host = entities ? Guest::Read32(base, entities + 4) : 0;
        const uint32_t tieSide = Side(base, host) == 2 ? 1u : 2u;
        g_botSide[slot] = BotTeamAssignment::Choose(allies, axis, tieSide);
        printf("bots: slot %u assigned side %u (allies %u, axis %u before join)\n",
               slot, g_botSide[slot], allies, axis); fflush(stdout);
        return g_botSide[slot];
    }

    // A player's whereabouts as the log shows them: the first words of his
    // record (entity+512 points to it) and of the entity itself from +336,
    // for working out which of them is his origin.
    void LogWhere(uint8_t* base, const char* what, uint32_t slot, uint32_t entity)
    {
        const uint32_t ps = entity ? Guest::Read32(base, entity + 512) : 0;
        printf("bots: %s %u record", what, slot);
        for (int i = 0; ps && i < 8; i++) printf(" %.1f", ReadFloat(base, ps + 4 * i));
        printf(" | entity");
        for (int i = 0; entity && i < 4; i++) printf(" %.1f", ReadFloat(base, entity + 336 + 4 * i));
        // ClientThink's tests (sub_82592CD0): +1932 must be 2; +1924 the
        // player's state (1 watching, 2 the end, 4 and 5 dead); his command's
        // time (+1936) against his last (+32) and the game's (0x82BB81D0+156).
        if (ps)
            printf(" | move type %d, health %d, +2040 %d, +2044 %d, speed %d",
                   int32_t(Guest::Read32(base, ps + 36)), int32_t(Guest::Read32(base, ps + 304)),
                   int32_t(Guest::Read32(base, ps + 2040)), int32_t(Guest::Read32(base, ps + 2044)),
                   int32_t(Guest::Read32(base, ps + 80)));
        if (ps)
            printf(" | connected %d, state %d, command %d, last %d, game %d",
                   int32_t(Guest::Read32(base, ps + 1932)), int32_t(Guest::Read32(base, ps + 1924)),
                   int32_t(Guest::Read32(base, ps + 1936)), int32_t(Guest::Read32(base, ps + 32)),
                   int32_t(Guest::Read32(base, 0x82BB81D0 + 156)));
        printf("\n"); fflush(stdout);
    }

    // The session's players (from +48832 of the session, Session below:
    // 24 of 784 bytes; sub_823E5F30 finds an entity's). Each: +0 a byte, his
    // number in the session's messages (24 and up: none), +4 not zero while
    // he is in, +8 his client slot. The spawn, and what each machine's game
    // draws of a player, go by these; a bot has none of his own.
    constexpr uint32_t SessionPlayers = 48832, SessionPlayerSize = 784, SessionMax = 24;

    // The session itself: the word the pointer at 0x82BCCAC0 points to (zero
    // with none, as sub_82406BB8 tests).
    uint32_t Session(uint8_t* base)
    {
        const uint32_t pointer = Guest::Read32(base, 0x82BCCAC0);
        return pointer ? Guest::Read32(base, pointer) : 0;
    }

    uint32_t SessionPlayer(uint8_t* base, uint32_t slot)
    {
        const uint32_t mode = Session(base);
        if (mode == 0) return 0;
        for (uint32_t i = 0; i < SessionMax; i++)
        {
            const uint32_t player = mode + SessionPlayers + i * SessionPlayerSize;
            if (Guest::Read8(base, player) < SessionMax && Guest::Read32(base, player + 4) != 0 &&
                Guest::Read32(base, player + 8) == slot)
                return player;
        }
        return 0;
    }

    void LogSession(uint8_t* base)
    {
        const uint32_t mode = Session(base);
        printf("bots: session %08X\n", mode);
        for (uint32_t i = 0; mode && i < SessionMax; i++)
        {
            const uint32_t player = mode + SessionPlayers + i * SessionPlayerSize;
            const uint8_t number = Guest::Read8(base, player);
            const uint32_t in = Guest::Read32(base, player + 4);
            if (number >= SessionMax && in == 0) continue;
            printf("bots: session player %u at %08X: number %u, in %08X, slot %d\n", i, player, number, in,
                   int32_t(Guest::Read32(base, player + 8)));
            for (uint32_t row = 0; row < SessionPlayerSize; row += 32)
            {
                printf("bots:   +%03X", row);
                for (uint32_t w = 0; w < 32 && row + w < SessionPlayerSize; w += 4)
                    printf(" %08X", Guest::Read32(base, player + row + w));
                printf("\n");
            }
        }
        fflush(stdout);
    }

    // A session player for a bot: the host's own copied into a free place,
    // with the place's number and the bot's slot.
    bool GiveSessionPlayer(uint8_t* base, uint32_t slot)
    {
        const uint32_t mode = Session(base);
        const uint32_t host = SessionPlayer(base, 0);
        if (mode == 0 || host == 0 || SessionPlayer(base, slot) != 0) return false;
        for (uint32_t i = 0; i < SessionMax; i++)
        {
            const uint32_t player = mode + SessionPlayers + i * SessionPlayerSize;
            if (Guest::Read8(base, player) < SessionMax && Guest::Read32(base, player + 4) != 0) continue;
            memcpy(base + player, base + host, SessionPlayerSize);
            // 0.5.130: the copied +4 is a counted object the host's entry
            // holds (sub_824046D8 adds one to its count, +4, when it keeps
            // it): counted again for the copy, so the bot's entry going away
            // doesn't free the host's.
            const uint32_t held = Guest::Read32(base, player + 4);
            // 0.5.135: his own identity, not the host's. +4 is a counted
            // holder (+4 its count) of the machine's player object (+8):
            // its +0x38 byte says "on this machine" (sub_821AE440) and +0x34
            // tells machines apart (sub_821AE448). Shared with the host, the
            // bot counted as a second local player of the host's machine:
            // Justin, on 0.5.134: his own guns went full auto with endless
            // rounds and his hits stopped counting for a while, and the bot's
            // death never reached the kill feed. Now the bot gets a copy of
            // both (0x100 bytes each), "not on this machine", with his own id.
            const uint32_t object = held ? Guest::Read32(base, held + 8) : 0;
            if (g_botRemote.load() && held >= 0x40000000u && held < 0xA0000000u && object >= 0x40000000u && object < 0xA0000000u)
            {
                const uint32_t mine = Guest::AllocatePhysical(0x100), myObject = Guest::AllocatePhysical(0x100);
                memcpy(base + mine, base + held, 0x100);
                memcpy(base + myObject, base + object, 0x100);
                Guest::Write32(base, mine + 4, 0x10000);   // never counted down to freeing (it is ours, not the game's heap's)
                Guest::Write32(base, mine + 8, myObject);
                // 0.5.163: left "on this machine" (+0x38 as the host's, 1).
                // Justin, on 0.5.162 (20 bots seen, amazing): still no death
                // animation, they freeze and vanish. That worked only while
                // the bot shared the host's machine object (0.5.137-0.5.144);
                // several checks read +0x38 directly (sub_821AE440, from
                // sub_821B2BF8, sub_821B35E0, sub_823E4D80, sub_823FB298,
                // sub_82405158, sub_82405A88...), past the sub_823D9B50 hook.
                // The bot keeps his own object and id (+0x34); only "local"
                // is now true, as it was then (g_botRemoteFlag true = the old way).
                Guest::Write8(base, myObject + 0x38, g_botRemoteFlag.load() ? 0 : 1);
                // 0.5.164: the host's machine id kept (+0x34, compared by
                // sub_821AE448). Justin, on 0.5.163: still no death animation
                // or kill feed. Both worked in 0.5.140-0.5.144, when the bot's
                // machine was the host's own, id and all; the session's
                // death and kill messages carry the machine they are about,
                // and one about a machine id nobody has goes nowhere.
                if (g_botRemoteFlag.load())
                    Guest::Write32(base, myObject + 0x34, Guest::Read32(base, object + 0x34) ^ (0x5B000000u + slot));
                Guest::Write32(base, player + 4, mine);
                printf("bots: slot %u has his own machine object %08X (holder %08X), not the host's %08X\n", slot, myObject, mine, object);
            }
            else if (held)
                Guest::Write32(base, held + 4, Guest::Read32(base, held + 4) + 1);
            Guest::Write8(base, player, uint8_t(i));
            Guest::Write32(base, player + 8, slot);
            // 0.5.137: a local place of his own. The session keeps which of
            // its players are this machine's four local ones: four bytes at
            // +0x5991 of its object (the players' list is at +0x1010 of it,
            // so the object is the list's address less 0x1010), each a
            // session player's number or 24 and up for none (sub_824046D8,
            // sub_823D8FB0 and sub_823D8FF0 read them). A bot was in none,
            // so code that looks up "which local player is this" found no
            // place and fell back to the host's (the current client, 0):
            // the host's view zoomed, his guns went full auto, and two bots
            // crashed the animation code. Now a bot takes a free local place
            // (1 to 3) when there is one. Console bot_local 0 leaves it out.
            if (g_botLocal.load())
            {
                const uint32_t object = mode + SessionPlayers - 0x1010;
                const uint32_t places = object + 0x5991;
                const uint8_t hostNumber = Guest::Read8(base, host);
                if (Guest::Read8(base, places) == hostNumber)
                {
                    // 0.5.142: his own index (ReservePlace), not merely the
                    // first free byte: a dead bot's place is emptied
                    // (0.5.141), and a second bot took it.
                    int place = -1;
                    const uint32_t own = slot < MaxClients ? ReservePlace(slot) : 0;
                    if (own != 0 && Guest::Read8(base, places + own) >= SessionMax) place = int(own);
                    if (place > 0)
                    {
                        Guest::Write8(base, places + place, uint8_t(i));
                        g_placeAt[slot] = places + uint32_t(place);
                        g_placeNumber[slot] = uint8_t(i);
                        printf("bots: slot %u takes local place %d (session player %u)\n", slot, place, i);
                    }
                    else printf("bots: slot %u: no free local place (places %02X %02X %02X %02X)\n", slot,
                                Guest::Read8(base, places), Guest::Read8(base, places + 1), Guest::Read8(base, places + 2), Guest::Read8(base, places + 3));
                }
                else printf("bots: slot %u: local places not where expected (first %02X, host %02X)\n", slot, Guest::Read8(base, places), hostNumber);
            }
            // 0.5.131: his own name (Justin: the copy showed the host's).
            // The session player's name is the text at +104 (the host's
            // "HOVGtv" there in 0.5.130's log), up to 32 bytes: "Bot N",
            // N picked at random from 1 to 24, none two bots share.
            {
                static int number[MaxClients] = {};
                bool used[25] = {};
                for (uint32_t k = 0; k < MaxClients; k++) if (k != slot && g_bot[k] && number[k] > 0) used[number[k]] = true;
                int free = 0;
                for (int n = 1; n <= 24; n++) if (!used[n]) free++;
                int pick = free ? 1 + int((uint32_t(GetTickCount()) ^ (slot * 2654435761u)) % uint32_t(free)) : 1;
                int n = 1;
                for (; n <= 24; n++) if (!used[n] && --pick == 0) break;
                if (n > 24) n = 1;
                number[slot] = n;
                char name[32] = {};
                snprintf(name, sizeof(name), "Bot %d", n);
                memset(base + player + 104, 0, 32);
                memcpy(base + player + 104, name, strlen(name));
                printf("bots: slot %u is named %s\n", slot, name);
            }
            printf("bots: slot %u given session player %u (a copy of %08X)\n", slot, i, host); fflush(stdout);
            return true;
        }
        return false;
    }

    bool AddBot(PPCContext& ctx, uint8_t* base)
    {
        const uint32_t clients = Clients(base);
        if (clients == 0) return false;
        // From slot 4: the first four are the local players' (the server
        // sends to those by loopback, sub_825334E8, as their local player
        // entries say), and a remote player is given the first free slot.
        uint32_t slot = MaxClients;
        for (uint32_t i = 4; i < MaxClients; i++)
            if (Guest::Read32(base, clients + i * ClientSize) == 0) { slot = i; break; }
        if (slot == MaxClients) { printf("bots: no free slot\n"); fflush(stdout); return false; }
        const uint32_t client = clients + slot * ClientSize;
        printf("bots: adding a bot in slot %u (client %08X); the host's gun view pointer 0x82C37070 holds %08X\n", slot, client,
               Guest::Read32(base, 0x82C37070));
        // 0.5.155: 0.5.154's watch on guest 0x4 never fired: the host's gun
        // view is not wrecked in place, its pointer (0x82C37070[0]) is
        // emptied (the game then falls back on guest 0x0, all zero: field
        // of view 0, gun gone). The processor now names whoever writes it.
        {
            static bool watching = false;
            // 0.5.156: on the host's object's +4 (the one whose table the
            // crash calls), the pointer itself never changed.
            const uint32_t hostView = Guest::Read32(base, 0x82C37070);
            // 0.5.157: back on the pointers (156's log: they went 0, then
            // floats, right when bot 2 came in; 155's run had not hit it).
            (void)hostView;
            // 0.5.177: watch the error-queue POINTER, which176 saw become0.
            // ArmWatchpoints skips its calling thread. AddBot runs on main,
            // so using it here synchronously left main unwatched! A helper
            // arms main along with the other existing game threads.
            if (!watching)
            {
                watching = true;
                Kernel::WatchWrite(0x82A87860);
                const HANDLE helper = CreateThread(nullptr, 0, [](void*) -> DWORD {
                    Kernel::ArmWatchpoints();
                    printf("bots: error-queue writer watch armed from helper (main included)\n"); fflush(stdout);
                    return 0;
                }, nullptr, 0, nullptr);
                if (helper) CloseHandle(helper);
                else { printf("bots: failed to start writer-watch helper: %lu\n", GetLastError()); fflush(stdout); }
            }
        }
        fflush(stdout);
        g_botSide[slot] = 0; // fresh slot/map: discard the previous occupant's team
        g_bot[slot] = true;
        g_placeIndex[slot] = 0;
        ReservePlace(slot);
        BotClientScope clientScope(base, slot, "create");

        GunPointersCheck(base, "before the bot's slot is set up", slot);
        Call(ctx, base, SetUpSlot, client);
        GunPointersCheck(base, "SetUpSlot", slot);
        const uint32_t entities = Guest::Read32(base, EntityList);
        const uint32_t entity = entities ? Guest::Read32(base, entities + 4 * (slot + 1)) : 0;
        if (entity == 0) { printf("bots: slot %u has no entity\n", slot); fflush(stdout); g_bot[slot] = false; return false; }

        // Where to stand and which way to face: nothing, as the title's join
        // passes for a new player (the spawn picks the place).
        const uint32_t scratch = (ctx.r1.u32 - 0x200) & ~0xFu;
        memset(base + scratch, 0, 32);
        Call(ctx, base, ConnectIntoWorld, entity, scratch, scratch + 16, 0, 0);
        Guest::Write32(base, client + 44, 0xFFFFFFFFu);
        Guest::Write32(base, client + 0, 1);

        // The player's game record, cleared where the title's join clears it.
        const uint32_t record = Guest::Read32(base, entity + 512);
        if (record != 0)
        {
            Guest::Write32(base, record + 1924, 0);
            Guest::Write16(base, record + 1928, 0);
            Guest::Write16(base, record + 1340, 0);
            // 0.5.115: bots skip local kit menus, so choose a valid default.
            // Native getters 820CE3D8/820CE538 feed these halfwords to
            // 8220D1C8 -> 82206028. The join sentinel FFFF became class -1,
            // leaving the living bot without a weapon or character model.
            // 0.5.168: a class of his own, at random (the game's seven,
            // 0 to 6), and a role.
            const uint16_t kit = uint16_t((GetTickCount() / 7 + slot * 2654435761u) % 7);
            Guest::Write16(base, record + 1920, kit);
            Guest::Write16(base, record + 1922, kit);
            {
                const uint32_t roll = (GetTickCount() * 2246822519u + slot * 3266489917u) >> 8;
                const uint32_t pick = roll % 100;
                g_role[slot] = pick < 40 ? RoleObjective : pick < 65 ? RoleRusher : pick < 85 ? RoleFlanker : RoleCamper;
            }
            printf("bots: slot %u is a %s, class %u\n", slot, RoleName(g_role[slot]), kit);
            memset(base + record + 1504, 0, 406);
            Guest::Write32(base, record + 1504 + 408, 0);
        }
        const uint32_t team = Guest::Read32(base, entity + 520);
        // 0.5.168: his side from the start (was 0 until his side was picked a
        // moment later: Justin saw new bots as friendly green arrows on the
        // compass as they came in).
        if (team != 0) Guest::Write32(base, team + 4, BotSide(base, slot));
        GunPointersCheck(base, "before BeginPlayer", slot);
        Call(ctx, base, BeginPlayer, entity, 0);
        GunPointersCheck(base, "BeginPlayer", slot);
        printf("bots: bot in slot %u begun (entity %08X, record %08X)\n", slot, entity, record);
        fflush(stdout);
        g_bots++;
        // 0.5.154: with a second bot the host's gun view (guest 0x0 on: its
        // +4 is the object whose table the crashing calls read) is wrecked;
        // a hardware watch on guest 0x4 names every writer from now on.
        // (0.5.155: the watch moved to the host's gun view pointer, below.)
        g_age[slot] = 0;
        return true;
    }
}

// A bot's side and kit, picked the way the host's own menus pick them for
// a local player (InGameSwitchSides' apply, sub_8239FB10, and the kit
// menu's, sub_823B01C0): the game's callbacks at +3320 (side) and +3328
// (kit) of the object 0x82BCBF64 points to, given the player entity's handle
// (+484 of the entity), or, in the other mode the title can be in (the word
// the pointer at 0x82BCCAC0 points to not zero), sub_823EA4E0.
namespace
{
    constexpr uint32_t Callbacks = 0x82BCBF64, ModeObject = 0x82BCCAC0;
    constexpr uint32_t PickSideOther = 0x823EA4E0;

    void PickSide(PPCContext& ctx, uint8_t* base, uint32_t entity, uint32_t side)
    {
        BotClientScope clientScope(base, BotSlot(base, entity), "side");
        if (g_botCamera.load())
        {
            // Native 823EA4E0 at 823EA614 sets these two team fields.
            // Bots use server respawn separately; they have no local menus.
            const uint32_t team=Guest::Read32(base,entity+520);
            const uint32_t sessionPlayer=SessionPlayer(base,BotSlot(base,entity));
            if (team) Guest::Write32(base,team+4,side);
            if (sessionPlayer) Guest::Write16(base,sessionPlayer+604,uint16_t(side));
            printf("bots: server-only side %u, entity %08X, session %08X; menu callback skipped\n",side,entity,sessionPlayer);
            fflush(stdout);
            return;
        }
        const uint32_t mode = Guest::Read32(base, ModeObject);
        const uint32_t scratch = (ctx.r1.u32 - 0x200) & ~0xFu;
        Guest::Write32(base, scratch, Guest::Read32(base, entity + 484));
        if (mode != 0 && Guest::Read32(base, mode) != 0)
        {
            printf("bots: side %u the other way\n", side); fflush(stdout);
            Call(ctx, base, PickSideOther, mode, entity, side, 0, 0);
            // That way goes through the session, which sets his team; setting
            // the team record under it froze him, half switched.
            return;
        }
        const uint32_t team = Guest::Read32(base, entity + 520);
        if (team != 0) Guest::Write32(base, team + 4, side);
        const uint32_t object = Guest::Read32(base, Callbacks);
        const uint32_t callback = object ? Guest::Read32(base, object + 3320) : 0;
        printf("bots: side %u (callback %08X)\n", side, callback); fflush(stdout);
        if (callback != 0) Call(ctx, base, callback, scratch, 1, 1);
    }

    void PickKit(PPCContext& ctx, uint8_t* base, uint32_t entity, uint32_t kit)
    {
        BotClientScope clientScope(base, BotSlot(base, entity), "kit");
        if (g_botCamera.load())
        {
            printf("bots: keeping spawn kit for %08X; local kit menu skipped\n",entity);
            fflush(stdout);
            return;
        }
        const uint32_t scratch = (ctx.r1.u32 - 0x200) & ~0xFu;
        Guest::Write32(base, scratch, Guest::Read32(base, entity + 484));
        const uint32_t object = Guest::Read32(base, Callbacks);
        const uint32_t callback = object ? Guest::Read32(base, object + 3328) : 0;
        printf("bots: kit %u (callback %08X)\n", kit, callback); fflush(stdout);
        if (callback != 0) Call(ctx, base, callback, scratch, kit);
    }

    // A bot put at one of his side's spawn points: the game's own
    // RespawnPlayer (sub_8220AF60), given the entity's handle and whether he
    // is on the allies, queues sub_822029F0, which asks the game type for a
    // spawn point of the side named "allies" or "axis" and spawns the player
    // there (the engine's spawn, +748 of the table at 0x82AA8030, given the
    // point's origin and angles).
    constexpr uint32_t RespawnPlayer = 0x8220AF60, SpawnAtPoint = 0x822029F0;
    constexpr uint32_t MakeString = 0x821E0E48;
    constexpr uint32_t AlliesName = 0x8205FCC8, AxisName = 0x8205FCD0;

    // RespawnPlayer only queues the spawn, and the queue it uses never ran it
    // for a bot; so sub_822029F0 is called here as the queued task would call
    // it: the entity's handle, and the side's name as one of the game's
    // strings (sub_821E0E48 makes one; sub_822029F0 lets go of it).
    void Respawn(PPCContext& ctx, uint8_t* base, uint32_t entity, bool allies)
    {
        // 0.5.139: Justin, on 0.5.138: frozen in place each time the bot
        // dies (his commands arrive, nothing moves), even with the current
        // client put back. The bot is spawned again while he holds a local
        // place, so the spawn is done as a local player's: the machine's
        // own player set-up runs for him and takes the host's controls. Out
        // of his local place for the spawn (as before 0.5.137, when spawning
        // never froze the host), and back in after.
        const uint32_t botSlot = BotSlot(base, entity);
        // 0.5.141: the place may already be empty (left at his death).
        const bool placed = botSlot < MaxClients && g_placeAt[botSlot] != 0 &&
                            (Guest::Read8(base, g_placeAt[botSlot]) == g_placeNumber[botSlot] ||
                             Guest::Read8(base, g_placeAt[botSlot]) == 0xFF);
        if (placed) Guest::Write8(base, g_placeAt[botSlot], 0xFF);
        struct PutBack
        {
            uint8_t* base; uint32_t at; uint8_t number; bool on;
            ~PutBack() { if (on) Guest::Write8(base, at, number); }
        } putBack{ base, placed ? g_placeAt[botSlot] : 0, placed ? g_placeNumber[botSlot] : uint8_t(0), placed };
        if (placed) { printf("bots: slot %u spawned out of his local place\n", botSlot); fflush(stdout); }
        struct SpawnFlag { SpawnFlag() { g_inSpawn = true; } ~SpawnFlag() { g_inSpawn = false; } } spawnFlag;
        BotClientScope clientScope(base, botSlot, "spawn");
        const uint32_t scratch = (ctx.r1.u32 - 0x200) & ~0xFu;
        Guest::Write32(base, scratch, Guest::Read32(base, entity + 484));
        Guest::Write32(base, scratch + 8, 0);
        Call(ctx, base, MakeString, scratch + 8, allies ? AlliesName : AxisName);
        const uint32_t table = 0x82A98030, game = Guest::Read32(base, 0x82A99494);
        printf("bots: spawning as %s (string %08X, spawn %08X, point %08X, origin %08X, game %08X)\n",
               allies ? "allies" : "axis", Guest::Read32(base, scratch + 8),
               Guest::Read32(base, table + 748), Guest::Read32(base, table + 4084),
               Guest::Read32(base, table + 3948), game); fflush(stdout);
        LogWhere(base, "before", 0, entity);
        Call(ctx, base, SpawnAtPoint, scratch, scratch + 8);
        LogWhere(base, "after", 0, entity);
        // 0.5.168: back in play (the 4 he was given dead is not undone by
        // the spawn itself, in case).
        if (const uint32_t record = Guest::Read32(base, entity + 512))
            if (int32_t(Guest::Read32(base, record + 304)) > 0 && Guest::Read32(base, record + 1924) == 4)
                Guest::Write32(base, record + 1924, 3);
        // 0.5.166: Justin: right after a bot respawns he shows for a moment
        // as a friendly green arrow on the compass. The game's spawn puts
        // the host's side back on him (his session player is a copy of the
        // host's) and Think put it right only on his next frame. Now at once.
        {
            const uint32_t want = allies ? 1u : 2u;
            const uint32_t team = Guest::Read32(base, entity + 520);
            if (team != 0) Guest::Write32(base, team + 4, want);
            if (botSlot < MaxClients)
                if (const uint32_t player = SessionPlayer(base, botSlot)) Guest::Write16(base, player + 604, uint16_t(want));
        }
    }

    void SetUpBots(PPCContext& ctx, uint8_t* base)
    {
        const uint32_t entities = Guest::Read32(base, EntityList);
        if (entities == 0) return;
        for (uint32_t slot = 4; slot < MaxClients; slot++)
        {
            if (!g_bot[slot]) continue;
            const int age = ++g_age[slot];
            const uint32_t entity = Guest::Read32(base, entities + 4 * (slot + 1));
            if (entity == 0) continue;
            if (age == 15) g_viewWatchFrom = GetTickCount();   // the host's view watched from here (0.5.92)
            if (age == 20 && !(g_botSetup.load() & 2)) PickSide(ctx, base, entity, BotSide(base, slot));
            if (age == 40 && !(g_botSetup.load() & 1)) PickKit(ctx, base, entity, 0);
            if (age == 60) Respawn(ctx, base, entity, BotSide(base, slot) == 1);
        }
    }
}

// A bot's move, made by the game the way it makes a remote player's when
// his commands arrive (a remote player is run from his packets, not from
// the game's frame): ClientThink (sub_82593518) on the bot's entity handle,
// with the game's current client number (0x82BCBE9C, which SV_GetUsercmd
// reads) the bot's while it runs.
namespace
{
    constexpr uint32_t ClientThink = 0x82593518;

    constexpr uint32_t ClientsMoveThemselves = 0x82B7C17C, MoveLocked = 0x82C73024;
    constexpr uint32_t FireWeapon = 0x82590C30;   // the server's firing of a player's weapon (his entity)

    void ThinkBots(PPCContext& ctx, uint8_t* base)
    {
        const uint32_t entities = Guest::Read32(base, EntityList);
        if (entities == 0) return;
        const uint32_t saved = Guest::Read32(base, CurrentClient);
        for (uint32_t slot = 4; slot < MaxClients; slot++)
        {
            if (!g_bot[slot] || g_age[slot] < 45) continue;
            const uint32_t entity = Guest::Read32(base, entities + 4 * (slot + 1));
            if (entity == 0) continue;
            KeepFov keepFov(base);   // 0.5.146
            BotGunViewScope gunView(base, slot, false);   // 0.5.153: his own gun view memory
            // 0.5.143: the current client is a local index (0-3); the bot's
            // own one (1-3) is used, not his slot (past the 4-entry lists:
            // the host's view zoomed with a second bot). ClientThink reads
            // it as the server client for SV_GetUsercmd, so that one call
            // is given the slot back (sub_82530270, below).
            Guest::Write32(base, CurrentClient, g_placeIndex[slot] ? g_placeIndex[slot] : slot);
            g_thinkSlot = slot;
            // His client's +1880 kept 2, as the host's own is: the server
            // fires a player's weapon (sub_82590C30) and applies damage to him
            // (sub_825862F8, by sub_8247E2F0) itself only for such a player;
            // others' it leaves to their own machines, which a bot has not.
            {
                const uint32_t clients = Clients(base);
                if (clients) Guest::Write32(base, clients + slot * ClientSize + 1880, 2);
            }
            // The weapon others see in his hands: the entity's byte +3 (the
            // host's holds his weapon there; a bot's was left 0, so his gun
            // was not drawn). Kept to the weapon in hand (record +164).
            {
                const uint32_t record = Guest::Read32(base, entity + 512);
                const uint32_t weapon = record ? Guest::Read32(base, record + 164) : 0;
                if (weapon < 256) Guest::Write8(base, entity + 3, uint8_t(weapon));
            }
            // Dead (health 0 or less) four seconds: spawned again, as the
            // game's own respawn would through his machine.
            {
                static uint32_t deadSince[MaxClients] = {};
                const uint32_t record = Guest::Read32(base, entity + 512);
                const int32_t health = record ? int32_t(Guest::Read32(base, record + 304)) : 1;
                const uint32_t nowMs = GetTickCount();
                if (health > 0) deadSince[slot] = 0;
                else if (deadSince[slot] == 0) deadSince[slot] = nowMs;
                // 0.5.168: dead is state 4. 0.5.165's dumps: the host's
                // record goes from 3 (in play) to 4 a moment after he dies; a
                // bot's stayed 3, so everything that shows a player's death
                // (sub_82400810 tests +1924 == 3: still alive) kept drawing
                // him standing: frozen, then gone at his respawn. Now a dead
                // bot is put in state 4 too, as the host is.
                // 0.5.169: this is its own test now. 0.5.168 put it in the
                // middle of the if/else chain above, so the respawn below ran
                // for every LIVE bot every frame (Justin: all 24 flashing,
                // falling, unkillable).
                if (health <= 0 && record && Guest::Read32(base, record + 1924) == 3)
                {
                    Guest::Write32(base, record + 1924, 4);
                    static int told = 0;
                    if ((told < 10 && told++ < 10)) { printf("bots: slot %u dead: state 3 -> 4, as the host's goes\n", slot); fflush(stdout); }
                }
                if (health <= 0 && deadSince[slot] != 0 && nowMs - deadSince[slot] > 4000 && SessionPlayer(base, slot) != 0)
                {
                    deadSince[slot] = 0;
                    printf("bots: slot %u respawns\n", slot); fflush(stdout);
                    Respawn(ctx, base, entity, BotSide(base, slot) == 1);
                }
            }
            {
                int pokeOffset = 0, pokeValue = 0;
                DebugProbe::BotPoke(pokeOffset, pokeValue);
                const uint32_t record = Guest::Read32(base, entity + 512);
                if (record && pokeOffset > 0 && pokeOffset < 2400) Guest::Write8(base, record + pokeOffset, uint8_t(pokeValue));
            }
            // ClientThink (sub_82592CD0) clears a command's forward, side and
            // up moves while the word at 0x82B7C17C is set, as it is in a
            // networked game: there each player's own machine moves him. A
            // bot's is the host, so for his think it is cleared.
            // PmoveSingle (sub_824B39B8) also clears the moves of a player
            // whose byte at 0x82C73024 + his slot is set. That is a table of
            // the four local players' (sub_8247E218 sets all four), so for a
            // bot's slot, 4 and on, it reads past it into something else.
            // That byte is zero only for the bot's think, then put back.
            const uint32_t lockByte = MoveLocked + slot;
            const uint8_t locked = Guest::Read8(base, lockByte);
            Guest::Write8(base, lockByte, 0);
            // 0.5.143: and his local index's byte (the current client now).
            const uint32_t lockLocal = g_placeIndex[slot] ? MoveLocked + g_placeIndex[slot] : 0;
            const uint8_t lockedLocal = lockLocal ? Guest::Read8(base, lockLocal) : 0;
            if (lockLocal) Guest::Write8(base, lockLocal, 0);
            const uint32_t clientMoves = Guest::Read32(base, ClientsMoveThemselves);
            Guest::Write32(base, ClientsMoveThemselves, 0);
            Call(ctx, base, ClientThink, Guest::Read32(base, entity + 484));
            Guest::Write32(base, ClientsMoveThemselves, clientMoves);
            Guest::Write8(base, lockByte, locked);
            if (lockLocal) Guest::Write8(base, lockLocal, lockedLocal);
            CheckFov(base, "think", slot);
            // His shot. His weapon's own firing (in his move) never fires for
            // him: the server fires a player's weapon itself only through
            // sub_82590C30, gated on his client's +1880 being 2 (the host's
            // is; a bot's is 0). So when he wants to fire, at most every
            // 125 ms, that is called for him with the gate open just for it.
            static uint32_t lastShot[MaxClients] = {};
            const uint32_t nowMs = GetTickCount();
            if (g_firing[slot] && nowMs - lastShot[slot] >= 125 && !(DebugProbe::BotButtons() & 0x0100))
            {
                lastShot[slot] = nowMs;
                const uint32_t clients = Clients(base);
                const uint32_t gate = clients ? clients + slot * ClientSize + 1880 : 0;
                const uint32_t was = gate ? Guest::Read32(base, gate) : 0;
                if (gate) Guest::Write32(base, gate, 2);
                // The weapon fired is the entity's byte +3 (the host's holds
                // his weapon there; a bot's was left 0): his in hand.
                const uint32_t record = Guest::Read32(base, entity + 512);
                const uint32_t weapon = record ? Guest::Read32(base, record + 164) : 0;
                if (weapon != 0 && weapon < 256) Guest::Write8(base, entity + 3, uint8_t(weapon));
                // The shot goes along his view (+204 pitch, +208 yaw): set to
                // where his brain wants to look, give or take a little (so he
                // is a soldier, not an aimbot), for the shot only.
                float saved[2] = {};
                if (record)
                {
                    saved[0] = ReadFloat(base, record + 204); saved[1] = ReadFloat(base, record + 208);
                    const float spread = 2.5f;
                    const float pitch = g_wantPitch[slot] + (float(rand() % 2001) / 1000.0f - 1.0f) * spread;
                    const float yaw = g_wantYaw[slot] + (float(rand() % 2001) / 1000.0f - 1.0f) * spread;
                    uint32_t bits;
                    memcpy(&bits, &pitch, 4); Guest::Write32(base, record + 204, bits);
                    Guest::Write32(base, record + 2096, bits);
                    memcpy(&bits, &yaw, 4); Guest::Write32(base, record + 208, bits);
                    Guest::Write32(base, record + 2100, bits);
                }
                Call(ctx, base, FireWeapon, entity);
                if (record)
                {
                    uint32_t bits;
                    memcpy(&bits, &saved[0], 4); Guest::Write32(base, record + 204, bits);
                    memcpy(&bits, &saved[1], 4); Guest::Write32(base, record + 208, bits);
                }
                if (gate) Guest::Write32(base, gate, was);
                static uint32_t shots = 0;
                if (shots++ % 20 == 0) { printf("bots: slot %u fires (%u shots)\n", slot, shots); fflush(stdout); }
            }
            static uint8_t said[MaxClients] = {};
            if (said[slot] != locked + 1) { said[slot] = uint8_t(locked + 1); printf("bots: slot %u's lock byte %u\n", slot, locked); fflush(stdout); }
        }
        g_thinkSlot = MaxClients;
        Guest::Write32(base, CurrentClient, saved);
    }
}

// The bot's brain, run once a frame for each bot before his command is
// written. What he can know is what the server knows: every player's
// entity (its origin, +336), his record (+512: origin +0, view pitch +120
// and yaw +124, health +304) and his side.
//
// This first one is simple and campaign-like: the nearest living player of
// the other side is his target; he turns toward the target's head (a few
// degrees a frame, so it reads as aiming, not snapping), runs at him until
// he is close, then strafes from side to side, and fires once his aim is
// near. The command's angles are what his view becomes less what the game
// added when he spawned, so they are steered by the difference between the
// view he has and the one he wants.
struct BotOrders { int8_t forward, right; uint32_t buttons; bool wantsFire; int8_t up = 0; };
// 0.5.170: how far the enemy he can see is (flat), for the melee; set by
// Think each frame, a long way when he sees no one.
static float g_seeDist[MaxClients] = {};
static float g_pitch[MaxClients] = {};

// 0.5.89: crouching and grenades. The buttons, read off the host's own
// commands while each key was pressed (0.5.88's log): the stance key 0x2000
// (a tap crouches or stands up again, held it goes prone), the frag grenade
// 0x80 (held to cook, let go to throw), the smoke grenade 0x2. 0.5.88's
// guesses (0x200, 0x4000) were wrong: 0x4000 is set in every command.
static uint32_t g_nadeUntil[MaxClients] = {}, g_nextNade[MaxClients] = {};
static bool g_crouched[MaxClients] = {};
static uint32_t g_stanceTaps[MaxClients] = {};   // frames left of a stance tap
constexpr uint32_t StanceButton = 0x2000, GrenadeButton = 0x80, SprintButton = 0x4;
static uint32_t BotGrenadeButton() { return GrenadeButton; }
// The stance key tapped (one frame) whenever he wants to go down or come up,
// as a player taps it; held, it would put him prone.
static uint32_t BotCrouch(uint32_t slot, bool want)
{
    if (want != g_crouched[slot])
    {
        g_crouched[slot] = want;
        g_stanceTaps[slot] = 1;
        static int said = 0;
        if ((said < 20 && said++ < 20)) { printf("bots: slot %u %s\n", slot, want ? "crouches" : "stands"); fflush(stdout); }
    }
    if (g_stanceTaps[slot] > 0) { g_stanceTaps[slot]--; return StanceButton; }
    return 0;
}

// Whether the command's pitch moves his view the same way (1) or the other
// way (-1). Set from debug.txt's bot_buttons 0x8000 bit for trying.
static float PitchSign() { return (DebugProbe::BotButtons() & 0x8000u) ? -1.0f : 1.0f; }

static float AngleDifference(float to, float from)
{
    float d = fmodf(to - from, 360.0f);
    if (d > 180.0f) d -= 360.0f;
    if (d < -180.0f) d += 360.0f;
    return d;
}

// 0.5.110: bounded native collision queries, using the corrected trace
// layout (position +0, plane normal +16). Each thread owns its scratch.
static BotNavigation::Hit BotTrace(PPCContext& ctx, uint8_t* base, uint32_t ignored,
                                  uint32_t bodyEntity, BotNavigation::Point from,
                                  BotNavigation::Point to, bool body)
{
    using namespace BotNavigation;
    Hit hit;
    if (!Finite(from) || !Finite(to)) return hit;
    static thread_local uint32_t memory = 0;
    if (!memory) memory = Guest::AllocatePhysical(0x21000);
    if (!memory) return hit;
    const uint32_t stack=memory+0x1FF00, scratch=memory+0x20000;
    const uint32_t start=scratch+96,end=scratch+112,mins=scratch+128,maxs=scratch+144,filter=scratch+160;
    memset(base+scratch,0,256);
    auto put=[&](uint32_t at,float f) { uint32_t word;memcpy(&word,&f,4);Guest::Write32(base,at,word); };
    put(start,from.x);put(start+4,from.y);put(start+8,from.z);
    put(end,to.x);put(end+4,to.y);put(end+8,to.z);
    if (body) {
        for (int k=0;k<3;k++) {
            float lo=ReadFloat(base,bodyEntity+240+4*k),hi=ReadFloat(base,bodyEntity+256+4*k);
            if (!std::isfinite(lo)||!std::isfinite(hi)||lo>hi||std::fabs(lo)>128||std::fabs(hi)>128) return hit;
            put(mins+4*k,lo);put(maxs+4*k,hi);
        }
        if (ReadFloat(base,maxs+8)-ReadFloat(base,mins+8)<8) return hit;
    }
    Guest::Write32(base,filter,0x82065DCC);
    Guest::Write32(base,filter+4,Guest::Read32(base,ignored+484));
    Guest::Write32(base,filter+20,0x00210011);
    Guest::Write32(base,stack,0);Guest::Write32(base,stack+84,0);Guest::Write32(base,stack+92,0);
    PPCContext call=ctx;call.r1.u64=stack;call.lr=0;
    call.r3.u64=scratch;call.r4.u64=start;call.r5.u64=mins;call.r6.u64=maxs;
    call.r7.u64=end;call.r8.u64=filter;call.r9.u64=0;call.r10.u64=0;call.f1.f64=0;
    sub_82534200(call,base);
    hit.fraction=ReadFloat(base,scratch+32);
    hit.end={ReadFloat(base,scratch),ReadFloat(base,scratch+4),ReadFloat(base,scratch+8)};
    hit.normal={ReadFloat(base,scratch+16),ReadFloat(base,scratch+20),ReadFloat(base,scratch+24)};
    hit.handle=Guest::Read32(base,scratch+48);
    hit.solid=base[scratch+60]||base[scratch+61];
    hit.valid=std::isfinite(hit.fraction)&&hit.fraction>=0&&hit.fraction<=1&&Finite(hit.end);
    return hit;
}
struct BotTacticalState {
    uint32_t entity=0,checked=0,planned=0,sampled=0,coverUntil=0,coverSearch=0,escapeUntil=0,sightSince=0,targetSlot=0xFFFFFFFFu,logged=0;
    int health=100,hand=1;
    bool visible=false,haveStep=false;
    BotNavigation::Point step{},cover{},last{};
};
static BotTacticalState g_tactical[MaxClients];

static void TacticalMove(PPCContext& ctx,uint8_t* base,uint32_t self,uint32_t threat,
                         uint32_t slot,uint32_t now,int health,float viewYaw,
                         const float target[3],bool atFlag,BotOrders& orders)
{
    if (!g_botTactics.load() || atFlag) return;
    using namespace BotNavigation;
    auto& state=g_tactical[slot];
    const uint32_t record=Guest::Read32(base,self+512);
    const Point feet{ReadFloat(base,record),ReadFloat(base,record+4),ReadFloat(base,record+8)};
    auto query=[&](Point a,Point b,bool body) {return BotTrace(ctx,base,self,self,a,b,body);};
    // 0.5.166: also when his gun is empty (reloading) with someone about, and
    // sooner when hurt (60, was 40).
    bool reloading=false;
    {
        const uint32_t weapon=Guest::Read32(base,record+164);
        const uint32_t list=Guest::Read32(base,0x82BCBF9C);
        const uint32_t def=(weapon>0&&weapon<128&&list)?Guest::Read32(base,list+4*weapon):0;
        if (def>=0x40000000u&&def<0xA0000000u) {
            const uint32_t clip=Guest::Read32(base,def+1468),spare=Guest::Read32(base,def+1460);
            if (clip<=128&&spare<=128) reloading=Guest::Read32(base,record+4*(172+clip))==0&&Guest::Read32(base,record+4*(80+spare))>0;
        }
    }
    if (threat && now-state.coverSearch>2500 && (health<=60 || health<state.health-14 || reloading)) {
        state.coverSearch=now;
        Point enemy{target[0],target[1],target[2]};
        auto sight=[&](Point a,Point b,bool body){return BotTrace(ctx,base,threat,self,a,b,body);};
        if (Cover(feet,enemy,query,sight,state.cover)) {
            state.coverUntil=now+4000;state.planned=0;
            printf("bots: tactics slot %u seeks cover at %.0f %.0f\n",slot,state.cover.x,state.cover.y);fflush(stdout);
        }
    }
    state.health=health;
    const bool shelter=now<state.coverUntil;
    if (shelter && Distance(feet,state.cover)<35) {
        orders.forward=orders.right=0;
        orders.buttons&=~SprintButton;
        orders.buttons|=BotCrouch(slot,true);
        return;
    }
    if (!shelter && orders.forward==0 && orders.right==0) return;
    Point goal=shelter?state.cover:Point{target[0],target[1],feet.z};
    if (!shelter && orders.forward<0) {
        float yaw=std::atan2(feet.y-goal.y,feet.x-goal.x);
        goal={feet.x+std::cos(yaw)*128,feet.y+std::sin(yaw)*128,feet.z};
    }
    if (now-state.sampled>1000) {
        if (state.sampled && Distance(feet,state.last)<12) { state.hand=-state.hand;state.escapeUntil=now+1400; }
        state.sampled=now;state.last=feet;
    }
    if (now-state.planned>=250 || !state.haveStep || Distance(feet,state.step)<30) {
        state.planned=now;
        state.haveStep=Step(feet,goal,state.hand,now<state.escapeUntil,query,state.step);
    }
    if (!state.haveStep) {orders.forward=orders.right=0;orders.buttons&=~SprintButton;return;}
    const float yaw=std::atan2(state.step.y-feet.y,state.step.x-feet.x);
    const float relative=yaw-viewYaw*0.01745329252f;
    orders.forward=int8_t(std::clamp(std::cos(relative)*127.0f,-127.0f,127.0f));
    // The stick's positive right is clockwise from the view's forward.
    orders.right=int8_t(std::clamp(-std::sin(relative)*127.0f,-127.0f,127.0f));
    if (shelter) {orders.buttons&=~SprintButton;orders.buttons|=BotCrouch(slot,false);}
    if (now-state.logged>3000) {
        state.logged=now;
        printf("bots: tactics slot %u route %.0f %.0f, visibility %d, cover %d\n",slot,state.step.x,state.step.y,state.visible,shelter);fflush(stdout);
    }
}

static BotOrders Think(PPCContext& ctx, uint8_t* base, uint32_t slot, uint32_t now)
{
    BotOrders orders = {};
    g_seeDist[slot] = 1.0e9f;
    const uint32_t entities = Guest::Read32(base, EntityList);
    const uint32_t self = entities ? Guest::Read32(base, entities + 4 * (slot + 1)) : 0;
    const uint32_t record = self ? Guest::Read32(base, self + 512) : 0;
    if (!record) return orders;
    const int32_t health = int32_t(Guest::Read32(base, record + 304));
    if (health <= 0)
    {
        // Dead: tap the use button now and then, which asks to respawn.
        orders.buttons = (now / 500) % 2 ? 0x20 : 0;
        g_crouched[slot] = false;   // he comes back standing
        g_stanceTaps[slot] = 0;
        g_tactical[slot] = {};
        return orders;
    }
    if (g_tactical[slot].entity != self) {
        g_tactical[slot] = {};g_tactical[slot].entity=self;g_tactical[slot].hand=slot%2?1:-1;
    }
    // 0.5.166: from his own eyes (+220, lower when he crouches or lies),
    // not always 60 above his feet.
    float myEye = ReadFloat(base, record + 220);
    if (!std::isfinite(myEye) || myEye < 4.0f || myEye > 80.0f) myEye = 60.0f;
    const float me[3] = { ReadFloat(base, record), ReadFloat(base, record + 4), ReadFloat(base, record + 8) + myEye };
    // His side, kept in both places the session's switch sets (the team
    // record's +4 and his session player's +604): the game's spawn puts the
    // host's side back on him (his session player is a copy of the host's).
    if (const uint32_t player = SessionPlayer(base, slot))
    {
        const uint32_t want = BotSide(base, slot);
        const uint32_t team = Guest::Read32(base, self + 520);
        if (team != 0 && (Guest::Read32(base, team + 4) != want || Guest::Read16(base, player + 604) != want))
        {
            static uint32_t told[MaxClients] = {};
            if (told[slot]++ < 5)
            {
                printf("bots: slot %u back to side %u (team %u, session %u)\n", slot, want, Guest::Read32(base, team + 4),
                       Guest::Read16(base, player + 604));
                fflush(stdout);
            }
            Guest::Write32(base, team + 4, want);
            Guest::Write16(base, player + 604, uint16_t(want));
        }
    }
    {
        int pokeOffset = 0, pokeValue = 0;
        DebugProbe::BotPoke(pokeOffset, pokeValue);
        if (pokeOffset > 0 && pokeOffset < 2400) Guest::Write8(base, record + pokeOffset, uint8_t(pokeValue));
    }
    const uint32_t mySide = Side(base, self);

    float best = 1e30f, target[3] = {};
    bool found = false;
    uint32_t targetSlot = 0xFFFFFFFFu;
    for (uint32_t other = 0; other < MaxClients; other++)
    {
        if (other == slot) continue;
        const uint32_t entity = Guest::Read32(base, entities + 4 * (other + 1));
        const uint32_t theirs = entity ? Guest::Read32(base, entity + 512) : 0;
        if (!theirs) continue;
        if (int32_t(Guest::Read32(base, theirs + 304)) <= 0) continue;
        if (Guest::Read32(base, theirs + 1924) != 3) continue;   // in the game, not watching or dead
        const uint32_t side = Side(base, entity);
        if (side == mySide && side != 0) continue;
        // 0.5.166: Justin: the bots should shoot him when he lies down.
        // A prone or crouched player's eyes (+220) are low (about 10 and
        // 40); anything under 20 was taken as a mistake and replaced by a
        // standing player's 58, and the aim never went under 24: they shot
        // over a prone player. Now his real eye height, a little under it
        // (the head and shoulders), down to 6.
        float eye=ReadFloat(base,theirs+220);
        if (!std::isfinite(eye)||eye<4||eye>80) eye=58;
        const float height=std::clamp(eye-2.0f,6.0f,72.0f);
        const float at[3] = { ReadFloat(base, theirs), ReadFloat(base, theirs + 4), ReadFloat(base, theirs + 8) + height };
        const float dx = at[0] - me[0], dy = at[1] - me[1], dz = at[2] - me[2];
        const float distance = dx * dx + dy * dy + dz * dz;
        if (distance < best) { best = distance; memcpy(target, at, sizeof(target)); found = true; targetSlot = other; }
    }

    uint32_t threat=found?Guest::Read32(base,entities+4*(targetSlot+1)):0;
    auto& tactics=g_tactical[slot];
    bool visible=found;
    if (g_botTactics.load() && found) {
        if (now-tactics.checked>=200 || tactics.targetSlot!=targetSlot) {
            bool before=tactics.visible;
            auto hit=BotTrace(ctx,base,self,self,{me[0],me[1],me[2]}, {target[0],target[1],target[2]},false);
            tactics.visible=hit.valid&&!hit.solid&&(hit.fraction>=0.97f || hit.handle==Guest::Read32(base,threat+484));
            if (tactics.visible && (!before || tactics.targetSlot!=targetSlot)) tactics.sightSince=now;
            tactics.targetSlot=targetSlot;tactics.checked=now;
        }
        visible=tactics.visible;
    }
    // 0.5.170: for the melee, smoke (BotExtras).
    if (found && visible) { const float ex = target[0] - me[0], ey = target[1] - me[1]; g_seeDist[slot] = sqrtf(ex * ex + ey * ey); }

    // His view: +204 pitch, +208 yaw (what the server's fire shoots along,
    // sub_8256E978).
    const float viewPitch = ReadFloat(base, record + 204), viewYaw = ReadFloat(base, record + 208);

    // 0.5.88: the objective. In a Domination the flag his side should take
    // or hold (dom.cpp); he goes for it unless an enemy is near (900 units).
    float goal[3] = {};
    bool atFlag = false;
    bool hasGoal = Domination::BotGoal(mySide, slot, me, goal, atFlag);
    // 0.5.151: in War, the flag in play (both sides go for it: taking it
    // pushes the line on, holding it keeps it).
    // 0.5.165: Justin, on 0.5.164 with 20 bots: most stood still. They all
    // went to War's one flag and held it crouched. Now about a third of them
    // (every third slot) hunt instead, the rest play the flag.
    // 0.5.168: by role: only the objective players go for it.
    if (!hasGoal && g_war.valid && GetTickCount() - g_war.when < 10000 && g_role[slot] == RoleObjective)
    {
        memcpy(goal, g_war.at, sizeof(goal));
        const float gx = goal[0] - me[0], gy = goal[1] - me[1];
        atFlag = gx * gx + gy * gy < 120.0f * 120.0f && fabsf(goal[2] - me[2]) < 120.0f;
        hasGoal = true;
    }
    const float enemyDistance = found ? sqrtf(best) : 1e30f;
    const bool toFlag = hasGoal && !atFlag && (!visible || enemyDistance > 900.0f);
    if (toFlag)
    {
        memcpy(target, goal, sizeof(target));
        target[2] = me[2];   // look ahead, level, while running there
    }
    if (!found && !hasGoal)
    {
        static uint32_t lastLonely[MaxClients] = {};
        if (now - lastLonely[slot] > 3000)
        {
            lastLonely[slot] = now;
            const uint32_t host = Guest::Read32(base, entities + 4);
            const uint32_t hostRecord = host ? Guest::Read32(base, host + 512) : 0;
            printf("bots: slot %u (side %u) finds no one; the host: side %u, health %d, state %d\n", slot, mySide,
                   Side(base, host), hostRecord ? int32_t(Guest::Read32(base, hostRecord + 304)) : -1,
                   hostRecord ? int32_t(Guest::Read32(base, hostRecord + 1924)) : -1);
            fflush(stdout);
        }
        // No one to fight. 0.5.168, by role:
        static uint32_t nextTurn[MaxClients] = {}, campUntil[MaxClients] = {}, roamUntil[MaxClients] = {};
        static float heading[MaxClients] = {};
        const uint32_t jitter = (slot * 7919u + now / 1000u) % 5000u;
        if (g_role[slot] == RoleCamper)
        {
            // Hold a spot crouched, looking about; every 40-60 s move on for 8 s.
            if (campUntil[slot] == 0) campUntil[slot] = now + 40000 + jitter * 4;
            if (now >= campUntil[slot]) { roamUntil[slot] = now + 8000; campUntil[slot] = now + 48000 + jitter * 4; }
            if (now < roamUntil[slot]) { g_yaw[slot] += 0.4f; orders.forward = 110; return orders; }
            g_yaw[slot] += (now / 2500) % 2 ? 0.7f : -0.7f;
            orders.buttons = BotCrouch(slot, true);
            return orders;
        }
        if (g_role[slot] == RoleFlanker || g_role[slot] == RoleRusher)
        {
            // Long straight runs in a heading changed every 6-11 s (the
            // outskirts: away from the middle of where everyone is); the
            // rusher sprints them.
            if (now >= nextTurn[slot])
            {
                nextTurn[slot] = now + 6000 + jitter;
                heading[slot] = g_yaw[slot] + 70.0f + float((slot * 37u + now / 977u) % 220u);
            }
            const float step = AngleDifference(heading[slot], g_yaw[slot]);
            g_yaw[slot] += fmaxf(-4.0f, fminf(4.0f, step));
            orders.forward = 127;
            if (g_role[slot] == RoleRusher || fabsf(step) < 10.0f) orders.buttons |= SprintButton;
            return orders;
        }
        // The objective player with nothing to do: wander, turning slowly.
        g_yaw[slot] += 0.6f;
        orders.forward = 90;
        return orders;
    }
    if (!found && hasGoal && atFlag)
    {
        // At his flag with no one about: hold it, crouched, looking around.
        g_yaw[slot] += 0.8f;
        orders.buttons = BotCrouch(slot, true);
        return orders;
    }
    const float dx = target[0] - me[0], dy = target[1] - me[1], dz = target[2] - me[2];
    const float flat = sqrtf(dx * dx + dy * dy);
    const float wantYaw = atan2f(dy, dx) * 57.29578f;
    // Pitch: up is positive in this game's view (tried: the other way he
    // aimed at the wall above a lower target).
    float wantPitch = atan2f(dz, flat) * 57.29578f;
    // 0.5.88: a grenade is thrown a little upward, for its arc.
    const bool throwing = now < g_nadeUntil[slot];
    if (throwing) wantPitch += 14.0f;
    g_wantYaw[slot] = wantYaw; g_wantPitch[slot] = wantPitch;
    const float yawError = AngleDifference(wantYaw, viewYaw);
    const float pitchError = AngleDifference(wantPitch, viewPitch);
    // Turn toward the target. His view is what is sent plus an offset the
    // game set at his spawn, shown several frames late; steering by the
    // error over-corrected and swung. So the offset is measured while what
    // is sent has held still for a while (the view has caught up), and what
    // is sent is the wanted yaw less the offset, eased (at most 8 degrees a
    // frame) so he turns like a player.
    {
        static float offset[MaxClients] = {};
        static bool known[MaxClients] = {};
        static float lastSent[MaxClients] = {};
        static uint32_t stillSince[MaxClients] = {};
        if (fabsf(AngleDifference(g_yaw[slot], lastSent[slot])) > 0.5f) { stillSince[slot] = now; lastSent[slot] = g_yaw[slot]; }
        if (now - stillSince[slot] > 300)
        {
            const float measured = AngleDifference(viewYaw, g_yaw[slot]);
            offset[slot] = known[slot] ? offset[slot] + AngleDifference(measured, offset[slot]) * 0.5f : measured;
            known[slot] = true;
        }
        if (known[slot])
        {
            const float step = AngleDifference(wantYaw - offset[slot], g_yaw[slot]);
            g_yaw[slot] += fmaxf(-8.0f, fminf(8.0f, step));
        }
    }
    // Pitch the same way as yaw: his view's pitch (+204) is what is sent
    // plus an offset (about 10 degrees seen), learnt while the sent pitch
    // holds still.
    {
        static float pOffset[MaxClients] = {};
        static float lastSentP[MaxClients] = {};
        static uint32_t stillSinceP[MaxClients] = {};
        if (fabsf(g_pitch[slot] - lastSentP[slot]) > 0.5f) { stillSinceP[slot] = now; lastSentP[slot] = g_pitch[slot]; }
        if (now - stillSinceP[slot] > 300)
            pOffset[slot] += ((viewPitch - g_pitch[slot] * PitchSign()) - pOffset[slot]) * 0.5f;
        const float target = (wantPitch - pOffset[slot]) * PitchSign();
        g_pitch[slot] += (target - g_pitch[slot]) * 0.3f;
    }
    g_pitch[slot] = fmaxf(-70.0f, fminf(70.0f, g_pitch[slot]));

    // Each bot a little different: his slot shifts his timings.
    const uint32_t phase = slot * 7919u;
    // 0.5.90: a reaction time. When he first sees someone (a new nearest
    // enemy) he takes 350 to 650 ms before he fires, as a player would.
    static uint32_t seenSlot[MaxClients], seenAt[MaxClients];
    if (found && targetSlot != seenSlot[slot]) { seenSlot[slot] = targetSlot; seenAt[slot] = now; }
    const bool reacted = found && visible && now - seenAt[slot] >= 350 + (phase % 300) &&
                         (!g_botTactics.load() || now-tactics.sightSince>=350+(phase%300));
    if (toFlag)
    {
        // Running to the flag, weaving a little, sprinting (0x4, the button
        // the host's commands carry while he runs flat out).
        orders.forward = 127;
        orders.right = ((now + phase) / 1400) % 2 ? 30 : -30;
        orders.buttons |= SprintButton;
        TacticalMove(ctx,base,self,0,slot,now,health,viewYaw,target,false,orders);
        return orders;
    }

    bool crouch = false;
    if (atFlag)
    {
        // Holding a flag with an enemy near: stays on it, crouched, fighting.
        orders.forward = 0;
        orders.right = 0;
        crouch = true;
    }
    else
    {
        if (flat > 500.0f) orders.forward = 127;
        else if (flat < 150.0f) orders.forward = -60;
        orders.right = ((now + phase) / 900) % 2 ? 70 : -70;
        // 0.5.88: at range, now and then he stops and crouches to shoot
        // (about a third of the time, a couple of seconds at a go).
        if (flat > 700.0f && fabsf(yawError) < 15.0f && ((now + phase) / 2200) % 3 == 0)
        {
            crouch = true;
            orders.forward = 0;
            orders.right = 0;
        }
    }
    // Fire when the aim is close, in bursts (pressed every other frame, so a
    // rifle that wants each shot its own press fires too).
    if (!throwing && reacted && fabsf(yawError) < 6.0f && fabsf(pitchError) < 6.0f && flat < 1600.0f)
    {
        static uint32_t frame = 0;
        const uint32_t fire = (DebugProbe::BotButtons() & 0x00FF) ? (DebugProbe::BotButtons() & 0x00FF) : 0x1;
        orders.buttons = (++frame & 1) ? fire : 0;
        orders.wantsFire = true;
    }
    // 0.5.88: a grenade, at someone 450 to 1400 units off he's facing,
    // every 18 to 25 seconds: the grenade button held a moment (aimed a
    // little up), then let go to throw.
    if (g_nextNade[slot] == 0) g_nextNade[slot] = now + 8000 + (phase % 6000);   // none in his first seconds
    // 0.5.166: also at someone he saw in the last 4 s who has gone behind
    // cover, 300 to 1100 units off: the grenade's job.
    static uint32_t lastSeen[MaxClients] = {};
    if (visible) lastSeen[slot] = now;
    const bool hidingNear = found && !visible && now - lastSeen[slot] < 4000 && flat > 300.0f && flat < 1100.0f;
    if (!throwing && (visible || hidingNear) && now >= g_nextNade[slot] && flat > (visible ? 450.0f : 300.0f) && flat < 1400.0f && fabsf(yawError) < 10.0f)
    {
        g_nadeUntil[slot] = now + 350;
        g_nextNade[slot] = now + 18000 + (phase % 7000);
        printf("bots: slot %u throws a grenade at %.0f\n", slot, flat); fflush(stdout);
    }
    if (now < g_nadeUntil[slot]) orders.buttons = BotGrenadeButton();
    // 0.5.90: far off (over 1200 units) and not shooting, he sprints in.
    if (!crouch && !orders.wantsFire && !throwing && orders.forward == 127 && flat > (g_role[slot] == RoleRusher ? 300.0f : 1200.0f)) orders.buttons |= SprintButton;
    orders.buttons |= BotCrouch(slot, crouch);
    // Finder for his real view angles: each second, every float in his
    // record that moved as much as the sent yaw (or pitch) did, modulo 360.
    {
        static float before[MaxClients][600] = {};
        static float sentBefore[MaxClients][2] = {};
        static uint32_t lastLook[MaxClients] = {};
        if (now - lastLook[slot] > 1000)
        {
            lastLook[slot] = now;
            const float dYaw = g_yaw[slot] - sentBefore[slot][0], dPitch = g_pitch[slot] - sentBefore[slot][1];
            if (fabsf(AngleDifference(dYaw, 0.0f)) > 8.0f || fabsf(dPitch) > 4.0f)
            {
                printf("bots: view finder (yaw moved %.1f, pitch %.1f):", AngleDifference(dYaw, 0.0f), dPitch);
                for (uint32_t i = 0; i < 600; i++)
                {
                    const float v = ReadFloat(base, record + 4 * i);
                    if (!(v > -400.0f && v < 400.0f)) continue;
                    const float d = AngleDifference(v - before[slot][i], 0.0f);
                    if (fabsf(AngleDifference(dYaw, 0.0f)) > 8.0f && fabsf(d - AngleDifference(dYaw, 0.0f)) < 1.0f) printf(" yaw@+%u(%.1f)", i * 4, v);
                    if (fabsf(dPitch) > 4.0f && fabsf(d - dPitch) < 1.0f) printf(" pitch@+%u(%.1f)", i * 4, v);
                    if (fabsf(dPitch) > 4.0f && fabsf(d + dPitch) < 1.0f) printf(" -pitch@+%u(%.1f)", i * 4, v);
                }
                printf("\n"); fflush(stdout);
            }
            for (uint32_t i = 0; i < 600; i++) before[slot][i] = ReadFloat(base, record + 4 * i);
            sentBefore[slot][0] = g_yaw[slot]; sentBefore[slot][1] = g_pitch[slot];
        }
    }
    static uint32_t lastSaid[MaxClients] = {};
    if (now - lastSaid[slot] > 2000)
    {
        lastSaid[slot] = now;
        printf("bots: slot %u side %u targets %.0f away, aim off %.1f %.1f (view %.1f %.1f, want %.1f %.1f, sent %.1f %.1f), fire %X\n",
               slot, mySide, flat, yawError, pitchError, viewYaw, viewPitch, wantYaw, wantPitch, g_yaw[slot], g_pitch[slot],
               orders.buttons);
        printf("bots:   record +100..+136:");
        for (uint32_t o = 100; o <= 136; o += 4) printf(" %.1f", ReadFloat(base, record + o));
        printf("\n");
        fflush(stdout);
    }
    TacticalMove(ctx,base,self,threat,slot,now,health,viewYaw,target,atFlag,orders);
    return orders;
}

// 0.5.170: Justin: the bots should melee when close, use their smoke
// grenades, reload, and jump. The buttons, from the game's own table of
// them (CL_FinishMove, sub_82522B10: bit n is the key at 0x82B53E20 +
// 0x138 + 0x18 n; each key's +command found by its handler): 0x1 attack,
// 0x2 smoke grenade, 0x4 sprint, 0x10 melee, 0x20 use, 0x40 use+reload,
// 0x80 the class's grenade, 0x400 reload, 0x800/0x1000 lean, 0x2000 stance.
// A jump is the command's up move (+26) at 127, as +moveup sends it.
constexpr uint32_t MeleeButton = 0x10, SmokeButton = 0x2, ReloadButton = 0x400;
static void BotExtras(uint8_t* base, uint32_t slot, BotOrders& orders)
{
    const uint32_t entities = Guest::Read32(base, EntityList);
    const uint32_t entity = entities ? Guest::Read32(base, entities + 4 * (slot + 1)) : 0;
    const uint32_t record = entity ? Guest::Read32(base, entity + 512) : 0;
    if (!record || int32_t(Guest::Read32(base, record + 304)) <= 0) return;
    const int32_t health = int32_t(Guest::Read32(base, record + 304));
    const uint32_t now = GetTickCount();
    const uint32_t phase = slot * 7919u;
    static uint32_t nextMelee[MaxClients], nextSmoke[MaxClients], smokeUntil[MaxClients];
    static uint32_t nextReload[MaxClients], reloadUntil[MaxClients];
    static uint32_t nextHop[MaxClients], lastCheck[MaxClients];
    static float lastAt[MaxClients][2];
    static int told = 0;
    // Melee: someone in sight within arm's reach (the game's own melee
    // reaches about 64; its assist pulls him the rest).
    if (g_seeDist[slot] < 72.0f && now >= nextMelee[slot])
    {
        orders.buttons = (orders.buttons & ~1u) | MeleeButton;
        nextMelee[slot] = now + 700 + phase % 300;
        if ((told < 60 && told++ < 60)) { printf("bots: slot %u melees (%.0f away)\n", slot, g_seeDist[slot]); fflush(stdout); }
    }
    // Smoke: hurt (half health or less) with someone in sight, once in a
    // while: thrown at his feet's way to cover himself, as a player would.
    if (nextSmoke[slot] == 0) nextSmoke[slot] = now + 10000 + phase % 8000;
    if (health <= 50 && g_seeDist[slot] < 1500.0f && now >= nextSmoke[slot])
    {
        smokeUntil[slot] = now + 350;
        nextSmoke[slot] = now + 30000 + phase % 15000;
        if ((told < 60 && told++ < 60)) { printf("bots: slot %u throws smoke (health %d)\n", slot, health); fflush(stdout); }
    }
    if (now < smokeUntil[slot]) orders.buttons = (orders.buttons & ~(1u | 0x80u)) | SmokeButton;
    // Reload: the gun in hand's clip empty with rounds to spare (Justin never
    // saw a bot reload).
    {
        const uint32_t weapon = Guest::Read32(base, record + 164);
        const uint32_t list = Guest::Read32(base, 0x82BCBF9C);
        const uint32_t def = (weapon && weapon < 128 && list) ? Guest::Read32(base, list + 4 * weapon) : 0;
        if (def >= 0x40000000u && def < 0xA0000000u)
        {
            const uint32_t clip = Guest::Read32(base, def + 1468), spare = Guest::Read32(base, def + 1460);
            if (clip <= 128 && spare <= 128 && Guest::Read32(base, record + 4 * (172 + clip)) == 0 &&
                Guest::Read32(base, record + 4 * (80 + spare)) > 0 && now >= nextReload[slot])
            {
                reloadUntil[slot] = now + 150;
                nextReload[slot] = now + 2500;
                if ((told < 60 && told++ < 60)) { printf("bots: slot %u reloads weapon %u\n", slot, weapon); fflush(stdout); }
            }
        }
        if (now < reloadUntil[slot]) orders.buttons = (orders.buttons & ~1u) | ReloadButton;
    }
    // Jump: when he wants to go forward but has hardly moved in a second
    // (stuck on something low), and now and then running about (rushers
    // and flankers more).
    {
        const float x = ReadFloat(base, record), y = ReadFloat(base, record + 4);
        if (now - lastCheck[slot] >= 1000)
        {
            const float mx = x - lastAt[slot][0], my = y - lastAt[slot][1];
            const bool stuck = lastCheck[slot] != 0 && orders.forward > 60 && mx * mx + my * my < 24.0f * 24.0f;
            lastAt[slot][0] = x; lastAt[slot][1] = y; lastCheck[slot] = now;
            if (stuck)
            {
                orders.up = 127;
                if ((told < 60 && told++ < 60)) { printf("bots: slot %u is stuck: jumps\n", slot); fflush(stdout); }
            }
        }
        if (nextHop[slot] == 0) nextHop[slot] = now + 6000 + phase % 9000;
        if (now >= nextHop[slot] && orders.forward > 60 && g_seeDist[slot] > 600.0f)
        {
            orders.up = 127;
            const uint32_t gap = (g_role[slot] == RoleRusher || g_role[slot] == RoleFlanker) ? 9000u : 20000u;
            nextHop[slot] = now + gap + (now * 2654435761u >> 20) % gap;
        }
    }
}

// Each bot's command for this frame, into its ring. For trying, from
// debug.txt: bot_sticks' left stick walks (lx right, ly forward), its right
// stick's x turns, bot_buttons are the buttons.
static void FeedBots(PPCContext& ctx, uint8_t* base)
{
    // The time the host's own player's newest command carries: the server's
    // clock as the commands count it.
    const uint32_t serverTime = Guest::Read32(base, NewestCommand(base, 0));
    int16_t lx, ly, rx, ry;
    DebugProbe::BotSticks(lx, ly, rx, ry);
    const uint32_t buttons = DebugProbe::BotButtons();
    for (uint32_t slot = 4; slot < MaxClients; slot++)
    {
        if (!g_bot[slot]) continue;
        // With bot_sticks all zero, the bot's own brain drives him (Think
        // below); otherwise the sticks do, for trying things by hand.
        const bool brain = lx == 0 && ly == 0 && rx == 0 && ry == 0;
        BotOrders orders = {};
        if (brain) { orders = Think(ctx, base, slot, serverTime); BotExtras(base, slot, orders); }
        else g_yaw[slot] += float(rx) / 32768.0f * 3.0f;
        const uint32_t player = CommandPlayer(base, slot);   // 0.5.159
        const uint32_t next = (Guest::Read32(base, player + Newest) + 1);
        const uint32_t command = player + Ring + (next & 63) * CommandSize;
        memset(base + command, 0, CommandSize);
        Guest::Write32(base, command + 0, serverTime);
        Guest::Write32(base, command + 4, brain ? orders.buttons : (buttons & 0x00FF));
        g_firing[slot] = ((brain ? orders.buttons : buttons) & 1) != 0 || (brain && orders.wantsFire);
        // The weapon he holds (+8 of a command, as the host's own carry it):
        // the one his record has in hand (+164), or the one waiting (+196).
        // With none his move speed is nothing; he could turn but not walk.
        {
            const uint32_t entities = Guest::Read32(base, EntityList);
            const uint32_t entity = entities ? Guest::Read32(base, entities + 4 * (slot + 1)) : 0;
            const uint32_t record = entity ? Guest::Read32(base, entity + 512) : 0;
            uint32_t weapon = record ? Guest::Read32(base, record + 164) : 0;
            if (weapon == 0 && record) weapon = Guest::Read32(base, record + 196);
            if (weapon > 255) weapon = 0;
            // 0.5.133: the session respawn leaves his hands empty though he
            // still owns his kit (0.5.132's kit again changed nothing: "in
            // hand 0" after it). A command's weapon is how a player asks for
            // the weapon he wants up, so with nothing in hand he asks for
            // the last one he held (his first kit's, 19 in the logs).
            static uint32_t lastHeld[MaxClients] = {};
            if (weapon != 0) lastHeld[slot] = weapon;
            else if (record)
            {
                const uint32_t remembered = lastHeld[slot];
                weapon = BotWeaponSelection::Recover(remembered,
                    Guest::Read8(base, record + 1066), Guest::Read8(base, record + 1065),
                    [&](uint32_t n) { return (Guest::Read32(base, record + 1056 + 4 * (n >> 5)) & (1u << (n & 31))) != 0; });
                lastHeld[slot] = weapon;
                if (weapon != remembered)
                {
                    static int recovered = 0;
                    if ((recovered < 60 && recovered++ < 60)) { printf("bots: slot %u empty hands: stale weapon %u replaced by owned kit weapon %u\n", slot, remembered, weapon); fflush(stdout); }
                }
            }
            // 0.5.134: out of ammunition, he asks for his other gun. A
            // weapon's rounds (as work/read_weapon_cycle.py reads them): its
            // def from the list at 0x82BCBF9C, its clip at the record's
            // +4*(172 + def+1468) and its spare at +4*(80 + def+1460); owned
            // when bit (n & 31) of +1056 + 4*(n >> 5) is set; the kit's guns
            // in the slot bytes +1065 (the pistol) and +1066 (the main one).
            if (record && weapon != 0)
            {
                auto rounds = [&](uint32_t n) -> int {
                    if (n == 0 || n >= 128) return -1;
                    const uint32_t list = Guest::Read32(base, 0x82BCBF9C);
                    const uint32_t def = list ? Guest::Read32(base, list + 4 * n) : 0;
                    if (def < 0x40000000u || def >= 0xA0000000u) return -1;
                    if (!(Guest::Read32(base, record + 1056 + 4 * (n >> 5)) & (1u << (n & 31)))) return -1;
                    const uint32_t clip = Guest::Read32(base, def + 1468), spare = Guest::Read32(base, def + 1460);
                    if (clip > 128 || spare > 128) return -1;
                    return int(Guest::Read32(base, record + 4 * (172 + clip))) + int(Guest::Read32(base, record + 4 * (80 + spare)));
                };
                if (rounds(weapon) == 0)
                {
                    const uint32_t main = Guest::Read8(base, record + 1066), pistol = Guest::Read8(base, record + 1065);
                    for (const uint32_t other : { main, pistol })
                        if (other != weapon && rounds(other) > 0)
                        {
                            static int told = 0;
                            if ((told < 40 && told++ < 40)) printf("bots: slot %u is out of rounds for %u: asks for %u (%d rounds)\n", slot, weapon, other, rounds(other));
                            weapon = other;
                            lastHeld[slot] = other;
                            break;
                        }
                }
            }
            Guest::Write32(base, command + 8, weapon);
            static uint32_t said[MaxClients] = {};
            if (said[slot] != weapon + 1)
            {
                said[slot] = weapon + 1;
                printf("bots: slot %u holds weapon %u\n", slot, weapon); fflush(stdout);
            }
        }
        if (brain)
        {
            if (!(DebugProbe::BotButtons() & 0x2000))
                Guest::Write32(base, command + 12, uint32_t(int32_t(g_pitch[slot] * 65536.0f / 360.0f)) & 0xFFFF);
            Guest::Write32(base, command + 16, uint32_t(int32_t(g_yaw[slot] * 65536.0f / 360.0f)) & 0xFFFF);
            Guest::Write8(base, command + 24, uint8_t(orders.forward));
            Guest::Write8(base, command + 25, uint8_t(orders.right));
            Guest::Write8(base, command + 26, uint8_t(orders.up));   // 0.5.170: a jump
            // The view his machine would send with each command (+28 pitch,
            // +32 yaw, as the host's carry it): for a host-side player the
            // server's fire shoots along it (record +2096/+2100), not +204/+208.
            {
                uint32_t bits;
                memcpy(&bits, &g_wantPitch[slot], 4); Guest::Write32(base, command + 28, bits);
                memcpy(&bits, &g_wantYaw[slot], 4); Guest::Write32(base, command + 32, bits);
            }
        }
        else
        {
            Guest::Write32(base, command + 16, uint32_t(int32_t(g_yaw[slot] * 65536.0f / 360.0f)) & 0xFFFF);
            Guest::Write8(base, command + 24, uint8_t(int8_t(ly / 258)));
            Guest::Write8(base, command + 25, uint8_t(int8_t(lx / 258)));
        }
        Guest::Write32(base, player + Newest, next);
    }
    // The host's buttons whenever they change, for learning which is which.
    {
        static uint32_t lastButtons = 0;
        const uint32_t hostButtons = Guest::Read32(base, NewestCommand(base, 0) + 4);
        if (hostButtons != lastButtons)
        {
            lastButtons = hostButtons;
            printf("bots: host buttons %08X\n", hostButtons); fflush(stdout);
        }
    }
    // Deaths, for learning how a death is shown: for any player (host or
    // bot) whose health falls to 0 or less, his entity's first 256 bytes and
    // record's +36..+60, at the death and 300 ms after.
    {
        static int32_t lastHealth[MaxClients] = {};
        static uint32_t diedAt[MaxClients] = {};
        static bool second[MaxClients] = {};
        const uint32_t entities = Guest::Read32(base, EntityList);
        for (uint32_t slot = 0; slot < MaxClients && entities; slot++)
        {
            if (slot != 0 && !g_bot[slot]) continue;
            const uint32_t entity = Guest::Read32(base, entities + 4 * (slot + 1));
            const uint32_t r = entity ? Guest::Read32(base, entity + 512) : 0;
            if (!r) continue;
            const int32_t health = int32_t(Guest::Read32(base, r + 304));
            bool dump = false;
            if (lastHealth[slot] > 0 && health <= 0) { diedAt[slot] = serverTime; second[slot] = true; dump = true; }
            else if (second[slot] && serverTime - diedAt[slot] > 300) { second[slot] = false; dump = true; }
            lastHealth[slot] = health;
            if (!dump) continue;
            printf("bots: death of %s %u (%s): entity", slot == 0 ? "host" : "bot", slot, second[slot] ? "at once" : "300 ms on");
            for (uint32_t o = 0; o < 256; o += 4) printf(" %08X", Guest::Read32(base, entity + o));
            printf(" | record +36..+60");
            for (uint32_t o = 36; o <= 60; o += 4) printf(" %08X", Guest::Read32(base, r + o));
            printf(" | state %d\n", int32_t(Guest::Read32(base, r + 1924)));
            // 0.5.165: his session player too (784 bytes), to compare a bot's
            // death with the host's: the host's death plays (others see it
            // on their consoles) and reaches the kill feed; a bot's does
            // neither. Also 1 s on (third dump).
            if (const uint32_t sp = SessionPlayer(base, slot))
            {
                printf("bots: session player of %s %u (%s) at %08X:", slot == 0 ? "host" : "bot", slot, second[slot] ? "at once" : "300 ms on", sp);
                for (uint32_t o = 0; o < SessionPlayerSize; o += 4) printf(" %08X", Guest::Read32(base, sp + o));
                printf("\n");
            }
            fflush(stdout);
        }
    }
    // bot_bring: each new number puts the bots in a row beside the host's
    // player (his record's origin, and the entity's), for finding them.
    static int brought = 0;
    const int bring = DebugProbe::BotBring();
    if (bring != brought)
    {
        brought = bring;
        const uint32_t entities = Guest::Read32(base, EntityList);
        const uint32_t host = entities ? Guest::Read32(base, entities + 4) : 0;
        const uint32_t hostRecord = host ? Guest::Read32(base, host + 512) : 0;
        int n = 0;
        for (uint32_t slot = 4; slot < MaxClients && hostRecord; slot++)
        {
            if (!g_bot[slot]) continue;
            const uint32_t entity = Guest::Read32(base, entities + 4 * (slot + 1));
            const uint32_t record = entity ? Guest::Read32(base, entity + 512) : 0;
            if (!record) continue;
            n++;
            for (int i = 0; i < 3; i++)
            {
                float v = ReadFloat(base, hostRecord + 4 * i);
                if (i == 0) v += 80.0f * n;
                if (i == 2) v += 8.0f;
                uint32_t bits; memcpy(&bits, &v, 4);
                Guest::Write32(base, record + 4 * i, bits);
                Guest::Write32(base, entity + 336 + 4 * i, bits);
                Guest::Write32(base, record + 16 + 4 * i, 0);
            }
            LogWhere(base, "brought", slot, entity);
        }
    }
    // Where each bot is, every two seconds, for watching it.
    static uint32_t last = 0;
    if (serverTime - last > 1000)
    {
        last = serverTime;
        const uint32_t entities = Guest::Read32(base, EntityList);
        for (uint32_t slot = 0; slot < MaxClients && entities; slot++)
        {
            if (slot != 0 && !g_bot[slot]) continue;
            const uint32_t entity = Guest::Read32(base, entities + 4 * (slot + 1));
            LogWhere(base, slot == 0 ? "host" : "bot", slot, entity);
            // The fire gate (sub_82590C30): his record's +2804 names a client,
            // whose +1880 must be 2 for his weapon to fire; and +240's flags.
            if (const uint32_t r = entity ? Guest::Read32(base, entity + 512) : 0)
            {
                const int32_t index = int32_t(Guest::Read32(base, r + 2804));
                const uint32_t clients = Clients(base);
                printf("bots: %s %u fire gate: client %d, its +1880 %d, +240 %08X, entity +603 %u\n",
                       slot == 0 ? "host" : "bot", slot, index,
                       (index >= 0 && clients) ? int32_t(Guest::Read32(base, clients + index * ClientSize + 1880)) : -1,
                       Guest::Read32(base, r + 240), Guest::Read8(base, entity + 603));
            }
            // His weapon and ammunition, as words: +160..+200 (the weapon in
            // hand and the one waiting) and +1040..+1100 (the ammunition).
            if (const uint32_t r = entity ? Guest::Read32(base, entity + 512) : 0)
            {
                printf("bots: %s %u weapon", slot == 0 ? "host" : "bot", slot);
                for (uint32_t o = 160; o <= 200; o += 4) printf(" %X", Guest::Read32(base, r + o));
                printf(" | ammo");
                for (uint32_t o = 1040; o <= 1100; o += 4) printf(" %08X", Guest::Read32(base, r + o));
                printf("\n");
            }
            // The newest command, to see where the game keeps what.
            const uint32_t command = NewestCommand(base, slot);
            printf("bots: %s %u command", slot == 0 ? "host" : "bot", slot);
            for (uint32_t w = 0; w < CommandSize; w += 4) printf(" %08X", Guest::Read32(base, command + w));
            printf("\n"); fflush(stdout);
        }
    }
}

// Each bot given a session player (a copy of the host's), his side set and
// spawned again the game's own way: debug.txt's bot_session 2, and done by
// itself once the console's bot_add has added its bots.
static std::string BotsCvar(PPCContext& ctx, uint8_t* base, const char* name)
{
    static uint32_t names = 0, used = 0;
    if (names == 0) names = Guest::AllocatePhysical(0x1000);
    const uint32_t size = uint32_t(strlen(name)) + 1;
    if (used + size > 0x1000) used = 0;
    const uint32_t at = names + used;
    memcpy(base + at, name, size);
    used += (size + 3) & ~3u;
    const uint32_t cvar = Call(ctx, base, 0x8254A468, at);
    const uint32_t text = cvar ? Guest::Read32(base, cvar + 4) : 0;
    return text ? std::string(reinterpret_cast<const char*>(base + text)) : std::string("?");
}
// 0.5.149: War, learnt for the bots (no behaviour yet). The War script
// (_mp_war, compiled into the title) gathers the map's "war_flag" entities
// into the level object's (0x82A99494) array at +0x138 (count +0x140,
// element i through sub_8221DB70); each flag's script variables are a table
// (sub_821DE680 on the entity's handle: +0 the count, +8 pairs of hash and
// value). Each 2 s in a War match, every flag's table is logged when it
// changed, so a capture shows which variable is the flag's owner.
static void WatchWar(PPCContext& ctx, uint8_t* base)
{
    static uint32_t last = 0;
    const uint32_t now = GetTickCount();
    if (now - last < 2000) return;
    last = now;
    static std::string mode;
    const std::string gametype = BotsCvar(ctx, base, "mp_gametype");
    if (gametype != mode) { mode = gametype; printf("war: game type now \"%s\"\n", gametype.c_str()); fflush(stdout); }
    if (gametype != "war") { g_war.valid = false; return; }
    const uint32_t level = Guest::Read32(base, 0x82A99494);
    if (level < 0x40000000u || level >= 0xA0000000u) return;
    const uint32_t count = Guest::Read32(base, level + 0x140);
    if (count == 0 || count > 16) return;
    static std::string seen[16];
    static int dumps = 0;
    for (uint32_t i = 0; i < count && dumps < 400; i++)
    {
        const uint32_t at = Call(ctx, base, 0x8221DB70, level + 0x138, i);
        const uint32_t handle = (at >= 0x40000000u && at < 0xA0000000u) ? Guest::Read32(base, at) : 0;
        if (handle == 0) continue;
        const uint32_t table = Call(ctx, base, 0x821DE680, handle);
        if (table < 0x40000000u || table >= 0xA0000000u) continue;
        const uint32_t n = Guest::Read32(base, table), pairs = Guest::Read32(base, table + 8);
        if (n > 64 || pairs < 0x40000000u || pairs >= 0xA0000000u) continue;
        std::string line;
        char text[96];
        for (uint32_t k = 0; k < n; k++)
        {
            const uint32_t hash = Guest::Read32(base, pairs + 8 * k), value = Guest::Read32(base, pairs + 8 * k + 4);
            snprintf(text, sizeof(text), " %08X=%08X", hash, value);
            line += text;
            if (value >= 0x40000000u && value < 0xA0000000u)
            {
                snprintf(text, sizeof(text), "[%08X %08X %08X %08X]", Guest::Read32(base, value), Guest::Read32(base, value + 4),
                         Guest::Read32(base, value + 8), Guest::Read32(base, value + 12));
                line += text;
            }
        }
        // 0.5.151: from 0.5.150's War: F430CD43 is 0 for the one flag in play
        // (the middle at the start, then the next one along after each
        // capture) and -999 for the rest; 53377998 is the holder (-1.0 or
        // +1.0 by side, 0 none). The flag in play is where both sides fight:
        // the bots go there. Its place: its entity's origin (+336, the
        // entity from the handle's entry at 0x82BD1FC0), else its F2F5EAB4
        // piece's.
        {
            int32_t inPlay = -1; uint32_t piece = 0;
            for (uint32_t k = 0; k < n; k++)
            {
                const uint32_t hash = Guest::Read32(base, pairs + 8 * k), value = Guest::Read32(base, pairs + 8 * k + 4);
                if (hash == 0xF430CD43u) inPlay = int32_t(value);
                if (hash == 0xF2F5EAB4u) piece = value;
            }
            if (inPlay == 0)
            {
                float at[3] = {};
                const uint32_t handles[2] = { handle, piece };
                for (uint32_t h = 0; h < 2; h++)
                {
                    const uint32_t entity = handles[h] ? Guest::Read32(base, 0x82BD1FC0 + 8 * (handles[h] & 0x3FFF)) : 0;
                    if (entity < 0x40000000u || entity >= 0xA0000000u) continue;
                    for (int c = 0; c < 3; c++) at[c] = ReadFloat(base, entity + 336 + 4 * c);
                    if (fabsf(at[0]) + fabsf(at[1]) > 1.0f && fabsf(at[0]) < 100000.0f && fabsf(at[1]) < 100000.0f) break;
                    at[0] = at[1] = at[2] = 0.0f;
                }
                if (fabsf(at[0]) + fabsf(at[1]) > 1.0f)
                {
                    if (!g_war.valid || g_war.index != i)
                    {
                        printf("war: the flag in play is %u, at %.1f %.1f %.1f: the bots go there\n", i, at[0], at[1], at[2]);
                        fflush(stdout);
                    }
                    g_war.valid = true; g_war.index = i; g_war.when = now;
                    memcpy(g_war.at, at, sizeof(at));
                }
            }
        }
        if (line == seen[i]) continue;
        seen[i] = line;
        dumps++;
        printf("war: flag %u of %u (handle %08X, level +0xb0 %d):%s\n", i, count, handle,
               int32_t(Guest::Read32(base, level + 0xb0)), line.c_str());
        fflush(stdout);
    }
}

// The host's view after bots are given out: his field of view set again
// (1 s and 3 s on), and the view's variables logged each second for 20 s.
static void WatchHostView(PPCContext& ctx, uint8_t* base)
{
    if (g_viewWatchFrom == 0) return;
    const uint32_t since = GetTickCount() - g_viewWatchFrom;
    static uint32_t lastSecond = 0xFFFFFFFFu;
    const uint32_t second = since / 1000;
    if (second == lastSecond) return;
    lastSecond = second;
    if (second == 1 || second == 3)
        Kernel::QueueConsoleCommand("set cg_fov " + std::to_string(Settings::Get().fov));
    const uint32_t entities = Guest::Read32(base, EntityList);
    const uint32_t host = entities ? Guest::Read32(base, entities + 4) : 0;
    const uint32_t record = host ? Guest::Read32(base, host + 512) : 0;
    printf("bots: (gun view pointers %08X %08X %08X %08X) host view %us after the bots: cg_fov %s, cg_fovscale %s, cg_drawgun %s, in hand %u, +172 %08X, +220 %.1f\n",
           Guest::Read32(base, 0x82C37070), Guest::Read32(base, 0x82C37074), Guest::Read32(base, 0x82C37078), Guest::Read32(base, 0x82C3707C),
           second, BotsCvar(ctx, base, "cg_fov").c_str(), BotsCvar(ctx, base, "cg_fovscale").c_str(),
           BotsCvar(ctx, base, "cg_drawgun").c_str(), record ? Guest::Read32(base, record + 164) : 0,
           record ? Guest::Read32(base, record + 172) : 0, record ? ReadFloat(base, record + 220) : 0.0f);
    fflush(stdout);
    if (second >= 20) { g_viewWatchFrom = 0; lastSecond = 0xFFFFFFFFu; }
}

static void SessionBots(PPCContext& ctx, uint8_t* base)
{
    const uint32_t entities = Guest::Read32(base, EntityList);
    for (uint32_t slot = 4; slot < MaxClients && entities; slot++)
    {
        if (!g_bot[slot] || !GiveSessionPlayer(base, slot)) continue;
        const uint32_t entity = Guest::Read32(base, entities + 4 * (slot + 1));
        if (entity == 0) continue;
        // The session player is a copy of the host's, his side with
        // it. A side is two things the session's switch
        // (sub_823EA4E0) sets together: the team record's +4 and the
        // session player's +604. Setting only the first froze him,
        // half switched; the switch itself refuses him (the side
        // balance test, +3324 of the callbacks). So both, here.
        const uint32_t side = BotSide(base, slot);
        const uint32_t team = Guest::Read32(base, entity + 520);
        if (team != 0) Guest::Write32(base, team + 4, side);
        if (const uint32_t player = SessionPlayer(base, slot)) Guest::Write16(base, player + 604, uint16_t(side));
        printf("bots: slot %u on side %u\n", slot, side); fflush(stdout);
        Respawn(ctx, base, entity, side == 1);
    }
    LogSession(base);
    // 0.5.90: Justin: adding bots leaves his own view zoomed in, and aiming
    // zooms it out while held: his field of view became narrower than his
    // sights'. Each bot's session player is a copy of his, and giving them
    // out looks to reset his view settings. So his own field of view is set
    // again a second and three seconds on, and his view's state logged for
    // twenty seconds.
    g_viewWatchFrom = GetTickCount();
}

// 0.5.127: host-only bots have no network-player visual update. 126 proved
// their sole rig update ran during composition, before component creation.
// Evaluate their native animated body after the server frame, using each
// bot's own rig/pose. Native hidden-state/LOD handling remains in8251ED08.
static void UpdateBotBodies(PPCContext& ctx, uint8_t* base)
{
    const uint32_t entities=Guest::Read32(base,EntityList);
    if (!entities) return;
    for (uint32_t slot=4;slot<MaxClients;++slot)
    {
        if (!g_bot[slot]) continue;
        const uint32_t entity=Guest::Read32(base,entities+4*(slot+1));
        const uint32_t ps=entity?Guest::Read32(base,entity+512):0;
        const uint32_t rig=entity?Guest::Read32(base,entity+492):0;
        if (!ps || Guest::Read32(base,ps+1932)!=2 || !rig ||
            Guest::Read32(base,rig+172)!=entity || !Guest::Read32(base,rig+128) ||
            !Guest::Read32(base,rig+112) || !Guest::Read32(base,rig+32)) continue;
        Call(ctx,base,0x8251ED08,rig,0xFFFFFFFFu);
    }
}

// 0.5.132: a bot's gun. The class kit (sub_82206028: r3 the address of the
// player's script handle, r4 the class) is given to a bot when he first
// spawns (log: "class: kit for handle 00004005 ... class 0", then "holds
// weapon 19"), but his session respawn (bot_session/130) spawns him again
// after it, empty-handed: "holds weapon 0" from then on, so no gun is drawn
// in his hands and he never has ammunition to run out of. A living bot (state
// 3, health above 0) with nothing in hand and nothing waiting (+164, +196) is
// given his kit again, at most once a second and five times a life.
static void ReKitBots(PPCContext& ctx, uint8_t* base)
{
    static uint32_t lastTry[MaxClients] = {};
    static int tries[MaxClients] = {};
    static uint32_t scratch = 0;
    const uint32_t entities = Guest::Read32(base, EntityList);
    if (!entities) return;
    const uint32_t now = GetTickCount();
    for (uint32_t slot = 4; slot < MaxClients; slot++)
    {
        if (!g_bot[slot]) { tries[slot] = 0; continue; }
        const uint32_t entity = Guest::Read32(base, entities + 4 * (slot + 1));
        const uint32_t ps = entity ? Guest::Read32(base, entity + 512) : 0;
        if (!ps) continue;
        const bool alive = Guest::Read32(base, ps + 1924) == 3 && int32_t(Guest::Read32(base, ps + 304)) > 0;
        const bool empty = Guest::Read32(base, ps + 164) == 0 && Guest::Read32(base, ps + 196) == 0;
        if (!alive) { tries[slot] = 0; continue; }
        if (!empty) continue;
        if (tries[slot] >= 5 || now - lastTry[slot] < 1000) continue;
        // His script handle: his entity's entry (index slot + 1, as the
        // kits' log shows 4005 for slot 4) and its generation.
        const uint32_t index = slot + 1;
        const uint32_t entry = 0x82BD1FC0 + index * 8;
        if (Guest::Read32(base, entry) != entity) continue;
        const uint32_t handle = (Guest::Read32(base, entry + 4) << 14) | index;
        if (!scratch) scratch = Guest::AllocatePhysical(0x10);
        Guest::Write32(base, scratch, handle);
        lastTry[slot] = now;
        tries[slot]++;
        printf("bots: slot %u alive with no weapon: kit given again (handle %08X, try %d)\n", slot, handle, tries[slot]);
        fflush(stdout);
        BotClientScope clientScope(base, slot, "re-kit");
        Call(ctx, base, 0x82206028, scratch, 0);
        printf("bots: slot %u after the kit: in hand %u, waiting %u\n", slot, Guest::Read32(base, ps + 164), Guest::Read32(base, ps + 196));
        fflush(stdout);
    }
}

// 0.5.138: the host's own client kept current. Justin, on 0.5.137: after he
// killed the bot (his local place 1) he was frozen in place, his commands
// still coming in but moving nothing. The current client (0x82BCBE9C, also
// the local player the game is working for) is left on the bot's after his
// death, so the host's controller drove nobody. Between server frames, with
// only the host playing on this machine, it is put back to 0.
static void KeepHostCurrent(uint8_t* base, const char* when)
{
    if (g_bots <= 0)
    {
        for (uint32_t k = 0; k < 4; k++) g_localStateBefore[k] = Guest::Read32(base, LocalState(k));
        g_localStateKnown = true;
        return;   // 0.5.145: also for remote bots
    }
    CheckFov(base, when, 0);
    // 0.5.147: remote bots are no local players: local clients 1-3 kept as
    // they were before any bot (none of them in play).
    if (!g_botLocal.load() && g_localStateKnown)
        for (uint32_t k = 1; k < 4; k++)
        {
            const uint32_t now = Guest::Read32(base, LocalState(k));
            if (now == g_localStateBefore[k]) continue;
            static int toldState = 0;
            if ((toldState < 30 && toldState++ < 30)) { printf("bots: local client %u's state %u -> %u %s: put back\n", k, g_localStateBefore[k], now, when); fflush(stdout); }
            Guest::Write32(base, LocalState(k), g_localStateBefore[k]);
        }
    const uint32_t current = Guest::Read32(base, CurrentClient);
    if (current == 0) return;
    static int told = 0;
    if ((told < 30 && told++ < 30)) { printf("bots: current client was %u %s: put back to the host's 0\n", current, when); fflush(stdout); }
    Guest::Write32(base, CurrentClient, 0);
}

static void UavShowsBots(uint8_t* base);   // 0.5.167
PPC_FUNC(sub_82535810)
{
    GunPointersCheck(base, "between server frames (the client's frame, menus, drawing)");
    KeepHostCurrent(base, "before the server frame");
    FeedBots(ctx, base);
    GunPointersCheck(base, "FeedBots");
    WatchHostView(ctx, base);
    WatchWar(ctx, base);   // 0.5.149
    GunPointersCheck(base, "WatchWar");
    __imp__sub_82535810(ctx, base);
    GunPointersCheck(base, "the game's server frame (SV_Frame)");
    LobbyRulesTick(ctx, base);
    Domination::Frame(ctx, base);
    MpFixesFrame(base);
    MpFixesAbilities(ctx, base);
    MpFixesLauncher(ctx, base);
    GunPointersCheck(base, "lobby rules / mp fixes");
    SetUpBots(ctx, base);
    GunPointersCheck(base, "SetUpBots");
    {
        // The console's bot_add: once its bots are in, their sessions.
        static int seen = 0, wait = 0;
        const int asked = g_consoleBots.load();
        if (asked != seen && asked > 0 && g_bots >= asked)
        {
            // (0.5.60 gave them their sessions here, as debug.txt's
            // bot_session 2 does: in a devmap game that crashed the game a
            // little after. Left to debug.txt until the bots get their turn.)
            // 0.5.130: Justin, on 0.5.129 with debug.txt's bot_session 2 in
            // a System Link match: the bot is SEEN for the first time (he
            // fought, took damage, died and came back). The game draws and
            // updates a player (sub_82400810, from sub_82403840's walk over
            // the session's 24 players) only through his session player.
            // So each bot is now given one here, half a second after the
            // last is added, once the session exists (a devmap has none).
            if (++wait > 30)
            {
                seen = asked; wait = 0;
                if (Session(base) != 0 && g_botAutoSession.load()) SessionBots(ctx, base);
            }
        }
        else if (asked == 0) seen = 0;
    }
    GunPointersCheck(base, "session players");
    // 0.5.162: Justin, on 0.5.161: bot_add 20 said 20, but he saw 3. The
    // log: 14 were added (8 tries a request), and only the first 6 got a
    // session player, the thing that makes them drawn, because those are
    // given out only once ALL the bots asked for are in. Now any bot
    // without one is given one once he is half a second old.
    {
        static int every = 0;
        if (++every >= 30 && Session(base) != 0 && g_botAutoSession.load())
        {
            every = 0;
            bool missing = false;
            for (uint32_t slot = 4; slot < MaxClients; slot++)
                if (g_bot[slot] && g_age[slot] > 30 && SessionPlayer(base, slot) == 0) missing = true;
            if (missing) SessionBots(ctx, base);
        }
    }
    ThinkBots(ctx, base);
    GunPointersCheck(base, "ThinkBots");
    ReKitBots(ctx, base);
    GunPointersCheck(base, "ReKitBots");
    UpdateBotBodies(ctx, base);
    GunPointersCheck(base, "UpdateBotBodies");
    UavShowsBots(base);   // 0.5.167 (defined at the end of this file)
    KeepHostCurrent(base, "after the server frame");
    // bot_session: log the session's players; 2 also gives each bot one and
    // spawns him again, this time the game's own way.
    static int sessionAsked = 0;
    const int session = DebugProbe::BotSession();
    if (session != sessionAsked)
    {
        sessionAsked = session;
        LogSession(base);
        // The host's player record and each bot's, word by word where they
        // differ, for finding what a bot is missing.
        {
            const uint32_t entities = Guest::Read32(base, EntityList);
            const uint32_t host = entities ? Guest::Read32(base, entities + 4) : 0;
            const uint32_t hostRecord = host ? Guest::Read32(base, host + 512) : 0;
            for (uint32_t slot = 4; slot < MaxClients && hostRecord; slot++)
            {
                if (!g_bot[slot]) continue;
                const uint32_t entity = Guest::Read32(base, entities + 4 * (slot + 1));
                const uint32_t record = entity ? Guest::Read32(base, entity + 512) : 0;
                if (!record) continue;
                printf("bots: record differences, host %08X, bot %u %08X\n", hostRecord, slot, record);
                for (uint32_t w = 0; w < 2400; w += 4)
                {
                    const uint32_t a = Guest::Read32(base, hostRecord + w), b = Guest::Read32(base, record + w);
                    if (a != b) printf("bots:   +%04u host %08X bot %08X\n", w, a, b);
                }
                printf("bots: entity differences, host %08X, bot %08X\n", host, entity);
                for (uint32_t w = 0; w < 900; w += 4)
                {
                    const uint32_t a = Guest::Read32(base, host + w), b = Guest::Read32(base, entity + w);
                    if (a != b) printf("bots:   +%04u host %08X bot %08X\n", w, a, b);
                }
                fflush(stdout);
            }
        }
        if (session == 2) SessionBots(ctx, base);
    }
    // A slot given back (the bot left with a map change or a restart).
    const uint32_t clients = Clients(base);
    if (clients == 0) return;
    for (uint32_t i = 0; i < MaxClients; i++)
        if (g_bot[i] && Guest::Read32(base, clients + i * ClientSize) == 0) { g_bot[i] = false; g_bots--; }
    static int tries = 0;
    static int consoleAsked = -1;
    const int asked = g_consoleBots.load();
    if (asked != consoleAsked) { consoleAsked = asked; tries = 0; }
    const int wanted = std::max(DebugProbe::BotAdd(), asked);
    if (g_bots < wanted && tries < 64)   // 0.5.162: was 8 (bot_add 20 stopped at 14)
    {
        tries++;
        const bool added = AddBot(ctx, base);
        GunPointersCheck(base, "AddBot");
        if (!added) return;
    }
    if (wanted == 0) tries = 0;
}

PPC_FUNC(sub_825320D0)
{
    const uint32_t clients = Clients(base);
    if (clients != 0 && ctx.r3.u32 >= clients)
    {
        const uint32_t slot = (ctx.r3.u32 - clients) / ClientSize;
        if (slot < MaxClients && g_bot[slot]) return;   // no one to send to
    }
    __imp__sub_825320D0(ctx, base);
}


// The game's spawn of a player (sub_824D0AF0: the entity's handle, the spawn
// point's origin and angles, the side's name). In a networked game it does
// not spawn him itself: it sends the spawn, through the session (the object
// 0x82BCCAC0 points to, sub_823E8840), to the machine whose player he is,
// and that machine puts him there. A bot's is the host, with nothing to hear
// it; so for a bot the host does what the spawn does when there is no
// session (sub_82406BB8): sub_82593608 with the point's origin and angles,
// and 1 for a spawn.
PPC_FUNC(sub_824D0AF0)
{
    const uint32_t handle = ctx.r3.u32;
    const uint32_t index = handle & 0x3FFF;
    const uint32_t entities = Guest::Read32(base, EntityList);
    if (index < 4096 && entities != 0)
    {
        const uint32_t record = 0x82BD1DC0 + 512 + index * 8;
        const uint32_t entity = Guest::Read32(base, record);
        if (entity != 0 && Guest::Read32(base, record + 4) == (handle >> 14))
        {
            for (uint32_t slot = 4; slot < MaxClients; slot++)
            {
                if (!g_bot[slot] || Guest::Read32(base, entities + 4 * (slot + 1)) != entity) continue;
                if (SessionPlayer(base, slot) != 0)
                {
                    // With a session player of his own: the game's own way.
                    printf("bots: slot %u spawns the game's way\n", slot); fflush(stdout);
                    __imp__sub_824D0AF0(ctx, base);
                    LogWhere(base, "spawned", slot, entity);
                    return;
                }
                const uint32_t origin = ctx.r4.u32, angles = ctx.r5.u32;
                printf("bots: slot %u spawns at %.0f %.0f %.0f\n", slot, ReadFloat(base, origin),
                       ReadFloat(base, origin + 4), ReadFloat(base, origin + 8)); fflush(stdout);
                Call(ctx, base, ConnectIntoWorld, entity, origin, angles, 1, 0);
                // 0.5.117: network player setup (823F818C / 82400D40)
                // creates generic_human through the native model factory.
                // Script kit/model-definition setup alone does not create
                // this per-entity drawable object (entity+536).
                if (!Guest::Read32(base, entity + 536))
                {
                    const uint32_t human = Call(ctx, base, 0x824B5688, 0x820653E4);
                    if (human && Guest::Read32(base, human + 4))
                        Call(ctx, base, 0x8257C090, entity, human);
                    printf("bots: slot %u native generic_human %08X, drawable %08X\n",
                           slot, human, Guest::Read32(base, entity + 536));
                    fflush(stdout);
                }
                // 0.5.114: native offline spawn (82406BB8) follows placement
                // with callbacks+3312(handle*, 0). Placement alone clears
                // health and leaves the player inactive. Let the game's
                // spawn finalizer set state, health and loadout for this bot.
                // Respawn's BotClientScope keeps the current client isolated.
                const uint32_t callbacks = Guest::Read32(base, Callbacks);
                const uint32_t finalize = callbacks ? Guest::Read32(base, callbacks + 3312) : 0;
                const uint32_t scratch = (ctx.r1.u32 - 0x200) & ~0xFu;
                Guest::Write32(base, scratch, handle);
                printf("bots: slot %u spawn finalizer %08X\n", slot, finalize);
                fflush(stdout);
                if (finalize) Call(ctx, base, finalize, scratch, 0);
                // 0.5.125: the normal network-player setup also constructs
                // the animation component for the composed body (82400D40).
                // A model/rig alone leaves no component at rig+32. Use the
                // native constructor, which validates the model resource,
                // allocates private per-entity state and schedules its task.
                // bot_setup bit4 skips this step for an old/new comparison.
                const uint32_t rig=Guest::Read32(base,entity+492);
                if (!(g_botSetup.load() & 4) && rig && !Guest::Read32(base,rig+32))
                {
                    Call(ctx,base,0x8243F138,rig,0);
                    printf("bots: slot %u native animation rig %08X component %08X flags %08X\n",
                           slot,rig,Guest::Read32(base,rig+32),Guest::Read32(base,entity+624));
                    fflush(stdout);
                }
                LogWhere(base, "spawned", slot, entity);
                return;
            }
        }
    }
    __imp__sub_824D0AF0(ctx, base);
}

// ClientThink (sub_82592CD0) moves a player (Pmove, sub_824B4270) only when
// sub_82483190 says yes for his entity (its place, +876, in a part of the
// level marked; a player's machine has it so), or when his move type is 1 or
// 2. A bot's never was, so he could turn but never walk: for a bot, yes.
PPC_FUNC(sub_82483190)
{
    const uint32_t entity = ctx.r3.u32;
    __imp__sub_82483190(ctx, base);
    if (ctx.r3.u32 != 0) return;
    const uint32_t entities = Guest::Read32(base, EntityList);
    for (uint32_t slot = 4; slot < MaxClients && entities; slot++)
        if (g_bot[slot] && Guest::Read32(base, entities + 4 * (slot + 1)) == entity) { ctx.r3.u64 = 1; return; }
}

// Pmove (sub_824B4270; pm+0 the player's record, pm+4 his command, so its
// forward move at pm+28). Watched for a bot's record: whether it runs, his
// flags (+44; 0x4000 freezes him: his moves are cleared), +2048 (which sets
// that flag), and his forward move.
PPC_FUNC(sub_824B4270)
{
    const uint32_t pm = ctx.r3.u32;
    const uint32_t record = Guest::Read32(base, pm);
    bool bot = false;
    const uint32_t entities = Guest::Read32(base, EntityList);
    for (uint32_t slot = 4; slot < MaxClients && entities; slot++)
    {
        const uint32_t entity = g_bot[slot] ? Guest::Read32(base, entities + 4 * (slot + 1)) : 0;
        if (entity && Guest::Read32(base, entity + 512) == record) bot = true;
    }
    static uint32_t count = 0;
    const bool say = bot && count++ % 60 == 0;
    if (say)
    {
        printf("bots: Pmove for a bot (%u): flags %08X, type %d, +2048 %d, forward %d right %d, buttons %X, time %d, last %d, ground %d, +36..+60:",
               count, Guest::Read32(base, record + 44), int32_t(Guest::Read32(base, record + 36)),
               int32_t(Guest::Read32(base, record + 2048)), int8_t(Guest::Read8(base, pm + 28)),
               int8_t(Guest::Read8(base, pm + 29)), Guest::Read32(base, pm + 8), int32_t(Guest::Read32(base, pm + 4)),
               int32_t(Guest::Read32(base, record + 32)), int32_t(Guest::Read32(base, record + 136)));
        for (uint32_t o = 36; o <= 60; o += 4) printf(" %08X", Guest::Read32(base, record + o));
        printf("\n");
        fflush(stdout);
    }
    __imp__sub_824B4270(ctx, base);
    if (say)
    {
        printf("bots: after Pmove at %.1f %.1f, velocity %.1f %.1f\n", ReadFloat(base, record),
               ReadFloat(base, record + 4), ReadFloat(base, record + 16), ReadFloat(base, record + 20));
        fflush(stdout);
    }
}

// A player's muzzle point (sub_8256B8E0: his entity, the point out): the
// entity's origin raised by his record's eye height (+220). For a bot the
// eye height may never have been set, which put his shots at his feet; so
// a bot's point is kept at least 50 above his origin. Logged now and then.
PPC_FUNC(sub_8256B8E0)
{
    const uint32_t entity = ctx.r3.u32, out = ctx.r4.u32;
    __imp__sub_8256B8E0(ctx, base);
    const uint32_t entities = Guest::Read32(base, EntityList);
    for (uint32_t slot = 4; slot < MaxClients && entities; slot++)
    {
        if (!g_bot[slot] || Guest::Read32(base, entities + 4 * (slot + 1)) != entity) continue;
        const float originZ = ReadFloat(base, entity + 344), muzzleZ = ReadFloat(base, out + 8);
        const uint32_t record = Guest::Read32(base, entity + 512);
        static uint32_t said = 0;
        if (said++ % 40 == 0)
        {
            printf("bots: slot %u muzzle %.1f above his origin (eye height %.1f)\n", slot, muzzleZ - originZ,
                   record ? ReadFloat(base, record + 220) : -1.0f);
            fflush(stdout);
        }
        if (muzzleZ - originZ < 50.0f)
        {
            const float raised = originZ + 60.0f;
            uint32_t bits; memcpy(&bits, &raised, 4);
            Guest::Write32(base, out + 8, bits);
        }
        break;
    }
}

// The move's scale for a command (sub_82487718: the larger of its forward,
// side and up moves, against the player's speed). Only the bot's commands
// have a forward of exactly 124 (bot_sticks' 32000), so those are counted,
// for telling whether his walking move is reached at all.
// Aiming down the sights (the record's +172, 0 to 1): with Faster aiming
// walk a move while the sights are up goes at the Aiming walk speed setting
// (per cent of the hip walk; the game's own is 40).
PPC_FUNC(sub_82487718)
{
    const bool bots = Guest::Read8(base, ctx.r3.u32 + 24) == 124;
    __imp__sub_82487718(ctx, base);
    if (Settings::Get().fasterAds)
    {
        const uint32_t move = Guest::Read32(base, 0x82B922C8);
        const uint32_t record = move ? Guest::Read32(base, move) : 0;
        if (record)
        {
            auto fl = [&](uint32_t at) { const uint32_t b = Guest::Read32(base, record + at); float f; memcpy(&f, &b, 4); return f; };
            // The sights' fraction is +172 (0 down, 1 fully up: seen with the
            // aiming probe, mp_fixes.cpp).
            const float sights = fl(172);
            if (sights > 0.01f && sights <= 1.0f)
            {
                const double before = ctx.f1.f64;
                // +1124 is the game's own aimed walk (0.4 of the hip walk);
                // the setting is the share wanted instead.
                float game = fl(1124);
                if (!(game > 0.05f && game < 1.0f)) game = 0.4f;
                const double wanted = Settings::Get().aimWalk / 100.0;
                const double factor = wanted > game ? wanted / game : 1.0;
                const double quicker = before * (1.0 + (factor - 1.0) * sights);
                ctx.f1.f64 = quicker;
                static int said = 0;
                if ((said < 6 && said++ < 6)) { printf("moves: aiming %.2f, scale %.3f -> %.3f (+1124 %.3f, +1128 %.3f)\n", sights, before, quicker, fl(1124), fl(1128)); fflush(stdout); }
            }
        }
    }
    if (!bots) return;
    static uint32_t count = 0;
    if (count++ % 120 == 0)
    {
        printf("bots: a bot's move scale %.3f (%u times)\n", ctx.f1.f64, count); fflush(stdout);
    }
}

#endif

#ifdef COD3_MP
// 0.5.113 camera boundary diagnostics: observe native results, never force
// the host FOV or ADS. Generated 82453810/824674E0 verify these exact fields.
extern "C" PPC_FUNC(__imp__sub_82453810);
PPC_FUNC(sub_82453810)
{
    const uint32_t client=Guest::Read32(base,0x82BCBE9C);
    __imp__sub_82453810(ctx,base);
    static uint32_t logged[MaxClients]={};
    const uint32_t now=GetTickCount();
    if (client>=MaxClients || now-logged[client]<2000) return;
    logged[client]=now;
    const uint32_t entities=Guest::Read32(base,EntityList);
    const uint32_t entity=entities?Guest::Read32(base,entities+4*(client+1)):0;
    const uint32_t record=entity?Guest::Read32(base,entity+512):0;
    printf("bots: native FOV client %u => %.2f (base %.2f), ADS %.3f, flags %08X, bots %d\n",client,ctx.f1.f64,ReadFloat(base,BaseFov),
           record?ReadFloat(base,record+172):-1.0f,record?Guest::Read32(base,record+240):0,g_bots);
    fflush(stdout);
}
extern "C" PPC_FUNC(__imp__sub_824674E0);
PPC_FUNC(sub_824674E0)
{
    const uint32_t camera=ctx.r3.u32;
    __imp__sub_824674E0(ctx,base);
    static uint32_t logged=0;
    const uint32_t now=GetTickCount();
    if (!camera || Guest::Read32(base,camera+480)!=0 || now-logged<2000) return;
    logged=now;
    printf("bots: local camera %08X auto %.3f, FOV %.2f, tween %.2f %.2f %.2f, bots %d\n",
           camera,ReadFloat(base,camera+48),ReadFloat(base,camera+96),ReadFloat(base,camera+272),
           ReadFloat(base,camera+276),ReadFloat(base,camera+280),g_bots);
    fflush(stdout);
}
#endif

#ifdef COD3_MP
// 0.5.116: model selection asks whether the player has a local session
// entry. Host bots intentionally have none. Admit only its exact call site;
// other ownership checks (local input, views and networking) stay native.
extern "C" PPC_FUNC(__imp__sub_824C3490);
PPC_FUNC(sub_824C3490)
{
    if (ctx.lr == 0x821FB408)
    {
        const uint32_t handle = ctx.r3.u32;
        const uint32_t index = handle & 0x3FFF;
        if (index < 4096)
        {
            const uint32_t entry = 0x82BD1FC0 + index * 8;
            const uint32_t entity = Guest::Read32(base, entry);
            if (entity && Guest::Read32(base, entry + 4) == (handle >> 14))
            {
                const uint32_t slot = BotSlot(base, entity);
                if (slot < MaxClients)
                {
                    printf("bots: slot %u admitted to native model selection (no local session)\n", slot);
                    fflush(stdout);
                    ctx.r3.u64 = 1;
                    return;
                }
            }
        }
    }
    __imp__sub_824C3490(ctx, base);
}
#endif
#ifdef COD3_MP
// 0.5.126: native-path diagnostics for two user-reproduced failures. These
// wrappers observe only; each delegates exactly once with the original context.
extern "C" PPC_FUNC(__imp__sub_8251ED08);
PPC_FUNC(sub_8251ED08)
{
    const uint32_t rig=ctx.r3.u32;
    const uint32_t entity=rig?Guest::Read32(base,rig+172):0;
    const uint32_t slot=entity?BotSlot(base,entity):MaxClients;
    static uint32_t calls[MaxClients]={}, logged[MaxClients]={};
    if (slot<MaxClients)
    {
        ++calls[slot];
        const uint32_t now=GetTickCount();
        if (calls[slot]==1 || now-logged[slot]>=2000)
        {
            logged[slot]=now;
            const uint32_t extra=Guest::Read32(base,entity+524);
            printf("bots: model update slot %u calls %u caller %08X mode %d rig %08X pose %08X component %08X entity620 %08X entity864 %08X extra1304 %08X\n",
                   slot,calls[slot],uint32_t(ctx.lr),int32_t(ctx.r4.u32),rig,
                   Guest::Read32(base,rig+112),Guest::Read32(base,rig+32),
                   Guest::Read32(base,entity+620),Guest::Read32(base,entity+864),
                   extra?Guest::Read32(base,extra+1304):0);
            fflush(stdout);
        }
    }
    __imp__sub_8251ED08(ctx,base);
}
extern "C" PPC_FUNC(__imp__sub_8251E5F0);
PPC_FUNC(sub_8251E5F0)
{
    const uint32_t rig=ctx.r3.u32;
    const uint32_t entity=rig?Guest::Read32(base,rig+172):0;
    const uint32_t slot=entity?BotSlot(base,entity):MaxClients;
    static uint32_t calls[MaxClients]={}, logged[MaxClients]={};
    if (slot<MaxClients)
    {
        ++calls[slot];
        const uint32_t now=GetTickCount();
        if (calls[slot]==1 || now-logged[slot]>=2000)
        {
            logged[slot]=now;
            printf("bots: animated model update slot %u calls %u model %u mode %d pose %08X\n",
                   slot,calls[slot],ctx.r4.u32,int32_t(ctx.r5.u32),Guest::Read32(base,rig+112));
            fflush(stdout);
        }
    }
    __imp__sub_8251E5F0(ctx,base);
}
extern "C" PPC_FUNC(__imp__sub_82457A60);
PPC_FUNC(sub_82457A60)
{
    const uint32_t client=Guest::Read32(base,CurrentClient);
    const uint32_t entities=Guest::Read32(base,EntityList);
    const uint32_t entity=(entities && client<4)?Guest::Read32(base,entities+4*(client+1)):0;
    const uint32_t ps=entity?Guest::Read32(base,entity+512):0;
    static uint32_t count=0;
    const bool say=ps && count++<128;
    if (say)
    {
        const uint32_t movement=0x82C80000u-11120u+client*6368u;
        printf("weapons: native cycle client %u caller %08X forward %u requireAmmo %u selected %u held %u last %u slots %u %u flags44 %08X flags240 %08X movement32 %08X\n",
               client,uint32_t(ctx.lr),ctx.r3.u32,ctx.r4.u32,
               Guest::Read32(base,0x82B9F310u+client*4),Guest::Read32(base,ps+164),Guest::Read32(base,ps+196),
               Guest::Read8(base,ps+1065),Guest::Read8(base,ps+1066),Guest::Read32(base,ps+44),
               Guest::Read32(base,ps+240),Guest::Read32(base,movement+32));
        fflush(stdout);
    }
    __imp__sub_82457A60(ctx,base);
    if (say)
    {
        printf("weapons: native cycle result client %u selected %u held %u\n",client,
               Guest::Read32(base,0x82B9F310u+client*4),Guest::Read32(base,ps+164));
        fflush(stdout);
    }
}
extern "C" PPC_FUNC(__imp__sub_82485D28);
PPC_FUNC(sub_82485D28)
{
    const uint32_t target=ctx.r3.u32, client=ctx.r4.u32, caller=uint32_t(ctx.lr);
    static uint32_t count=0;
    const bool say=client<4 && count++<128;
    const uint32_t before=say?Guest::Read32(base,0x82B9F310u+client*4):0;
    __imp__sub_82485D28(ctx,base);
    if (say)
    {
        printf("weapons: native select client %u caller %08X target %u selected %u -> %u result %u\n",
               client,caller,target,before,Guest::Read32(base,0x82B9F310u+client*4),ctx.r3.u32);
        fflush(stdout);
    }
}

// A session player's death (sub_82404DD0, the session object in r3). At its
// end, for a player of this machine (sub_823D9B50: his machine object is
// local), it clears the byte at [[0x82BCCAC0]]+8. The bot shares the host's
// machine object, so his death cleared it too, and the host froze, unable even
// to pause, until he himself died (0.5.137-0.5.139). While the host is alive,
// a change of that byte by a bot's death is undone (0.5.140).
extern "C" PPC_FUNC(__imp__sub_82404DD0);
PPC_FUNC(sub_82404DD0)
{
    auto flagAt = [&]() -> uint32_t {
        const uint32_t session = Guest::Read32(base, 0x82BCCAC0u);
        if (session < 0x40000000u || session >= 0x90000000u) return 0;
        const uint32_t first = Guest::Read32(base, session);
        if (first < 0x40000000u || first >= 0x90000000u) return 0;
        return first + 8;
    };
    const uint32_t at = flagAt();
    const uint8_t before = at ? Guest::Read8(base, at) : 0;
    __imp__sub_82404DD0(ctx, base);
    if (!at || g_bots <= 0) return;
    const uint8_t after = Guest::Read8(base, at);
    const uint32_t entities = Guest::Read32(base, EntityList);
    const uint32_t host = entities ? Guest::Read32(base, entities + 4) : 0;
    const uint32_t hostRecord = host ? Guest::Read32(base, host + 512) : 0;
    const int32_t health = hostRecord ? int32_t(Guest::Read32(base, hostRecord + 304)) : -1;
    const int32_t state = hostRecord ? int32_t(Guest::Read32(base, hostRecord + 1924)) : -1;
    static int told = 0;
    if ((told < 40 && told++ < 40))
    {
        printf("bots: a session death: local flag %u -> %u, the host's health %d, state %d\n", before, after, health, state);
        fflush(stdout);
    }
    if (after != before && health > 0 && state == 3)
    {
        Guest::Write8(base, at, before);
        static int fixed = 0;
        if ((fixed < 40 && fixed++ < 40)) { printf("bots: a bot's death cleared the host's local flag: put back to %u\n", before); fflush(stdout); }
    }
}


// 0.5.141: Justin, on 0.5.140: still frozen when the bot dies. The log shows
// that in the frame of the bot's death the session's frame (sub_82404580,
// from sub_82406C70) calls through a broken object (its table read 0x4035),
// then other threads do the same: something of this machine's local players
// is wrecked by the death of a bot holding a local place. So a bot who has
// been alive leaves his local place as soon as he is dead, before the session
// layer's update (sub_82406C70) sees him; his respawn (Respawn) gives it back.
static void ReleaseDeadPlaces(uint8_t* base)
{
    static bool wasAlive[MaxClients] = {};
    const uint32_t entities = Guest::Read32(base, EntityList);
    if (!entities) return;
    for (uint32_t slot = 1; slot < MaxClients; slot++)
    {
        if (!g_bot[slot] || g_placeAt[slot] == 0) continue;
        const uint32_t entity = Guest::Read32(base, entities + 4 * (slot + 1));
        const uint32_t r = entity ? Guest::Read32(base, entity + 512) : 0;
        if (!r) continue;
        const bool alive = int32_t(Guest::Read32(base, r + 304)) > 0 && Guest::Read32(base, r + 1924) == 3;
        if (alive)
        {
            wasAlive[slot] = true;
            // 0.5.142: alive again with his place still empty (it was
            // emptied once more right after a respawn): given back.
            if (Guest::Read8(base, g_placeAt[slot]) == 0xFF)
            {
                Guest::Write8(base, g_placeAt[slot], g_placeNumber[slot]);
                static int back = 0;
                if ((back < 40 && back++ < 40)) { printf("bots: slot %u is alive: back in his local place\n", slot); fflush(stdout); }
            }
            continue;
        }
        if (!wasAlive[slot]) continue;   // not yet spawned once: keep it (the zoom, 0.5.137)
        if (Guest::Read8(base, g_placeAt[slot]) != g_placeNumber[slot]) continue;
        Guest::Write8(base, g_placeAt[slot], 0xFF);
        static int told = 0;
        if ((told < 40 && told++ < 40)) { printf("bots: slot %u is dead: leaves his local place\n", slot); fflush(stdout); }
    }
}

extern "C" PPC_FUNC(__imp__sub_82406C70);
PPC_FUNC(sub_82406C70)
{
    if (g_bots > 0 && !g_inSpawn) ReleaseDeadPlaces(base);
    __imp__sub_82406C70(ctx, base);
}


// 0.5.143: SV_GetUsercmd(client, out). ClientThink passes the current client
// as the server client; for a bot's think that is now his local index, so
// his slot is given instead (his commands are there).
extern "C" PPC_FUNC(__imp__sub_82530270);
PPC_FUNC(sub_82530270)
{
    if (g_thinkSlot < MaxClients && g_placeIndex[g_thinkSlot] != 0 && ctx.r3.u32 == g_placeIndex[g_thinkSlot])
        ctx.r3.u32 = g_thinkSlot;
    // 0.5.159: a bot's command from his own ring (see CommandPlayer).
    const uint32_t client = ctx.r3.u32;
    if (client >= 4 && client < MaxClients && g_bot[client] && g_commandBlock[client] != 0)
    {
        const uint32_t command = NewestCommand(base, client);
        const uint32_t out = ctx.r4.u32;
        if (out) memcpy(base + out, base + command, CommandSize);
        return;
    }
    __imp__sub_82530270(ctx, base);
}


// 0.5.145: bots as remote players (each his own machine object, not on this
// machine), so there is no limit of 3 and none of this machine's local-player
// code takes them. A remote player is drawn from what his own machine sends
// (sub_82400810: for a player not on this machine, sub_823D9B50 false, it
// reads the network's copy, sub_823EC930); a bot's machine is the host and
// sends nothing, so he was invisible (0.5.135). While sub_82400810 updates a
// bot's session player, "is he on this machine" answers yes for him alone,
// so his body is worked out from his entity, as the host's own is.
namespace {
    uint32_t g_drawingBot = 0;
    int g_botsLocal = 0;
    // Nonzero only inside the supplemental session tick. Its native tail
    // walks all 24 players; only this bot may run under its private gun view.
    uint32_t g_supplementalBotPlayer = 0;
}
static bool IsBotSessionPlayer(uint8_t* base, uint32_t player)
{
    if (player < 0x40000000u || player >= 0xA0000000u) return false;
    const int32_t slot = int32_t(Guest::Read32(base, player + 8));
    return slot >= 4 && slot < int32_t(MaxClients) && g_bot[slot];
}
extern "C" PPC_FUNC(__imp__sub_82400810);
PPC_FUNC(sub_82400810)
{
    const uint32_t player = ctx.r3.u32;
    if (g_supplementalBotPlayer && player != g_supplementalBotPlayer)
    {
        static int skipped = 0;
        if ((skipped < 5 && skipped++ < 5)) { printf("bots: isolated session tick skips player %08X (active bot %08X)\n", player, g_supplementalBotPlayer); fflush(stdout); }
        return; // native caller ignores the return value
    }
    const bool bot = g_botRemote.load() && g_bots > 0 && IsBotSessionPlayer(base, player);
    const uint32_t was = g_drawingBot;
    if (bot)
    {
        // The normal session tail also reaches bots while CurrentClient=0.
        // Give this bot the same private context as his supplemental tick.
        const uint32_t slot = Guest::Read32(base, player + 8);
        if (!BotGunViewBlock(base, slot)) return;
        KeepFov keep(base);
        BotGunViewScope gunView(base, slot, !g_botLocal.load());
        const uint32_t thinking = g_thinkSlot;
        g_thinkSlot = slot;
        g_drawingBot = player;
        __imp__sub_82400810(ctx, base);
        g_thinkSlot = thinking;
    }
    else __imp__sub_82400810(ctx, base);
    g_drawingBot = was;
    static int told = 0;
    if (bot && (told < 10 && told++ < 10)) { printf("bots: session player %08X (slot %d) drawn as this machine's\n", player, int32_t(Guest::Read32(base, player + 8))); fflush(stdout); }
}
extern "C" PPC_FUNC(__imp__sub_823D9B50);
PPC_FUNC(sub_823D9B50)
{
    if (g_drawingBot != 0 && ctx.r3.u32 == g_drawingBot) { ctx.r3.u64 = 1; return; }
    // 0.5.160: inside a call that plays a player's death (sub_823E9998),
    // every bot counts as this machine's.
    if (g_botsLocal > 0 && IsBotSessionPlayer(base, ctx.r3.u32)) { ctx.r3.u64 = 1; return; }
    // 0.5.161: everywhere. Justin, on 0.5.160 (3 bots, all stable): no
    // death animation, no kill feed for their deaths, no gun in their hands.
    // All three are what the game takes from a remote player's own console
    // (sub_823D9B50 false: "his machine sends it"); a bot's never does. The
    // trouble that made the bots remote (0.5.135-0.5.144: zoom, the host's
    // guns going full auto, freezes) came from their commands being written
    // over the host's input and gun view (fixed in 0.5.159), so the bots now
    // count as this machine's players in every one of these checks, while
    // keeping their own machine object, no local place and their own
    // command rings and gun view memory.
    if (g_botRemote.load() && g_bots > 0 && IsBotSessionPlayer(base, ctx.r3.u32))
    {
        static int told = 0;
        if ((told < 5 && told++ < 5)) { printf("bots: session player %08X (a bot) counted as this machine's\n", ctx.r3.u32); fflush(stdout); }
        ctx.r3.u64 = 1;
        return;
    }
    __imp__sub_823D9B50(ctx, base);
}


// 0.5.147: a local client's frame (sub_82593D50: his view and weapon object,
// 0x82C37070[current client]) run by the server frame for a local client
// other than the host's while the bots are remote players: there is no such
// player, only the bots' shared index (3), and running it wrecked the host's
// view (field of view 0), gun and controls. Skipped.
extern "C" PPC_FUNC(__imp__sub_82593D50);
PPC_FUNC(sub_82593D50)
{
    const uint32_t current = Guest::Read32(base, CurrentClient);
    if (current != 0 && current < 4 && g_bots > 0 && !g_botLocal.load() && g_thinkSlot >= MaxClients)
    {
        static int told = 0;
        if ((told < 20 && told++ < 20)) { printf("bots: local client %u's frame skipped (no such player; bots are remote)\n", current); fflush(stdout); }
        return;
    }
    __imp__sub_82593D50(ctx, base);
}


// 0.5.148: 0.5.147's log: the field of view still went to 0 every frame,
// now from the host's own local-client frame (no phantom one ran). The
// weapon-view object (sub_82519178's r3, 0x82C37070[current client]) keeps a
// saved field of view at +0x74 that it puts back into the global one; once a
// bot's code had left the global at 0 while it was saved, the saved value
// itself is 0 and comes back every frame. Healed: if this call leaves the
// global below 1 while it was sane before, both the global and the
// object's +0x74 get the sane value back.
extern "C" PPC_FUNC(__imp__sub_82519178);
PPC_FUNC(sub_82519178)
{
    const uint32_t object = ctx.r3.u32;
    const float before = ReadFloat(base, BaseFov);
    __imp__sub_82519178(ctx, base);
    if (g_bots <= 0) return;
    const float after = ReadFloat(base, BaseFov);
    const float good = (before >= 10.0f && before <= 170.0f) ? before : g_goodFov;
    if (after >= 1.0f || good < 10.0f) return;
    uint32_t bits; memcpy(&bits, &good, 4);
    Guest::Write32(base, BaseFov, bits);
    // 0.5.150: the object was 0 in 0.5.148's log: its +0x74 is guest
    // address 0x74, which the bots' own runs of this code share.
    const bool objectOk = object == 0 || (object >= 0x40000000u && object < 0xA0000000u);
    const float saved = objectOk ? ReadFloat(base, object + 0x74) : -1.0f;
    if (objectOk && !(saved >= 10.0f && saved <= 170.0f)) Guest::Write32(base, object + 0x74, bits);
    static int told = 0;
    if ((told < 20 && told++ < 20))
    {
        printf("bots: weapon view %08X (current client %u) put the field of view to %.2f (its saved %.2f): healed to %.2f\n",
               object, Guest::Read32(base, CurrentClient), after, saved, good);
        fflush(stdout);
    }
}


// 0.5.150: the first-person weapon view of a local client (sub_8251A950, its
// object 0x82C37070[current client]) run while the current client is a
// bot's (index 3 or his slot): the bots have no first-person view, and the
// object for them is 0, the same as the host's at times, so their runs wrote
// over the host's saved field of view (guest 0x74): his gun flickered in and
// out. Skipped for bots.
extern "C" PPC_FUNC(__imp__sub_8251A950);
PPC_FUNC(sub_8251A950)
{
    const uint32_t current = Guest::Read32(base, CurrentClient);
    if (current != 0 && g_bots > 0 && !g_botLocal.load())
    {
        static int told = 0;
        if ((told < 10 && told++ < 10)) { printf("bots: first-person weapon view for current client %u skipped (a bot's)\n", current); fflush(stdout); }
        return;
    }
    __imp__sub_8251A950(ctx, base);
}


// 0.5.152: 0.5.151's log: with two bots the host's gun went (in hand 0) and
// the game then crashed in the gun view (sub_8251A848) under a player's
// spawn/think done by the game itself (sub_82593608, the entity in r3), not
// by our steps. The host's gun view lives at guest 0x0 (its object is 0),
// and so does a bot's. So whatever the game itself runs for a bot's entity
// (his spawn sub_82593608, his think sub_82593518 by handle) keeps guest
// 0x0-0x7FF and the field of view as they were (KeepFov).
static bool IsBotEntity(uint8_t* base, uint32_t entity)
{
    return g_bots > 0 && entity >= 0x40000000u && entity < 0xA0000000u && BotSlot(base, entity) < MaxClients;
}
// 0.5.175: 8246A7D8 receives entity events after the session/think scopes.
// Events165/166/167 ONLY set the current local client's input stance latch
// to stand/crouch/prone (native cases8246AEB4/AEF4/AF34). Bots have no local
// controller; their body stance comes from their own player-state movement.
// Applying their echoed stance events to CurrentClient0 stands the host up.
extern "C" PPC_FUNC(__imp__sub_8246A7D8);
PPC_FUNC(sub_8246A7D8)
{
    const uint32_t event = ctx.r4.u32;
    if (event >= 165 && event <= 167 && IsBotEntity(base, ctx.r3.u32))
    {
        static int told = 0;
        if ((told < 60 && told++ < 60)) { printf("bots: stance event %u from slot %u excluded from local input (client %u, caller %08X)\n", event, BotSlot(base, ctx.r3.u32), Guest::Read32(base, CurrentClient), uint32_t(ctx.lr)); fflush(stdout); }
        return;
    }
    __imp__sub_8246A7D8(ctx, base);
}

extern "C" PPC_FUNC(__imp__sub_82593608);
PPC_FUNC(sub_82593608)
{
    if (!IsBotEntity(base, ctx.r3.u32)) { __imp__sub_82593608(ctx, base); return; }
    const uint32_t slot = BotSlot(base, ctx.r3.u32);
    KeepFov keep(base);
    BotGunViewScope gunView(base, slot, !g_botLocal.load());   // 0.5.153
    const uint32_t thinking = g_thinkSlot;
    if (thinking >= MaxClients) g_thinkSlot = slot;
    __imp__sub_82593608(ctx, base);
    g_thinkSlot = thinking;
}
extern "C" PPC_FUNC(__imp__sub_82593518);
PPC_FUNC(sub_82593518)
{
    const uint32_t handle = ctx.r3.u32;
    const uint32_t entity = handle ? Guest::Read32(base, 0x82BD1FC0 + 8 * (handle & 0x3FFF)) : 0;
    if (!IsBotEntity(base, entity)) { __imp__sub_82593518(ctx, base); return; }
    const uint32_t slot = BotSlot(base, entity);
    KeepFov keep(base);
    BotGunViewScope gunView(base, slot, !g_botLocal.load());   // 0.5.153
    const uint32_t thinking = g_thinkSlot;
    if (thinking >= MaxClients) g_thinkSlot = slot;
    __imp__sub_82593518(ctx, base);
    g_thinkSlot = thinking;
}


// 0.5.157: THE CAUSE of the host's broken gun, freeze and crashes with two
// bots in War. War's script (_mp_war, e.g. sub_82214EB8, sub_82212190) calls
// two of the engine's callbacks for every player, by his client number:
// table 0x82BCBF64 +0x400 (sub_824C9810) and +0x404 (sub_824B52A0). Both end
// in sub_8244A1D0, which SETS the current client (0x82BCBE9C) to that number
// and writes that client's view kick into the per-local-client view state
// (0x82C7D490 + n * 0x18E0, 4 entries). Those are for this machine's local
// players (0-3); for a bot (client 4, 5...) the current client was left at
// his number, and the client side then indexed its 4-entry lists past their
// end: the per-client array at 0x82C35030 (0x804 each) runs straight into the
// gun view pointers at 0x82C37070 (seen zeroed then full of floats in
// 0.5.156's log), the host's weapon was reselected to 0, and the gun view
// code crashed. A non-local client's view kick is now skipped, and the
// current client left alone.
static bool NotLocalClient(uint32_t client) { return client != 0xFFFFFFFFu && client > 3; }
static void TellSkippedKick(const char* where, uint32_t client)
{
    static int told = 0;
    if ((told < 20 && told++ < 20)) { printf("bots: %s for client %u (not a local player) skipped: the current client stays %u\n", where, client, 0u); fflush(stdout); }
}
extern "C" PPC_FUNC(__imp__sub_8244A1D0);
PPC_FUNC(sub_8244A1D0)
{
    if (NotLocalClient(ctx.r9.u32)) { TellSkippedKick("a view kick (sub_8244A1D0)", ctx.r9.u32); return; }
    __imp__sub_8244A1D0(ctx, base);
}
extern "C" PPC_FUNC(__imp__sub_824C9810);
PPC_FUNC(sub_824C9810)
{
    if (NotLocalClient(ctx.r4.u32)) { TellSkippedKick("the script's callback +0x400 (sub_824C9810)", ctx.r4.u32); return; }
    __imp__sub_824C9810(ctx, base);
}
extern "C" PPC_FUNC(__imp__sub_824B52A0);
PPC_FUNC(sub_824B52A0)
{
    if (NotLocalClient(ctx.r4.u32)) { TellSkippedKick("the script's callback +0x404 (sub_824B52A0)", ctx.r4.u32); return; }
    __imp__sub_824B52A0(ctx, base);
}


// 0.5.160: the bots' death animation, gone since they became remote players
// (0.5.145). A player's death from the server's side (sub_8258C778) goes to
// the session layer through sub_823E9998 with the players' handles: for a
// player of this machine it plays it here (0x823E9C68 on), for a remote one
// it makes a message for his machine to play it, and a bot's machine is the
// host, which never receives it. During that call the bots count as this
// machine's, so their deaths are played here as they were before 0.5.145.
extern "C" PPC_FUNC(__imp__sub_823E9998);
PPC_FUNC(sub_823E9998)
{
    if (g_bots <= 0 || !g_botRemote.load()) { __imp__sub_823E9998(ctx, base); return; }
    g_botsLocal++;
    __imp__sub_823E9998(ctx, base);
    g_botsLocal--;
    static int told = 0;
    if ((told < 20 && told++ < 20)) { printf("bots: a death (sub_823E9998) played with the bots as this machine's\n"); fflush(stdout); }
}


// 0.5.162: Justin, on 0.5.161: the bots' guns show again, but still no death
// animation and nothing in the kill feed. Both worked in 0.5.137-0.5.144,
// when a bot held one of the session's local places (session +0x5991, the
// numbers of this machine's 4 players). The game asks those through
// sub_823D8FF0 (is this number one of ours) and sub_823D8FB0 (which local
// index is this entry). A bot's number now answers "ours", index 3 (the
// bots' shared local index, 0.5.145), without holding a place.
static bool IsBotSessionNumber(uint8_t* base, uint32_t object, uint32_t number)
{
    if (number >= 24 || object < 0x40000000u || object >= 0xA0000000u) return false;
    return IsBotSessionPlayer(base, object + 0x1010 + number * 0x310);
}
extern "C" PPC_FUNC(__imp__sub_823D8FF0);
PPC_FUNC(sub_823D8FF0)
{
    if (g_botRemote.load() && g_bots > 0 && IsBotSessionNumber(base, ctx.r3.u32, ctx.r4.u32 & 0xFF))
    {
        static int told = 0;
        if ((told < 5 && told++ < 5)) { printf("bots: session number %u (a bot) answered as one of this machine's places\n", ctx.r4.u32 & 0xFF); fflush(stdout); }
        ctx.r3.u64 = 1;
        return;
    }
    __imp__sub_823D8FF0(ctx, base);
}
extern "C" PPC_FUNC(__imp__sub_823D8FB0);
PPC_FUNC(sub_823D8FB0)
{
    const uint32_t object = ctx.r3.u32, entry = ctx.r4.u32;
    __imp__sub_823D8FB0(ctx, base);
    if (int32_t(ctx.r3.u32) >= 0 || entry == 0 || !g_botRemote.load() || g_bots <= 0) return;
    if (IsBotSessionNumber(base, object, Guest::Read8(base, entry)))
    {
        static int told = 0;
        if ((told < 5 && told++ < 5)) { printf("bots: session entry %08X (a bot) given local index 3\n", entry); fflush(stdout); }
        ctx.r3.u64 = 3;
    }
}


// 0.5.167: the bots' death animation and their deaths in the kill feed.
// 0.5.165's dumps: at a death the host's session player has a block at
// +0x100..+0x138 filled (where and how he died); a bot's stays empty. The
// session fills it, and sends it to every console, only in its update of
// this machine's local players (sub_82403840: the current client's local
// place, session +0x5991, names the session player; sub_823F2E20 builds the
// messages), run by the session's frame (sub_82404580) for each local
// player in play. The bots hold no place, so it never ran for them. Now,
// after the session's frame, it is run once for each bot: local place 3
// (the bots' scratch index) given his number for that call, the current
// client 3, his own gun view memory, and the session timer words that call
// advances (+0x85B0, +0x861C of the session object) kept as they were. Its
// 0.5.173 explicitly locks physical input for this scratch local client.
// The player-update wrapper also excludes every other player during this call.
namespace { bool g_inBotUpdate = false; }
extern "C" PPC_FUNC(__imp__sub_82404580);
PPC_FUNC(sub_82404580)
{
    const double dt = ctx.f1.f64;
    const uint32_t mode = ctx.r3.u32;
    PPCContext saved = ctx;
    __imp__sub_82404580(ctx, base);
    if (g_bots <= 0 || g_inBotUpdate || !g_botRemote.load() || mode < 0x40000000u || mode >= 0xA0000000u) return;
    const uint32_t object = mode + 0xAEB0;
    const uint32_t places = object + 0x5991;
    const uint32_t timerA = object + 0x85B0, timerB = object + 0x861C;
    g_inBotUpdate = true;
    // 0.5.171: Justin, on 0.5.170: lying prone he now and then stood up "like
    // someone was controlling me". The keys' state (0x82B53E20, read and
    // cleared by the command builder) and the host's input state (0x82C35030)
    // are kept as they were across the bots' updates, and the log says if a
    // bot's update changed them.
    constexpr uint32_t Keys = 0x82B53E20, KeysSize = 0x300, HostInput = 0x82C35030, HostInputSize = 0x804;
    static uint8_t keysBefore[KeysSize], inputBefore[HostInputSize];
    memcpy(keysBefore, base + Keys, KeysSize);
    memcpy(inputBefore, base + HostInput, HostInputSize);
    for (uint32_t slot = 4; slot < MaxClients; slot++)
    {
        if (!g_bot[slot]) continue;
        const uint32_t player = SessionPlayer(base, slot);
        if (player == 0) continue;
        const uint8_t number = Guest::Read8(base, player);
        if (number >= 24) continue;
        const uint8_t place3 = Guest::Read8(base, places + 3);
        if (place3 < 24 && place3 != number) continue;   // a real third local player (split screen): leave it
        const uint32_t a = Guest::Read32(base, timerA), b = Guest::Read32(base, timerB);
        if (!BotGunViewBlock(base, slot)) continue; // never borrow the host gun view
        KeepFov keep(base);
        BotGunViewScope gunView(base, slot, true);   // current client 3, his own gun view
        Guest::Write8(base, places + 3, number);
        PPCContext call = saved;
        call.r1.u32 = (saved.r1.u32 - 0x3000) & ~0xFu;
        call.r3.u32 = object;
        call.f1.f64 = dt;
        call.lr = 0;
        const uint8_t locked = Guest::Read8(base, MoveLocked + BotGunView);
        Guest::Write8(base, MoveLocked + BotGunView, 1);
        const uint32_t previousPlayer = g_supplementalBotPlayer;
        g_supplementalBotPlayer = player;
        Guest::Lookup(0x82403840)(call, base);
        g_supplementalBotPlayer = previousPlayer;
        Guest::Write8(base, MoveLocked + BotGunView, locked);
        Guest::Write8(base, places + 3, place3);
        Guest::Write32(base, timerA, a);
        Guest::Write32(base, timerB, b);
    }
    {
        const bool keys = memcmp(keysBefore, base + Keys, KeysSize) != 0;
        const bool input = memcmp(inputBefore, base + HostInput, HostInputSize) != 0;
        if (keys || input)
        {
            static int changed = 0;
            if ((changed < 30 && changed++ < 30)) { printf("bots: a bot's session update changed the host's %s%s%s: put back\n", keys ? "keys" : "", keys && input ? " and " : "", input ? "input" : ""); fflush(stdout); }
            memcpy(base + Keys, keysBefore, KeysSize);
            memcpy(base + HostInput, inputBefore, HostInputSize);
        }
    }
    g_inBotUpdate = false;
    static int told = 0;
    if ((told < 3 && told++ < 3)) { printf("bots: isolated session update for each bot (weapon/session state; physical input locked)\n"); fflush(stdout); }
}

// 0.5.167: the UAV shows the bots. The compass's enemy markers
// (sub_825049C8, on while the UAV sets enemy_radar, 0x82B2CF90 +12) are for
// players whose +0x84 (the time they last fired) is within 15 s of the
// level's time (0x82C2826C): a bot's firing (sub_82590C30, called by us)
// never sets it, so they never showed. While the UAV is on, every bot's is
// kept at the level's time: all of them on the compass, all the time.
static void UavShowsBots(uint8_t* base)
{
    if (Guest::Read32(base, 0x82B2CF90 + 12) != 1) return;
    const uint32_t entities = Guest::Read32(base, EntityList);
    if (!entities) return;
    const uint32_t levelTime = Guest::Read32(base, 0x82C2826C);
    for (uint32_t slot = 4; slot < MaxClients; slot++)
    {
        if (!g_bot[slot]) continue;
        const uint32_t entity = Guest::Read32(base, entities + 4 * (slot + 1));
        const uint32_t record = entity ? Guest::Read32(base, entity + 512) : 0;
        if (record) Guest::Write32(base, record + 0x84, levelTime);
    }
    // 0.5.169: what the compass tests (sub_825049C8), every 2 s while the
    // UAV is on: a bot shows if his side (entity +520 -> +4) is the host's
    // enemy and his +0x84 is within 15 s of the level's time.
    static uint32_t lastTold = 0;
    const uint32_t now = GetTickCount();
    if (now - lastTold < 2000) return;
    lastTold = now;
    const uint32_t host = Guest::Read32(base, entities + 4);
    const uint32_t hostTeam = host ? Guest::Read32(base, host + 520) : 0;
    printf("bots: UAV check: level time %u, host side %u:", levelTime, hostTeam ? Guest::Read32(base, hostTeam + 4) : 99);
    int shown = 0;
    for (uint32_t slot = 4; slot < MaxClients && shown < 6; slot++)
    {
        if (!g_bot[slot]) continue;
        const uint32_t entity = Guest::Read32(base, entities + 4 * (slot + 1));
        const uint32_t record = entity ? Guest::Read32(base, entity + 512) : 0;
        const uint32_t team = entity ? Guest::Read32(base, entity + 520) : 0;
        printf(" [%u side %u fired %u]", slot, team ? Guest::Read32(base, team + 4) : 99, record ? Guest::Read32(base, record + 0x84) : 0);
        shown++;
    }
    printf("\n"); fflush(stdout);
}


// 0.5.170: which of the game's death steps run for a bot's death, against
// the host's own (Justin: still no death animation for any bot). Each of
// these, the first 40 calls, with what its arguments are: a player's
// entity, record or script handle (index slot + 1) names him.
static const char* WhoIs(uint8_t* base, uint32_t v, char* out)
{
    const uint32_t entities = Guest::Read32(base, EntityList);
    for (uint32_t slot = 0; entities && slot < MaxClients; slot++)
    {
        const uint32_t entity = Guest::Read32(base, entities + 4 * (slot + 1));
        if (!entity) continue;
        const char* who = slot < 4 ? "host" : "bot";
        if (v == entity) { sprintf(out, "%s %u's entity", who, slot); return out; }
        if (v == Guest::Read32(base, entity + 512)) { sprintf(out, "%s %u's record", who, slot); return out; }
        if (v == Guest::Read32(base, entity + 520)) { sprintf(out, "%s %u's team", who, slot); return out; }
        if (v > 0x3FFF && v < 0x01000000u && (v & 0x3FFF) == slot + 1) { sprintf(out, "%s %u's handle?", who, slot); return out; }
    }
    sprintf(out, "%08X", v);
    return out;
}
#define DEATH_TRACE(addr, name) \
    extern "C" PPC_FUNC(__imp__sub_##addr); \
    PPC_FUNC(sub_##addr) \
    { \
        static int calls = 0; \
        if ((calls < 40 && calls++ < 40)) \
        { \
            char a[48], b[48], c[48]; \
            printf("bots: death step %s (%s) at %u: r3 %s, r4 %s, r5 %s\n", #addr, name, GetTickCount(), \
                   WhoIs(base, ctx.r3.u32, a), WhoIs(base, ctx.r4.u32, b), WhoIs(base, ctx.r5.u32, c)); \
            fflush(stdout); \
        } \
        __imp__sub_##addr(ctx, base); \
    }
DEATH_TRACE(8258C778, "server: a player killed")
DEATH_TRACE(8220B008, "script: DeathState")
DEATH_TRACE(821E5028, "script: Player_Dying notify")
DEATH_TRACE(821E4660, "script: PLAYER_DYING notify")
DEATH_TRACE(82463B78, "playerDeathHitGround")
DEATH_TRACE(82584188, "PLAYER_DEATH_FLYING")
#undef DEATH_TRACE


// 0.5.171: THE BOTS' DEATHS. 0.5.170's trace: at the host's death the game
// ran its player-dies routine (sub_82587268: drops the gun, the death
// animation through sub_82584188, the scripts' DeathState and Player_Dying,
// the obituary); at a bot's death none of it ran. A lethal hit reaches
// sub_823FD118, which with no network session (*r3 == 0) calls the
// player-dies routine itself and then the obituary callback; with a session
// it hands the death to the session layer (sub_823ED658), whose message
// comes back to the victim's own machine: a bot has none, so his death was
// never played. A bot's death now takes the no-session way: r3 points at a
// zero word for that call, so the routine runs here, on the host.
extern "C" PPC_FUNC(__imp__sub_823FD118);
PPC_FUNC(sub_823FD118)
{
    static uint32_t zero = 0;
    char who[48];
    const char* victim = WhoIs(base, ctx.r4.u32, who);
    const bool bot = strncmp(victim, "bot ", 4) == 0 && strstr(victim, "entity") != nullptr;
    static int told = 0;
    if ((told < 60 && told++ < 60)) { printf("bots: lethal hit (sub_823FD118) on %s, session %08X%s\n", victim, Guest::Read32(base, ctx.r3.u32), bot ? ": played here (no-session way)" : ""); fflush(stdout); }
    if (bot && g_botRemote.load())
    {
        if (!zero) { zero = Guest::AllocatePhysical(16); if (zero) memset(base + zero, 0, 16); }
        if (zero) ctx.r3.u32 = zero;
    }
    __imp__sub_823FD118(ctx, base);
}
extern "C" PPC_FUNC(__imp__sub_82587268);
PPC_FUNC(sub_82587268)
{
    static int calls = 0;
    if ((calls < 40 && calls++ < 40))
    {
        char a[48], b[48];
        printf("bots: player dies (sub_82587268): %s, killed by %s, means %u\n", WhoIs(base, ctx.r3.u32, a), WhoIs(base, ctx.r5.u32, b), ctx.r7.u32);
        fflush(stdout);
    }
    const uint32_t queue = Guest::Read32(base, 0x82A87860);
    printf("bots: death queue BEFORE: object %08X count %u entries %08X\n", queue, Guest::Read32(base, queue), Guest::Read32(base, queue + 4)); fflush(stdout);
    __imp__sub_82587268(ctx, base);
    printf("bots: death queue AFTER: object %08X count %u entries %08X\n", Guest::Read32(base, 0x82A87860), Guest::Read32(base, queue), Guest::Read32(base, queue + 4)); fflush(stdout);
}

// 0.5.176: kill-crash evidence. This is the eight-entry native error queue,
// not a draw list. Log its producer and death transition before renderer drain.
extern "C" PPC_FUNC(__imp__sub_8210DCF8);
PPC_FUNC(sub_8210DCF8)
{
    const uint32_t queue = ctx.r3.u32, format = ctx.r5.u32, caller = uint32_t(ctx.lr);
    const uint32_t count = Guest::Read32(base, queue), severity = ctx.r4.u32;
    char message[193] = {};
    if (format >= 0x40000000u && format < 0xA0000000u)
        for (size_t i = 0; i < sizeof(message) - 1; ++i) { const uint8_t c = Guest::Read8(base, format + uint32_t(i)); if (!c) break; message[i] = (c >= 32 && c < 127) ? char(c) : ' '; }
    printf("bots: native error enqueue: object %08X count %u severity %u caller %08X format %08X [%s] args %08X %08X %08X\n", queue, count, severity, caller, format, message, ctx.r6.u32, ctx.r7.u32, ctx.r8.u32); fflush(stdout);
    __imp__sub_8210DCF8(ctx, base);
    printf("bots: native error enqueued: count %u entries %08X\n", Guest::Read32(base, queue), Guest::Read32(base, queue + 4)); fflush(stdout);
}
extern "C" PPC_FUNC(__imp__sub_824309F0);
PPC_FUNC(sub_824309F0)
{
    const uint32_t queue = Guest::Read32(base, 0x82A87860);
    if (!queue)
    {
        static int missing = 0;
        if ((missing < 5 && missing++ < 5)) { printf("bots: error-queue pointer cleared; drain skipped, guest0 untouched; cause unresolved\n"); fflush(stdout); }
        ctx.r3.u64 = 0;
        return;
    }
    const uint32_t count = Guest::Read32(base, queue);
    if (count != 0)
    {
        printf("bots: native error drain: object %08X count %u entries %08X caller %08X\n", queue, count, Guest::Read32(base, queue + 4), uint32_t(ctx.lr)); fflush(stdout);
    }
    // Native producer8210DCF8 admits at most8; never walk a corrupt count
    // until the 2GB demand-commit limit. Keep evidence and quarantine count.
    if (count > 8)
    {
        printf("bots: CORRUPT error queue quarantined; count %u exceeds native capacity8; cause unresolved\n", count); fflush(stdout);
        Guest::Write32(base, queue, 0);
        ctx.r3.u64 = count;
        return;
    }
    __imp__sub_824309F0(ctx, base);
}

#endif
