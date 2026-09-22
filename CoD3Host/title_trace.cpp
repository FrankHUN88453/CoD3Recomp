// Traces of a few of the title's own functions, for the times the picture
// says nothing was drawn and the question is whether the title ever asked.
// The recompiled functions are reached through weak aliases, which these
// stronger definitions replace; each prints and then runs the original.
// All of it is behind COD3_TRACETITLE=1 and costs nothing otherwise.

#include "kernel.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <Windows.h>

namespace
{
    bool Wanted()
    {
        static const bool wanted = getenv("COD3_TRACETITLE") != nullptr;
        return wanted;
    }

    const char* Text(uint8_t* base, uint32_t address)
    {
        if (address < 0x10000 || address >= 0xFFFF0000u) return "?";
        return reinterpret_cast<const char*>(base + address);
    }
}

// The front end manager's panel dispatcher, sub_824E7998(manager, name,
// argument): the level's chapter title screen is one of its names.
extern "C" PPC_FUNC(__imp__sub_824E7998);
PPC_FUNC(sub_824E7998)
{
    if (Wanted()) { printf("title: panel \"%s\" (argument %08X)\n", Text(base, ctx.r4.u32), ctx.r5.u32); fflush(stdout); }
    __imp__sub_824E7998(ctx, base);
}

// The chapter title screen, sub_824DFF30(screen, panel): finds its two
// images and four text lines in the panel and starts their fades.
extern "C" PPC_FUNC(__imp__sub_824DFF30);
PPC_FUNC(sub_824DFF30)
{
    const uint32_t screen = ctx.r3.u32;
    if (Wanted()) { printf("title: chapter title screen %08X opens on panel %08X\n", screen, ctx.r4.u32); fflush(stdout); }
    __imp__sub_824DFF30(ctx, base);
    if (Wanted())
    {
        for (uint32_t i = 0; i < 6; i++)
            printf("title:   element %u: kind %u at %08X\n", i, Guest::Read32(base, screen + i * 8), Guest::Read32(base, screen + i * 8 + 4));
        fflush(stdout);
    }
}

// The text element lookup, sub_824D07D0(panel, name).
extern "C" PPC_FUNC(__imp__sub_824D07D0);
PPC_FUNC(sub_824D07D0)
{
    const uint32_t name = ctx.r4.u32;
    __imp__sub_824D07D0(ctx, base);
    if (Wanted()) { printf("title: text element \"%s\" -> %08X\n", Text(base, name), ctx.r3.u32); fflush(stdout); }
}

// The image element lookup, sub_824DFC70(panel, name).
extern "C" PPC_FUNC(__imp__sub_824DFC70);
PPC_FUNC(sub_824DFC70)
{
    const uint32_t name = ctx.r4.u32;
    __imp__sub_824DFC70(ctx, base);
    if (Wanted()) { printf("title: image element \"%s\" -> %08X\n", Text(base, name), ctx.r3.u32); fflush(stdout); }
}

// The chapter title screen's text, sub_824C0CD8(screen, line, text): the
// script's settext on a hud element of types ten to thirteen lands here.
extern "C" PPC_FUNC(__imp__sub_824C0CD8);
PPC_FUNC(sub_824C0CD8)
{
    if (Wanted()) { printf("title: screen %08X line %u text \"%s\"\n", ctx.r3.u32, ctx.r4.u32, Text(base, ctx.r5.u32)); fflush(stdout); }
    __imp__sub_824C0CD8(ctx, base);
}

// A hud element's text, sub_824ACC90(element, string): every settext.
extern "C" PPC_FUNC(__imp__sub_824ACC90);
PPC_FUNC(sub_824ACC90)
{
    if (Wanted())
    {
        const uint32_t element = ctx.r3.u32, string = ctx.r4.u32;
        const uint32_t text = string != 0 ? Guest::Read32(base, string) : 0;
        printf("title: hud element %08X (index %u) settext \"%s\"\n", element, Guest::Read32(base, element), text != 0 ? Text(base, text + 12) : "");
        fflush(stdout);
    }
    __imp__sub_824ACC90(ctx, base);
}

