// SPDX-License-Identifier: GPL-3.0-only
#include "FM1Live.h"

namespace sloop {

void Fm1Live::setEnabled(bool on)
{
    if (on == enabled.load()) return;
    enabled = on;
    if (on)
    {
        setState(State::Connecting, juce::String::fromUTF8("Conectando con el FM-1…"));
        startThread();
    }
    else
    {
        signalThreadShouldExit();
        notify();
        stopThread(3000);   // at most one request timeout
        setState(State::Off, {});
    }
}

void Fm1Live::setState(State s, const juce::String& m)
{
    stateValue = (int) s;
    const juce::ScopedLock sl(msgLock);
    msg = m;
}

void Fm1Live::run()
{
    while (!threadShouldExit())
    {
        if (!fm && !paused.load() && !connect())
        {
            wait(kRetryEveryMs);
            continue;
        }

        if (paused.load())
        {
            idle = true;
            rewatch = fm != nullptr;   // WATCH ends by itself 3 s after the last request
            wait(20);
            continue;
        }
        idle = false;
        if (rewatch)
        {
            rewatch = false;
            if (!fm->request(fm1::WATCH, { (uint8_t) fm1::kWatchWithTracks })) { disconnect(false); continue; }
            lastPollMs = 0;   // the FM-1 may have changed meanwhile: read it all again
        }

        const auto now = juce::Time::getMillisecondCounter();
        bool ok = midiStep() && pushSelection() && transport() && pushLocalEdits() && handlePushes() && soundStep();
        if (ok && now - lastPollMs >= (juce::uint32) kPollEveryMs) ok = poll();
        if (ok && now - lastPingMs >= (juce::uint32) fm1::kPingEveryMs)
        {
            ok = fm->request(fm1::PING).has_value();
            lastPingMs = now;
        }
        if (!ok)
        {
            disconnect(false);
            setState(State::Error, juce::String::fromUTF8("Se perdió la conexión con el FM-1. Reintentando…"));
            wait(kRetryEveryMs);
            continue;
        }
        wait(25);
    }
    disconnect(true);
}

bool Fm1Live::connect()
{
    heldLo = 0;
    heldHi = 0;
    setState(State::Connecting, juce::String::fromUTF8("Conectando con el FM-1…"));
    fm = std::make_unique<Fm1Connection>();
    fm->onMidi = [this](const juce::MidiMessage& m) {   // the FM-1's keys, for MAPA MIDI
        if (!m.isNoteOnOrOff()) return;
        const int n = m.getNoteNumber();
        auto& w = n < 64 ? heldLo : heldHi;
        const uint64_t bit = uint64_t { 1 } << (n & 63);
        if (m.isNoteOn()) w.fetch_or(bit); else w.fetch_and(~bit);
        lastNoteValue = n | (m.getChannel() << 8) | (m.isNoteOn() ? 1 << 16 : 0);
        noteRev.fetch_add(1);
    };
    juce::String err;
    if (!fm->open(err) || !readInfo(*fm, info, err))
    {
        fm.reset();
        setState(State::Error, err);
        return false;
    }
    steps = juce::jlimit(1, kSteps, info.nStep);

    // Take the FM-1's pattern (the UI asked before turning the link on), then start watching.
    Fm1Pattern device;
    if (!readPattern(*fm, steps, device, err))
    {
        fm.reset();
        setState(State::Error, err);
        return false;
    }
    device.applyTo(session);
    mirror = device;

    const auto w = fm->request(fm1::WATCH, { (uint8_t) fm1::kWatchWithTracks });
    if (!w || w->empty() || !((*w)[0] & 1))
    {
        fm.reset();
        setState(State::Error, juce::String::fromUTF8("El FM-1 no acepta la sincronización en vivo (WATCH)."));
        return false;
    }

    lastPollMs = lastPingMs = lastStateMs = juce::Time::getMillisecondCounter();
    liveOps = info.protocol >= fm1::kLiveProtocol;
    bridgeKnown = false;
    descCache.clear();
    namesCache.clear();
    soundDirty = true;
    userDirty = true;

    // The FM-1's selected track becomes the plugin's (TRACK: the selected track first).
    mirrorSel = -1;
    if (const auto tr = fm->request(fm1::TRACK); tr && !tr->empty() && (*tr)[0] <= fm1::kDeviceDrumTrack)
    {
        mirrorSel = pluginTrack((*tr)[0]);
        session.selected = mirrorSel;
        session.touch();
    }
    playPresses = 0;
    recPresses = 0;
    juce::String m = juce::String::fromUTF8("En vivo con el FM-1 \xc2\xb7 ") + juce::String(info.version);
    if (device.ties) m << juce::String::fromUTF8(" \xc2\xb7 ") << device.ties << " notas ligadas no se muestran";
    setState(State::Live, m);
    return true;
}

void Fm1Live::disconnect(bool sayGoodbye)
{
    if (fm && sayGoodbye && remoteOn.load())   // switching the link off puts REMOTE back as it was
    {
        fm1::Bytes a { 1, (uint8_t) kRemoteParam };
        const auto v = fm1::encodeV14(remoteBefore < 0 ? 0 : remoteBefore);
        a.insert(a.end(), v.begin(), v.end());
        fm->request(3, a, 150);
    }
    if (fm && sayGoodbye && clockOutOn.load())   // the FM-1's clock settings put back too
    {
        setGlobal(kClockOutParam, clockOutBefore < 0 ? 0 : clockOutBefore);
        setGlobal(kSyncParam, syncBefore < 0 ? 0 : syncBefore);
    }
    clockOutOn = false;
    if (fm && sayGoodbye) fm->request(fm1::WATCH, { 0 }, 150);
    fm.reset();
    liveOps = playing = recording = false;
    remoteOn = 0;   // (the FM-1 keeps its REMOTE setting; switching it on again remembers it anew)
}

void Fm1Live::bridge(bool fmPlaying)
{
    if (!bridgeKnown) { bridgeKnown = true; lastFmPlaying = fmPlaying; return; }
    if (fmPlaying == lastFmPlaying) return;
    lastFmPlaying = fmPlaying;
    if (!session.fm1DrivesFl.load()) return;
    const bool fl = session.hostPlaying.load();
    if (fmPlaying == fl) return;   // FL is already there (its own PLAY / STOP started this)
    const uint8_t msg[3] { VirtualMidiSource::kChannelStatus, fmPlaying ? VirtualMidiSource::kPlayCc : VirtualMidiSource::kStopCc, 127 };
    VirtualMidiSource::get().send(msg, 3);
}

bool Fm1Live::setGlobal(int id, int value)
{
    fm1::Bytes a { 1, (uint8_t) id };
    const auto v = fm1::encodeV14(value);
    a.insert(a.end(), v.begin(), v.end());
    return fm->request(3, a).has_value();
}

std::optional<int> Fm1Live::getGlobal(int id)
{
    const auto g = fm->request(2, { 1, (uint8_t) id });
    if (!g || g->size() < 4) return std::nullopt;
    return fm1::decodeV14((*g)[2], (*g)[3]);
}

bool Fm1Live::midiStep()
{
    if (const int want = clockOutWanted.exchange(-1); want >= 0)
    {
        if (want == 1 && !clockOutOn.load())
        {
            // check the names first (SYNC, CLOCK): these ids are SLOOP 2.7's
            const auto d = fm->request(5, { 1, (uint8_t) kClockOutParam });
            const auto desc = d ? snd::parseDesc(*d) : std::nullopt;
            if (!desc || desc->label != "CLOCK")
            {
                const juce::ScopedLock sl(soundLock);
                soundNotice = juce::String::fromUTF8("Este firmware no tiene CLOCK OUT (SLOOP 2.7 o más nuevo).");
                return d.has_value();
            }
            const auto s = getGlobal(kSyncParam), c = getGlobal(kClockOutParam);
            if (!s || !c) return false;
            syncBefore = *s;
            clockOutBefore = *c;
            if (!setGlobal(kSyncParam, 0) || !setGlobal(kClockOutParam, 1)) return false;
            clockOutOn = true;
        }
        else if (want == 0 && clockOutOn.load())
        {
            if (!setGlobal(kClockOutParam, clockOutBefore < 0 ? 0 : clockOutBefore)) return false;
            if (!setGlobal(kSyncParam, syncBefore < 0 ? 0 : syncBefore)) return false;
            clockOutOn = false;
        }
    }

    std::vector<std::vector<uint8_t>> out;
    { const juce::ScopedLock sl(sendLock); out.swap(sendQueue); }
    for (const auto& m : out) fm->sendNow(juce::MidiMessage(m.data(), (int) m.size()));

    const int want = remoteWanted.exchange(-1);
    if (want < 0) return true;
    auto set = [this](int value) {
        fm1::Bytes a { 1, (uint8_t) kRemoteParam };
        const auto v = fm1::encodeV14(value);
        a.insert(a.end(), v.begin(), v.end());
        return fm->request(3, a).has_value();
    };
    if (want == 1 && !remoteOn.load())
    {
        // Check it is REMOTE on this firmware (by its name), and remember the user's setting.
        const auto d = fm->request(5, { 1, (uint8_t) kRemoteParam });
        const auto desc = d ? snd::parseDesc(*d) : std::nullopt;
        if (!desc || desc->label != "REMOTE")
        {
            const juce::ScopedLock sl(soundLock);
            soundNotice = juce::String::fromUTF8("Este firmware no tiene REMOTE (SLOOP 2.7 o más nuevo).");
            return d.has_value();
        }
        const auto g = fm->request(2, { 1, (uint8_t) kRemoteParam });
        remoteBefore = g && g->size() >= 4 ? fm1::decodeV14((*g)[2], (*g)[3]) : 0;
        if (!set(kRemoteChannel)) return false;
        remoteOn = kRemoteChannel;
    }
    else if (want == 0 && remoteOn.load())
    {
        if (!set(remoteBefore < 0 ? 0 : remoteBefore)) return false;
        remoteOn = 0;
    }
    return true;
}

bool Fm1Live::pushSelection()
{
    const int sel = session.selected.load();
    if (sel == mirrorSel || sel < 0 || sel >= kTracks) return true;
    const auto r = fm->request(fm1::TRACK, { (uint8_t) deviceTrack(sel) });
    if (!r) return false;
    mirrorSel = sel;
    soundDirty = true;
    return true;
}

void Fm1Live::setSoundParam(int scope, int id, int value)
{
    {
        const juce::ScopedLock sl(soundLock);
        sound_.values[soundKey(scope, id)] = value;
        pendingParams[soundKey(scope, id)] = value;
    }
    soundChanged();
    notify();
}

std::optional<snd::Desc> Fm1Live::describe(int scope, int id, int engine, int itype)
{
    // What a parameter is depends on the engine (P_E0..P_E7) and the insert's type (P_IA..P_IC).
    const bool byEngine = scope == 0 && id >= snd::P_E0 && id <= snd::P_E7;
    const bool byInsert = scope == 0 && id >= snd::P_IA && id <= snd::P_IC;
    const long long key = (long long) scope * 1000 + id + (byEngine ? (long long) (engine + 1) * 100000 : 0)
                        + (byInsert ? (long long) (itype + 1) * 10000000 : 0);
    if (const auto it = descCache.find(key); it != descCache.end()) return it->second;
    const auto r = fm->request(5, { (uint8_t) scope, (uint8_t) id });
    if (!r) return std::nullopt;
    auto d = snd::parseDesc(*r);
    if (d) descCache[key] = *d;
    return d;
}

bool Fm1Live::readSound()
{
    const auto r = fm->request(4);   // DUMP: the selected track's engine, preset, parameters, and the globals
    const auto dump = r ? snd::parseDump(*r, info.pCount, info.gCount) : std::nullopt;
    if (!dump) return r.has_value();   // (a short reply: keep the old sound)

    Sound s;
    s.valid = info.pCount == snd::P_COUNT;   // the pages are SLOOP 2.6+'s
    s.track = mirrorSel;
    s.engine = dump->engine;
    s.preset = dump->preset;
    s.drums = dump->engine >= info.nEngines;
    s.engines = info.engines;
    if (!s.drums)
    {
        if (!namesCache.count(dump->engine))
            if (const auto n = fm->request(10, { (uint8_t) dump->engine }))
                if (auto nn = snd::parseNames(*n)) namesCache[dump->engine] = *nn;
        if (const auto it = namesCache.find(dump->engine); it != namesCache.end())
        {
            s.presets = it->second.presets;
            s.edit1 = it->second.edit1;
            s.edit2 = it->second.edit2;
        }
    }
    if (userDirty && !readUserPresets()) return false;
    s.user = userPresets;
    const int itype = snd::P_ITYPE < (int) dump->params.size() ? dump->params[(size_t) snd::P_ITYPE] : 0;
    for (const auto& pg : s.drums ? snd::drumPages() : snd::synthPages())
        for (int id : pg.ids)
        {
            if (id == snd::kNone) continue;
            const auto d = describe(pg.scope, id, dump->engine, itype);
            if (!d) return false;
            const auto& src = pg.scope == 0 ? dump->params : dump->globals;
            s.desc[soundKey(pg.scope, id)] = *d;
            s.values[soundKey(pg.scope, id)] = id < (int) src.size() ? src[(size_t) id] : d->def;
        }
    {
        const juce::ScopedLock sl(soundLock);
        for (const auto& [k, v] : pendingParams) s.values[k] = v;   // the UI's changes still on their way win
        sound_ = std::move(s);
    }
    soundChanged();
    return true;
}

bool Fm1Live::readUserPresets()
{
    std::vector<snd::UserPreset> all;
    int total = 32;
    for (int start = 0; start < total; start += 16)
    {
        const auto r = fm->request(16, { (uint8_t) start, 16 });   // UP_LIST
        if (!r) return false;
        const auto part = snd::parseUserList(*r, total);
        if (!part || part->empty()) break;
        all.insert(all.end(), part->begin(), part->end());
    }
    userPresets = std::move(all);
    userDirty = false;
    return true;
}

bool Fm1Live::soundStep()
{
    if (const int u = pendingUserLoad.exchange(-1); u >= 0)
    {
        const auto r = fm->request(20, { (uint8_t) u });   // UP_LOAD: slot, rc
        if (!r) return false;
        const juce::ScopedLock sl(soundLock);
        soundNotice = r->size() >= 2 && (*r)[1] == 0 ? juce::String::fromUTF8("Preset de usuario U") + juce::String(u + 1).paddedLeft('0', 2) + " cargado."
                                                     : juce::String::fromUTF8("Ese preset de usuario está vacío.");
        soundDirty = true;
    }
    if (const int u = pendingUserStore.exchange(-1); u >= 0)
    {
        std::string name;
        { const juce::ScopedLock sl(soundLock); name = pendingStoreName; }
        fm1::Bytes a { (uint8_t) u };
        for (char c : name) a.push_back((uint8_t) (c & 0x7F));
        a.push_back(0);
        const auto r = fm->request(19, a, 3000);   // UP_STORE: slot, name -> slot, rc (writes flash)
        if (!r) return false;
        const int rc = r->size() >= 2 ? (*r)[1] : -1;
        const juce::ScopedLock sl(soundLock);
        soundNotice = rc == 0 ? juce::String::fromUTF8("Sonido guardado en U") + juce::String(u + 1).paddedLeft('0', 2) + "."
                    : rc == 3 ? juce::String::fromUTF8("Detén la canción del FM-1 para guardar un preset.")
                    : rc == 2 ? juce::String::fromUTF8("El FM-1 no pudo escribir su memoria (queda hasta apagarlo).")
                              : juce::String::fromUTF8("El FM-1 no aceptó ese nombre o espacio.");
        userDirty = true;
        soundDirty = true;
    }
    if (const int e = pendingEngine.exchange(-1); e >= 0)
    {
        // G_ENGSEL: the selected track's engine, with its defaults (a RELOAD follows).
        fm1::Bytes a { 1, (uint8_t) snd::G_ENGSEL };
        const auto v = fm1::encodeV14(e);
        a.insert(a.end(), v.begin(), v.end());
        if (!fm->request(3, a)) return false;
        soundDirty = true;
    }
    if (const int p = pendingPreset.exchange(-1); p >= 0)
    {
        int engine;
        { const juce::ScopedLock sl(soundLock); engine = sound_.engine; }
        if (!fm->request(8, { (uint8_t) engine, (uint8_t) p })) return false;
        soundDirty = true;
    }
    std::map<int, int> writes;
    { const juce::ScopedLock sl(soundLock); writes.swap(pendingParams); }
    for (const auto& [key, value] : writes)
    {
        const int scope = key / 1000, id = key % 1000;
        std::optional<fm1::Bytes> r;
        if (scope == 0) r = fm->request(fm1::TRACK_PARAM, fm1::trackParamSet(deviceTrack(mirrorSel < 0 ? 0 : mirrorSel), id, value));
        else
        {
            fm1::Bytes a { 1, (uint8_t) id };
            const auto v = fm1::encodeV14(value);
            a.insert(a.end(), v.begin(), v.end());
            r = fm->request(3, a);
        }
        if (!r) return false;
        if (r->size() >= 4)   // the value after clamping, as the FM-1 keeps it
        {
            const juce::ScopedLock sl(soundLock);
            if (!pendingParams.count(key)) sound_.values[key] = fm1::decodeV14((*r)[2], (*r)[3]);
        }
        if (scope == 0 && (id == snd::P_ITYPE || id == snd::P_E0)) soundDirty = true;   // other knobs follow these
        soundChanged();
    }
    if (soundDirty && mirrorSel >= 0)
    {
        soundDirty = false;
        if (!readSound()) return false;
    }
    return true;
}

void Fm1Live::takeSelection(int device)
{
    const int t = pluginTrack(device & 3);
    if (t == mirrorSel) return;
    soundDirty = true;
    if (session.selected.load() == mirrorSel || mirrorSel < 0)   // no tab change of ours on its way
    {
        session.selected = t;
        session.touch();
    }
    mirrorSel = t;
}

bool Fm1Live::transport()
{
    if (!liveOps.load()) { playPresses = 0; recPresses = 0; return true; }
    std::optional<fm1::Bytes> r;
    // A press goes out once, never repeated on a timeout (it would toggle back).
    if (playPresses.exchange(0) > 0) r = fm->request(fm1::LIVE, { fm1::LivePlay, fm1::kPlayToggle }, 250, false);
    if (recPresses.exchange(0) > 0) r = fm->request(fm1::LIVE, { fm1::LiveRec, 0 }, 250, false);   // (the firmware runs an op only with 2+ bytes)

    const auto now = juce::Time::getMillisecondCounter();
    if (!r && now - lastStateMs >= (juce::uint32) kStateEveryMs)
    {
        r = fm->request(fm1::LIVE, { fm1::LiveState });
        if (!r) return false;
    }
    if (r)
    {
        lastStateMs = lastPingMs = now;   // any request keeps WATCH alive
        if (const auto s = fm1::parseLive(*r))
        {
            playing = s->playing;
            bridge(s->playing);
            songMode = s->songMode;
            recording = s->recWaiting || s->recTracks != 0;
            bpm = s->bpm;
            takeSelection(s->selected);
        }
    }
    return true;
}

bool Fm1Live::pushLocalEdits()
{
    auto cur = Fm1Pattern::fromSession(session);

    for (int s = 0; s < steps; ++s)
        if (!cur.sameDrumStep(mirror, s))
        {
            if (!writeDrumStep(*fm, s, cur)) return false;
            mirror.drumOn[s] = cur.drumOn[s];
            mirror.drumLevel[s] = cur.drumLevel[s] & ~fm1::levelMaskOff(cur.drumOn[s]);
            mirror.drumRoll[s] = cur.drumRoll[s] & ~fm1::levelMaskOff(cur.drumOn[s]);
        }

    for (int t = 1; t < kTracks; ++t)
        for (int s = 0; s < steps; ++s)
            if (!cur.sameSynthStep(mirror, t, s))
            {
                const uint64_t lo = cur.lo[t][s], hi = cur.hi[t][s];
                if (!writeSynthStep(*fm, t, s, cur)) return false;   // keeps 4 notes at most
                if (cur.lo[t][s] != lo || cur.hi[t][s] != hi) cur.applySynthStep(session, t, s);
                mirror.lo[t][s] = cur.lo[t][s];
                mirror.hi[t][s] = cur.hi[t][s];
            }

    for (int t = 0; t < kTracks; ++t)
    {
        if (cur.length[t] != mirror.length[t])
        {
            if (!writeTrackParam(*fm, t, fm1::P_SLEN, cur.length[t])) return false;
            mirror.length[t] = cur.length[t];
        }
        if (cur.muted[t] != mirror.muted[t])
        {
            if (!writeTrackParam(*fm, t, fm1::P_MUTE, cur.muted[t])) return false;
            mirror.muted[t] = cur.muted[t];
        }
    }
    return true;
}

bool Fm1Live::handlePushes()
{
    bool params = false;
    for (const auto& [cmd, a] : fm->takePushes())
    {
        if (cmd == fm1::STEP_CHANGED)
        {
            if (a.size() >= 2 && a[0] < steps && a[1] <= fm1::kDeviceDrumTrack)
            {
                if (!refreshStep(pluginTrack(a[1]), a[0])) return false;
            }
            else lastPollMs = 0;   // no track in the push (old firmware): re-read everything
        }
        else if (cmd == fm1::RELOAD)                   // a project, a sound or the selection: re-read all
        {
            lastPollMs = 0;
            soundDirty = true;
            if (a.size() >= 3 && a[2] <= fm1::kDeviceDrumTrack) takeSelection(a[2]);
        }
        else
        {
            params = true;                             // CHANGED / TRACK_CHANGED: length or mute may have moved
            if (cmd == fm1::CHANGED && a.size() >= 4)  // a knob on the FM-1 (the selected track, or a global)
            {
                const int scope = a[0], id = a[1], v = fm1::decodeV14(a[2], a[3]);
                {
                    const juce::ScopedLock sl(soundLock);
                    if (sound_.values.count(soundKey(scope, id))) sound_.values[soundKey(scope, id)] = v;
                }
                if (scope == 0 && id == snd::P_ITYPE) soundDirty = true;   // the insert's knobs change with it
                soundChanged();
            }
        }
    }
    return !params || refreshParams();
}

bool Fm1Live::refreshStep(int t, int s)
{
    Fm1Pattern device = mirror;
    if (t == kDrums ? !readDrumStep(*fm, s, device) : !readSynthStep(*fm, t, s, device)) return false;
    takeFromDevice(device);
    return true;
}

bool Fm1Live::refreshParams()
{
    Fm1Pattern device = mirror;
    if (!readTrackParams(*fm, device)) return false;
    takeFromDevice(device);
    return true;
}

bool Fm1Live::poll()
{
    Fm1Pattern device = mirror;
    juce::String err;
    if (!readPattern(*fm, steps, device, err)) return false;
    takeFromDevice(device);
    lastPollMs = juce::Time::getMillisecondCounter();
    return true;
}

void Fm1Live::takeFromDevice(const Fm1Pattern& device)
{
    // Only where the Session still equals the mirror: a step the user just edited in Floop Studio
    // keeps the edit, and it goes to the FM-1 on the next pass.
    const auto cur = Fm1Pattern::fromSession(session);
    bool changed = false;

    for (int s = 0; s < steps; ++s)
    {
        if (!device.sameDrumStep(mirror, s) && cur.sameDrumStep(mirror, s))
        {
            device.applyDrumStep(session, s);
            changed = true;
        }
        if (cur.sameDrumStep(mirror, s))
        {
            mirror.drumOn[s] = device.drumOn[s];
            mirror.drumLevel[s] = device.drumLevel[s];
            mirror.drumRoll[s] = device.drumRoll[s];
        }
        for (int t = 1; t < kTracks; ++t)
        {
            if (!cur.sameSynthStep(mirror, t, s)) continue;
            if (!device.sameSynthStep(mirror, t, s)) { device.applySynthStep(session, t, s); changed = true; }
            mirror.lo[t][s] = device.lo[t][s];
            mirror.hi[t][s] = device.hi[t][s];
        }
    }
    for (int t = 0; t < kTracks; ++t)
    {
        if (cur.length[t] == mirror.length[t])
        {
            if (device.length[t] != mirror.length[t]) { session.length[t] = (uint8_t) device.length[t]; changed = true; }
            mirror.length[t] = device.length[t];
        }
        if (cur.muted[t] == mirror.muted[t])
        {
            if (device.muted[t] != mirror.muted[t]) { session.muted[t] = (uint8_t) device.muted[t]; changed = true; }
            mirror.muted[t] = device.muted[t];
        }
    }
    if (changed) session.touch();
}

} // namespace sloop
