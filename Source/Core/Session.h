// SPDX-License-Identifier: GPL-3.0-only
// Floop Studio — the pattern shared by every plugin instance in the process.
//
// FL Studio gives each plugin a single MIDI output port, and an instrument listening on a port
// hears every MIDI channel on it. To send each track to a different instrument (DRUMS -> FPC,
// SYNTH 1 -> FLEX...), the user loads several instances, each set to output one track. They all
// edit and play this one Session, so the pattern stays the same everywhere.
//
// Every field is an atomic: the UI (message thread) writes, the audio threads read, no locks.
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <string>

namespace sloop {

constexpr int kTracks = 4;     // 0 DRUMS, 1..3 SYNTH 1..3 (as on the FM-1, drums first in the UI)
constexpr int kDrums = 0;
constexpr int kSteps = 64;          // as SLOOP (NSTEP): a pattern of up to 64 steps (4 bars of 16ths)
constexpr int kDefaultLength = 16;
constexpr int kPageSteps = 16;      // the grid shows 16 at a time (or all 64, zoomed out)
constexpr int kLanes = 16;     // drum sounds, one per white key on SLOOP

// SLOOP's drum lanes (firmware/src/drums.c LANE_NOTE): kick, kick 2, snare, clap, hat, open hat,
// pedal, rim, snare 2, low tom, hi tom, crash, ride, shaker, conga, cowbell — as GM notes.
constexpr std::array<uint8_t, kLanes> kDefaultLaneNotes { 36, 35, 38, 39, 42, 46, 44, 37,
                                                          40, 43, 48, 49, 51, 70, 63, 56 };
constexpr std::array<const char*, kLanes> kLaneNames { "KICK", "KICK 2", "SNARE", "CLAP", "HAT",
                                                       "OPEN HAT", "PEDAL", "RIM", "SNARE 2",
                                                       "LOW TOM", "HI TOM", "CRASH", "RIDE",
                                                       "SHAKER", "CONGA", "COWBELL" };
// A drum hit's level, as on SLOOP (firmware/src/core.h LV_*; 2 bits per lane in drumLevel).
enum Level : uint8_t { LvNorm = 0, LvGhost = 1, LvSoft = 2, LvHard = 3 };
constexpr std::array<const char*, 4> kLevelNames { "NORM", "GHOST", "SOFT", "HARD" };

// The MIDI velocity of a hit: NORM plays the track's VELOCIDAD, the others are fixed.
inline uint8_t levelVelocity(int level, uint8_t normVelocity)
{
    switch (level)
    {
        case LvGhost: return 35;
        case LvSoft:  return 70;
        case LvHard:  return 127;
        default:      return normVelocity;
    }
}

constexpr std::array<const char*, kTracks> kTrackNames { "DRUMS", "SYNTH 1", "SYNTH 2", "SYNTH 3" };

struct Session
{
    // DRUMS: per step, one bit per lane.
    std::array<std::atomic<uint16_t>, kSteps> drum {};
    std::array<std::atomic<uint8_t>, kLanes> laneNote {};
    // Per step, 2 bits per lane (lane l in bits 2l..2l+1), as the FM-1 stores them:
    std::array<std::atomic<uint32_t>, kSteps> drumLevel {};   // Level
    std::array<std::atomic<uint32_t>, kSteps> drumRoll {};    // 0..3 = x1..x4 hits in the step

    // SYNTH 1..3: per step, a 128-bit set of MIDI notes (chords allowed). Index 0 is unused.
    std::array<std::array<std::atomic<uint64_t>, kSteps>, kTracks> notesLo {};
    std::array<std::array<std::atomic<uint64_t>, kSteps>, kTracks> notesHi {};

    // Per track.
    std::array<std::atomic<uint8_t>, kTracks> channel {};   // 1..16
    std::array<std::atomic<uint8_t>, kTracks> muted {};
    std::array<std::atomic<uint8_t>, kTracks> length {};    // 1..16 steps
    std::array<std::atomic<uint8_t>, kTracks> gate {};      // 5..100 % of a step
    std::array<std::atomic<uint8_t>, kTracks> velocity {};  // 1..127

