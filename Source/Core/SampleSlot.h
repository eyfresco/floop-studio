// SPDX-License-Identifier: GPL-3.0-only
// The FM-1's user sample slots (USR1..USR4), as SLOOP builds them: a C++ port of sloop-fm1
// tools/sampleio.py (resample, to_int16, ima_encode, key_split, note_from_name, user_slot) and of
// the web editor's DRUM KIT (kitPad, kitZones, kitFit, kitLaneOf). The bytes must match the Python
// reference: Tests/CoreTests.cpp compares them with a slot built by sampleio.py.
//
// A slot: header (480 bytes: "FSMP", zones, name, data length, CRC-32) + IMA ADPCM data, mono,
// 22050 Hz, up to 16 zones, 80 KiB (about 7.4 s in all). Format: web/EDITOR_PROTOCOL.md.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <regex>
#include <string>
#include <vector>

namespace sloop::smp {

constexpr int kSlots = 4;                       // USR1..USR4
constexpr int kSlotSize = 0x14000;              // 80 KiB
constexpr int kDataOffset = 512;                // the ADPCM data starts here in the slot
constexpr int kRate = 22050;
constexpr int kZones = 16;
constexpr int kHeaderLen = 32 + kZones * 28;    // 480
constexpr int kMaxData = kSlotSize - kDataOffset;
constexpr double kMaxSeconds = kMaxData * 2.0 / kRate;

using Bytes = std::vector<uint8_t>;

// --- sampleio.py ------------------------------------------------------------------------------

// Crude anti-alias (moving average over the ratio), then linear interpolation (sampleio.resample).
inline std::vector<double> resample(std::vector<double> x, double sr, double to)
{
    if (sr == to || x.empty()) return x;
    if (sr > to)
    {
        const int k = std::max(1, (int) std::nearbyint(sr / to));   // as Python round(): half to even
        if (k == 2)
        {
            std::vector<double> y;
            y.reserve(x.size());
            for (size_t i = 0; i + 1 < x.size(); ++i) y.push_back((x[i] + x[i + 1]) / 2);
            y.push_back(x.back() / 2);
            x.swap(y);
        }
        else if (k > 1)
        {
            std::vector<double> y(x.size());
            for (size_t i = 0; i < x.size(); ++i)
            {
                double acc = 0.0;   // left to right, like reduce(add, x[i:i + k])
                bool first = true;
                for (size_t j = i; j < std::min(x.size(), i + (size_t) k); ++j) { acc = first ? x[j] : acc + x[j]; first = false; }
                y[i] = acc / k;
            }
            x.swap(y);
        }
    }
    std::vector<double> out;
    const double step = sr / to;
    const double last = (double) x.size() - 1;
    for (double p = 0.0; p < last; p += step)
    {
        const size_t i = (size_t) p;
        const double f = p - (double) i;
        out.push_back(x[i] * (1 - f) + x[i + 1] * f);
    }
    return out;
}

// Mono float -> int16, peak normalised to 30000 (sampleio.to_int16).
inline std::vector<int16_t> toInt16(const std::vector<double>& x)
{
    double pk = 1e-9;
    for (double v : x) pk = std::max(pk, std::abs(v));
    std::vector<int16_t> out(x.size());
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double v = x[i] / pk * 30000;
        const int iv = (int) v;   // Python int(): towards zero
        out[i] = (int16_t) std::clamp(iv, -32768, 32767);
    }
    return out;
}

inline const std::array<int, 89>& imaSteps()
{
    static const std::array<int, 89> s { 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60,
        66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598,
        658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026,
        4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
        22385, 24623, 27086, 29794, 32767 };
    return s;
}

// IMA ADPCM, 4 bit, low nibble first, from predictor 0 / index 0 (sampleio.ima_encode).
struct Adpcm { Bytes data; int predAtLoop = 0, idxAtLoop = 0; };

