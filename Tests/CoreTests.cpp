// SPDX-License-Identifier: GPL-3.0-only
// Tests for the sequencer core: timing against the host transport, block boundaries, loops,
// stop, mutes, routing masks and the saved state.
#include "SeqEngine.h"
#include "Fm1Protocol.h"
#include "ClockEngine.h"
#include "SoundModel.h"
#include <cstdio>
#include <vector>

using namespace sloop;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

struct Abs { int64_t sample; uint8_t status, d1, d2; };

// Plays `blocks` blocks of `block` samples from ppq `start`, returning events in absolute samples.
static std::vector<Abs> run(Session& s, SeqEngine& e, double start, int blocks, int block,
                            uint32_t mask = 0xF, double sr = 48000, double bpm = 120)
{
    std::vector<Abs> all;
    EventList out;
    const double ppqPerSample = bpm / 60.0 / sr;
    for (int b = 0; b < blocks; ++b)
    {
        out.clear();
        Transport t { true, start + (double) b * block * ppqPerSample, bpm };
        e.process(s, t, block, sr, mask, out);
        for (int i = 0; i < out.count; ++i)
            all.push_back({ (int64_t) b * block + out.items[i].sample, out.items[i].status, out.items[i].data1, out.items[i].data2 });
    }
    return all;
}

static int countOn(const std::vector<Abs>& v, uint8_t note = 0xFF)
{
    int n = 0;
    for (auto& e : v) if ((e.status & 0xF0) == 0x90 && (note == 0xFF || e.d1 == note)) ++n;
    return n;
}
static int countOff(const std::vector<Abs>& v)
{
    int n = 0;
    for (auto& e : v) if ((e.status & 0xF0) == 0x80) ++n;
    return n;
}