    std::atomic<int> selected { kDrums };   // the track the FM-1's keys play and record into
    std::atomic<bool> recording { false };
    std::atomic<bool> hostPlaying { false };    // FL's transport, as the audio threads last saw it
    std::atomic<bool> fm1DrivesFl { false };    // the FM-1's PLAY / STOP start / stop FL (FM1Live bridge)
    std::atomic<uint32_t> revision { 0 };   // bumped on every edit, so every open UI repaints

    Session() { reset(); }

    static Session& shared()
    {
        static Session s;
        return s;
    }

    void reset()
    {
        for (int s = 0; s < kSteps; ++s)
        {
            drum[s] = 0; drumLevel[s] = 0; drumRoll[s] = 0;
            for (int t = 0; t < kTracks; ++t) { notesLo[t][s] = 0; notesHi[t][s] = 0; }
        }
        for (int l = 0; l < kLanes; ++l) laneNote[l] = kDefaultLaneNotes[l];
        for (int t = 0; t < kTracks; ++t)
        {
            channel[t] = (uint8_t) (t == kDrums ? 10 : t);
            muted[t] = 0;
            length[t] = kDefaultLength;
            gate[t] = t == kDrums ? 25 : 80;
            velocity[t] = 100;
        }
        touch();
    }

    void touch() { revision.fetch_add(1, std::memory_order_relaxed); }

    // --- drums ---
    bool drumOn(int step, int lane) const { return (drum[step].load() >> lane) & 1u; }
    // A new hit (or a cleared one) starts at NORM, x1.
    void setDrum(int step, int lane, bool on)
    {
        const uint16_t bit = (uint16_t) (1u << lane);
        const uint32_t two = ~(3u << (2 * lane));
        drumLevel[step].fetch_and(two);
        drumRoll[step].fetch_and(two);
        if (on) drum[step].fetch_or(bit); else drum[step].fetch_and((uint16_t) ~bit);
        touch();
    }
    int laneLevel(int step, int lane) const { return (int) ((drumLevel[step].load() >> (2 * lane)) & 3u); }
    int laneRoll(int step, int lane) const { return (int) ((drumRoll[step].load() >> (2 * lane)) & 3u); }   // 0..3 = x1..x4
    void setLaneLevel(int step, int lane, int level) { set2(drumLevel[step], lane, level); touch(); }
    void setLaneRoll(int step, int lane, int roll) { set2(drumRoll[step], lane, roll); touch(); }

    // --- synths ---
    bool noteOn(int track, int step, int note) const
    {
        if (note < 0 || note > 127) return false;
        const auto& w = note < 64 ? notesLo[track][step] : notesHi[track][step];
        return (w.load() >> (note & 63)) & 1u;
    }
    void setNote(int track, int step, int note, bool on)
    {
        if (note < 0 || note > 127) return;
        auto& w = note < 64 ? notesLo[track][step] : notesHi[track][step];
        const uint64_t bit = uint64_t { 1 } << (note & 63);
        if (on) w.fetch_or(bit); else w.fetch_and(~bit);
        touch();
    }

    void clearTrack(int track)
    {
        for (int s = 0; s < kSteps; ++s)
        {
            if (track == kDrums) { drum[s] = 0; drumLevel[s] = 0; drumRoll[s] = 0; }
            else { notesLo[track][s] = 0; notesHi[track][s] = 0; }
        }
        touch();
    }

    int stepIndex(int track, int64_t absoluteStep) const
    {
        const int len = length[track].load() < 1 ? 1 : length[track].load();
        const int64_t m = absoluteStep % len;
        return (int) (m < 0 ? m + len : m);
    }

