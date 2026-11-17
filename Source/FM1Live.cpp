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
        bool ok = pushSelection() && transport() && pushLocalEdits() && handlePushes();
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
    setState(State::Connecting, juce::String::fromUTF8("Conectando con el FM-1…"));
    fm = std::make_unique<Fm1Connection>();
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
    if (fm && sayGoodbye) fm->request(fm1::WATCH, { 0 }, 150);
    fm.reset();
    liveOps = playing = recording = false;
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

bool Fm1Live::pushSelection()
{
    const int sel = session.selected.load();
    if (sel == mirrorSel || sel < 0 || sel >= kTracks) return true;
    const auto r = fm->request(fm1::TRACK, { (uint8_t) deviceTrack(sel) });
    if (!r) return false;
    mirrorSel = sel;
    return true;
}

void Fm1Live::takeSelection(int device)
{
    const int t = pluginTrack(device & 3);
    if (t == mirrorSel) return;
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
            if (a.size() >= 3 && a[2] <= fm1::kDeviceDrumTrack) takeSelection(a[2]);
        }
        else params = true;                            // CHANGED / TRACK_CHANGED: length or mute may have moved
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
    // Only where the Session still equals the mirror: a step the user just edited in SLOOP Studio
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
