// SPDX-License-Identifier: GPL-3.0-only
// SLOOP Studio — turns the shared Session into MIDI notes, sample-accurate, following FL's transport.
//
// One step = a 16th note. The pattern position comes from the host's song position (PPQ), so a
// step always lands on FL's grid, also after a loop, a jump or a tempo change. No allocation, no
// locks: process() runs on the audio thread.
#pragma once
#include "Session.h"
#include <cmath>
#include <cstdint>

namespace sloop {

struct Transport
{
    bool playing = false;
    double ppq = 0.0;     // song position at the first sample of the block, in quarter notes
    double bpm = 120.0;
};

struct MidiEvent
{
    int sample;
    uint8_t status, data1, data2;
};

// Collects the events of one block (fixed capacity: no allocation on the audio thread).
struct EventList
{
    static constexpr int kCapacity = 2048;
    MidiEvent items[kCapacity];
    int count = 0;
    void clear() { count = 0; }
    void add(int sample, uint8_t s, uint8_t d1, uint8_t d2)
    {
        if (count < kCapacity) items[count++] = { sample, s, d1, d2 };
    }
};

class SeqEngine
{
public:
    static constexpr double kStepPpq = 0.25;
    // Each step is cut into 12 ticks so that x2, x3 and x4 rolls land exactly (12 = lcm of 1..4).
    static constexpr int kTicksPerStep = 12;
    static constexpr double kTickPpq = kStepPpq / kTicksPerStep;

    // trackMask: bit t = this instance outputs track t.
    void process(const Session& s, const Transport& t, int numSamples, double sampleRate,
                 uint32_t trackMask, EventList& out)
    {
        if (numSamples <= 0 || sampleRate <= 0.0) return;

        if (!t.playing || t.bpm <= 0.0)
        {
            if (wasPlaying) allNotesOff(0, out);
            wasPlaying = false;
            for (auto& c : current) c = -1;
            return;
        }

        const double samplesPerPpq = sampleRate * 60.0 / t.bpm;
        const double startPpq = t.ppq;
        const double endPpq = startPpq + numSamples / samplesPerPpq;
        auto sampleAt = [&](double ppq) {
            const double x = std::floor((ppq - startPpq) * samplesPerPpq + 1e-6);
            return x < 0 ? 0 : x > numSamples - 1 ? numSamples - 1 : (int) x;
        };

        // A jump (loop back, FL pattern restart, click on the timeline): close what sounds and
        // restart the step count from the new position.
        const bool jumped = wasPlaying && std::fabs(startPpq - lastEndPpq) > 1e-3;
        if (!wasPlaying || jumped)
        {
            allNotesOff(0, out);
            lastTick = (int64_t) std::ceil(startPpq * kTicksPerStep / kStepPpq - 1e-6) - 1;
        }

        // Ticks in this block: a step starts on tick 0 of 12, a roll's later hits on 3, 4, 6 or 8.
        // The note-offs that fall before a tick go out first (a hit also closes a still-sounding
        // copy of the same note, so retriggers never stick).
        // tick * 0.25 / 12 (not tick * (0.25 / 12)): exact on every step start, so a step never
        // slips into the previous block by a rounding error.
        auto ppqOf = [](int64_t tick) { return (double) tick * kStepPpq / kTicksPerStep; };
        for (int64_t tick = lastTick + 1; ppqOf(tick) < endPpq; ++tick)
        {
            const double tickPpq = ppqOf(tick);
            const int at = sampleAt(tickPpq);
            emitOffsetsBefore(tickPpq, sampleAt, out);
            const int64_t step = floorDiv(tick, kTicksPerStep);
            const int sub = (int) (tick - step * kTicksPerStep);
            for (int tr = 0; tr < kTracks; ++tr)
            {
                current[tr] = s.stepIndex(tr, step);
                if (!((trackMask >> tr) & 1u) || s.muted[tr].load()) continue;
                const uint8_t ch = (uint8_t) ((s.channel[tr].load() - 1) & 15);
                const uint8_t vel = s.velocity[tr].load();
                const double gate = kStepPpq * s.gate[tr].load() / 100.0;
                const int idx = s.stepIndex(tr, step);
                if (tr == kDrums)
                {
                    const uint16_t lanes = s.drum[idx].load();
                    if (!lanes) continue;
                    const uint32_t levels = s.drumLevel[idx].load(), rolls = s.drumRoll[idx].load();
                    for (int l = 0; l < kLanes; ++l)
                    {
                        if (!((lanes >> l) & 1u)) continue;
                        const int hits = (int) ((rolls >> (2 * l)) & 3u) + 1;
                        const int every = kTicksPerStep / hits;
                        if (sub % every) continue;
                        const double len = gate < kStepPpq / hits ? gate : kStepPpq / hits;
                        noteOn(at, ch, s.laneNote[l].load(), levelVelocity((int) ((levels >> (2 * l)) & 3u), vel),
                               tickPpq + len, out);
                    }
                }
                else if (sub == 0)
                {
                    const uint64_t words[2] = { s.notesLo[tr][idx].load(), s.notesHi[tr][idx].load() };
                    for (int w = 0; w < 2; ++w)
                        for (uint64_t bits = words[w]; bits; bits &= bits - 1)
                            noteOn(at, ch, (uint8_t) (w * 64 + ctz(bits)), vel, tickPpq + gate, out);
                }
            }
            lastTick = tick;
        }
        emitOffsetsBefore(endPpq, sampleAt, out);

        lastEndPpq = endPpq;
        wasPlaying = true;
    }

    // Closes every note this engine started (stop, bypass, output change).
    void allNotesOff(int sample, EventList& out)
    {
        for (auto& p : pending)
            if (p.active) { out.add(sample, (uint8_t) (0x80 | p.ch), p.note, 0); p.active = false; }
    }

    int currentStep(int track) const { return current[track]; }

private:
    struct Pending { bool active = false; double offPpq = 0; uint8_t ch = 0, note = 0; };
    static constexpr int kMaxPending = 512;
    Pending pending[kMaxPending];

    bool wasPlaying = false;
    double lastEndPpq = 0;
    int64_t lastTick = -1;
    int current[kTracks] { -1, -1, -1, -1 };

    static int64_t floorDiv(int64_t a, int64_t b) { const int64_t q = a / b; return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q; }

    static int ctz(uint64_t v)
    {
        int n = 0;
        while (!(v & 1u)) { v >>= 1; ++n; }
        return n;
    }

    template <typename SampleAt>
    void emitOffsetsBefore(double ppq, SampleAt sampleAt, EventList& out)
    {
        for (auto& p : pending)
            if (p.active && p.offPpq < ppq - 1e-9)
            {
                out.add(sampleAt(p.offPpq), (uint8_t) (0x80 | p.ch), p.note, 0);
                p.active = false;
            }
    }

    void noteOn(int at, uint8_t ch, uint8_t note, uint8_t vel, double offPpq, EventList& out)
    {
        Pending* slot = nullptr;
        for (auto& p : pending)
        {
            if (p.active && p.ch == ch && p.note == note)
            {
                out.add(at, (uint8_t) (0x80 | ch), note, 0);   // retrigger: close it first
                p.active = false;
            }
            if (!p.active && !slot) slot = &p;
        }
        if (!slot) return;   // more than kMaxPending notes at once: drop the note rather than hang one
        out.add(at, (uint8_t) (0x90 | ch), note, vel);
        *slot = { true, offPpq, ch, note };
    }
};

} // namespace sloop