    // --- persistence: a compact text form the plugin stores in the FL project ---
    std::string serialise() const
    {
        std::string out = "SLOOP3";   // SLOOP3: 64 steps; SLOOP1 / SLOOP2: 16
        auto hex = [&out](uint64_t v, int digits) {
            static const char* d = "0123456789abcdef";
            for (int i = digits - 1; i >= 0; --i) out += d[(v >> (i * 4)) & 15u];
        };
        for (int s = 0; s < kSteps; ++s) hex(drum[s].load(), 4);
        for (int l = 0; l < kLanes; ++l) hex(laneNote[l].load(), 2);
        for (int t = 1; t < kTracks; ++t)
            for (int s = 0; s < kSteps; ++s) { hex(notesLo[t][s].load(), 16); hex(notesHi[t][s].load(), 16); }
        for (int t = 0; t < kTracks; ++t)
        {
            hex(channel[t].load(), 2); hex(muted[t].load(), 2); hex(length[t].load(), 2);
            hex(gate[t].load(), 2); hex(velocity[t].load(), 2);
        }
        hex((uint64_t) selected.load(), 2);
        for (int st = 0; st < kSteps; ++st) { hex(drumLevel[st].load(), 8); hex(drumRoll[st].load(), 8); }   // SLOOP2
        return out;
    }

    bool deserialise(const std::string& in)
    {
        // SLOOP1 (prototype 1): 16 steps, no levels / rolls; SLOOP2: 16 steps; SLOOP3: 64 steps
        const bool v3 = in.rfind("SLOOP3", 0) == 0, v2 = v3 || in.rfind("SLOOP2", 0) == 0;
        if (!v2 && in.rfind("SLOOP1", 0) != 0) return false;
        const int n = v3 ? kSteps : 16;
        size_t p = 6;
        bool ok = true;
        auto take = [&](int digits) -> uint64_t {
            uint64_t v = 0;
            for (int i = 0; i < digits; ++i)
            {
                if (p >= in.size()) { ok = false; return 0; }
                const char c = in[p++];
                const int n = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
                if (n < 0) { ok = false; return 0; }
                v = (v << 4) | (uint64_t) n;
            }
            return v;
        };
        Session tmp;   // parse fully before touching the live pattern
        for (int s = 0; s < n; ++s) tmp.drum[s] = (uint16_t) take(4);
        for (int l = 0; l < kLanes; ++l) tmp.laneNote[l] = (uint8_t) (take(2) & 127u);
        for (int t = 1; t < kTracks; ++t)
            for (int s = 0; s < n; ++s) { tmp.notesLo[t][s] = take(16); tmp.notesHi[t][s] = take(16); }
        for (int t = 0; t < kTracks; ++t)
        {
            tmp.channel[t] = clampU8(take(2), 1, 16);
            tmp.muted[t] = take(2) ? 1 : 0;
            tmp.length[t] = clampU8(take(2), 1, kSteps);
            tmp.gate[t] = clampU8(take(2), 5, 100);
            tmp.velocity[t] = clampU8(take(2), 1, 127);
        }
        const int sel = (int) take(2);
        if (v2)
            for (int st = 0; st < n; ++st) { tmp.drumLevel[st] = (uint32_t) take(8); tmp.drumRoll[st] = (uint32_t) take(8); }
        if (!ok) return false;

        for (int s = 0; s < kSteps; ++s)
        {
            drum[s] = tmp.drum[s].load();
            drumLevel[s] = tmp.drumLevel[s].load();
            drumRoll[s] = tmp.drumRoll[s].load();
            for (int t = 1; t < kTracks; ++t) { notesLo[t][s] = tmp.notesLo[t][s].load(); notesHi[t][s] = tmp.notesHi[t][s].load(); }
        }
        for (int l = 0; l < kLanes; ++l) laneNote[l] = tmp.laneNote[l].load();
        for (int t = 0; t < kTracks; ++t)
        {
            channel[t] = tmp.channel[t].load(); muted[t] = tmp.muted[t].load(); length[t] = tmp.length[t].load();
            gate[t] = tmp.gate[t].load(); velocity[t] = tmp.velocity[t].load();
        }
        selected = sel >= 0 && sel < kTracks ? sel : kDrums;
        touch();
        return true;
    }

private:
    static void set2(std::atomic<uint32_t>& w, int lane, int v)
    {
        const uint32_t shift = (uint32_t) (2 * lane);
        uint32_t cur = w.load();
        while (!w.compare_exchange_weak(cur, (cur & ~(3u << shift)) | ((uint32_t) (v & 3) << shift))) {}
    }

    static uint8_t clampU8(uint64_t v, int lo, int hi)
    {
        return (uint8_t) ((int64_t) v < lo ? lo : (int64_t) v > hi ? hi : (int) v);
    }
};

} // namespace sloop