inline Adpcm imaEncode(const std::vector<int16_t>& samples, size_t loopStart = 0)
{
    static const int adj[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };
    const auto& steps = imaSteps();
    Adpcm a;
    int pred = 0, idx = 0;
    std::vector<uint8_t> nib;
    nib.reserve(samples.size() + 1);
    for (size_t n = 0; n < samples.size(); ++n)
    {
        if (n == loopStart) { a.predAtLoop = pred; a.idxAtLoop = idx; }
        const int step = steps[(size_t) idx];
        int diff = samples[n] - pred, code = 0;
        if (diff < 0) { code = 8; diff = -diff; }
        int vd = step >> 3;
        if (diff >= step) { code |= 4; diff -= step; vd += step; }
        if (diff >= step >> 1) { code |= 2; diff -= step >> 1; vd += step >> 1; }
        if (diff >= step >> 2) { code |= 1; vd += step >> 2; }
        pred = (code & 8) ? pred - vd : pred + vd;
        pred = std::clamp(pred, -32768, 32767);
        idx = std::clamp(idx + adj[code & 7], 0, 88);
        nib.push_back((uint8_t) code);
    }
    if (nib.size() & 1) nib.push_back(0);
    a.data.reserve(nib.size() / 2);
    for (size_t i = 0; i < nib.size(); i += 2) a.data.push_back((uint8_t) (nib[i] | (nib[i + 1] << 4)));
    return a;
}

// Sorted roots -> (lo, hi): each zone reaches halfway to its neighbours, the outer ones to the ends.
inline std::vector<std::pair<int, int>> keySplit(const std::vector<int>& roots)
{
    std::vector<std::pair<int, int>> out;
    const size_t n = roots.size();
    for (size_t j = 0; j < n; ++j)
    {
        const int lo = j == 0 ? 0 : (roots[j - 1] + roots[j]) / 2 + 1;
        const int hi = j == n - 1 ? 127 : (roots[j] + roots[j + 1]) / 2;
        out.push_back({ lo, hi });
    }
    return out;
}

// "C4" / "F#3" / "Bb2" in a file name, C4 = 60, clamped 0..127 (sampleio.note_from_name).
inline int noteFromName(const std::string& name, int fallback = 60)
{
    static const std::regex re("(?:^|[^A-Za-z])([A-G])([#b]?)(-?\\d)(?!\\d)");
    std::smatch m;
    if (!std::regex_search(name, m, re)) return fallback;
    static const std::string order = "C D EF G A B";
    int pc = (int) order.find(m[1].str()[0]);
    if (m[2] == "#") pc += 1; else if (m[2] == "b") pc -= 1;
    return std::clamp((std::stoi(m[3].str()) + 1) * 12 + pc, 0, 127);
}

