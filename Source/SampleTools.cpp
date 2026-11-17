// SPDX-License-Identifier: GPL-3.0-only
#include "SampleTools.h"

namespace sloop {

bool isSoundFile(const juce::String& path)
{
    const auto ext = juce::File(path).getFileExtension().toLowerCase();
    return ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".mp3" || ext == ".flac" || ext == ".ogg"
        || ext == ".m4a" || ext == ".caf";
}

bool loadSound(const juce::File& file, std::vector<int16_t>& out, juce::String& error)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(file));
    if (!r)
    {
        error = file.getFileName() + juce::String::fromUTF8(": no puedo leer este archivo de audio.");
        return false;
    }
    const auto maxLen = (juce::int64) (r->sampleRate * 30.0);   // a slot holds ~7 s: 30 s is plenty to choose from
    const int n = (int) juce::jmin(r->lengthInSamples, maxLen);
    if (n <= 1 || r->sampleRate <= 0)
    {
        error = file.getFileName() + juce::String::fromUTF8(": no tiene audio.");
        return false;
    }
    juce::AudioBuffer<float> buf((int) r->numChannels, n);
    r->read(&buf, 0, n, 0, true, true);

    std::vector<double> mono((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        double acc = 0.0;
        for (int c = 0; c < buf.getNumChannels(); ++c) acc += buf.getSample(c, i);
        mono[(size_t) i] = acc / juce::jmax(1, buf.getNumChannels());
    }
    auto x = smp::resample(std::move(mono), r->sampleRate, smp::kRate);
    if (x.empty())
    {
        error = file.getFileName() + juce::String::fromUTF8(": demasiado corto.");
        return false;
    }
    out = smp::toInt16(x);
    return true;
}

bool readSlots(Fm1Connection& fm, std::vector<SlotInfo>& slots, juce::String& error)
{
    const auto r = fm.request(smpcmd::INFO);
    if (!r || r->size() < 2)
    {
        error = juce::String::fromUTF8("El FM-1 no responde a la lista de samples.");
        return false;
    }
    const auto& a = *r;
    const int n = a[0];
    size_t i = 2;
    slots.clear();
    for (int k = 0; k < n && i < a.size(); ++k)
    {
        SlotInfo s;
        s.zones = a[i++];
        juce::String name;
        while (i < a.size() && a[i] != 0) name += (juce::juce_wchar) a[i++];
        ++i;
        s.name = name;
        if (i < a.size()) s.kib = a[i++];
        slots.push_back(s);
    }
    return true;
}

bool uploadSlot(Fm1Connection& fm, int slot, const smp::Slot& s, const std::function<void(float)>& progress,
                juce::String& error)
{
    const auto begin = fm.request(smpcmd::BEGIN, { (uint8_t) slot }, 3000);
    if (!begin || begin->size() < 2 || (*begin)[1] != 0)
    {
        error = juce::String::fromUTF8("El FM-1 no pudo preparar el espacio USR") + juce::String(slot + 1) + ".";
        return false;
    }
    const size_t total = s.data.size();
    for (size_t off = 0; off < total; off += 256)
    {
        const size_t n = std::min<size_t>(256, total - off);
        const int a = smp::kDataOffset + (int) off;
        fm1::Bytes args { (uint8_t) slot, (uint8_t) (a & 0x7F), (uint8_t) ((a >> 7) & 0x7F), (uint8_t) ((a >> 14) & 0x7F) };
        const auto packed = smp::pack7(s.data.data() + off, n);
        args.insert(args.end(), packed.begin(), packed.end());
        const auto r = fm.request(smpcmd::WRITE, args, 3000);
        if (!r || r->size() < 5 || (*r)[4] != 0)
        {
            error = juce::String::fromUTF8("Falló la escritura en el byte ") + juce::String((int) off) + juce::String::fromUTF8(". El espacio quedó vacío.");
            return false;
        }
        if (progress) progress((float) (off + n) / (float) juce::jmax<size_t>(1, total));
    }
    fm1::Bytes end { (uint8_t) slot };
    const auto packed = smp::pack7(s.header.data(), s.header.size());
    end.insert(end.end(), packed.begin(), packed.end());
    const auto r = fm.request(smpcmd::END, end, 6000);
    if (!r || r->size() < 2 || (*r)[1] != 0)
    {
        static const char* why[] = { "", "tamaño", "cabecera", "CRC de los datos", "flash", "zonas" };
        const int rc = r && r->size() >= 2 ? (*r)[1] : -1;
        error = juce::String::fromUTF8("El FM-1 rechazó el sample")
              + (rc > 0 && rc <= 5 ? juce::String(" (") + juce::String::fromUTF8(why[rc]) + ")" : juce::String()) + ".";
        return false;
    }
    return true;
}

bool useAsDrumKit(Fm1Connection& fm, int slot, int pE0, juce::String& error)
{
    const auto tr = fm.request(fm1::TRACK);
    if (!tr || tr->empty()) { error = juce::String::fromUTF8("El FM-1 no responde."); return false; }
    const uint8_t before = (*tr)[0];

    // DESC of KIT describes the drum track's P_E0 only while the drum track is selected.
    bool ok = fm.request(fm1::TRACK, { (uint8_t) fm1::kDeviceDrumTrack }).has_value();
    std::optional<int> value;
    if (ok)
        if (const auto d = fm.request(smpcmd::DESC, { 0, (uint8_t) pE0 }); d && d->size() > 9 && (*d)[2] == 8)
        {
            const auto& a = *d;
            const int lo = fm1::decodeV14(a[3], a[4]);
            size_t i = 9;
            for (int skip = 0; skip < 2; ++skip) { while (i < a.size() && a[i] != 0) ++i; ++i; }   // label, unit
            const juce::String want = "USR" + juce::String(slot + 1);
            for (int k = 0; i < a.size(); ++k)
            {
                juce::String nm;
                while (i < a.size() && a[i] != 0) nm += (juce::juce_wchar) a[i++];
                ++i;
                if (nm == want) { value = lo + k; break; }
            }
        }
    if (value)
        ok = fm.request(fm1::TRACK_PARAM, fm1::trackParamSet(fm1::kDeviceDrumTrack, pE0, *value)).has_value();
    fm.request(fm1::TRACK, { before });   // the selection as it was
    if (!value || !ok)
    {
        error = juce::String::fromUTF8("Subido, pero no pude poner KIT = USR") + juce::String(slot + 1)
              + juce::String::fromUTF8(" en DRUMS: elígelo en el FM-1.");
        return false;
    }
    return true;
}

} // namespace sloop
