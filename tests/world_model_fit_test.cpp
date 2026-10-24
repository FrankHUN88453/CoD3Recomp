// g++ -std=c++20 -I CoD3Host tests/world_model_fit_test.cpp -o /tmp/wmf && /tmp/wmf
#include "world_model_fit.h"
#include <cassert>
#include <cstdio>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)
static bool Near(float a, float b, float e = 0.02f) { return fabsf(a - b) <= e; }

static WorldFit::Box MakeBox(float x0, float y0, float z0, float x1, float y1, float z1)
{
    WorldFit::Box b;
    const float lo[3] = { x0, y0, z0 }, hi[3] = { x1, y1, z1 };
    b.Add(lo); b.Add(hi);
    return b;
}

int main()
{
    using namespace WorldFit;
    // Parsing
    {
        std::string problems;
        auto fits = Parse("# c\n* auto\nmg42 0 0 0 1 2 -3 4.5\nmills_bomb off\nbren auto 90 0 0 1 0 0 1 # extra\nbad\n", &problems);
        CHECK(fits.size() == 4);
        CHECK(fits["*"].mode == Mode::Auto && !fits["*"].extra);
        CHECK(fits["mg42"].mode == Mode::Manual && Near(fits["mg42"].x, 2) && Near(fits["mg42"].y, -3) && Near(fits["mg42"].z, 4.5f));
        CHECK(fits["mills_bomb"].mode == Mode::Off);
        CHECK(fits["bren"].mode == Mode::Auto && fits["bren"].extra && Near(fits["bren"].yaw, 90));
        CHECK(problems.find("bad") != std::string::npos);
        CHECK(For(fits, "STEN").mode == Mode::Auto);
        CHECK(For(fits, "MG42").mode == Mode::Manual);
        auto hides = Parse("* auto\nmg42 hide stand, bipod\nmg42 0 0 0 1 0 0 -2\nsten hide mag\n");
        CHECK(For(hides, "mg42").mode == Mode::Manual && For(hides, "mg42").hide.size() == 2 && For(hides, "mg42").hide[1] == "bipod");
        CHECK(For(hides, "sten").mode == Mode::Auto && For(hides, "sten").hide.size() == 1);
        CHECK(For(hides, "bren").hide.empty());
        auto defaults = Parse(DefaultText());
        CHECK(For(defaults, "mg42").hide.size() == 1 && For(defaults, "mg42").hide[0] == "stand");
        CHECK(For(defaults, "mg42").mode == Mode::Manual && Near(For(defaults, "mg42").scale, 0.8f) && For(defaults, "bren").mode == Mode::Off);
    }
    // Angles: yaw 90 takes +x to +y; pitch 90 takes +x up (+z).
    {
        const float x[3] = { 1, 0, 0 };
        float o[3];
        Point(FromAngles(90, 0, 0, 1, 0, 0, 0), x, o);
        CHECK(Near(o[0], 0) && Near(o[1], 1) && Near(o[2], 0));
        Point(FromAngles(0, 90, 0, 1, 0, 0, 0), x, o);
        CHECK(Near(o[0], 0) && Near(o[2], 1));
        Point(FromAngles(0, 0, 0, 2, 1, 2, 3), x, o);
        CHECK(Near(o[0], 3) && Near(o[1], 2) && Near(o[2], 3));
    }
    // Auto: a model with its barrel to -x, against a copy with it to +x: half
    // a turn about up, same size, the grip already at the same fraction.
    {
        const Box own = MakeBox(-40, -2, -3, 10, 2, 5), copy = MakeBox(-10, -2, -3, 40, 2, 5);
        std::string why;
        const Transform t = Auto(own, copy, &why);
        const float tip[3] = { -40, 0, 0 };
        float o[3];
        Point(t, tip, o);
        CHECK(Near(o[0], 40) && Near(o[1], 0) && Near(o[2], 0));
        CHECK(Near(t.scale, 1));
        printf("half turn: %s\n", why.c_str());
    }
    // Auto: along y (barrel +y) to along x: a quarter turn.
    {
        const Box own = MakeBox(-2, -10, -3, 2, 40, 5), copy = MakeBox(-10, -2, -3, 40, 2, 5);
        const Transform t = Auto(own, copy);
        const float tip[3] = { 0, 40, 0 };
        float o[3];
        Point(t, tip, o);
        CHECK(Near(o[0], 40) && Near(o[1], 0));
    }
    // Auto: the same way round, 2.5 times too big: scaled.
    {
        const Box own = MakeBox(-25, -5, -7.5f, 100, 5, 12.5f), copy = MakeBox(-10, -2, -3, 40, 2, 5);
        const Transform t = Auto(own, copy);
        CHECK(Near(t.scale, 0.4f));
        CHECK(Identity(Transform{}) && !Identity(t));
    }
    // Auto: the same way, nearly the same size (the Sten against the
    // Thompson): nothing done.
    {
        const Box own = MakeBox(-9, -1.5f, -4, 30, 1.5f, 3), copy = MakeBox(-10, -2, -5, 34, 2, 4);
        const Transform t = Auto(own, copy);
        CHECK(Identity(t));
    }
    // Auto: floating: right way round and size, but the origin is well
    // behind the butt (the model hangs out in front of the hand): moved so
    // the origin lies where the copy's does along the length.
    {
        const Box own = MakeBox(20, -2, 10, 70, 2, 18), copy = MakeBox(-10, -2, -3, 40, 2, 5);
        std::string why;
        const Transform t = Auto(own, copy, &why);
        printf("floating: %s\n", why.c_str());
        const float butt[3] = { 20, 0, 10 }, top[3] = { 70, 0, 18 };
        float a[3], b[3];
        Point(t, butt, a);
        Point(t, top, b);
        CHECK(Near(a[0], -10) && Near(b[0], 40));
        CHECK(Near(a[2], -3) && Near(b[2], 5));
    }
    // Then: auto followed by a hand move.
    {
        const Transform a = FromAngles(180, 0, 0, 1, 0, 0, 0), b = FromAngles(0, 0, 0, 1, 1, 2, 3);
        const Transform c = Then(a, b);
        const float p[3] = { 1, 0, 0 };
        float o[3];
        Point(c, p, o);
        CHECK(Near(o[0], 0) && Near(o[1], 2) && Near(o[2], 3));
    }
    // Packed directions: +x (x-low) turned a quarter becomes +y; the top
    // two bits kept.
    {
        const uint32_t word = 0x40000000u | 400u;   // x 400, y 0, z 0
        const uint32_t turned = TurnPacked(word, false, FromAngles(90, 0, 0, 1, 0, 0, 0));
        CHECK(Field10(turned, 0) == 0 && Field10(turned, 10) == 400 && Field10(turned, 20) == 0 && (turned & 0xC0000000u) == 0x40000000u);
        const uint32_t reversed = TurnPacked(uint32_t(400u) << 20, true, FromAngles(180, 0, 0, 1, 0, 0, 0));
        CHECK(Field10(reversed, 20) == -400);
    }
    printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
