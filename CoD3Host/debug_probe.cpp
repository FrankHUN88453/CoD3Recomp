// debug.txt beside the executable, read once a second. See debug_probe.h.
#include "debug_probe.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#include <Windows.h>

namespace
{
    std::atomic<uint64_t> g_dumpVs{ 0 }, g_skipVs{ 0 }, g_skipPs{ 0 };
    std::atomic<int> g_dumpCount{ 4 }, g_dumpLeft{ 0 }, g_depthWriteback{ 1 };
    std::atomic<bool> g_resolveLog{ false }, g_resolveLogLeft{ false };
    std::atomic<long> g_spinWait{ -1 };
    std::atomic<bool> g_shockCapture{ false };
    std::atomic<int> g_botPads{ 0 }, g_botAdd{ 0 }, g_botBring{ 0 }, g_botSession{ 0 }, g_moveWatch{ 0 }, g_botPokeOffset{ 0 }, g_botPokeValue{ 0 };
    std::atomic<uint32_t> g_botButtons{ 0 };
    std::atomic<int32_t> g_botLX{ 0 }, g_botLY{ 0 }, g_botRX{ 0 }, g_botRY{ 0 };

    std::wstring Path()
    {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        return (std::filesystem::path(exe).parent_path() / "debug.txt").wstring();
    }
}

void DebugProbe::Poll()
{
    // The resolve log covers the swap after the poll that armed it.
    static int swapsSinceArmed = 0;
    if (g_resolveLogLeft.load(std::memory_order_relaxed) && ++swapsSinceArmed > 1)
        g_resolveLogLeft.store(false, std::memory_order_relaxed);
    static auto last = std::chrono::steady_clock::now() - std::chrono::seconds(2);
    const auto now = std::chrono::steady_clock::now();
    if (now - last < std::chrono::seconds(1)) return;
    last = now;
    static const std::wstring path = Path();

    uint64_t dumpVs = 0, skipVs = 0, skipPs = 0;
    int dumpCount = 4, writeback = 1;
    long spinWait = -1;
    bool resolveLog = false, shockCapture = false;
    int botPads = 0, botAdd = 0, botBring = 0, botSession = 0, moveWatch = 0, botPokeOffset = 0, botPokeValue = 0;
    uint32_t botButtons = 0;
    int32_t botSticks[4] = { 0, 0, 0, 0 };
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"rb") == 0 && file != nullptr)
    {
        char line[256];
        while (fgets(line, sizeof(line), file))
        {
            char key[64] = {}, value[64] = {};
            if (sscanf(line, "%63s %63s", key, value) < 1 || key[0] == '#') continue;
            if (strcmp(key, "bot_sticks") == 0)
            {
                int a = 0, b = 0, c = 0, d = 0;
                sscanf(line, "%*s %d %d %d %d", &a, &b, &c, &d);
                botSticks[0] = a; botSticks[1] = b; botSticks[2] = c; botSticks[3] = d;
                continue;
            }
            const uint64_t hex = strtoull(value, nullptr, 16);
            const long number = strtol(value, nullptr, 10);
            if (strcmp(key, "dump_vs") == 0) dumpVs = hex;
            else if (strcmp(key, "dump_count") == 0) dumpCount = int(number);
            else if (strcmp(key, "skip_vs") == 0) skipVs = hex;
            else if (strcmp(key, "skip_ps") == 0) skipPs = hex;
            else if (strcmp(key, "depth_writeback") == 0) writeback = int(number);
            else if (strcmp(key, "resolve_log") == 0) resolveLog = number != 0;
            else if (strcmp(key, "spin_wait") == 0) spinWait = number;
            else if (strcmp(key, "shock_capture") == 0) shockCapture = number != 0;
            else if (strcmp(key, "bot_pads") == 0) botPads = int(number);
            else if (strcmp(key, "bot_add") == 0) botAdd = int(number);
            else if (strcmp(key, "bot_buttons") == 0) botButtons = uint32_t(hex);
            else if (strcmp(key, "bot_bring") == 0) botBring = int(number);
            else if (strcmp(key, "bot_session") == 0) botSession = int(number);
            else if (strcmp(key, "move_watch") == 0) moveWatch = int(number);
            else if (strcmp(key, "bot_poke") == 0)
            {
                int offset = 0, value = 0;
                sscanf(line, "%*s %d %d", &offset, &value);
                botPokeOffset = offset; botPokeValue = value;
            }
        }
        fclose(file);
    }
    static std::string said;
    char summary[256];
    snprintf(summary, sizeof(summary), "dump_vs %016llx x%d, skip_vs %016llx, skip_ps %016llx, depth_writeback %d, resolve_log %d, spin_wait %ld",
        (unsigned long long)dumpVs, dumpCount, (unsigned long long)skipVs, (unsigned long long)skipPs, writeback, resolveLog ? 1 : 0, spinWait);
    if (said != summary)
    {
        said = summary;
        printf("debug.txt: %s\n", summary);
        fflush(stdout);
    }
    g_dumpVs.store(dumpVs);
    g_dumpCount.store(dumpCount);
    g_dumpLeft.store(dumpVs != 0 ? dumpCount : 0);
    g_skipVs.store(skipVs);
    g_skipPs.store(skipPs);
    g_depthWriteback.store(writeback);
    g_resolveLog.store(resolveLog);
    g_resolveLogLeft.store(resolveLog);
    g_spinWait.store(spinWait);
    g_shockCapture.store(shockCapture);
    if (botPads != g_botPads.load() || botButtons != g_botButtons.load())
    {
        printf("debug.txt: bot pads %d, buttons %04X, sticks %d %d %d %d\n", botPads, botButtons, botSticks[0], botSticks[1], botSticks[2], botSticks[3]);
        fflush(stdout);
    }
    g_botPads.store(botPads < 0 ? 0 : botPads > 3 ? 3 : botPads);
    g_botButtons.store(botButtons);
    if (botAdd != g_botAdd.load()) { printf("debug.txt: bot_add %d\n", botAdd); fflush(stdout); }
    g_botAdd.store(botAdd < 0 ? 0 : botAdd > 20 ? 20 : botAdd);
    g_botBring.store(botBring);
    g_botSession.store(botSession);
    g_moveWatch.store(moveWatch);
    g_botPokeOffset.store(botPokeOffset);
    g_botPokeValue.store(botPokeValue);
    g_botLX.store(botSticks[0]); g_botLY.store(botSticks[1]); g_botRX.store(botSticks[2]); g_botRY.store(botSticks[3]);
    swapsSinceArmed = 0;
}

