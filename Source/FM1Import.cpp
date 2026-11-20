// SPDX-License-Identifier: GPL-3.0-only
#include "FM1Import.h"

namespace sloop {

namespace {
// The names macOS / Windows give the FM-1's ports (sloop-fm1 web/editor.html FM1_PORT, minus the
// generic "usb-midi" / "composite" names, which could be any other device).
bool looksLikeFm1(const juce::String& n)
{
    const auto s = n.toLowerCase();
    if (s.contains("studio")) return false;   // our own "Floop Studio" MIDI source (VirtualMidiSource), not the FM-1
    return s.contains("felucca") || s.contains("sloop") || s.contains("fm-1") || s.contains("fm1");
}

bool isPush(uint8_t cmd)
{
    return cmd == fm1::CHANGED || cmd == fm1::RELOAD || cmd == fm1::STEP_CHANGED || cmd == fm1::TRACK_CHANGED;
}
}

// --- connection -----------------------------------------------------------------------------

Fm1Connection::~Fm1Connection()
{
    if (in) in->stop();
}

bool Fm1Connection::open(juce::String& error)
{
    juce::MidiDeviceInfo inInfo, outInfo;
    for (const auto& d : juce::MidiInput::getAvailableDevices())
        if (looksLikeFm1(d.name)) { inInfo = d; break; }
    for (const auto& d : juce::MidiOutput::getAvailableDevices())
        if (looksLikeFm1(d.name)) { outInfo = d; break; }

    if (inInfo.identifier.isEmpty() || outInfo.identifier.isEmpty())
    {
        error = juce::String::fromUTF8("No encuentro el FM-1. Conéctalo por USB con SLOOP encendido.");
        return false;
    }
    in = juce::MidiInput::openDevice(inInfo.identifier, this);
    raw = RawMidiOut::open([](const std::string& n) { return looksLikeFm1(juce::String::fromUTF8(n.c_str())); });
    if (!raw) out = juce::MidiOutput::openDevice(outInfo.identifier);
    if (!in || (!out && !raw))
    {
        error = "No pude abrir el puerto MIDI del FM-1 (" + inInfo.name + ").";
        return false;
    }
    name = inInfo.name;
    in->start();
    // The first frame after opening the ports can be lost (sloop-fm1 tools/fm1_sample_upload.py
    // does the same): a few PINGs until the FM-1 answers, so the caller's first request is not the one lost.
    for (int i = 0; i < 3 && !request(fm1::PING, {}, 300, false); ++i) {}
    return true;
}

void Fm1Connection::sendNow(const juce::MidiMessage& m)
{
    if (raw) raw->send(m.getRawData(), (size_t) m.getRawDataSize());
    else if (out) out->sendMessageNow(m);
}

bool Fm1Connection::stillConnected() const
{
    if (raw) return raw->stillThere();
    for (const auto& d : juce::MidiOutput::getAvailableDevices())
        if (out && d.identifier == out->getIdentifier()) return true;
    return false;
}

std::optional<fm1::Bytes> Fm1Connection::request(uint8_t cmd, const fm1::Bytes& args, int timeoutMs, bool retry)
{
    const auto payload = fm1::request(cmd, args);
    for (int attempt = 0; attempt < (retry ? 2 : 1); ++attempt)
    {
        {
            const juce::ScopedLock sl(lock);
            waitingFor = cmd;
            reply.clear();
            replied.reset();
        }
        sendNow(juce::MidiMessage::createSysExMessage(payload.data(), (int) payload.size()));
        if (replied.wait(timeoutMs))
        {
            const juce::ScopedLock sl(lock);
            waitingFor = -1;
            return reply;
        }
    }
    const juce::ScopedLock sl(lock);
    waitingFor = -1;
    return std::nullopt;
}

std::vector<Fm1Connection::Push> Fm1Connection::takePushes()
{
    const juce::ScopedLock sl(lock);
    std::vector<Push> out;
    out.swap(pushes);
    return out;
}

void Fm1Connection::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m)
{
    if (!m.isSysEx()) { if (onMidi) onMidi(m); return; }
    auto frame = fm1::parseFrame(m.getSysExData(), (size_t) m.getSysExDataSize());
    if (!frame) return;
    const juce::ScopedLock sl(lock);
    if (isPush(frame->first))
    {
        if (pushes.size() < 512) pushes.push_back(std::move(*frame));
    }
    else if (frame->first == waitingFor)
    {
        reply = frame->second;
        replied.signal();
    }
}

