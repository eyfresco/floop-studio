// SPDX-License-Identifier: GPL-3.0-only
// A track's sound on the FM-1, as Floop Studio shows and edits it: the parameter pages (the FM-1's
// own, firmware/src/params.c PAGES), what DESC says about each parameter, and its value shown the
// way the FM-1 shows it (params.c fmt_val, with the curves of tools/gen_tables.py).
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace sloop::snd {

// Parameter ids (firmware/src/core.h, SLOOP 2.6 and later: P_COUNT 71, P_E0 63).
enum P : int
{
    P_LEVEL = 0, P_ATK, P_DEC, P_SUS, P_REL, P_ED_FLT, P_ED_PIT, P_ED_SHP, P_ED_FX,
    P_LRATE, P_LWAVE, P_LPHASE, P_LFADE, P_LD_PIT, P_LD_FLT, P_LD_SHP, P_LD_AMP,
    P_AMODE, P_ARATE, P_AOCT, P_AGATE, P_ASWING, P_APROB, P_AHOLD, P_AORDER,
    P_ROOT, P_SCALE, P_QUANT, P_TRANS, P_SLEN, P_SDIV, P_SSWING, P_SGATE,
    P_DIST, P_CHOR, P_DLY, P_REV, P_VOICE, P_GLIDE, P_PAN, P_MUTE,
    P_GLMODE, P_PRIO, P_ALLOC, P_DETUNE, P_SLCR, P_SLPAT, P_SLRATE, P_SLDEPTH,
    P_CHORD, P_TFLT, P_STRUM, P_VLEAD, P_ITYPE, P_IA, P_IB, P_IC, P_IMIX,
    P_FTYPE, P_ESYNC, P_LSYNC, P_LTRIG, P_LPOL, P_E0, P_E7 = P_E0 + 7, P_COUNT
};
enum G : int { G_ENGSEL = 20, G_DRLVL = 25, G_DRREV = 26, G_DRDLY = 32 };
constexpr int kNone = -1;

struct Page
{
    const char* title;
    int scope;          // 0 the track's (TRACK_PARAM), 1 global (SET scope 1)
    int ids[4];         // kNone: an empty knob
    bool engine;        // EDIT 1 / 2: their titles come from NAMES, their params from the engine
};

// The pages of a synth track, in the FM-1's order (params.c PAGES; PATTERN, NOTES and the SAVE
// pages are the plugin's own grid and sounds).
inline const std::vector<Page>& synthPages()
{
    static const std::vector<Page> p {
        { "EDIT 1", 0, { P_E0, P_E0 + 1, P_E0 + 2, P_E0 + 3 }, true },
        { "EDIT 2", 0, { P_E0 + 4, P_E0 + 5, P_E0 + 6, P_E0 + 7 }, true },
        { "ENV", 0, { P_ATK, P_DEC, P_SUS, P_REL }, false },
        { "ENV DEST", 0, { P_ED_FLT, P_ED_PIT, P_ED_SHP, P_ESYNC }, false },
        { "LFO", 0, { P_LRATE, P_LWAVE, P_LPHASE, P_LFADE }, false },
        { "LFO 2", 0, { P_LSYNC, P_LTRIG, P_LPOL, kNone }, false },
        { "LFO DEST", 0, { P_LD_PIT, P_LD_FLT, P_LD_SHP, P_LD_AMP }, false },
        { "FX", 0, { P_DIST, P_CHOR, P_DLY, P_REV }, false },
        { "FILTER", 0, { P_TFLT, P_FTYPE, kNone, kNone }, false },
        { "INSERT", 0, { P_ITYPE, P_IA, P_IB, P_IC }, false },
        { "INSERT 2", 0, { P_IMIX, kNone, kNone, kNone }, false },
        { "SLICER", 0, { P_SLCR, P_SLPAT, P_SLRATE, P_SLDEPTH }, false },
        { "VOICE", 0, { P_VOICE, P_GLIDE, P_GLMODE, P_PRIO }, false },
        { "VOICE 2", 0, { P_ALLOC, P_DETUNE, P_PAN, P_LEVEL }, false },
        { "ARP", 0, { P_AMODE, P_ARATE, P_AOCT, P_AGATE }, false },
        { "ARP 2", 0, { P_ASWING, P_APROB, P_AHOLD, P_AORDER }, false },
        { "SEL", 0, { P_ROOT, P_SCALE, P_QUANT, P_CHORD }, false },
        { "SEL 2", 0, { P_TRANS, P_STRUM, P_VLEAD, kNone }, false },
    };
    return p;
}

// The drum track: the kit, its filter, insert and slicer, and GLO > DRUMS (params.c page_for_drum).
inline const std::vector<Page>& drumPages()
{
    static const std::vector<Page> p {
        { "KIT", 0, { P_E0, P_PAN, kNone, kNone }, false },
        { "DRUMS", 1, { G_DRLVL, G_DRREV, G_DRDLY, kNone }, false },
        { "FILTER", 0, { P_TFLT, kNone, kNone, kNone }, false },
        { "INSERT", 0, { P_ITYPE, P_IA, P_IB, P_IC }, false },
        { "INSERT 2", 0, { P_IMIX, kNone, kNone, kNone }, false },
        { "SLICER", 0, { P_SLCR, P_SLPAT, P_SLRATE, P_SLDEPTH }, false },
    };
    return p;
}

// --- DESC -----------------------------------------------------------------------------------

enum Fmt { F_INT, F_PCT, F_BIPCT, F_TIME, F_LFOHZ, F_CUTOFF, F_DB, F_SEMI, F_ENUM, F_BPM, F_NOTE, F_ONOFF, F_OCT,
           F_STEPS, F_SWING, F_FILT };

struct Desc
{
    int scope = 0, id = 0, fmt = F_INT, min = 0, max = 127, def = 0;
    std::string label, unit;
    std::vector<std::string> names;   // an enum's values
    bool fixed() const { return min == max; }
};

inline int v14(const std::vector<uint8_t>& a, size_t at) { return (int) ((a[at] & 0x7F) | ((a[at + 1] & 0x7F) << 7)) - 8192; }

// DESC reply: scope, id, fmt, min v14, max v14, def v14, label, unit, then an enum's names.
inline std::optional<Desc> parseDesc(const std::vector<uint8_t>& a)
{
    if (a.size() < 11) return std::nullopt;
    Desc d;
    d.scope = a[0]; d.id = a[1]; d.fmt = a[2];
    d.min = v14(a, 3); d.max = v14(a, 5); d.def = v14(a, 7);
    size_t i = 9;
    auto str = [&](std::string& s) { s.clear(); while (i < a.size() && a[i] != 0) s += (char) a[i++]; if (i < a.size()) ++i; };
    str(d.label);
    str(d.unit);
    while (i < a.size()) { std::string n; str(n); d.names.push_back(n); }
    return d;
}

// DUMP reply: engine, preset, then P_COUNT x v14 (the selected track), then G_COUNT x v14.
struct Dump { int engine = 0, preset = 0; std::vector<int> params, globals; };

inline std::optional<Dump> parseDump(const std::vector<uint8_t>& a, int pCount, int gCount)
{
    if ((int) a.size() < 2 + 2 * pCount) return std::nullopt;
    Dump d;
    d.engine = a[0]; d.preset = a[1];
    for (int i = 0; i < pCount; ++i) d.params.push_back(v14(a, 2 + 2 * (size_t) i));
    for (int i = 0; i < gCount && 2 + 2 * (size_t) (pCount + i) + 1 < a.size(); ++i) d.globals.push_back(v14(a, 2 + 2 * (size_t) (pCount + i)));
    return d;
}

// NAMES reply: engine, count, the preset names, then the two edit-page titles.
struct Names { int engine = 0; std::vector<std::string> presets; std::string edit1 = "EDIT 1", edit2 = "EDIT 2"; };

inline std::optional<Names> parseNames(const std::vector<uint8_t>& a)
{
    if (a.size() < 2) return std::nullopt;
    Names n;
    n.engine = a[0];
    const int count = a[1];
    size_t i = 2;
    auto str = [&]() { std::string s; while (i < a.size() && a[i] != 0) s += (char) a[i++]; if (i < a.size()) ++i; return s; };
    for (int k = 0; k < count && i < a.size(); ++k) n.presets.push_back(str());
    if (i < a.size()) n.edit1 = str();
    if (i < a.size()) n.edit2 = str();
    return n;
}

// UP_LIST reply: start, count, total, then per slot: used, engine, name (the 32 user presets).
struct UserPreset { bool used = false; int engine = 0; std::string name; };

inline std::optional<std::vector<UserPreset>> parseUserList(const std::vector<uint8_t>& a, int& total)
{
    if (a.size() < 3) return std::nullopt;
    const int count = a[1];
    total = a[2];
    std::vector<UserPreset> out;
    size_t i = 3;
    for (int k = 0; k < count && i + 2 <= a.size(); ++k)
    {
        UserPreset u;
        u.used = a[i++] != 0;
        u.engine = a[i++];
        while (i < a.size() && a[i] != 0) u.name += (char) a[i++];
        if (i < a.size()) ++i;
        out.push_back(u);
    }
    return out;
}

// --- the value as the FM-1 shows it (params.c fmt_val) ---------------------------------------

inline std::string fixed(double v, int decimals)
{
    char b[32];
    std::snprintf(b, sizeof b, "%.*f", decimals, v);
    return b;
}

inline std::string formatValue(const Desc& d, int v)
{
    static const char* notes[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const double t = (v & 127) / 127.0;
    switch (d.fmt)
    {
        case F_PCT: return std::to_string(d.max ? (int) std::lround(v * 100.0 / d.max) : v) + " %";
        case F_BIPCT: { const int p = v * 100 / 64; return (p > 0 ? "+" : "") + std::to_string(p) + " %"; }
        case F_TIME:
        {
            const double ms = std::pow(10000.0, t);   // 1 ms .. 10 s
            if (ms < 10.0) return fixed(ms, 1) + " ms";
            if (ms < 1000.0) return std::to_string((int) std::lround(ms)) + " ms";
            return fixed(ms / 1000.0, ms >= 10000.0 ? 1 : 2) + " s";
        }
        case F_LFOHZ:
        {
            const double hz = 0.05 * std::pow(800.0, t);   // 0.05 .. 40 Hz
            return fixed(hz, hz < 10.0 ? 2 : 1) + " Hz";
        }
        case F_CUTOFF:
        {
            const double hz = 30.0 * std::pow(16000.0 / 30.0, t);   // 30 Hz .. 16 kHz
            if (hz < 1000.0) return std::to_string((int) std::lround(hz)) + " Hz";
            return fixed(hz / 1000.0, hz < 10000.0 ? 1 : 0) + " kHz";
        }
        case F_DB: return v <= 0 ? "OFF" : fixed((v - 112) / 2.0, 1) + " dB";   // 112 = 0 dB
        case F_SEMI: return (v > 0 ? "+" : "") + std::to_string(v) + " st";
        case F_ENUM:
        {
            const int k = v - d.min;
            if (k >= 0 && k < (int) d.names.size()) return d.names[(size_t) k] + (d.unit.empty() ? "" : " " + d.unit);
            return std::to_string(v);
        }
        case F_BPM: return std::to_string(v) + " BPM";
        case F_NOTE: return notes[((v % 12) + 12) % 12];
        case F_ONOFF: return v ? "ON" : "OFF";
        case F_OCT: return (v > 0 ? "+" : "") + std::to_string(v) + " oct";
        case F_STEPS: return std::to_string(v) + " pasos";
        case F_SWING: return std::to_string(50 + v / 4) + " %";
        case F_FILT:
            if (!v) return "OFF";
            return std::string(v < 0 ? "LP " : "HP ") + std::to_string(v < 0 ? (-v * 100 + 32) / 64 : (v * 100 + 31) / 63) + " %";
        default: return std::to_string(v) + (d.unit.empty() ? "" : " " + d.unit);
    }
}

} // namespace sloop::snd
