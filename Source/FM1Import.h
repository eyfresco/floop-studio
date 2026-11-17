// SPDX-License-Identifier: GPL-3.0-only
// Talks to a connected FM-1 (SLOOP firmware) over its USB-MIDI ports: reads and writes the
// pattern (drums with levels and rolls, the three synth tracks' notes, each track's length and
// mute). Used by the one-shot import and by the live link (FM1Live.h).
#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include "Fm1Protocol.h"
#include "Session.h"
#include "MacMidiOut.h"

namespace sloop {

// One request at a time, waiting for the reply with the same cmd. Pushes (CHANGED, RELOAD,
// STEP_CHANGED, TRACK_CHANGED) are kept in a queue for takePushes().
class Fm1Connection : private juce::MidiInputCallback
{
public:
    ~Fm1Connection() override;

    // Opens the first in/out pair whose name looks like the FM-1 ("Felucca", "SLOOP", "FM-1").
    bool open(juce::String& error);
    juce::String portName() const { return name; }

    // retry = false for requests that toggle something on the FM-1 (PLAY, REC): never sent twice.
    std::optional<fm1::Bytes> request(uint8_t cmd, const fm1::Bytes& args = {}, int timeoutMs = 250, bool retry = true);

    // A plain MIDI message (the clock's realtime bytes), no reply.
    void sendNow(const juce::MidiMessage& m);

    // The FM-1's ports are still there (unplugged: false).
    bool stillConnected() const;

    using Push = std::pair<uint8_t, fm1::Bytes>;
    std::vector<Push> takePushes();

private:
    std::unique_ptr<juce::MidiInput> in;
    std::unique_ptr<juce::MidiOutput> out;
    std::unique_ptr<RawMidiOut> raw;   // macOS: MIDI 1.0 API (see MacMidiOut.h); else JUCE's output
    juce::String name;

    juce::CriticalSection lock;
    int waitingFor = -1;
    fm1::Bytes reply;
    juce::WaitableEvent replied;
    std::vector<Push> pushes;

    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage&) override;
};

// The pattern as the FM-1 holds it (first kSteps steps). Index 0 of the synth arrays is unused:
// track numbers are the plugin's (0 DRUMS, 1..3 SYNTH 1..3).
struct Fm1Pattern
{
    uint16_t drumOn[kSteps] {};
    uint32_t drumLevel[kSteps] {}, drumRoll[kSteps] {};
    uint64_t lo[kTracks][kSteps] {}, hi[kTracks][kSteps] {};
    int length[kTracks] { kSteps, kSteps, kSteps, kSteps };
    int muted[kTracks] {};
    int ties = 0;   // TIE steps read (a tie holds the previous note: not shown in the plugin yet)

    static Fm1Pattern fromSession(const Session&);
    void applyTo(Session&) const;
    void applyDrumStep(Session&, int step) const;
    void applySynthStep(Session&, int track, int step) const;

    bool sameDrumStep(const Fm1Pattern& o, int s) const
    {
        return drumOn[s] == o.drumOn[s] && (drumLevel[s] & ~fm1::levelMaskOff(drumOn[s])) == (o.drumLevel[s] & ~fm1::levelMaskOff(o.drumOn[s]))
            && (drumRoll[s] & ~fm1::levelMaskOff(drumOn[s])) == (o.drumRoll[s] & ~fm1::levelMaskOff(o.drumOn[s]));
    }
    bool sameSynthStep(const Fm1Pattern& o, int t, int s) const { return lo[t][s] == o.lo[t][s] && hi[t][s] == o.hi[t][s]; }
};

inline int deviceTrack(int track) { return track == kDrums ? fm1::kDeviceDrumTrack : track - 1; }
inline int pluginTrack(int device) { return device == fm1::kDeviceDrumTrack ? kDrums : device + 1; }

// Reads (false: no reply). The step / track ones fill only their part of the pattern.
bool readInfo(Fm1Connection&, fm1::Info&, juce::String& error);
bool readDrumStep(Fm1Connection&, int step, Fm1Pattern&);
bool readSynthStep(Fm1Connection&, int track, int step, Fm1Pattern&);
bool readTrackParams(Fm1Connection&, Fm1Pattern&);
bool readPattern(Fm1Connection&, int steps, Fm1Pattern&, juce::String& error);

// Writes (false: no reply). writeSynthStep sends the 4 lowest notes at most (the FM-1's limit) and
// leaves in `p` what the FM-1 now holds.
bool writeDrumStep(Fm1Connection&, int step, const Fm1Pattern&);
bool writeSynthStep(Fm1Connection&, int track, int step, Fm1Pattern& p);
bool writeTrackParam(Fm1Connection&, int track, int id, int value);

struct ImportResult
{
    bool ok = false;
    juce::String message;   // shown in the UI, in Spanish
};

// One-shot import (blocking: run it on a background thread). On success the Session holds the
// FM-1's pattern.
ImportResult importFromFm1(Session& session);

} // namespace sloop
