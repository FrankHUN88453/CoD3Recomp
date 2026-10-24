#pragma once
#include <cstdint>
#include <cstdlib>

// 0.5.240: the number of players in play is chosen when the game starts:
// 24 (the game's own), or 32 with COD3_PLAYERS=32. The generated code's
// reviewed player-limit sites read it too (scripts/patch_recomp.py
// patch_cap32, docs/player-limit/cap32-sites.csv). Defined in mp_fixes.cpp.
extern uint32_t MpCapClients;

namespace PlayerCapacity
{
    // Change only with the reviewed guest loops, sentinels, entities and UI.
    inline constexpr uint32_t ActiveClients = 24;
    inline constexpr uint32_t StorageClients = 32;
    inline constexpr uint32_t ReservedLocalClients = 4;
    inline constexpr uint32_t MaxBots = ActiveClients - ReservedLocalClients;
    inline constexpr uint32_t ServerClientBytes = 4992;
    static_assert(ReservedLocalClients <= ActiveClients && ActiveClients <= StorageClients);
    static_assert(StorageClients <= 32); // session message masks are uint32_t

    // The slots in play now (24 or 32), and the bots that leaves room for.
    inline uint32_t Active() { return MpCapClients; }
    // 0.5.283: bots may also take the local players' spare slots 1-3 (filled
    // last, after 4 and up; no split screen here), so all slots but the
    // host's: 31 at 32 players. COD3_BOTS_ALL_SLOTS=0 for the old 28/20.
    inline bool BotsInLocalSlots() { static const bool on = [] { const char* e = std::getenv("COD3_BOTS_ALL_SLOTS"); return !(e && e[0] == '0'); }(); return on; }
    inline uint32_t FirstBotSlot() { return BotsInLocalSlots() ? 1u : ReservedLocalClients; }
    inline uint32_t MaxBotsNow() { return Active() - (BotsInLocalSlots() ? 1u : ReservedLocalClients); }
}