// --- the pattern <-> the Session -----------------------------------------------------------

Fm1Pattern Fm1Pattern::fromSession(const Session& s)
{
    Fm1Pattern p;
    for (int st = 0; st < kSteps; ++st)
    {
        p.drumOn[st] = s.drum[st].load();
        p.drumLevel[st] = s.drumLevel[st].load();
        p.drumRoll[st] = s.drumRoll[st].load();
        for (int t = 1; t < kTracks; ++t) { p.lo[t][st] = s.notesLo[t][st].load(); p.hi[t][st] = s.notesHi[t][st].load(); }
    }
    for (int t = 0; t < kTracks; ++t) { p.length[t] = s.length[t].load(); p.muted[t] = s.muted[t].load(); }
    return p;
}

void Fm1Pattern::applyDrumStep(Session& s, int st) const
{
    s.drum[st] = drumOn[st];
    s.drumLevel[st] = drumLevel[st] & ~fm1::levelMaskOff(drumOn[st]);
    s.drumRoll[st] = drumRoll[st] & ~fm1::levelMaskOff(drumOn[st]);
    s.touch();
}

void Fm1Pattern::applySynthStep(Session& s, int t, int st) const
{
    s.notesLo[t][st] = lo[t][st];
    s.notesHi[t][st] = hi[t][st];
    s.touch();
}

void Fm1Pattern::applyTo(Session& s) const
{
    for (int st = 0; st < kSteps; ++st)
    {
        s.drum[st] = drumOn[st];
        s.drumLevel[st] = drumLevel[st] & ~fm1::levelMaskOff(drumOn[st]);
        s.drumRoll[st] = drumRoll[st] & ~fm1::levelMaskOff(drumOn[st]);
        for (int t = 1; t < kTracks; ++t) { s.notesLo[t][st] = lo[t][st]; s.notesHi[t][st] = hi[t][st]; }
    }
    for (int t = 0; t < kTracks; ++t) { s.length[t] = (uint8_t) length[t]; s.muted[t] = (uint8_t) muted[t]; }
    s.touch();
}

// --- reads ----------------------------------------------------------------------------------

bool readInfo(Fm1Connection& fm, fm1::Info& info, juce::String& error)
{
    const auto r = fm.request(fm1::INFO);
    const auto i = r ? fm1::parseInfo(*r) : std::nullopt;
    if (!i)
    {
        error = "El FM-1 (" + fm.portName() + juce::String::fromUTF8(") no responde. ¿Tiene SLOOP instalado?");
        return false;
    }
    if (i->protocol < 5 || i->nTracks < 4)
    {
        error = "Tu firmware (" + juce::String(i->version) + ") es anterior a SLOOP 2.0: no puedo leer las 4 pistas.";
        return false;
    }
    info = *i;
    return true;
}

bool readDrumStep(Fm1Connection& fm, int st, Fm1Pattern& p)
{
    const auto r = fm.request(fm1::DRUM_STEP, { (uint8_t) st });
    const auto d = r ? fm1::parseDrumStep(*r) : std::nullopt;
    if (!d || d->index != st) return false;
    p.drumOn[st] = d->on;
    p.drumLevel[st] = d->level & ~fm1::levelMaskOff(d->on);
    p.drumRoll[st] = d->roll & ~fm1::levelMaskOff(d->on);
    return true;
}

bool readSynthStep(Fm1Connection& fm, int t, int st, Fm1Pattern& p)
{
    const int dev = deviceTrack(t);
    const auto r = fm.request(fm1::TRACK_STEP, { (uint8_t) dev, (uint8_t) st });
    const auto s = r ? fm1::parseTrackStep(*r) : std::nullopt;
    if (!s || s->track != dev || s->index != st) return false;
    p.lo[t][st] = 0;
    p.hi[t][st] = 0;
    if (s->time == 1) ++p.ties;   // a tie holds the previous note: not supported yet
    if (s->time != 0) return true;
    for (int i = 0; i < s->count; ++i)
    {
        const int n = s->notes[i];
        if (n < 64) p.lo[t][st] |= uint64_t { 1 } << n; else p.hi[t][st] |= uint64_t { 1 } << (n - 64);
    }
    return true;
}

