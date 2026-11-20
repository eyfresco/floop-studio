// SPDX-License-Identifier: GPL-3.0-only
// Live link with the FM-1, like the SLOOP web editor: while it is on, an edit in Floop Studio is
// written to the FM-1 at once, and an edit on the FM-1 (recording, clearing, a step edit, a
// project load) shows up in Floop Studio.
//
// How it stays in sync, on its own thread:
//  - `mirror` is the pattern the FM-1 is known to hold. Where the Session differs from it, the
//    user edited Floop Studio: that step is written to the FM-1 (the plugin wins a tie).
//  - The FM-1 pushes STEP_CHANGED for its selected track (WATCH), so that step is re-read at once.
//    It says nothing about the other tracks, so the whole pattern is re-read every 2.5 s too.
//  - WATCH stops 3 s after the last request: a PING goes out every second.
//
// One link for the whole process (juce::SharedResourcePointer): every Floop Studio instance shares
// the Session, so they share the FM-1 too.
#pragma once
#include "FM1Import.h"
#include "SoundModel.h"
#include <map>

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
    // --- SONIDO: the selected track's sound, as the SLOOP Editor's Sound page ---
    struct Sound
    {
        bool valid = false, drums = false;
        int track = -1, engine = 0, preset = 0;
        std::vector<std::string> engines, presets;
        std::string edit1 = "EDIT 1", edit2 = "EDIT 2";
        std::map<int, snd::Desc> desc;   // key: scope * 1000 + id
        std::map<int, int> values;       // key: scope * 1000 + id
        std::vector<snd::UserPreset> user;   // the FM-1's 32 user presets
    };
    static int soundKey(int scope, int id) { return scope * 1000 + id; }
    Sound sound() const { const juce::ScopedLock sl(soundLock); return sound_; }
    uint32_t soundRevision() const { return soundRev.load(); }
    // From the UI: the value shows at once here, goes to the FM-1 on the link's next pass (last one wins).
    void setSoundParam(int scope, int id, int value);
    void chooseEngine(int engine) { pendingEngine = engine; notify(); }
    void choosePreset(int preset) { pendingPreset = preset; notify(); }
    // The FM-1's user presets (U01..U32): load one on the selected track, or store its sound in one.
    void loadUserPreset(int slot) { pendingUserLoad = slot; notify(); }
    void storeUserPreset(int slot, const std::string& name)
    {
        { const juce::ScopedLock sl(soundLock); pendingStoreName = name; }
        pendingUserStore = slot;
        notify();
    }
    juce::String takeSoundNotice() { const juce::ScopedLock sl(soundLock); auto n = soundNotice; soundNotice.clear(); return n; }

    // --- MAPA MIDI ---
    // The keys the FM-1 is holding (its notes, as they come in on its port).
    bool noteHeld(int n) const { return n >= 0 && n < 128 && ((n < 64 ? heldLo.load() : heldHi.load()) >> (n & 63)) & 1u; }
    uint32_t noteRevision() const { return noteRev.load(); }
    // The last note the FM-1 sent: note | channel << 8 | (on ? 1 << 16 : 0); -1 none yet.
    int lastNote() const { return lastNoteValue.load(); }
    // A MIDI message to the FM-1 (a REMOTE button, a key), sent on the link's next pass.
    void sendMidi(std::initializer_list<uint8_t> bytes)
    {
        { const juce::ScopedLock sl(sendLock); sendQueue.emplace_back(bytes); }
        notify();
    }
    // REMOTE (GLO > MIDI 3): the FM-1's 14 buttons from CC 102..115 on this channel. On: the FM-1's
    // setting is remembered and set to kRemoteChannel; off: put back.
    static constexpr int kRemoteChannel = 16, kRemoteCc0 = 102, kRemoteParam = 47, kFxChParam = 45;
    void setRemote(bool on) { remoteWanted = on ? 1 : 0; notify(); }
    int remoteChannel() const { return remoteOn.load(); }   // 0: off

    // The FM-1 as FL's clock: SYNC = INT and CLOCK (GLO > MIDI 3, "CLK OUT") = ON, so the FM-1 sends its
    // tempo, START and STOP on USB, and FL follows it (FL: MIDI Settings > External sync). Off: both
    // settings put back.
    static constexpr int kSyncParam = 13, kClockOutParam = 48;
    void setClockOut(bool on) { clockOutWanted = on ? 1 : 0; notify(); }
    bool clockOut() const { return clockOutOn.load(); }

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

    juce::CriticalSection soundLock;
    Sound sound_;
    std::atomic<uint32_t> soundRev { 0 };
    std::map<int, int> pendingParams;                    // (under soundLock)
    std::atomic<int> pendingEngine { -1 }, pendingPreset { -1 }, pendingUserLoad { -1 }, pendingUserStore { -1 };
    std::string pendingStoreName;                        // (under soundLock)
    juce::String soundNotice;                            // (under soundLock) a result for the UI
    std::vector<snd::UserPreset> userPresets;
    bool userDirty = true;
    bool readUserPresets();
    bool soundDirty = true;
    std::map<long long, snd::Desc> descCache;            // per connection
    std::map<int, snd::Names> namesCache;
    bool soundStep();                                    // writes the UI's changes, re-reads when needed
    bool readSound();
    std::optional<snd::Desc> describe(int scope, int id, int engine, int itype);
    void soundChanged() { soundRev.fetch_add(1); }
    // The FM-1's PLAY / STOP -> FL's, through the "Floop Studio" MIDI source and FL's script: only
    // when the FM-1 changed on its own (FL not already there), so FL's own PLAY never echoes back.
    bool bridgeKnown = false, lastFmPlaying = false;
    void bridge(bool fmPlaying);
    // The selected track, as in the SLOOP Editor: a tab chosen here selects that track on the FM-1,
    // and a track chosen on the FM-1 selects its tab here. mirrorSel = the FM-1's, as last known.
    int mirrorSel = -1;
    bool pushSelection();
    std::atomic<uint64_t> heldLo { 0 }, heldHi { 0 };
    std::atomic<uint32_t> noteRev { 0 };
    std::atomic<int> lastNoteValue { -1 };
    juce::CriticalSection sendLock;
    std::vector<std::vector<uint8_t>> sendQueue;
    std::atomic<int> remoteWanted { -1 }, remoteOn { 0 }, clockOutWanted { -1 };
    std::atomic<bool> clockOutOn { false };
    int syncBefore = -1, clockOutBefore = -1;
    bool setGlobal(int id, int value);
    std::optional<int> getGlobal(int id);
    int remoteBefore = -1;
    bool midiStep();   // sends the queue, switches REMOTE
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