int main()
{
    // 120 BPM at 48 kHz: one 16th = 6000 samples.
    {   // four-on-the-floor lands exactly on the grid, whatever the block size
        for (int block : { 64, 100, 512, 997, 4096 })
        {
            Session s; SeqEngine e;
            for (int st : { 0, 4, 8, 12 }) s.setDrum(st, 0, true);
            auto ev = run(s, e, 0.0, (int) (96000 / block), block);   // up to 2 s = one bar (not the next downbeat)
            std::vector<int64_t> ons;
            for (auto& x : ev) if ((x.status & 0xF0) == 0x90) ons.push_back(x.sample);
            CHECK(ons.size() == 4);
            for (size_t i = 0; i < ons.size() && i < 4; ++i) CHECK(ons[i] == (int64_t) i * 24000);
            for (auto& x : ev) if ((x.status & 0xF0) == 0x90) { CHECK(x.status == 0x99); CHECK(x.d1 == 36); CHECK(x.d2 == 100); }
        }
    }
    {   // gate: drums 25 % of a step -> off 1500 samples after the on
        Session s; SeqEngine e;
        s.setDrum(0, 2, true);
        auto ev = run(s, e, 0.0, 20, 512);
        CHECK(countOn(ev, 38) == 1);
        CHECK(countOff(ev) == 1);
        int64_t on = -1, off = -1;
        for (auto& x : ev) { if ((x.status & 0xF0) == 0x90) on = x.sample; else off = x.sample; }
        CHECK(on == 0); CHECK(off == 1500);
    }
    {   // synth chord on channel 1, note-offs balanced
        Session s; SeqEngine e;
        s.setNote(1, 0, 60, true); s.setNote(1, 0, 64, true); s.setNote(1, 0, 67, true); s.setNote(1, 0, 100, true);
        auto ev = run(s, e, 0.0, 30, 480);
        CHECK(countOn(ev) == 4);
        CHECK(countOff(ev) == 4);
        for (auto& x : ev) CHECK((x.status & 0x0F) == 0);
    }
    {   // starting mid-bar: the next step, not the past ones
        Session s; SeqEngine e;
        for (int st = 0; st < 16; ++st) s.setDrum(st, 4, true);
        auto ev = run(s, e, 0.3, 1, 6000);   // ppq 0.3..0.55 -> step 2 at ppq 0.5
        CHECK(countOn(ev) == 1);
        CHECK(!ev.empty() && ev[0].sample == 4800);
    }
    {   // loop back (FL pattern loop): notes close and the bar replays
        Session s; SeqEngine e;
        s.setDrum(0, 0, true);
        s.gate[0] = 100;
        EventList out;
        Transport t { true, 3.9, 120 };
        e.process(s, t, 4800, 48000, 0xF, out);      // up to ppq 4.1: step 16 (= step 0 of bar 2)
        CHECK(out.count == 1);
        out.clear();
        t.ppq = 0.0;                                 // jump back to the start
        e.process(s, t, 512, 48000, 0xF, out);
        int ons = 0, offs = 0;
        for (int i = 0; i < out.count; ++i) ((out.items[i].status & 0xF0) == 0x90 ? ons : offs)++;
        CHECK(offs == 1); CHECK(ons == 1);
        CHECK((out.items[0].status & 0xF0) == 0x80);   // the old note closes before the new one
    }
    {   // stop closes everything
        Session s; SeqEngine e;
        s.setNote(2, 0, 48, true); s.gate[2] = 100;
        EventList out;
        e.process(s, { true, 0.0, 120 }, 256, 48000, 0xF, out);
        CHECK(out.count == 1);
        out.clear();
        e.process(s, { false, 0.01, 120 }, 256, 48000, 0xF, out);
        CHECK(out.count == 1 && (out.items[0].status & 0xF0) == 0x80);
    }
    {   // routing mask and mute
        Session s; SeqEngine e1, e2, e3;
        s.setDrum(0, 0, true); s.setNote(1, 0, 60, true);
        auto drumsOnly = run(s, e1, 0.0, 1, 512, 1u << 0);
        CHECK(countOn(drumsOnly) == 1 && countOn(drumsOnly, 36) == 1);
        auto synthOnly = run(s, e2, 0.0, 1, 512, 1u << 1);
        CHECK(countOn(synthOnly) == 1 && countOn(synthOnly, 60) == 1);
        s.muted[0] = 1;
        auto muted = run(s, e3, 0.0, 1, 512);
        CHECK(countOn(muted) == 1 && countOn(muted, 60) == 1);
    }
    {   // track length: a 3-step pattern repeats every 3 steps
        Session s; SeqEngine e;
        s.length[0] = 3; s.setDrum(0, 0, true);
        auto ev = run(s, e, 0.0, 1, 96000);   // 16 steps
        CHECK(countOn(ev) == 6);              // steps 0,3,6,9,12,15
    }
    {   // tempo change between blocks stays on the grid of the new block's PPQ
        Session s; SeqEngine e;
        for (int st = 0; st < 16; ++st) s.setDrum(st, 4, true);
        EventList out;
        e.process(s, { true, 0.0, 120 }, 6000, 48000, 0xF, out);    // step 0
        out.clear();
        e.process(s, { true, 0.25, 90 }, 8000, 48000, 0xF, out);    // at 90 BPM a 16th = 8000 samples
        int ons = 0; for (int i = 0; i < out.count; ++i) if ((out.items[i].status & 0xF0) == 0x90) { ++ons; CHECK(out.items[i].sample == 0); }
        CHECK(ons == 1);
    }
    {   // state round trip
        Session a;
        a.setDrum(3, 7, true); a.setNote(2, 15, 127, true); a.setNote(3, 1, 0, true);
        a.channel[1] = 5; a.length[2] = 7; a.gate[3] = 33; a.velocity[0] = 90; a.muted[3] = 1;
        a.laneNote[5] = 61; a.selected = 2;
        Session b;
        CHECK(b.deserialise(a.serialise()));
        CHECK(b.serialise() == a.serialise());
        CHECK(b.drumOn(3, 7) && b.noteOn(2, 15, 127) && b.noteOn(3, 1, 0));
        CHECK(b.channel[1] == 5 && b.length[2] == 7 && b.selected == 2);
        CHECK(!b.deserialise("SLOOP1zz"));
        CHECK(!b.deserialise("garbage"));
        CHECK(b.drumOn(3, 7));   // a bad load leaves the pattern as it was
    }

    {   // drum levels -> velocities; a new hit starts at NORM
        Session s; SeqEngine e;
        s.setDrum(0, 0, true); s.setLaneLevel(0, 0, LvGhost);
        s.setDrum(4, 0, true); s.setLaneLevel(4, 0, LvSoft);
        s.setDrum(8, 0, true);                                   // NORM = track velocity (100)
        s.setDrum(12, 0, true); s.setLaneLevel(12, 0, LvHard);
        auto ev = run(s, e, 0.0, 1, 95000);
        std::vector<int> vels;
        for (auto& x : ev) if ((x.status & 0xF0) == 0x90) vels.push_back(x.d2);
        CHECK((vels == std::vector<int> { 35, 70, 100, 127 }));
        s.setDrum(12, 0, false); s.setDrum(12, 0, true);
        CHECK(s.laneLevel(12, 0) == LvNorm && s.laneRoll(12, 0) == 0);
    }
    {   // rolls: x2, x3, x4 split the step evenly (a 16th = 6000 samples at 120 BPM, 48 kHz)
        for (int roll = 0; roll < 4; ++roll)
        {
            Session s; SeqEngine e;
            s.setDrum(0, 4, true); s.setLaneRoll(0, 4, roll);
            auto ev = run(s, e, 0.0, 2, 6000);   // two steps: the last roll hit closes on the next one
            std::vector<int64_t> ons;
            for (auto& x : ev) if ((x.status & 0xF0) == 0x90) ons.push_back(x.sample);
            const int hits = roll + 1;
            CHECK((int) ons.size() == hits);
            for (int k = 0; k < (int) ons.size(); ++k) CHECK(ons[(size_t) k] == (int64_t) k * 6000 / hits);
            CHECK(countOff(ev) == hits);   // every roll hit closes before the next one
        }
    }
    {   // levels and rolls survive save / load; a prototype-1 state (SLOOP1) still loads
        Session a;
        a.setDrum(2, 3, true); a.setLaneLevel(2, 3, LvHard); a.setLaneRoll(2, 3, 2);
        Session b;
        CHECK(b.deserialise(a.serialise()));
        CHECK(b.laneLevel(2, 3) == LvHard && b.laneRoll(2, 3) == 2);
        // a prototype-1 state: 16 steps, no levels / rolls (built by hand, as the old format wrote it)
        auto hex = [](uint64_t v, int digits) { std::string o; static const char* d = "0123456789abcdef";
                                                for (int i = digits - 1; i >= 0; --i) o += d[(v >> (i * 4)) & 15u]; return o; };
        std::string v1 = "SLOOP1";
        for (int st = 0; st < 16; ++st) v1 += hex(a.drum[st].load(), 4);
        for (int l = 0; l < kLanes; ++l) v1 += hex(a.laneNote[l].load(), 2);
        for (int t = 1; t < kTracks; ++t) for (int st = 0; st < 16; ++st) v1 += hex(a.notesLo[t][st].load(), 16) + hex(a.notesHi[t][st].load(), 16);
        for (int t = 0; t < kTracks; ++t) v1 += hex(a.channel[t], 2) + hex(a.muted[t], 2) + hex(a.length[t], 2) + hex(a.gate[t], 2) + hex(a.velocity[t], 2);
        v1 += hex((uint64_t) a.selected.load(), 2);
        Session c;
        CHECK(c.deserialise(v1));
        CHECK(c.drumOn(2, 3) && c.laneLevel(2, 3) == LvNorm && c.laneRoll(2, 3) == 0);
    }
    {   // 64 steps: a 32-step track plays its 32 steps, then loops; the state keeps step 64
        Session s; SeqEngine e;
        s.length[0] = 32;
        s.setDrum(0, 0, true); s.setDrum(31, 2, true); s.setDrum(63, 4, true);
        auto ev = run(s, e, 0.0, 1, 6000 * 64);   // 64 steps
        CHECK(countOn(ev, 36) == 2 && countOn(ev, 38) == 2 && countOn(ev, 42) == 0);
        s.length[0] = 64;
        SeqEngine e2;
        auto ev2 = run(s, e2, 0.0, 1, 6000 * 64);
        CHECK(countOn(ev2, 36) == 1 && countOn(ev2, 38) == 1 && countOn(ev2, 42) == 1);
        Session b;
        CHECK(b.deserialise(s.serialise()) && b.drumOn(63, 4) && b.length[0] == 64);
        CHECK(Session().length[0] == kDefaultLength);
    }
    {   // the FM-1 protocol: frames, v14, INFO, DRUM_STEP, TRACK_STEP, TRACK_PARAM
        using namespace sloop::fm1;
        const Bytes req = request(DRUM_STEP, { 5 });
        CHECK((req == Bytes { 0x7D, 0x46, 0x4C, 33, 5 }));
        const uint8_t frame[] = { 0x7D, 0x46, 0x4C, 33, 1, 2 };
        auto f = parseFrame(frame, sizeof frame);
        CHECK(f && f->first == 33 && f->second.size() == 2);
        const uint8_t other[] = { 0x7E, 0x46, 0x4C, 33 };
        CHECK(!parseFrame(other, sizeof other));
        CHECK(decodeV14(0, 64) == 0 && decodeV14(16, 64) == 16 && decodeV14(127, 127) == 8191);

        Bytes info;
        for (char c : std::string("2.8.5")) info.push_back((uint8_t) c);
        info.push_back(0);
        info.insert(info.end(), { 2, 71, 45, 16, 63 });   // NENGINES, P_COUNT, G_COUNT, NSTEP, P_E0
        for (const char* n : { "ANALOG", "FM6" }) { for (const char* c = n; *c; ++c) info.push_back((uint8_t) *c); info.push_back(0); }
        info.push_back(4); info.push_back(9);              // NTRK, protocol
        auto inf = parseInfo(info);
        CHECK(inf && inf->version == "2.8.5" && inf->nStep == 16 && inf->nTracks == 4 && inf->protocol == 9);

        // step 3: kick (lane 0) HARD x2, hat (lane 4) GHOST; lanes in 3 x 7 bits, levels/rolls in 5 x 7 bits
        const uint16_t on = (1u << 0) | (1u << 4);
        const uint32_t lvl = (3u << 0) | (1u << 8), rat = (1u << 0);
        Bytes ds { 3 };
        for (int i = 0; i < 3; ++i) ds.push_back((uint8_t) ((on >> (7 * i)) & 0x7F));
        for (int i = 0; i < 5; ++i) ds.push_back((uint8_t) ((lvl >> (7 * i)) & 0x7F));
        for (int i = 0; i < 5; ++i) ds.push_back((uint8_t) ((rat >> (7 * i)) & 0x7F));
        auto d = parseDrumStep(ds);
        CHECK(d && d->index == 3 && d->on == on && d->level == lvl && d->roll == rat);
        CHECK(!parseDrumStep(Bytes { 3, 0, 0 }));

        auto st = parseTrackStep(Bytes { 1, 7, 2, 60, 64, 0, 0, 0, 0, 100, 0, 0, 0 });
        CHECK(st && st->track == 1 && st->index == 7 && st->count == 2 && st->notes[1] == 64 && st->time == 0);

        CHECK(parseTrackParam(Bytes { 3, 29, 12, 64 }, 3, 29) == 12);

        // writes: what goes out parses back the same; off lanes carry no level / roll bits
        CHECK((encodeV14(12) == Bytes { 12, 64 }) && (encodeV14(-1) == Bytes { 127, 63 }));
        const uint32_t junk = 0xFFFFFFFFu;
        auto set = drumStepSet(3, on, junk, rat);
        auto back = parseDrumStep(set);
        CHECK(back && back->on == on && back->level == (junk & ~levelMaskOff(on)) && back->roll == rat);
        CHECK(levelMaskOff(0xFFFF) == 0 && levelMaskOff(0) == 0xFFFFFFFFu);
        const uint8_t chord[5] = { 48, 52, 55, 59, 62 };
        auto ts = trackStepSet(2, 9, chord, 5);   // the 5th note does not fit
        auto tsb = parseTrackStep(ts);
        CHECK(ts.size() == 13 && tsb && tsb->count == 4 && tsb->notes[3] == 59 && tsb->time == 0);
        auto rest = parseTrackStep(trackStepSet(0, 1, chord, 0));
        CHECK(rest && rest->count == 0 && rest->time == 2);
        CHECK((trackParamSet(3, 29, 12) == Bytes { 3, 29, 12, 64 }));
        CHECK(!parseTrackParam(Bytes { 2, 29, 12, 64 }, 3, 29));
    }

    {   // MIDI clock: FL plays from bar 1 at 120 BPM -> START at once, then 24 pulses a beat
        ClockEngine c;
        HostTransport t { true, true, 0.0, 120.0, 0.0, 4, 4 };
        int clocks = 0, starts = 0, firstStartAt = -1, stops = 0;
        for (int ms = 0; ms < 1000; ++ms)
        {
            if (ms % 10 == 0) { t.ppq = ms * 120.0 / 60000.0; t.atMs = ms; }   // a 10 ms audio block
            auto o = c.advance(ms, t);
            for (int i = 0; i < o.count; ++i)
            {
                if (o.bytes[i] == ClockEngine::kClock) ++clocks;
                if (o.bytes[i] == ClockEngine::kStart) { ++starts; if (firstStartAt < 0) firstStartAt = ms; CHECK(i + 1 < o.count && o.bytes[i + 1] == ClockEngine::kClock); }
                if (o.bytes[i] == ClockEngine::kStop) ++stops;
            }
        }
        CHECK(starts == 1 && firstStartAt == 0 && stops == 0);
        CHECK(clocks >= 47 && clocks <= 49);   // 2 beats in 1 s = 48 pulses
        // FL stops: STOP once, then free pulses at FL's tempo
        t.playing = false; t.atMs = 1000;
        auto o = c.advance(1000, t);
        CHECK(o.count >= 1 && o.bytes[0] == ClockEngine::kStop);
        int free = 0;
        for (int ms = 1001; ms < 2000; ++ms) { t.atMs = ms; auto f = c.advance(ms, t); for (int i = 0; i < f.count; ++i) free += f.bytes[i] == ClockEngine::kClock; }
        CHECK(free >= 46 && free <= 49);
    }
    {   // FL starts mid-bar: pulses at once (tempo), START on the next bar line (ppq 4)
        ClockEngine c;
        HostTransport t { true, true, 1.3, 120.0, 0.0, 4, 4 };
        int startMs = -1, pulsesBefore = 0;
        for (int ms = 0; ms < 2000 && startMs < 0; ++ms)
        {
            t.ppq = 1.3 + ms * 120.0 / 60000.0; t.atMs = ms;
            auto o = c.advance(ms, t);
            for (int i = 0; i < o.count; ++i)
            {
                if (o.bytes[i] == ClockEngine::kStart) { startMs = ms; break; }
                if (o.bytes[i] == ClockEngine::kClock) ++pulsesBefore;
            }
        }
        CHECK(startMs >= 1349 && startMs <= 1351);   // (4.0 - 1.3) beats at 120 BPM = 1350 ms
        CHECK(pulsesBefore >= 63 && pulsesBefore <= 66);
        CHECK(c.isRunning());
        // FL loops back to bar 1: STOP, then START again right there
        t.ppq = 0.0; t.atMs = 1400;
        auto o = c.advance(1400, t);
        bool stop = false, start = false;
        for (int i = 0; i < o.count; ++i) { stop |= o.bytes[i] == ClockEngine::kStop; start |= o.bytes[i] == ClockEngine::kStart; }
        CHECK(stop && start);
        // FL goes away (no audio blocks): STOP, then silence
        auto g = c.advance(2000, t);
        CHECK(g.count == 1 && g.bytes[0] == ClockEngine::kStop);
        CHECK(c.advance(2001, t).count == 0);
    }
    {   // the LIVE state: playing, REC
        using namespace sloop::fm1;
        std::vector<uint8_t> a(51, 0);
        a[0] = 0; a[1] = 1 | 4; a[2] = 0; a[3] = (uint8_t) ((8192 + 128) & 127); a[4] = (uint8_t) ((8192 + 128) >> 7); a[11] = 2; a[12] = 3;
        auto s = parseLive(a);
        CHECK(s && s->playing && s->songMode && !s->recWaiting && s->recTracks == 2 && s->bpm == 128 && s->selected == 3);
        CHECK(!parseLive(std::vector<uint8_t>(5, 0)));
    }

    {   // SONIDO: DESC / DUMP / NAMES / UP_LIST, and values in the FM-1's units
        using namespace sloop::snd;
        auto v14b = [](std::vector<uint8_t>& b, int v) { const int x = v + 8192; b.push_back((uint8_t) (x & 127)); b.push_back((uint8_t) (x >> 7)); };
        std::vector<uint8_t> d { 0, (uint8_t) P_VOICE, F_ENUM };
        v14b(d, 0); v14b(d, 3); v14b(d, 0);
        for (const char* str : { "VCE", "", "POLY", "MONO", "LEGATO", "UNISON" }) { for (const char* c = str; *c; ++c) d.push_back((uint8_t) *c); d.push_back(0); }
        auto desc = parseDesc(d);
        CHECK(desc && desc->id == P_VOICE && desc->label == "VCE" && desc->max == 3 && desc->names.size() == 4);
        CHECK(desc && formatValue(*desc, 1) == "MONO");

        std::vector<uint8_t> dump { 2, 5 };
        for (int i = 0; i < P_COUNT; ++i) v14b(dump, i == P_PAN ? -10 : i);
        for (int i = 0; i < 45; ++i) v14b(dump, i * 2);
        auto du = parseDump(dump, P_COUNT, 45);
        CHECK(du && du->engine == 2 && du->preset == 5 && du->params[(size_t) P_PAN] == -10 && du->params[(size_t) P_E0] == P_E0
              && du->globals.size() == 45 && du->globals[(size_t) G_DRLVL] == 50);

        std::vector<uint8_t> nm { 0, 2 };
        for (const char* str : { "INIT", "SUPERSAW", "OSC", "FILTER" }) { for (const char* c = str; *c; ++c) nm.push_back((uint8_t) *c); nm.push_back(0); }
        auto names = parseNames(nm);
        CHECK(names && names->presets.size() == 2 && names->presets[1] == "SUPERSAW" && names->edit1 == "OSC" && names->edit2 == "FILTER");

        std::vector<uint8_t> ul { 0, 2, 32, 1, 3 };
        for (const char* c = "MI BAJO"; *c; ++c) ul.push_back((uint8_t) *c);
        ul.push_back(0); ul.push_back(0); ul.push_back(0); ul.push_back(0);
        int total = 0;
        auto up = parseUserList(ul, total);
        CHECK(up && total == 32 && up->size() == 2 && (*up)[0].used && (*up)[0].name == "MI BAJO" && (*up)[0].engine == 3 && !(*up)[1].used);

        Desc t; t.fmt = F_TIME;   CHECK(formatValue(t, 0) == "1.0 ms" && formatValue(t, 127) == "10.0 s");
        Desc h; h.fmt = F_LFOHZ;  CHECK(formatValue(h, 0) == "0.05 Hz" && formatValue(h, 127) == "40.0 Hz");
        Desc c; c.fmt = F_CUTOFF; CHECK(formatValue(c, 0) == "30 Hz" && formatValue(c, 127) == "16 kHz");
        Desc db; db.fmt = F_DB;   CHECK(formatValue(db, 0) == "OFF" && formatValue(db, 112) == "0.0 dB" && formatValue(db, 96) == "-8.0 dB");
        Desc f; f.fmt = F_FILT;   CHECK(formatValue(f, 0) == "OFF" && formatValue(f, -64) == "LP 100 %" && formatValue(f, 63) == "HP 100 %");
        Desc pc; pc.fmt = F_PCT; pc.max = 127; CHECK(formatValue(pc, 127) == "100 %");
        CHECK(synthPages().size() == 18 && drumPages().size() == 6);
    }

    std::printf(failures ? "%d FAILED\n" : "all core tests passed\n", failures);
    return failures ? 1 : 0;
}
