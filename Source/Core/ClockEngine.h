// SPDX-License-Identifier: GPL-3.0-only
// MIDI clock for the FM-1, from FL's transport: 24 pulses a quarter note, START / STOP.
//
// SLOOP follows a clock pulse by pulse (GLO > SYSTEM > SYNC = USB) but knows no song position:
// START always plays from the top of its pattern. So START goes out on a bar line of FL: at once
// when FL starts on one, otherwise on the next one (the pulses before it keep the FM-1 at FL's
// tempo). While FL is stopped the pulses go on at FL's tempo, so the FM-1 shows the right BPM.
//
// advance() is called about every millisecond from a timer thread with the host transport the
// audio thread last published; it returns the realtime bytes to send now.
#pragma once
#include <cmath>
#include <cstdint>

namespace sloop {

struct HostTransport
{
    bool valid = false;
    bool playing = false;
    double ppq = 0.0;        // at atMs
    double bpm = 120.0;
    double atMs = 0.0;       // when the audio thread saw that position
    int num = 4, den = 4;    // time signature (the bar START waits for)
};

class ClockEngine
{
public:
    static constexpr uint8_t kClock = 0xF8, kStart = 0xFA, kStop = 0xFC;
    static constexpr double kHostGoneMs = 300.0;   // no audio block for this long: FL is not processing

    struct Out { uint8_t bytes[24]; int count = 0; void add(uint8_t b) { if (count < 24) bytes[count++] = b; } };

    Out advance(double nowMs, const HostTransport& t)
    {
        Out out;
        const bool hostAlive = t.valid && t.bpm > 0.0 && nowMs - t.atMs < kHostGoneMs;
        if (!hostAlive)
        {
            if (running) out.add(kStop);
            running = pendingStart = wasPlaying = false;
            nextFreeMs = 0.0;
            return out;
        }

        if (t.playing)
        {
            const double ppq = t.ppq + (nowMs - t.atMs) * t.bpm / 60000.0;
            const bool jumped = wasPlaying && (ppq < lastPpq - 0.05 || ppq > lastPpq + 1.0);
            if (!wasPlaying || jumped)
            {
                if (running) out.add(kStop);
                running = false;
                pendingStart = true;
                const double bar = barLength(t);
                const double b = ppq / bar;
                const double into = (b - std::floor(b)) * bar;
                startTick = (int64_t) std::llround((into < 1.0 / 24.0 ? std::floor(b) : std::ceil(b)) * bar * 24.0);
                lastTick = (int64_t) std::floor(ppq * 24.0 + 1e-9) - 1;
                if (lastTick >= startTick) lastTick = startTick - 1;   // started on the bar line: its pulse is now
            }
            const int64_t tickNow = (int64_t) std::floor(ppq * 24.0 + 1e-9);
            int sent = 0;
            for (int64_t tick = lastTick + 1; tick <= tickNow && sent < 8; ++tick, ++sent)
            {
                if (pendingStart && tick >= startTick)
                {
                    out.add(kStart);   // then the downbeat's pulse, as SLOOP's own CLOCK OUT does
                    pendingStart = false;
                    running = true;
                }
                out.add(kClock);
            }
            lastTick = tickNow;
            lastPpq = ppq;
            nextFreeMs = 0.0;
        }
        else
        {
            if (running) out.add(kStop);
            running = pendingStart = false;
            const double every = 60000.0 / (t.bpm * 24.0);
            if (nextFreeMs <= 0.0 || nowMs - nextFreeMs > 100.0) nextFreeMs = nowMs;   // (re)start the free pulses
            for (int sent = 0; nowMs >= nextFreeMs && sent < 8; ++sent)
            {
                out.add(kClock);
                nextFreeMs += every;
            }
        }
        wasPlaying = t.playing;
        return out;
    }

    // Sent when the clock is switched off: the FM-1 must not keep playing.
    bool stopIfRunning(Out& out) { const bool r = running; if (r) out.add(kStop); running = pendingStart = wasPlaying = false; return r; }

    bool isRunning() const { return running; }
    bool waitingForBar() const { return pendingStart; }

private:
    bool wasPlaying = false, running = false, pendingStart = false;
    int64_t lastTick = 0, startTick = 0;
    double lastPpq = 0.0, nextFreeMs = 0.0;

    static double barLength(const HostTransport& t)
    {
        const double b = t.num > 0 && t.den > 0 ? t.num * 4.0 / t.den : 4.0;
        return b > 0.0 ? b : 4.0;
    }
};

} // namespace sloop
