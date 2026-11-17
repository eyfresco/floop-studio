// SPDX-License-Identifier: GPL-3.0-only
// The SLOOP editor protocol (SysEx over USB-MIDI), the part SLOOP Studio uses to read the FM-1's
// pattern. Reference: sloop-fm1 web/EDITOR_PROTOCOL.md (firmware/src/editor.c).
//
// Frame: F0 7D 46 4C <cmd> <args...> F7. Every request gets one reply with the same <cmd>.
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sloop::fm1 {

using Bytes = std::vector<uint8_t>;

constexpr uint8_t kHeader[3] = { 0x7D, 0x46, 0x4C };   // non-commercial ID, "FL"

enum Cmd : uint8_t
{
    INFO = 1, WATCH = 22, PING = 25, TRACK = 27, TRACK_STEP = 30, TRACK_PARAM = 31, DRUM_STEP = 33,
    // Pushes the FM-1 sends on its own while WATCH is on (never replies to a request):
    CHANGED = 23,          // scope, id, v14: a parameter of the selected track (scope 0) or a global changed
    RELOAD = 24,           // engine, preset, track: a sound, project or the selection changed
    STEP_CHANGED = 26,     // index, track: a step of the SELECTED track changed on the device
    TRACK_CHANGED = 32,    // track, id, v14: level / pan / mute of a track that is not selected
    LIVE = 77              // op [args]: the SLOOP editor's LIVE page (protocol v12, SLOOP 2.7)
};

// LIVE ops used here (firmware/src/editor_live.c; each runs the code of the FM-1's own button).
// The firmware runs an op only when the request has 2 bytes or more: an op without arguments
// (REC) needs a padding byte, else only the state comes back.
enum LiveOp : uint8_t { LiveState = 0, LivePlay = 1, LiveRec = 4 };
constexpr uint8_t kPlayToggle = 2;   // LIVE PLAY arg: 0 stop, 1 play, 2 as the PLAY button
constexpr int kLiveProtocol = 12;

// The LIVE reply: op, then the state (sloop-fm1 web/EDITOR_PROTOCOL.md "v12: LIVE").
struct LiveStatus { bool playing = false, songMode = false, recWaiting = false; int recTracks = 0, bpm = 0, selected = 0; };

inline std::optional<LiveStatus> parseLive(const std::vector<uint8_t>& a)
{
    if (a.size() < 12) return std::nullopt;
    LiveStatus s;
    s.playing = a[1] & 1;           // flags bit 0
    s.songMode = (a[1] >> 2) & 1;   // flags bit 2: song mode (REC is refused while the song plays)
    s.recWaiting = a[2] & 1;        // flags 2 bit 0: REC armed, waiting for a note
    s.bpm = (int) ((a[3] & 0x7F) | ((a[4] & 0x7F) << 7)) - 8192;
    s.recTracks = a[11] & 15;       // the tracks recording (bit per track)
    s.selected = a[12] & 3;         // the selected track (device numbering: 3 = drums)
    return s;
}

// Live sync: WATCH ends by itself 3 s after the last request, so ask something about every second.
constexpr int kWatchWithTracks = 3;   // bit 0 watch, bit 1 also TRACK_CHANGED (protocol v4)
constexpr int kPingEveryMs = 1000;

constexpr int kDeviceDrumTrack = 3;   // device tracks 0..2 = SYNTH 1..3, 3 = drums
constexpr int P_SLEN = 29;            // firmware/src/core.h: a track's pattern length
constexpr int P_SDIV = 30;            // ... and its step division
constexpr int P_MUTE = 40;

// SysEx payload without F0/F7 (what juce::MidiMessage::createSysExMessage takes).
inline Bytes request(uint8_t cmd, const Bytes& args = {})
{
    Bytes b { kHeader[0], kHeader[1], kHeader[2], cmd };
    b.insert(b.end(), args.begin(), args.end());
    return b;
}

// A received SysEx payload (without F0/F7): the command and its arguments, if it is a SLOOP frame.
inline std::optional<std::pair<uint8_t, Bytes>> parseFrame(const uint8_t* d, size_t n)
{
    if (n < 4 || d[0] != kHeader[0] || d[1] != kHeader[1] || d[2] != kHeader[2]) return std::nullopt;
    return std::make_pair(d[3], Bytes(d + 4, d + n));
}

inline int decodeV14(uint8_t lo, uint8_t hi) { return (int) ((lo & 0x7F) | ((hi & 0x7F) << 7)) - 8192; }

inline Bytes encodeV14(int value)
{
    const int x = (value < -8192 ? -8192 : value > 8191 ? 8191 : value) + 8192;
    return { (uint8_t) (x & 0x7F), (uint8_t) ((x >> 7) & 0x7F) };
}

inline void append7(Bytes& b, uint64_t v, int count)   // count × 7 bits, LSB first
{
    for (int i = 0; i < count; ++i) b.push_back((uint8_t) ((v >> (7 * i)) & 0x7F));
}

inline uint64_t decode7(const Bytes& b, size_t at, int count)   // count × 7 bits, LSB first
{
    uint64_t v = 0;
    for (int i = 0; i < count && at + (size_t) i < b.size(); ++i) v |= (uint64_t) (b[at + (size_t) i] & 0x7F) << (7 * i);
    return v;
}

struct Info
{
    std::string version;
    int nEngines = 0, pCount = 0, gCount = 0, nStep = 0, pE0 = 0, nTracks = 1, protocol = 1;
};

inline std::optional<Info> parseInfo(const Bytes& a)
{
    Info info;
    size_t p = 0;
    auto str = [&](std::string& out) {
        out.clear();
        while (p < a.size() && a[p] != 0) out += (char) a[p++];
        if (p >= a.size()) return false;
        ++p;
        return true;
    };
    if (!str(info.version) || p + 5 > a.size()) return std::nullopt;
    info.nEngines = a[p++]; info.pCount = a[p++]; info.gCount = a[p++]; info.nStep = a[p++]; info.pE0 = a[p++];
    std::string skip;
    for (int e = 0; e < info.nEngines; ++e)
        if (!str(skip)) return info;   // old firmware may end early
    if (p < a.size()) info.nTracks = a[p++];
    if (p < a.size()) info.protocol = a[p++];
    return info;
}

// The 2-bit level / roll fields of the lanes that are off (their bits must be 0 on the device).
inline uint32_t levelMaskOff(uint16_t on)
{
    uint32_t m = 0;
    for (int l = 0; l < 16; ++l)
        if (!((on >> l) & 1u)) m |= 3u << (2 * l);
    return m;
}

// DRUM_STEP reply: index, on (3 × 7 bit), lvl (5 × 7 bit), rat (5 × 7 bit).
struct DrumStep { int index = 0; uint16_t on = 0; uint32_t level = 0, roll = 0; };

inline std::optional<DrumStep> parseDrumStep(const Bytes& a)
{
    if (a.size() < 14) return std::nullopt;
    DrumStep s;
    s.index = a[0];
    s.on = (uint16_t) decode7(a, 1, 3);
    s.level = (uint32_t) decode7(a, 4, 5);
    s.roll = (uint32_t) decode7(a, 9, 5);
    return s;
}

// TRACK_STEP reply: track, index, n, note0..3, time (0 NOTE, 1 TIE, 2 REST), flags, vel, ...
struct SynthStep { int track = 0, index = 0, count = 0, time = 2; uint8_t notes[4] {}; };

inline std::optional<SynthStep> parseTrackStep(const Bytes& a)
{
    if (a.size() < 10) return std::nullopt;
    SynthStep s;
    s.track = a[0]; s.index = a[1]; s.count = a[2] > 4 ? 4 : a[2];
    for (int i = 0; i < 4; ++i) s.notes[i] = a[3 + (size_t) i] & 0x7F;
    s.time = a[7];
    return s;
}

// DRUM_STEP set: the whole step (lanes off read back with level and roll 0).
inline Bytes drumStepSet(int index, uint16_t on, uint32_t level, uint32_t roll)
{
    Bytes a { (uint8_t) index };
    append7(a, on, 3);
    append7(a, level & ~levelMaskOff(on), 5);
    append7(a, roll & ~levelMaskOff(on), 5);
    return a;
}

// TRACK_STEP set: up to 4 notes, plain NOTE (no accent / slide), level NORM x1. No notes = REST.
inline Bytes trackStepSet(int track, int index, const uint8_t* notes, int count)
{
    count = count < 0 ? 0 : count > 4 ? 4 : count;
    Bytes a { (uint8_t) track, (uint8_t) index, (uint8_t) count };
    for (int i = 0; i < 4; ++i) a.push_back(i < count ? (uint8_t) (notes[i] & 0x7F) : 0);
    a.push_back(count ? 0 : 2);   // time: NOTE, or REST
    a.push_back(0);               // flags
    a.push_back(100);             // vel
    a.push_back(0); a.push_back(0); a.push_back(0);   // lvl, hi, rat (v5)
    return a;
}

inline Bytes trackParamSet(int track, int id, int value)
{
    Bytes a { (uint8_t) track, (uint8_t) id };
    const auto v = encodeV14(value);
    a.insert(a.end(), v.begin(), v.end());
    return a;
}

// TRACK_PARAM reply: track, id, v14.
inline std::optional<int> parseTrackParam(const Bytes& a, int track, int id)
{
    if (a.size() < 4 || a[0] != track || a[1] != id) return std::nullopt;
    return decodeV14(a[2], a[3]);
}

} // namespace sloop::fm1
