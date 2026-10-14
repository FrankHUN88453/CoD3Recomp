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
// The console's "bot_add <n>" (kernel_video.cpp): bots added until there are
// n, as debug.txt's bot_add does (the larger of the two counts).
static std::atomic<int> g_consoleBots{0};
void BotsAsk(int count) { g_consoleBots.store(count < 0 ? 0 : count > 20 ? 20 : count); }

#include "debug_probe.h"
#include "dom.h"
#include "settings.h"
#include "kernel.h"

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
    bool g_firing[MaxClients] = {};   // the bot wants his weapon to fire this frame
    float g_wantYaw[MaxClients] = {}, g_wantPitch[MaxClients] = {};   // where his brain wants to look
    int g_age[MaxClients] = {};   // frames since the bot was added
    float g_yaw[MaxClients] = {};
    int g_bots = 0;

    // The players' command rings: 6352 bytes a player at 0x82C2ECF0, 64
    // commands of 48 bytes at +1688, the newest's number at +4760. A command:
    // +0 the server time, +4 the buttons (a bit each), +8 the weapon, +12/16/20
    // the view angles as shorts (360 degrees in 65536), +24/25/26 forward,
    // right and up (-127 .. 127), five floats from +28 (CL_FinishMove,
    // sub_82522B10; the moves from sub_82525338).
    constexpr uint32_t Players = 0x82C2ECF0, PlayerSize = 6352;
    constexpr uint32_t Ring = 1688, Newest = 4760, CommandSize = 48;

    uint32_t NewestCommand(uint8_t* base, uint32_t slot)
    {
        const uint32_t player = Players + slot * PlayerSize;
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

    // A player's side: +4 of the team record his entity's +520 points to
    // (1 the allies, 2 the axis, as the side menus set it).
    uint32_t Side(uint8_t* base, uint32_t entity)
    {
        const uint32_t team = entity ? Guest::Read32(base, entity + 520) : 0;
        return team ? Guest::Read32(base, team + 4) : 0;
    }

    // A bot's side: the one the host is not on, so the bots fight him.
    uint32_t BotSide(uint8_t* base)
    {
        // bot_buttons 0x4000: the old way, the host's own side, for trying.
        if (DebugProbe::BotButtons() & 0x4000) return 1;
        const uint32_t entities = Guest::Read32(base, EntityList);
        const uint32_t host = entities ? Guest::Read32(base, entities + 4) : 0;
        return Side(base, host) == 2 ? 1 : 2;
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
            Guest::Write8(base, player, uint8_t(i));
            Guest::Write32(base, player + 8, slot);
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
        printf("bots: adding a bot in slot %u (client %08X)\n", slot, client);
        fflush(stdout);
        g_bot[slot] = true;

        Call(ctx, base, SetUpSlot, client);
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
            Guest::Write16(base, record + 1920, 0xFFFF);
            Guest::Write16(base, record + 1922, 0xFFFF);
            memset(base + record + 1504, 0, 406);
            Guest::Write32(base, record + 1504 + 408, 0);
        }
        const uint32_t team = Guest::Read32(base, entity + 520);
        if (team != 0) Guest::Write32(base, team + 4, 0);
        Call(ctx, base, BeginPlayer, entity, 0);
        printf("bots: bot in slot %u begun (entity %08X, record %08X)\n", slot, entity, record);
        fflush(stdout);
        g_bots++;
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
            if (age == 20) PickSide(ctx, base, entity, BotSide(base));
            if (age == 40) PickKit(ctx, base, entity, 0);
            if (age == 60) Respawn(ctx, base, entity, BotSide(base) == 1);
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
    constexpr uint32_t ClientThink = 0x82593518, CurrentClient = 0x82BCBE9C;

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
            Guest::Write32(base, CurrentClient, slot);
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
                else if (nowMs - deadSince[slot] > 4000 && SessionPlayer(base, slot) != 0)
                {
                    deadSince[slot] = 0;
                    printf("bots: slot %u respawns\n", slot); fflush(stdout);
                    Respawn(ctx, base, entity, BotSide(base) == 1);
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
            const uint32_t clientMoves = Guest::Read32(base, ClientsMoveThemselves);
            Guest::Write32(base, ClientsMoveThemselves, 0);
            Call(ctx, base, ClientThink, Guest::Read32(base, entity + 484));
            Guest::Write32(base, ClientsMoveThemselves, clientMoves);
            Guest::Write8(base, lockByte, locked);
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
struct BotOrders { int8_t forward, right; uint32_t buttons; bool wantsFire; };
static float g_pitch[MaxClients] = {};

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

static BotOrders Think(uint8_t* base, uint32_t slot, uint32_t now)
{
    BotOrders orders = {};
    const uint32_t entities = Guest::Read32(base, EntityList);
    const uint32_t self = entities ? Guest::Read32(base, entities + 4 * (slot + 1)) : 0;
    const uint32_t record = self ? Guest::Read32(base, self + 512) : 0;
    if (!record) return orders;
    const int32_t health = int32_t(Guest::Read32(base, record + 304));
    if (health <= 0)
    {
        // Dead: tap the use button now and then, which asks to respawn.
        orders.buttons = (now / 500) % 2 ? 0x20 : 0;
        return orders;
    }
    const float me[3] = { ReadFloat(base, record), ReadFloat(base, record + 4), ReadFloat(base, record + 8) + 60.0f };
    // His side, kept in both places the session's switch sets (the team
    // record's +4 and his session player's +604): the game's spawn puts the
    // host's side back on him (his session player is a copy of the host's).
    if (const uint32_t player = SessionPlayer(base, slot))
    {
        const uint32_t want = BotSide(base);
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
        const float at[3] = { ReadFloat(base, theirs), ReadFloat(base, theirs + 4), ReadFloat(base, theirs + 8) + 50.0f };
        const float dx = at[0] - me[0], dy = at[1] - me[1], dz = at[2] - me[2];
        const float distance = dx * dx + dy * dy + dz * dz;
        if (distance < best) { best = distance; memcpy(target, at, sizeof(target)); found = true; }
    }

    // His view: +204 pitch, +208 yaw (what the server's fire shoots along,
    // sub_8256E978).
    const float viewPitch = ReadFloat(base, record + 204), viewYaw = ReadFloat(base, record + 208);
    if (!found)
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
        // No one to fight: wander, turning slowly.
        g_yaw[slot] += 0.6f;
        orders.forward = 90;
        return orders;
    }
    const float dx = target[0] - me[0], dy = target[1] - me[1], dz = target[2] - me[2];
    const float flat = sqrtf(dx * dx + dy * dy);
    const float wantYaw = atan2f(dy, dx) * 57.29578f;
    // Pitch: up is positive in this game's view (tried: the other way he
    // aimed at the wall above a lower target).
    const float wantPitch = atan2f(dz, flat) * 57.29578f;
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

    if (flat > 500.0f) orders.forward = 127;
    else if (flat < 150.0f) orders.forward = -60;
    orders.right = ((now / 900) % 2) ? 70 : -70;
    // Fire when the aim is close, in bursts (pressed every other frame, so a
    // rifle that wants each shot its own press fires too).
    if (fabsf(yawError) < 6.0f && flat < 1600.0f)
    {
        static uint32_t frame = 0;
        const uint32_t fire = (DebugProbe::BotButtons() & 0x00FF) ? (DebugProbe::BotButtons() & 0x00FF) : 0x1;
        orders.buttons = (++frame & 1) ? fire : 0;
        orders.wantsFire = true;
    }
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
    return orders;
}

// Each bot's command for this frame, into its ring. For trying, from
// debug.txt: bot_sticks' left stick walks (lx right, ly forward), its right
// stick's x turns, bot_buttons are the buttons.
static void FeedBots(uint8_t* base)
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
        if (brain) orders = Think(base, slot, serverTime);
        else g_yaw[slot] += float(rx) / 32768.0f * 3.0f;
        const uint32_t player = Players + slot * PlayerSize;
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
        const uint32_t side = BotSide(base);
        const uint32_t team = Guest::Read32(base, entity + 520);
        if (team != 0) Guest::Write32(base, team + 4, side);
        if (const uint32_t player = SessionPlayer(base, slot)) Guest::Write16(base, player + 604, uint16_t(side));
        printf("bots: slot %u on side %u\n", slot, side); fflush(stdout);
        Respawn(ctx, base, entity, side == 1);
    }
    LogSession(base);
}

