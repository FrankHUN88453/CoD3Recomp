#pragma once
// Switches for looking into the picture while the game runs, read from
// debug.txt beside the executable once a second. Nothing changes while the
// file is not there. One switch a line, a name and a number (hexadecimal for
// a program's hash):
//
//   dump_vs <hash>      the vertex stream, depth state and target of the next
//                       few draws of that vertex program go to the log
//   dump_count <n>      how many draws each second (4 when not given)
//   skip_vs <hash>      that vertex program's draws are left out
//   skip_ps <hash>      that pixel program's draws are left out
//   depth_writeback 0   the depth copies are not written back into the
//                       title's memory (they are by default: the glows of
//                       lights behind walls go by them)
//   resolve_log 1       each resolve's destination registers, once a second
//   spin_wait <us>      how long a wait that would block looks again first
//                       (kernel_objects.cpp; 60 when not given, 0 never)
//   bot_pads <n>        n more controllers (players 2 to n+1) plugged in and
//                       signed in as "Bot 1" ...: the bot experiment
//   bot_buttons <hex>   the buttons those controllers hold (XINPUT bits)
//   bot_sticks <lx> <ly> <rx> <ry>   and their sticks (-32768 .. 32767)
//   bot_add <n>         on the host, bots added until there are n (bots.cpp)
//   bot_bring <n>       each new n puts the bots beside the host's player
//   bot_session <n>     each new n logs the session's players; 2 also gives
//                       each bot one (a copy of the host's) and respawns him
//   shock_capture 1     a shell shock beginning takes four F12 captures over
//                       its first half second (mp_fixes.cpp)

#include <cstdint>

namespace DebugProbe
{
    void Poll();   // at each swap

    uint64_t DumpVs();
    bool TakeDump();   // whether one more draw may be dumped this second
    uint64_t SkipVs();
    uint64_t SkipPs();
    int DepthWriteback();
    bool TakeResolveLog();
    long SpinWaitMicros();   // -1: not given
    bool ShockCapture();
    int BotPads();
    int BotAdd();
    int BotBring();
    int BotSession();
    // move_watch 1: the host's own record's words that change, ten times a
    // second, for finding the sprint's and the landing's timers.
    int MoveWatch();
    // bot_poke <offset> <value>: each bot's record byte at offset kept at value
    // (offset 0: none), for finding fields by trying.
    void BotPoke(int& offset, int& value);
    uint16_t BotButtons();
    void BotSticks(int16_t& lx, int16_t& ly, int16_t& rx, int16_t& ry);
}