inline uint32_t crc32(const Bytes& d)
{
    uint32_t c = 0xFFFFFFFFu;
    for (uint8_t b : d)
    {
        c ^= b;
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

struct Zone
{
    std::vector<int16_t> samples;   // at kRate
    int root = 60;
    int lo = -1, hi = -1;           // -1: split the keyboard between the roots
};

struct Slot { Bytes header, data; int zones = 0; };

// Zones -> header + ADPCM data (sampleio.user_slot). Errors are in Spanish for the UI.
inline std::optional<Slot> buildSlot(const std::string& name, std::vector<Zone> zones, std::string& error)
{
    if (zones.empty() || (int) zones.size() > kZones) { error = "Entre 1 y 16 sonidos por espacio."; return std::nullopt; }
    struct Z { uint32_t off, n, ls, le; int root, lo, hi, pred, idx; };
    std::vector<Z> zs;
    Slot slot;
    for (const auto& z : zones)
    {
        auto a = imaEncode(z.samples, 0);
        zs.push_back({ (uint32_t) slot.data.size(), (uint32_t) z.samples.size(), 0,
                       (uint32_t) (z.samples.empty() ? 0 : z.samples.size() - 1), z.root, z.lo, z.hi, a.predAtLoop, a.idxAtLoop });
        slot.data.insert(slot.data.end(), a.data.begin(), a.data.end());
    }
    if ((int) slot.data.size() > kMaxData)
    {
        char buf[160];
        std::snprintf(buf, sizeof buf, "Demasiado largo: %.1f s de %.1f s que caben en un espacio.",
                      slot.data.size() * 2.0 / kRate, kMaxSeconds);
        error = buf;
        return std::nullopt;
    }
    std::stable_sort(zs.begin(), zs.end(), [](const Z& a, const Z& b) { return a.root < b.root; });
    std::vector<int> roots;
    for (auto& z : zs) roots.push_back(z.root);
    const auto split = keySplit(roots);
    for (size_t i = 0; i < zs.size(); ++i)
        if (zs[i].lo < 0) { zs[i].lo = split[i].first; zs[i].hi = split[i].second; }

    auto u32 = [](Bytes& b, uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back((uint8_t) (v >> (8 * i))); };
    auto i16 = [](Bytes& b, int v) { const uint16_t u = (uint16_t) (int16_t) v; b.push_back((uint8_t) u); b.push_back((uint8_t) (u >> 8)); };
    const uint32_t rate = (uint32_t) std::lround((double) kRate / 44100 * 65536);

    Bytes& h = slot.header;
    u32(h, 0x504D5346);                                     // "FSMP"
    h.push_back(1); h.push_back(0);                          // version 1
    h.push_back((uint8_t) zs.size()); h.push_back(0);
    std::string nm;
    for (char c : name)
    {
        const char u = (char) std::toupper((unsigned char) c);
        if (u >= 0x20 && u <= 0x7E && nm.size() < 8) nm += u;
    }
    for (int i = 0; i < 8; ++i) h.push_back(i < (int) nm.size() ? (uint8_t) nm[(size_t) i] : 0);
    u32(h, (uint32_t) slot.data.size());
    u32(h, crc32(slot.data));
    for (int i = 0; i < 8; ++i) h.push_back(0);
    for (const auto& z : zs)
    {
        u32(h, z.off); u32(h, z.n); u32(h, z.ls); u32(h, z.le); u32(h, rate);
        i16(h, z.root * 16); i16(h, z.pred);
        h.push_back((uint8_t) z.idx); h.push_back((uint8_t) z.lo); h.push_back((uint8_t) z.hi); h.push_back(0);   // one-shot
    }
    h.resize(kHeaderLen, 0);
    slot.zones = (int) zs.size();
    return slot;
}

// Groups of up to 7 bytes, each preceded by their top bits (the SysEx transfer encoding).
inline Bytes pack7(const uint8_t* b, size_t n)
{
    Bytes out;
    for (size_t i = 0; i < n; i += 7)
    {
        uint8_t top = 0;
        const size_t g = std::min<size_t>(7, n - i);
        for (size_t j = 0; j < g; ++j) top |= (uint8_t) (((b[i + j] >> 7) & 1) << j);
        out.push_back(top);
        for (size_t j = 0; j < g; ++j) out.push_back(b[i + j] & 0x7F);
    }
    return out;
}

// --- the web editor's DRUM KIT ---------------------------------------------------------------

// The lane's GM note (drums.c LANE_NOTE): a kit zone covers exactly that note.
constexpr std::array<int, 16> kLaneGm { 36, 35, 38, 39, 42, 46, 44, 37, 40, 43, 48, 49, 51, 70, 63, 56 };

struct Pad
{
    std::vector<int16_t> samples;   // at kRate, normalised (toInt16)
    std::string file;
    int tune = 0;                   // semitones -12..12 (played faster or slower, as a sampler does)
    double gainDb = 0.0;            // -24..0
    int length = 0;                 // samples kept after the tune (0: all)
};

inline int padLength(const Pad& p)
{
    const int n = (int) p.samples.size();
    const int full = p.tune ? (int) std::floor((n - 1) / std::pow(2.0, p.tune / 12.0)) : n;
    return std::max(1, std::min(full, p.length > 0 ? p.length : full));
}

// A pad as it goes to the slot: tuned, cut (with a 4 ms fade so it ends without a click), its gain (kitPad).
inline std::vector<int16_t> renderPad(const Pad& p)
{
    std::vector<double> x(p.samples.begin(), p.samples.end());
    if (p.tune) x = resample(x, kRate * std::pow(2.0, p.tune / 12.0), kRate);
    const int n = std::max(1, std::min((int) x.size(), p.length > 0 ? p.length : (int) x.size()));
    const double g = std::pow(10.0, std::clamp(p.gainDb, -24.0, 0.0) / 20.0);
    const int fadeLen = (int) std::lround(kRate * 0.004);
    const int fade = n < (int) x.size() ? std::min(fadeLen, n) : 0;
    std::vector<int16_t> out((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const double f = fade && i >= n - fade ? (double) (n - i) / fade : 1.0;
        out[(size_t) i] = (int16_t) std::clamp((int) std::floor(x[(size_t) i] * g * f + 0.5), -32768, 32767);   // JS Math.round
    }
    return out;
}

inline int kitBytes(const std::array<std::optional<Pad>, 16>& pads)
{
    int b = 0;
    for (const auto& p : pads) if (p) b += (padLength(*p) + 1) >> 1;
    return b;
}

// Shortens the longest pads until the kit fits one slot (kitFit). True if a pad was shortened.
inline bool kitFit(std::array<std::optional<Pad>, 16>& pads, int room = kMaxData)
{
    std::vector<std::pair<int, int>> use;   // lane, samples
    for (int l = 0; l < 16; ++l) if (pads[(size_t) l]) use.push_back({ l, padLength(*pads[(size_t) l]) });
    auto sum = [&use](long long L) { long long a = 0; for (auto& u : use) a += (std::min<long long>(u.second, L) + 1) >> 1; return a; };
    if (use.empty() || sum(1LL << 40) <= room) return false;
    long long lo = 1, hi = 0;
    for (auto& u : use) hi = std::max<long long>(hi, u.second);
    while (lo < hi) { const long long L = (lo + hi + 1) >> 1; if (sum(L) <= room) lo = L; else hi = L - 1; }
    for (auto& u : use) if (u.second > lo) pads[(size_t) u.first]->length = (int) lo;
    return true;
}

inline std::vector<Zone> kitZones(const std::array<std::optional<Pad>, 16>& pads)
{
    std::vector<Zone> zs;
    for (int l = 0; l < 16; ++l)
        if (pads[(size_t) l])
            zs.push_back({ renderPad(*pads[(size_t) l]), kLaneGm[(size_t) l], kLaneGm[(size_t) l], kLaneGm[(size_t) l] });
    return zs;
}

// A file name -> the lane it most likely is (-1: none): "BD 808.wav" kick, "OH_01" open hat... (kitLaneOf)
inline int kitLaneOf(std::string name)
{
    for (auto& c : name) c = (char) std::tolower((unsigned char) c);
    name = std::regex_replace(name, std::regex("\\.[a-z0-9]{2,4}$"), "");
    name = std::regex_replace(name, std::regex("[_\\-.()\\[\\]]+"), " ");
    static const std::vector<std::pair<int, std::regex>> words {
        { 5, std::regex("open|\\boh\\b|ophat|o ?hat") }, { 6, std::regex("pedal|\\bph\\b|foot") },
        { 4, std::regex("clos|\\bch\\b|\\bhh\\b|hi ?hat|hihat|\\bhat") }, { 7, std::regex("\\brim|\\brs\\b|side ?stick|stick") },
        { 3, std::regex("clap|\\bcp\\b|\\bclp\\b|snap") }, { 1, std::regex("kick ?2|\\bbd ?2|sub ?kick") },
        { 0, std::regex("kick|\\bbd\\b|bass ?drum|\\bkik\\b|\\bkck\\b") }, { 8, std::regex("snare ?2|\\bsd ?2|snr ?2") },
        { 2, std::regex("snare|\\bsd\\b|\\bsnr\\b") },
        { 10, std::regex("hi(gh)? ?tom|\\bht\\b|tom ?h|mid ?tom|\\bmt\\b|tom ?[23]\\b") },
        { 9, std::regex("lo(w)? ?tom|\\blt\\b|floor|tom ?l|tom ?1\\b|\\btom") },
        { 11, std::regex("crash|\\bcr\\b|\\bcy\\b|cymbal|splash|china") }, { 12, std::regex("ride|\\brd\\b") },
        { 13, std::regex("shak|\\bsh\\b|maraca|cabasa|tamb|\\bshk\\b") },
        { 14, std::regex("conga|bongo|\\bcg\\b|\\bperc|timbal|djembe") },
        { 15, std::regex("cow ?bell|\\bcb\\b|bell|block|clave|agogo") } };
    for (const auto& [lane, re] : words)
        if (std::regex_search(name, re)) return lane;
    return -1;
}

// Where a new sound goes: its lane, else that lane's alternative, else the first free pad (kitPlace).
inline int kitPlace(const std::array<std::optional<Pad>, 16>& pads, const std::string& name)
{
    static const int alt[16] = { 1, 0, 8, 8, 6, 4, 4, 3, 2, 10, 9, 12, 11, 14, 13, 14 };
    const int l = kitLaneOf(name);
    if (l >= 0 && !pads[(size_t) l]) return l;
    if (l >= 0 && !pads[(size_t) alt[l]]) return alt[l];
    for (int i = 0; i < 16; ++i) if (!pads[(size_t) i]) return i;
    return -1;
}

} // namespace sloop::smp