PPC_FUNC(sub_82535810)
{
    FeedBots(base);
    __imp__sub_82535810(ctx, base);
    LobbyRulesTick(ctx, base);
    Domination::Frame(ctx, base);
    MpFixesFrame(base);
    MpFixesAbilities(ctx, base);
    MpFixesLauncher(ctx, base);
    SetUpBots(ctx, base);
    {
        // The console's bot_add: once its bots are in, their sessions.
        static int seen = 0, wait = 0;
        const int asked = g_consoleBots.load();
        if (asked != seen && asked > 0 && g_bots >= asked)
        {
            // (0.5.60 gave them their sessions here, as debug.txt's
            // bot_session 2 does: in a devmap game that crashed the game a
            // little after. Left to debug.txt until the bots get their turn.)
            if (++wait > 30) { seen = asked; wait = 0; }
        }
        else if (asked == 0) seen = 0;
    }
    ThinkBots(ctx, base);
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
    if (g_bots < wanted && tries < 8)
    {
        tries++;
        if (!AddBot(ctx, base)) return;
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
                if (said++ < 6) { printf("moves: aiming %.2f, scale %.3f -> %.3f (+1124 %.3f, +1128 %.3f)\n", sights, before, quicker, fl(1124), fl(1128)); fflush(stdout); }
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
