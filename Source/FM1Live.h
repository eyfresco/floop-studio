// SPDX-License-Identifier: GPL-3.0-only
// Live link with the FM-1, like the SLOOP web editor: while it is on, an edit in SLOOP Studio is
// written to the FM-1 at once, and an edit on the FM-1 (recording, clearing, a step edit, a
// project load) shows up in SLOOP Studio.
//
// How it stays in sync, on its own thread:
//  - `mirror` is the pattern the FM-1 is known to hold. Where the Session differs from it, the
//    user edited SLOOP Studio: that step is written to the FM-1 (the plugin wins a tie).
//  - The FM-1 pushes STEP_CHANGED for its selected track (WATCH), so that step is re-read at once.
//    It says nothing about the other tracks, so the whole pattern is re-read every 2.5 s too.
//  - WATCH stops 3 s after the last request: a PING goes out every second.
//
// One link for the whole process (juce::SharedResourcePointer): every SLOOP Studio instance shares
// the Session, so they share the FM-1 too.
#pragma once
#include "FM1Import.h"

namespace sloop {

class Fm1Live : private juce::Thread
{
public:
    enum class State { Off, Connecting, Live, Error };

    Fm1Live() : juce::Thread("SLOOP FM-1 live") {}
    ~Fm1Live() override { setEnabled(false); }

    // On: connects (retrying every 2 s until the FM-1 answers), takes the FM-1's pattern, then
    // keeps both in sync. Off: stops watching and closes the ports.
    void setEnabled(bool on);
    bool isEnabled() const { return enabled.load(); }

    State state() const { return (State) stateValue.load(); }
    bool isLive() const { return state() == State::Live; }
    juce::String message() const { const juce::ScopedLock sl(msgLock); return msg; }

    // The FM-1's PLAY and REC, as its own buttons (LIVE, SLOOP 2.7 and later).
    void pressPlay() { playPresses.fetch_add(1); notify(); }
    void pressRec() { recPresses.fetch_add(1); notify(); }
    bool hasTransport() const { return liveOps.load(); }
    bool fm1Playing() const { return playing.load(); }
    bool fm1Recording() const { return recording.load(); }
    // The FM-1 refuses REC while its song plays (it shows STOP THE SONG FIRST).
    bool recBlockedBySong() const { return playing.load() && songMode.load(); }
    int fm1Bpm() const { return bpm.load(); }

    // While another job talks to the FM-1 (a sample upload): the link stops sending, keeps the
    // edits made meanwhile, and on resume watches again and sends them. pause() waits (up to 1 s)
    // until the link's request in flight is done.
    void pause()
    {
        paused = true;
        for (int i = 0; i < 100 && isEnabled() && !idle.load() && isThreadRunning(); ++i) juce::Thread::sleep(10);
    }
    void resume() { paused = false; notify(); }

    static constexpr int kPollEveryMs = 2500;
    static constexpr int kStateEveryMs = 250;
    static constexpr int kRetryEveryMs = 2000;

private:
    Session& session = Session::shared();
    std::unique_ptr<Fm1Connection> fm;
    fm1::Info info;
    Fm1Pattern mirror;
    int steps = kSteps;
    juce::uint32 lastPollMs = 0, lastPingMs = 0, lastStateMs = 0;
    std::atomic<int> playPresses { 0 }, recPresses { 0 };
    std::atomic<bool> liveOps { false }, playing { false }, recording { false }, songMode { false };
    std::atomic<int> bpm { 0 };
    bool transport();   // sends the pressed buttons, reads PLAY / REC
    // The FM-1's PLAY / STOP -> FL's, through the "SLOOP Studio" MIDI source and FL's script: only
    // when the FM-1 changed on its own (FL not already there), so FL's own PLAY never echoes back.
    bool bridgeKnown = false, lastFmPlaying = false;
    void bridge(bool fmPlaying);
    // The selected track, as in the SLOOP Editor: a tab chosen here selects that track on the FM-1,
    // and a track chosen on the FM-1 selects its tab here. mirrorSel = the FM-1's, as last known.
    int mirrorSel = -1;
    bool pushSelection();
    void takeSelection(int deviceTrack);

    std::atomic<bool> enabled { false }, paused { false }, idle { false };
    bool rewatch = false;
    std::atomic<int> stateValue { (int) State::Off };
    juce::CriticalSection msgLock;
    juce::String msg;

    void run() override;
    void setState(State, const juce::String&);
    bool connect();
    void disconnect(bool sayGoodbye);

    bool pushLocalEdits();   // Session != mirror  ->  write to the FM-1
    bool handlePushes();     // the FM-1's notices ->  re-read what changed
    bool poll();             // the whole pattern, for what the FM-1 does not announce
    bool refreshStep(int track, int step);
    bool refreshParams();
    void takeFromDevice(const Fm1Pattern& device);   // apply what changed on the FM-1 where the user has no pending edit

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Fm1Live)
};

} // namespace sloop
