// SPDX-License-Identifier: GPL-3.0-only
// Sends FL's tempo and PLAY / STOP to the FM-1 as MIDI clock (ClockEngine), on its own thread and
// its own connection to the FM-1. Switching it on also sets the FM-1's GLO > SYSTEM > SYNC to USB
// (and puts the old value back when it is switched off).
//
// One clock for the whole process (juce::SharedResourcePointer). Every Floop Studio instance
// publishes FL's transport from its audio thread; they all see the same transport.
#pragma once
#include "FM1Import.h"
#include "ClockEngine.h"

namespace sloop {

class Fm1Clock : private juce::Thread
{
public:
    enum class State { Off, Connecting, On, Error };

    Fm1Clock() : juce::Thread("SLOOP FM-1 clock", 0) {}
    ~Fm1Clock() override { setEnabled(false); }

    void setEnabled(bool on);
    bool isEnabled() const { return enabled.load(); }
    State state() const { return (State) stateValue.load(); }
    juce::String message() const { const juce::ScopedLock sl(msgLock); return msg; }

    // Audio thread: never blocks (a contended update is skipped; the next block brings a fresh one).
    void publish(bool playing, double ppq, double bpm, int num, int den)
    {
        const juce::SpinLock::ScopedTryLockType tl(transportLock);
        if (!tl.isLocked()) return;
        transport = { true, playing, ppq, bpm, juce::Time::getMillisecondCounterHiRes(), num, den };
    }

    // For the UI: the FM-1 waits for the next bar line of FL to start.
    bool waitingForBar() const { return waiting.load(); }

    static constexpr int kSyncParam = 13;   // G_SYNC: 0 INT, 1 USB, 2 TRS (firmware/src/params.c)

private:
    std::atomic<bool> enabled { false }, waiting { false };
    std::atomic<int> stateValue { (int) State::Off };
    juce::CriticalSection msgLock;
    juce::String msg;

    juce::SpinLock transportLock;
    HostTransport transport;

    std::unique_ptr<Fm1Connection> fm;
    ClockEngine engine;
    int previousSync = -1;
    juce::uint32 lastDeviceCheckMs = 0;

    void run() override;
    void setState(State, const juce::String&);
    bool connect();
    void disconnect();
};

} // namespace sloop