bool readTrackParams(Fm1Connection& fm, Fm1Pattern& p)
{
    for (int t = 0; t < kTracks; ++t)
    {
        const int dev = deviceTrack(t);
        const auto len = fm.request(fm1::TRACK_PARAM, { (uint8_t) dev, (uint8_t) fm1::P_SLEN });
        const auto lenV = len ? fm1::parseTrackParam(*len, dev, fm1::P_SLEN) : std::nullopt;
        const auto mute = fm.request(fm1::TRACK_PARAM, { (uint8_t) dev, (uint8_t) fm1::P_MUTE });
        const auto muteV = mute ? fm1::parseTrackParam(*mute, dev, fm1::P_MUTE) : std::nullopt;
        if (!lenV || !muteV) return false;
        p.length[t] = juce::jlimit(1, kSteps, *lenV);   // (SLOOP: up to 64)
        p.muted[t] = *muteV ? 1 : 0;
    }
    return true;
}

bool readPattern(Fm1Connection& fm, int steps, Fm1Pattern& p, juce::String& error)
{
    // The lengths first: each track is read up to its own length (a 16-step track costs 16 reads
    // of the 64 the FM-1 holds; steps past it keep what the caller's pattern had for them).
    if (!readTrackParams(fm, p))
    {
        error = juce::String::fromUTF8("Se cortó la lectura del largo de las pistas.");
        return false;
    }
    steps = juce::jlimit(0, kSteps, steps);
    p.ties = 0;
    for (int s = 0; s < juce::jmin(steps, p.length[kDrums]); ++s)
        if (!readDrumStep(fm, s, p))
        {
            error = juce::String::fromUTF8("Se cortó la lectura en DRUMS, paso ") + juce::String(s + 1) + ".";
            return false;
        }
    for (int t = 1; t < kTracks; ++t)
        for (int s = 0; s < juce::jmin(steps, p.length[t]); ++s)
            if (!readSynthStep(fm, t, s, p))
            {
                error = juce::String::fromUTF8("Se cortó la lectura en ") + kTrackNames[t] + ", paso " + juce::String(s + 1) + ".";
                return false;
            }
    return true;
}

// --- writes ---------------------------------------------------------------------------------

bool writeDrumStep(Fm1Connection& fm, int st, const Fm1Pattern& p)
{
    const auto r = fm.request(fm1::DRUM_STEP, fm1::drumStepSet(st, p.drumOn[st], p.drumLevel[st], p.drumRoll[st]));
    return r.has_value();
}

bool writeSynthStep(Fm1Connection& fm, int t, int st, Fm1Pattern& p)
{
    uint8_t notes[4] {};
    int count = 0;
    for (int n = 0; n < 128 && count < 4; ++n)
        if (((n < 64 ? p.lo[t][st] : p.hi[t][st]) >> (n & 63)) & 1u) notes[count++] = (uint8_t) n;
    p.lo[t][st] = 0;
    p.hi[t][st] = 0;
    for (int i = 0; i < count; ++i)
    {
        if (notes[i] < 64) p.lo[t][st] |= uint64_t { 1 } << notes[i];
        else p.hi[t][st] |= uint64_t { 1 } << (notes[i] - 64);
    }
    const auto r = fm.request(fm1::TRACK_STEP, fm1::trackStepSet(deviceTrack(t), st, notes, count));
    return r.has_value();
}

bool writeTrackParam(Fm1Connection& fm, int t, int id, int value)
{
    const int dev = deviceTrack(t);
    const auto r = fm.request(fm1::TRACK_PARAM, fm1::trackParamSet(dev, id, value));
    return r.has_value();
}

// --- one-shot import ------------------------------------------------------------------------

ImportResult importFromFm1(Session& session)
{
    Fm1Connection fm;
    juce::String err;
    fm1::Info info;
    if (!fm.open(err) || !readInfo(fm, info, err)) return { false, err };

    Fm1Pattern p;   // read everything first: the Session changes only if the whole read worked
    if (!readPattern(fm, info.nStep, p, err)) return { false, err };
    p.applyTo(session);

    juce::String msg = juce::String::fromUTF8("Importado del FM-1 (") + juce::String(info.version) + ").";
    if (p.ties) msg << " " << p.ties << juce::String::fromUTF8(" notas ligadas (TIE) aún no se importan.");
    return { true, msg };
}

} // namespace sloop
