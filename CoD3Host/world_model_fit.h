#pragma once
// 0.5.206: the campaign guns' third-person (world) models made to sit in a
// multiplayer player's hand the way the multiplayer gun each one copies does.
//
// Why: a campaign gun's world model is drawn on the multiplayer body in place
// of its copy's (weapon +1432, mp_fixes.cpp MakeGun). The campaign's models
// were made for the campaign's soldiers, not for multiplayer's hand tag, and
// some lie another way round or at another size (the MG42 the soldiers carry,
// w_ge_MG42_nobipod, lies along x with its barrel to -x: 0.5.900's view-model
// fit had to turn it a quarter and scale it 0.40 to match the MG34's view
// body). Bots holding it looked wrong.
//
// What: each world model's vertices are moved in place (turned, scaled,
// moved), from a copy of them as loaded. "auto" turns the campaign model so
// its longest side and barrel end point the way its copy's do (the barrel:
// the end of the long side further from the model's origin, the grip), and
// scales it to its copy's length when the two differ by more than 40% (a
// model made at another size; nearer, the real guns' own sizes are kept).
// world_models.txt beside the executable adds turns and moves by hand, and
// is read again whenever it is saved (no restart, no rebuild).
//
// Pure functions only: mp_fixes.cpp finds the meshes and writes the guest
// memory; tests/world_model_fit_test.cpp tests these.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace WorldFit
{
    enum class Mode { Auto, Manual, Off };

    // One line of world_models.txt: the gun, then "auto", "off", or seven
    // numbers (yaw pitch roll in degrees, scale, x y z forward/left/up in the
    // model's own units); "auto" may be followed by the seven numbers too,
    // done after the automatic fit.
    struct Fit
    {
        Mode mode = Mode::Auto;
        float yaw = 0.0f, pitch = 0.0f, roll = 0.0f, scale = 1.0f, x = 0.0f, y = 0.0f, z = 0.0f;
        bool extra = false;   // auto with numbers after it
        bool modeSet = false;  // a line set the mode (a "hide" line alone does not: "*"'s then)
        std::vector<std::string> hide;   // parts not drawn ("<gun> hide stand": its meshes "stand.<model>")
    };

    // A turn (rows), a scale and a move: out = R * (in * scale) + t.
    struct Transform
    {
        float r[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
        float scale = 1.0f;
        float t[3] = { 0, 0, 0 };
    };

    struct Box
    {
        float lo[3] = { 0, 0, 0 }, hi[3] = { 0, 0, 0 };
        bool valid = false;
        void Add(const float p[3])
        {
            for (int c = 0; c < 3; c++)
            {
                if (!valid || p[c] < lo[c]) lo[c] = p[c];
                if (!valid || p[c] > hi[c]) hi[c] = p[c];
            }
            valid = true;
        }
        void Add(const Box& b) { if (b.valid) { Add(b.lo); Add(b.hi); } }
    };

    inline const char* DefaultText()
    {
        return
            "# Third-person (world) models of the campaign guns: how each sits in a\n"
            "# player's hands. Read again whenever this file is saved, in the game.\n"
            "#\n"
            "#   <gun> auto                 turned and sized like the gun it copies (default)\n"
            "#   <gun> off                  as the campaign made it\n"
            "#   <gun> yaw pitch roll scale x y z\n"
            "#                              by hand, from the campaign's model: degrees\n"
            "#                              (yaw turns about up, pitch tips the muzzle,\n"
            "#                              roll about the barrel), scale, then a move\n"
            "#                              (model units, about an inch each)\n"
            "#   <gun> auto yaw pitch roll scale x y z\n"
            "#                              the automatic fit, then these on top\n"
            "#   <gun> hide <part>          a part of its model not drawn (the part's\n"
            "#                              name before the dot in the log's mesh names)\n"
            "#\n"
            "# Guns: lee_enfield bren sten fg42 fg42_scoped gewehr43 mg42\n"
            "#       lee_enfield_sniper (shares lee_enfield's model) mills_bomb\n"
            "# \"*\" sets every gun not listed. CoD3MP.log's \"world fit:\" lines say\n"
            "# what each model's box was and what was done.\n"
            "\n"
            "# auto is a guess from the models' boxes (it turned the MG42 the wrong\n"
            "# way round in 0.5.206's first test): off unless a gun asks for it.\n"
            "* off\n"
            "# The MG42 (the campaign soldiers' model): it sat about 14 above the\n"
            "# hands, reached 25 behind them, and is a fifth bigger than the guns\n"
            "# multiplayer's soldiers carry (pose test, 0.5.206). Its stand (the\n"
            "# struts under it) hung from the hands.\n"
            "mg42 0 0 0 0.8 25 0 -14\n"
            "mg42 hide stand\n";
    }

    inline std::string Lower(std::string t)
    {
        for (char& c : t) c = char(tolower(static_cast<unsigned char>(c)));
        return t;
    }

    inline std::map<std::string, Fit> Parse(const std::string& text, std::string* problems = nullptr)
    {
        std::map<std::string, Fit> fits;
        size_t at = 0;
        int number = 0;
        while (at < text.size())
        {
            size_t end = text.find('\n', at);
            if (end == std::string::npos) end = text.size();
            std::string line = text.substr(at, end - at);
            at = end + 1;
            number++;
            const size_t hash = line.find('#');
            if (hash != std::string::npos) line.resize(hash);
            char name[64] = {}, word[32] = {};
            float v[7] = { 0, 0, 0, 1, 0, 0, 0 };
            if (sscanf(line.c_str(), " %63s", name) != 1) continue;
            Fit fit;
            const char* rest = line.c_str() + line.find(name) + strlen(name);
            if (sscanf(rest, " %31s", word) == 1 && Lower(word) == "hide")
            {
                rest = strstr(rest, word) + strlen(word);
                char part[64] = {};
                Fit& existing = fits[Lower(name)];
                while (sscanf(rest, " %63[^, \t\r\n]", part) == 1)
                {
                    existing.hide.push_back(Lower(part));
                    rest = strstr(rest, part) + strlen(part);
                    while (*rest == ',' || *rest == ' ' || *rest == '\t') rest++;
                }
                continue;
            }
            if (sscanf(rest, " %31s", word) == 1 && (Lower(word) == "auto" || Lower(word) == "off"))
            {
                fit.mode = Lower(word) == "auto" ? Mode::Auto : Mode::Off;
                rest = strstr(rest, word) + strlen(word);
                const int got = sscanf(rest, " %f %f %f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6]);
                if (fit.mode == Mode::Auto && got > 0) fit.extra = true;
            }
            else
            {
                const int got = sscanf(rest, " %f %f %f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6]);
                if (got <= 0)
                {
                    if (problems) *problems += "line " + std::to_string(number) + ": \"" + name + "\" needs auto, off or numbers; ";
                    continue;
                }
                fit.mode = Mode::Manual;
            }
            fit.yaw = v[0]; fit.pitch = v[1]; fit.roll = v[2];
            fit.scale = (v[3] > 0.01f && v[3] < 100.0f) ? v[3] : 1.0f;
            fit.x = v[4]; fit.y = v[5]; fit.z = v[6];
            fit.modeSet = true;
            fit.hide = fits[Lower(name)].hide;
            fits[Lower(name)] = fit;
        }
        return fits;
    }

    inline Fit For(const std::map<std::string, Fit>& fits, const std::string& gun)
    {
        Fit fit;
        const auto all = fits.find("*");
        if (all != fits.end()) fit = all->second;
        fit.hide.clear();
        const auto found = fits.find(Lower(gun));
        if (found != fits.end())
        {
            if (found->second.modeSet) fit = found->second;
            fit.hide = found->second.hide;
        }
        return fit;
    }

    inline void Multiply(const float a[3][3], const float b[3][3], float out[3][3])
    {
        float m[3][3];
        for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) m[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
        memcpy(out, m, sizeof(m));
    }

    // yaw about z (up), pitch about y (muzzle up for a gun along +x: negative
    // pitch about y lifts +x; here pitch > 0 lifts it), roll about x.
    inline Transform FromAngles(float yaw, float pitch, float roll, float scale, float x, float y, float z)
    {
        const float d = 3.14159265358979f / 180.0f;
        const float cy = cosf(yaw * d), sy = sinf(yaw * d), cp = cosf(pitch * d), sp = sinf(pitch * d), cr = cosf(roll * d), sr = sinf(roll * d);
        const float rz[3][3] = { { cy, -sy, 0 }, { sy, cy, 0 }, { 0, 0, 1 } };
        const float ry[3][3] = { { cp, 0, -sp }, { 0, 1, 0 }, { sp, 0, cp } };
        const float rx[3][3] = { { 1, 0, 0 }, { 0, cr, -sr }, { 0, sr, cr } };
        Transform t;
        float m[3][3];
        Multiply(rz, ry, m);
        Multiply(m, rx, t.r);
        t.scale = scale;
        t.t[0] = x; t.t[1] = y; t.t[2] = z;
        return t;
    }

    // a then b: out = b(a(in))
    inline Transform Then(const Transform& a, const Transform& b)
    {
        Transform t;
        Multiply(b.r, a.r, t.r);
        t.scale = a.scale * b.scale;
        for (int i = 0; i < 3; i++)
            t.t[i] = b.r[i][0] * a.t[0] * b.scale + b.r[i][1] * a.t[1] * b.scale + b.r[i][2] * a.t[2] * b.scale + b.t[i];
        return t;
    }

    inline void Point(const Transform& t, const float in[3], float out[3])
    {
        float p[3];
        for (int i = 0; i < 3; i++) p[i] = (t.r[i][0] * in[0] + t.r[i][1] * in[1] + t.r[i][2] * in[2]) * t.scale + t.t[i];
        memcpy(out, p, sizeof(p));
    }

    inline bool Identity(const Transform& t)
    {
        for (int i = 0; i < 3; i++)
        {
            if (fabsf(t.t[i]) > 1e-4f) return false;
            for (int j = 0; j < 3; j++) if (fabsf(t.r[i][j] - (i == j ? 1.0f : 0.0f)) > 1e-4f) return false;
        }
        return fabsf(t.scale - 1.0f) < 1e-4f;
    }

    inline std::string Describe(const Transform& t)
    {
        char text[256];
        snprintf(text, sizeof(text), "[%.3f %.3f %.3f | %.3f %.3f %.3f | %.3f %.3f %.3f] x%.3f +(%.2f %.2f %.2f)",
                 t.r[0][0], t.r[0][1], t.r[0][2], t.r[1][0], t.r[1][1], t.r[1][2], t.r[2][0], t.r[2][1], t.r[2][2], t.scale, t.t[0], t.t[1], t.t[2]);
        return text;
    }

    // The long side of a box (0 x, 1 y, 2 z) and which way its barrel end
    // lies (+1, -1; 0 when the two ends are within 15% of each other from
    // the origin, so it can't be told).
    inline void LongSide(const Box& b, int& axis, int& sign)
    {
        axis = 0;
        for (int c = 1; c < 3; c++) if (b.hi[c] - b.lo[c] > b.hi[axis] - b.lo[axis]) axis = c;
        const float plus = fabsf(b.hi[axis]), minus = fabsf(b.lo[axis]);
        const float most = plus > minus ? plus : minus;
        sign = most <= 0.0f || fabsf(plus - minus) < 0.15f * most ? 0 : (plus > minus ? 1 : -1);
    }

    // The turn taking unit axis u to unit axis v (both along x, y or z).
    inline void AxisTurn(const float u[3], const float v[3], float r[3][3])
    {
        const float dot = u[0] * v[0] + u[1] * v[1] + u[2] * v[2];
        for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) r[i][j] = i == j ? 1.0f : 0.0f;
        if (dot > 0.5f) return;
        if (dot < -0.5f)
        {   // half a turn about up (or about x, for a model standing on end)
            float a[3] = { 0, 0, 1 };
            if (fabsf(u[2]) > 0.5f) { a[0] = 1; a[2] = 0; }
            for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) r[i][j] = 2.0f * a[i] * a[j] - (i == j ? 1.0f : 0.0f);
            return;
        }
        // a quarter about w = u x v: R = I + K + K^2
        const float w[3] = { u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0] };
        const float k[3][3] = { { 0, -w[2], w[1] }, { w[2], 0, -w[0] }, { -w[1], w[0], 0 } };
        float k2[3][3];
        Multiply(k, k, k2);
        for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) r[i][j] = (i == j ? 1.0f : 0.0f) + k[i][j] + k2[i][j];
    }

    // The automatic fit of a campaign model (box "own") to its copy's.
    inline Transform Auto(const Box& own, const Box& copy, std::string* why = nullptr)
    {
        Transform t;
        if (!own.valid || !copy.valid) { if (why) *why = "no box"; return t; }
        int ownAxis, ownSign, copyAxis, copySign;
        LongSide(own, ownAxis, ownSign);
        LongSide(copy, copyAxis, copySign);
        static const char* names[] = { "x", "y", "z" };
        char text[512];
        // When either end can't be told: only a change of side is made (the
        // barrel assumed toward + on both), and no half turns.
        if (ownSign == 0 || copySign == 0)
        {
            if (ownAxis == copyAxis) { ownSign = copySign = 1; }
            else { if (ownSign == 0) ownSign = 1; if (copySign == 0) copySign = 1; }
        }
        float u[3] = { 0, 0, 0 }, v[3] = { 0, 0, 0 };
        u[ownAxis] = float(ownSign);
        v[copyAxis] = float(copySign);
        AxisTurn(u, v, t.r);
        const float ownLength = own.hi[ownAxis] - own.lo[ownAxis], copyLength = copy.hi[copyAxis] - copy.lo[copyAxis];
        const float ratio = ownLength > 0.01f ? copyLength / ownLength : 1.0f;
        if (ratio < 0.7f || ratio > 1.0f / 0.7f) t.scale = ratio;
        // Where the hand is: a multiplayer world model's origin is its grip
        // (the hand tag holds it there). Where the origin lies within each
        // box, as a fraction of the box on each side (0 one end, 1 the
        // other), is compared after the turn; a campaign model whose origin
        // is somewhere else (off the gun, or toward its muzzle or butt) is
        // moved so its origin lies where the copy's does: along the length
        // and across by fraction, up and down by fraction too. A model whose
        // origin is already near (within 20% of its length, 35% across and
        // up) is not moved: the guns differ in shape (the Bren's magazine on
        // top, the FG42's at the side), so a box can't place them closer.
        Box turned;
        for (int corner = 0; corner < 8; corner++)
        {
            const float p[3] = { (corner & 1) ? own.hi[0] : own.lo[0], (corner & 2) ? own.hi[1] : own.lo[1], (corner & 4) ? own.hi[2] : own.lo[2] };
            float q[3];
            Point(t, p, q);
            turned.Add(q);
        }
        float shift[3] = { 0, 0, 0 };
        bool moved = false;
        char moves[160] = "";
        for (int c = 0; c < 3; c++)
        {
            const float span = turned.hi[c] - turned.lo[c], copySpan = copy.hi[c] - copy.lo[c];
            if (span < 0.01f || copySpan < 0.01f) continue;
            const float ownAt = -turned.lo[c] / span, copyAt = -copy.lo[c] / copySpan;
            const float limit = c == copyAxis ? 0.20f : 0.35f;
            if (fabsf(ownAt - copyAt) > limit)
            {
                shift[c] = -(turned.lo[c] + copyAt * span);
                moved = true;
            }
            char one[56];
            snprintf(one, sizeof(one), " %s %.2f/%.2f", names[c], ownAt, copyAt);
            strncat(moves, one, sizeof(moves) - strlen(moves) - 1);
        }
        if (moved) for (int c = 0; c < 3; c++) t.t[c] = shift[c];
        snprintf(text, sizeof(text), "barrel %s%s (length %.1f) -> %s%s (length %.1f): %s, scale %.2f%s; grip at (own/copy)%s: %s %.1f %.1f %.1f",
                 ownSign > 0 ? "+" : "-", names[ownAxis], ownLength, copySign > 0 ? "+" : "-", names[copyAxis], copyLength,
                 (t.r[0][0] > 0.999f && t.r[1][1] > 0.999f && t.r[2][2] > 0.999f) ? "same way round" : "turned", t.scale,
                 t.scale == 1.0f && ratio != 1.0f ? " (sizes near enough, kept)" : "", moves, moved ? "moved by" : "not moved", shift[0], shift[1], shift[2]);
        if (why) *why = text;
        return t;
    }

    inline Transform Make(const Fit& fit, const Box& own, const Box& copy, std::string* why = nullptr)
    {
        if (fit.mode == Mode::Off) { if (why) *why = "off"; return Transform{}; }
        const Transform hand = FromAngles(fit.yaw, fit.pitch, fit.roll, fit.scale, fit.x, fit.y, fit.z);
        if (fit.mode == Mode::Manual) { if (why) *why = "by hand"; return hand; }
        const Transform automatic = Auto(own, copy, why);
        if (!fit.extra) return automatic;
        if (why) *why += ", then by hand";
        return Then(automatic, hand);
    }

    // A packed 10:10:10 direction (normal, tangent, binormal), turned.
    // reversed: z in the low bits, x in the high (mp_fixes.cpp tells which).
    inline int Field10(uint32_t word, int shift) { int v = int((word >> shift) & 0x3FF); return v >= 512 ? v - 1024 : v; }
    inline uint32_t TurnPacked(uint32_t word, bool reversed, const Transform& t)
    {
        const int xs = reversed ? 20 : 0, zs = reversed ? 0 : 20;
        const float in[3] = { float(Field10(word, xs)), float(Field10(word, 10)), float(Field10(word, zs)) };
        int out[3];
        for (int i = 0; i < 3; i++)
        {
            const float f = t.r[i][0] * in[0] + t.r[i][1] * in[1] + t.r[i][2] * in[2];
            int n = int(lroundf(f));
            out[i] = n > 511 ? 511 : n < -511 ? -511 : n;
        }
        return (word & 0xC0000000u) | (uint32_t(out[0] & 0x3FF) << xs) | (uint32_t(out[1] & 0x3FF) << 10) | (uint32_t(out[2] & 0x3FF) << zs);
    }
}
