// SPDX-License-Identifier: GPL-3.0-only
#include "FM1Clock.h"

namespace sloop {

void Fm1Clock::setEnabled(bool on)
{
    if (on == enabled.load()) return;
    enabled = on;
    if (on)
    {
        setState(State::Connecting, juce::String::fromUTF8("Conectando el reloj con el FM-1…"));
        startThread(juce::Thread::Priority::highest);
    }
    else
    {
        signalThreadShouldExit();
        notify();
        stopThread(3000);
        setState(State::Off, {});
    }
}

void Fm1Clock::setState(State s, const juce::String& m)
{
    stateValue = (int) s;
    const juce::ScopedLock sl(msgLock);
    msg = m;
}

bool Fm1Clock::connect()
{
    fm = std::make_unique<Fm1Connection>();
    juce::String err;
    if (!fm->open(err))
    {
        fm.reset();
        setState(State::Error, err);
        return false;
    }

    // GLO > SYSTEM > SYNC = USB, remembering what it was (GET / SET, scope 1 = global).
    const auto got = fm->request(2, { 1, (uint8_t) kSyncParam });
    if (!got || got->size() < 4)
    {
        fm.reset();
        setState(State::Error, juce::String::fromUTF8("El FM-1 no responde (¿tiene SLOOP instalado?)."));
        return false;
    }
    previousSync = fm1::decodeV14((*got)[2], (*got)[3]);
    fm1::Bytes set { 1, (uint8_t) kSyncParam };
    const auto one = fm1::encodeV14(1);
    set.insert(set.end(), one.begin(), one.end());
    if (!fm->request(3, set))
    {
        fm.reset();
        setState(State::Error, juce::String::fromUTF8("No pude poner SYNC = USB en el FM-1."));
        return false;
    }

    engine = ClockEngine();
    lastDeviceCheckMs = juce::Time::getMillisecondCounter();
    setState(State::On, juce::String::fromUTF8("Reloj de FL \xe2\x86\x92 FM-1 (SYNC = USB)"));
    return true;
}

void Fm1Clock::disconnect()
{
    if (!fm) return;
    ClockEngine::Out out;
    if (engine.stopIfRunning(out))
        for (int i = 0; i < out.count; ++i) fm->sendNow(juce::MidiMessage(out.bytes[i]));
    if (previousSync >= 0 && previousSync != 1)   // put SYNC back as the user had it
    {
        fm1::Bytes set { 1, (uint8_t) kSyncParam };
        const auto v = fm1::encodeV14(previousSync);
        set.insert(set.end(), v.begin(), v.end());
        fm->request(3, set, 150);
    }
    previousSync = -1;
    fm.reset();
}

void Fm1Clock::run()
{
    while (!threadShouldExit())
    {
        if (!fm && !connect())
        {
            wait(2000);
            continue;
        }

        HostTransport t;
        {
            const juce::SpinLock::ScopedLockType sl(transportLock);
            t = transport;
        }
        const auto out = engine.advance(juce::Time::getMillisecondCounterHiRes(), t);
        for (int i = 0; i < out.count; ++i) fm->sendNow(juce::MidiMessage(out.bytes[i]));
        waiting = engine.waitingForBar();

        const auto now = juce::Time::getMillisecondCounter();
        if (now - lastDeviceCheckMs > 2000)
        {
            lastDeviceCheckMs = now;
            if (!fm->stillConnected())
            {
                fm.reset();
                previousSync = -1;
                setState(State::Error, juce::String::fromUTF8("Se desconectó el FM-1. Reintentando…"));
                continue;
            }
        }
        wait(1);
    }
    disconnect();
}

} // namespace sloop
