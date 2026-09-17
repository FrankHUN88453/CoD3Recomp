// Traces of a few of the title's own functions, for the times the picture
// says nothing was drawn and the question is whether the title ever asked.
// The recompiled functions are reached through weak aliases, which these
// stronger definitions replace; each prints and then runs the original.
// All of it is behind COD3_TRACETITLE=1 and costs nothing otherwise.

#include "kernel.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

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