uint64_t DebugProbe::DumpVs() { return g_dumpVs.load(std::memory_order_relaxed); }
bool DebugProbe::TakeDump() { return g_dumpLeft.fetch_sub(1, std::memory_order_relaxed) > 0; }
uint64_t DebugProbe::SkipVs() { return g_skipVs.load(std::memory_order_relaxed); }
uint64_t DebugProbe::SkipPs() { return g_skipPs.load(std::memory_order_relaxed); }
int DebugProbe::DepthWriteback() { return g_depthWriteback.load(std::memory_order_relaxed); }
bool DebugProbe::TakeResolveLog()
{
    // Each resolve of the frame after a poll: a frame's worth, not one.
    return g_resolveLogLeft.load(std::memory_order_relaxed);
}
long DebugProbe::SpinWaitMicros() { return g_spinWait.load(std::memory_order_relaxed); }
bool DebugProbe::ShockCapture() { return g_shockCapture.load(std::memory_order_relaxed); }
void DebugProbe::BotPoke(int& offset, int& value) { offset = g_botPokeOffset.load(); value = g_botPokeValue.load(); }
int DebugProbe::BotSession() { return g_botSession.load(std::memory_order_relaxed); }
int DebugProbe::MoveWatch() { return g_moveWatch.load(std::memory_order_relaxed); }
int DebugProbe::BotBring() { return g_botBring.load(std::memory_order_relaxed); }
int DebugProbe::BotAdd() { return g_botAdd.load(std::memory_order_relaxed); }
int DebugProbe::BotPads() { return g_botPads.load(std::memory_order_relaxed); }
uint16_t DebugProbe::BotButtons() { return uint16_t(g_botButtons.load(std::memory_order_relaxed)); }
void DebugProbe::BotSticks(int16_t& lx, int16_t& ly, int16_t& rx, int16_t& ry)
{
    auto clamp = [](int32_t v) { return int16_t(v < -32768 ? -32768 : v > 32767 ? 32767 : v); };
    lx = clamp(g_botLX.load()); ly = clamp(g_botLY.load()); rx = clamp(g_botRX.load()); ry = clamp(g_botRY.load());
}