// The front end's level launcher, sub_82517840: runs "spmap <name>" for
// the name in the pending map buffer, once its gates let it.
extern "C" PPC_FUNC(__imp__sub_82517840);
PPC_FUNC(sub_82517840)
{
    static int shown = 0;
    const char* pending = Text(base, 0x829C3A48);
    const bool interesting = pending[0] != 0;
    __imp__sub_82517840(ctx, base);
    if (Wanted() && interesting && shown++ < 20)
    {
        printf("title: launcher saw \"%s\", returned %u, gate object %08X (+40 %08X vs %08X), loading flag %08X\n", pending, ctx.r3.u32,
            Guest::Read32(base, 0x829C39E8), Guest::Read32(base, 0x829C39E8) ? Guest::Read32(base, Guest::Read32(base, 0x829C39E8) + 40) : 0,
            Guest::Read32(base, 0x82A56CF0), Guest::Read32(base, 0x829C3A24));
        fflush(stdout);
    }
}

// The XMA voice's decode step, sub_823B8C60(voice, destination, ?, &count):
// one context job and the copy of what it produced.
extern "C" PPC_FUNC(__imp__sub_823B8C60);
PPC_FUNC(sub_823B8C60)
{
    static int shown = 0;
    const uint32_t voice = ctx.r3.u32, dest = ctx.r4.u32, arg5 = ctx.r5.u32, countPtr = ctx.r6.u32;
    const uint32_t before = countPtr ? Guest::Read32(base, countPtr) : 0;
    __imp__sub_823B8C60(ctx, base);
    if (getenv("COD3_XMATRACE") != nullptr && shown++ < 60)
    {
        printf("title: xma step voice %08X dest %08X r5 %08X count %u -> returned %u, count now %u, dest words %08X %08X\n",
            voice, dest, arg5, before, ctx.r3.u32, countPtr ? Guest::Read32(base, countPtr) : 0,
            Guest::Read32(base, dest), Guest::Read32(base, dest + 4));
        fflush(stdout);
    }
}

// --- the mixer ------------------------------------------------------------------
// The audio callback sub_822C1108(mixer) runs on the render driver's
// frame; with a mixer thread it wakes it and waits for it, without one it
// mixes inline. The thread's loop is sub_822C0F90 and the mix itself
// sub_822C0990(mixer, flag). Behind COD3_XMATRACE: how often each runs.

namespace
{
    bool MixTrace()
    {
        static const bool wanted = getenv("COD3_XMATRACE") != nullptr;
        return wanted;
    }
    uint64_t Now()
    {
        return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    }
}

extern "C" PPC_FUNC(__imp__sub_822C1108);
PPC_FUNC(sub_822C1108)
{
    static int shown = 0;
    static uint64_t lastSecond = 0, calls = 0;
    const uint32_t mixer = ctx.r3.u32;
    const uint32_t thread = Guest::Read32(base, mixer + 304);
    calls++;
    if (MixTrace() && shown++ < 8)
        printf("title: audio callback mixer %08X thread %08X at %llu ms\n", mixer, thread, (unsigned long long)Now());
    __imp__sub_822C1108(ctx, base);
    if (MixTrace() && Now() - lastSecond >= 1000)
    {
        lastSecond = Now();
        printf("title: audio callback %llu calls so far, thread %08X\n", (unsigned long long)calls, thread);
        fflush(stdout);
    }
}

extern "C" PPC_FUNC(__imp__sub_822C0990);
PPC_FUNC(sub_822C0990)
{
    static uint64_t calls = 0, lastSecond = 0;
    calls++;
    const uint64_t started = Now();
    __imp__sub_822C0990(ctx, base);
    if (MixTrace() && Now() - lastSecond >= 1000)
    {
        lastSecond = Now();
        printf("title: mix %llu calls so far, the last took %llu ms, flag %u\n", (unsigned long long)calls, (unsigned long long)(Now() - started), ctx.r4.u32);
        fflush(stdout);
    }
}

// The mixer thread's function, sub_822C0F90(a, b): loops while b is zero.
extern "C" PPC_FUNC(__imp__sub_822C0F90);
PPC_FUNC(sub_822C0F90)
{
    static int shown = 0;
    const uint32_t a = ctx.r3.u32, b = ctx.r4.u32;
    if (MixTrace() && shown < 20) printf("title: mixer thread function (%08X, %08X) enters on thread %lu at %llu ms\n", a, b, GetCurrentThreadId(), (unsigned long long)Now());
    __imp__sub_822C0F90(ctx, base);
    if (MixTrace() && shown++ < 20) printf("title: mixer thread function (%08X, %08X) returns %u at %llu ms\n", a, b, ctx.r3.u32, (unsigned long long)Now());
    fflush(stdout);
}
