// SPDX-License-Identifier: GPL-3.0-only
// The sample slot port against SLOOP's own Python (tools/sampleio.py): the same input must give
// the same bytes. Tests/fixtures/mkref.py made inst.hdr / inst.bin from the src_*.f64 signals.
//   sloop_sample_tests <fixtures dir>
#include "SampleSlot.h"
#include <cstdio>
#include <fstream>
#include <iterator>

using namespace sloop::smp;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static Bytes readFile(const std::string& p)
{
    std::ifstream f(p, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(f), {});
}

int main(int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : "Tests/fixtures";

    // notes from file names, as sampleio.note_from_name
    CHECK(noteFromName("PIANO_A3") == 57 && noteFromName("PIANO_A4") == 69 && noteFromName("PIANO_A5") == 81);
    CHECK(noteFromName("Bb2 pad") == 46 && noteFromName("x C#-1") == 1 && noteFromName("nada") == 60);
    CHECK(noteFromName("SNARE") == 60);   // "E" inside a word is not a note

    struct Src { int sr, n; const char* name; };
    const Src srcs[] = { { 44100, 30000, "PIANO_A3" }, { 48000, 20000, "PIANO_A4" }, { 22050, 9000, "PIANO_A5" } };
    std::vector<Zone> zones;
    for (const auto& s : srcs)
    {
        const auto raw = readFile(dir + "/src_" + s.name + ".f64");
        CHECK((int) raw.size() == s.n * 8);
        std::vector<double> x((size_t) s.n);
        std::memcpy(x.data(), raw.data(), raw.size());
        zones.push_back({ toInt16(resample(x, s.sr, kRate)), noteFromName(s.name), -1, -1 });
    }
    std::string err;
    const auto slot = buildSlot("Piano test", zones, err);
    CHECK(slot.has_value());
    const auto hdr = readFile(dir + "/inst.hdr"), bin = readFile(dir + "/inst.bin");
    if (slot)
    {
        CHECK(slot->data.size() == bin.size());
        CHECK(slot->header.size() == hdr.size());
        size_t firstDiff = 0;
        while (firstDiff < std::min(slot->data.size(), bin.size()) && slot->data[firstDiff] == bin[firstDiff]) ++firstDiff;
        if (slot->data != bin) std::printf("  ADPCM differs from byte %zu of %zu\n", firstDiff, bin.size());
        CHECK(slot->data == bin);
        if (slot->header != hdr)
            for (size_t i = 0; i < std::min(hdr.size(), slot->header.size()); ++i)
                if (slot->header[i] != hdr[i]) { std::printf("  header differs at %zu: %d vs %d\n", i, slot->header[i], hdr[i]); break; }
        CHECK(slot->header == hdr);
    }

    // too long for a slot; no zones
    std::vector<Zone> big { { std::vector<int16_t>((size_t) (kMaxData * 2 + 10), 100), 60, -1, -1 } };
    CHECK(!buildSlot("x", big, err) && !err.empty());
    CHECK(!buildSlot("x", {}, err));

    // pack7: 8 bytes -> 2 groups
    const uint8_t raw8[8] = { 0x80, 1, 2, 3, 4, 5, 0xFF, 7 };
    const Bytes p = pack7(raw8, 8);
    CHECK((p == Bytes { 0x41, 0, 1, 2, 3, 4, 5, 0x7F, 0, 7 }));

    // drum kit: lanes by name, placing, fitting
    CHECK(kitLaneOf("BD 808.wav") == 0 && kitLaneOf("OH_01.wav") == 5 && kitLaneOf("Low Tom.aif") == 9);
    CHECK(kitLaneOf("Snare 2.wav") == 8 && kitLaneOf("hihat closed") == 4 && kitLaneOf("vocal chop") == -1);
    std::array<std::optional<Pad>, 16> pads;
    pads[0] = Pad { std::vector<int16_t>(1000, 1000), "kick.wav" };
    CHECK(kitPlace(pads, "kick 2.wav") == 1);
    CHECK(kitPlace(pads, "another kick.wav") == 1);   // the kick is taken: its alternative, KICK 2
    CHECK(kitPlace(pads, "vocal.wav") == 1);          // no lane by name: the first free pad
    pads[2] = Pad { std::vector<int16_t>((size_t) kMaxData * 2, 2000), "snare.wav" };
    CHECK(kitBytes(pads) > kMaxData);
    CHECK(kitFit(pads));
    CHECK(kitBytes(pads) <= kMaxData);
    const auto kz = kitZones(pads);
    CHECK(kz.size() == 2 && kz[0].lo == 36 && kz[0].hi == 36 && kz[1].root == 38);
    CHECK(buildSlot("kit", kz, err).has_value());
    Pad cut { std::vector<int16_t>(2000, 10000), "x" };
    cut.length = 500; cut.gainDb = -6.0;
    const auto r = renderPad(cut);
    CHECK(r.size() == 500 && r.back() < r[0] / 10 && r[0] == 5012);   // -6 dB, faded to the end

    std::printf(failures ? "%d FAILED\n" : "all sample slot tests passed\n", failures);
    return failures ? 1 : 0;
}
