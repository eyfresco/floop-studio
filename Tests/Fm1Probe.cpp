// SPDX-License-Identifier: GPL-3.0-only
// Reads the pattern of the connected FM-1 and prints it (the same reads the plugin uses).
//   sloop_fm1_probe              one read, then quit. Read only.
//   sloop_fm1_probe escritura    writes one drum hit and takes it back (the step ends as it was).
//   sloop_fm1_probe reloj        FL's clock -> the FM-1: the FM-1 plays 4 s at 120 BPM, then stops.
//   sloop_fm1_probe transporte   the FM-1's PLAY and REC through the live link (on, then off again).
//   sloop_fm1_probe pista        the selected track: plugin tab -> FM-1, then back as it was.
//   sloop_fm1_probe samples      what USR1..USR4 hold (read only).
//   sloop_fm1_probe subir N nombre archivos...   ERASES USR N and uploads the files as an instrument.
//   sloop_fm1_probe vivo [s]     the live link for s seconds (default 30): prints the DRUMS grid
//                                each time something changes on the FM-1. It only listens: with
//                                nothing edited on this side, it never writes to the FM-1.
#include <juce_audio_devices/juce_audio_devices.h>
#include "FM1Live.h"
#include "FM1Clock.h"
#include "SampleTools.h"

using namespace sloop;

static void printDrums(const Session& s)
{
    for (int l = 0; l < kLanes; ++l)
    {
        bool any = false;
        for (int st = 0; st < kSteps; ++st) any |= s.drumOn(st, l);
        if (!any) continue;
        std::printf("  %-9s", kLaneNames[l]);
        for (int st = 0; st < kSteps; ++st) std::printf("%c", s.drumOn(st, l) ? "NgsH"[s.laneLevel(st, l)] : '.');
        std::printf("\n");
    }
}

static int live(int seconds)
{
    juce::SharedResourcePointer<Fm1Live> link;
    auto& s = Session::shared();
    link->setEnabled(true);
    juce::String lastMsg;
    uint32_t lastRev = s.revision.load();
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) seconds * 1000u;
    while (juce::Time::getMillisecondCounter() < end)
    {
        if (const auto m = link->message(); m != lastMsg) { lastMsg = m; std::printf("[estado] %s\n", m.toRawUTF8()); }
        if (const auto r = s.revision.load(); r != lastRev && link->isLive())
        {
            lastRev = r;
            std::printf("[cambio] DRUMS:\n");
            printDrums(s);
        }
        juce::Thread::sleep(100);
    }
    link->setEnabled(false);
    return 0;
}

// Writes one drum hit to the FM-1 and takes it back, reading after each write: the step ends as
// it started. OPEN HAT on step 5 unless that hit is already there.
static int writeRoundTrip()
{
    Fm1Connection fm;
    juce::String err;
    fm1::Info info;
    if (!fm.open(err) || !readInfo(fm, info, err)) { std::printf("ERROR %s\n", err.toRawUTF8()); return 1; }
    const int step = 4, lane = 5;
    Fm1Pattern before, p, check;
    if (!readDrumStep(fm, step, before)) { std::printf("ERROR lectura\n"); return 1; }
    if ((before.drumOn[step] >> lane) & 1u) { std::printf("OPEN HAT ya está en el paso 5: no toco nada\n"); return 0; }

    p = before;
    p.drumOn[step] |= (uint16_t) (1u << lane);
    p.drumLevel[step] |= (uint32_t) LvHard << (2 * lane);   // HARD x3: checks level and roll too
    p.drumRoll[step] |= 2u << (2 * lane);
    const bool w1 = writeDrumStep(fm, step, p) && readDrumStep(fm, step, check);
    const bool ok1 = w1 && check.sameDrumStep(p, step);
    std::printf("escribir OPEN HAT HARD x3 en paso 5: %s (FM-1: on=%04x lvl=%08x rat=%08x)\n", ok1 ? "OK" : "FALLO",
                check.drumOn[step], check.drumLevel[step], check.drumRoll[step]);

    const bool w2 = writeDrumStep(fm, step, before) && readDrumStep(fm, step, check);
    const bool ok2 = w2 && check.sameDrumStep(before, step);
    std::printf("restaurar el paso 5 como estaba: %s\n", ok2 ? "OK" : "FALLO");
    return ok1 && ok2 ? 0 : 1;
}

// The clock with the FM-1: a fake FL plays 4 s at 120 BPM from bar 1, then stops. Reads the FM-1's
// PLAY and BPM while it plays and after. The FM-1 plays its pattern for those 4 s. SYNC goes back
// to what it was at the end.
static int clockTest()
{
    juce::SharedResourcePointer<Fm1Clock> clk;
    clk->setEnabled(true);
    for (int i = 0; i < 40 && clk->state() != Fm1Clock::State::On; ++i) juce::Thread::sleep(50);
    std::printf("[reloj] %s\n", clk->message().toRawUTF8());
    if (clk->state() != Fm1Clock::State::On) return 1;

    Fm1Connection fm;
    juce::String err;
    if (!fm.open(err)) { std::printf("ERROR %s\n", err.toRawUTF8()); return 1; }
    auto state = [&fm](const char* when) {
        const auto r = fm.request(fm1::LIVE, { fm1::LiveState });
        const auto s = r ? fm1::parseLive(*r) : std::nullopt;
        if (s) std::printf("[FM-1 %s] %s, %d BPM\n", when, s->playing ? "PLAY" : "STOP", s->bpm);
        else std::printf("[FM-1 %s] sin respuesta\n", when);
        return s;
    };

    const double bpm = 120.0;
    clk->publish(false, 0.0, bpm, 4, 4);        // FL stopped: pulses only (tempo)
    for (int i = 0; i < 150; ++i) { clk->publish(false, 0.0, bpm, 4, 4); juce::Thread::sleep(10); }
    const auto stopped = state("con FL parado");

    const double t0 = juce::Time::getMillisecondCounterHiRes();
    std::optional<fm1::LiveStatus> during;
    while (juce::Time::getMillisecondCounterHiRes() - t0 < 4000.0)
    {
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        clk->publish(true, ms * bpm / 60000.0, bpm, 4, 4);
        if (!during && ms > 2500.0) during = state("con FL en PLAY");
        juce::Thread::sleep(10);
    }
    for (int i = 0; i < 30; ++i) { clk->publish(false, 8.0, bpm, 4, 4); juce::Thread::sleep(10); }
    const auto after = state("tras STOP de FL");
    clk->setEnabled(false);
    std::printf("[reloj] apagado (SYNC restaurado)\n");

    const bool ok = stopped && !stopped->playing && during && during->playing && std::abs(during->bpm - 120) <= 1
                 && after && !after->playing;
    std::printf("%s\n", ok ? "RELOJ OK" : "RELOJ FALLO");
    return ok ? 0 : 1;
}

// PLAY and REC through the live link (as the plugin's buttons): PLAY ~1.5 s then STOP, REC on
// then off at once (no notes played: nothing is recorded). Also prints GLO > SYSTEM > SYNC.
static int transportTest()
{
    juce::SharedResourcePointer<Fm1Live> link;
    link->setEnabled(true);
    for (int i = 0; i < 100 && !link->isLive(); ++i) juce::Thread::sleep(50);
    std::printf("[vivo] %s\n", link->message().toRawUTF8());
    if (!link->isLive() || !link->hasTransport()) { link->setEnabled(false); return 1; }
    juce::Thread::sleep(400);
    auto show = [&link](const char* when) {
        std::printf("[%s] FM-1 %s%s%s, %d BPM\n", when, link->fm1Playing() ? "PLAY" : "STOP",
                    link->fm1Recording() ? " + REC" : "", link->recBlockedBySong() ? " (canción: REC bloqueado)" : "", link->fm1Bpm());
    };
    show("inicio");
    const bool p0 = link->fm1Playing(), r0 = link->fm1Recording();
    link->pressPlay(); juce::Thread::sleep(1500); show("PLAY");
    const bool p1 = link->fm1Playing();
    link->pressPlay(); juce::Thread::sleep(700); show("PLAY otra vez");
    const bool p2 = link->fm1Playing();
    link->pressRec(); juce::Thread::sleep(700); show("REC");
    const bool r1 = link->fm1Recording();
    link->pressRec(); juce::Thread::sleep(700); show("REC otra vez");
    const bool r2 = link->fm1Recording();
    link->setEnabled(false);

    Fm1Connection fm;
    juce::String err;
    if (fm.open(err))
        if (const auto g = fm.request(2, { 1, (uint8_t) Fm1Clock::kSyncParam }); g && g->size() >= 4)
            std::printf("[SYNC] %s\n", std::array<const char*, 3> { "INT", "USB", "TRS" }[(size_t) juce::jlimit(0, 2, fm1::decodeV14((*g)[2], (*g)[3]))]);

    const bool ok = p1 != p0 && p2 == p0 && r1 != r0 && r2 == r0;
    std::printf("%s\n", ok ? "PLAY/REC OK" : "PLAY/REC FALLO");
    return ok ? 0 : 1;
}

// The selected track through the live link: picks the next track as a tab click would, checks the
// FM-1 selected it too (TRACK), then picks the original again.
static int trackTest()
{
    juce::SharedResourcePointer<Fm1Live> link;
    auto& s = Session::shared();
    link->setEnabled(true);
    for (int i = 0; i < 100 && !link->isLive(); ++i) juce::Thread::sleep(50);
    std::printf("[vivo] %s\n", link->message().toRawUTF8());
    if (!link->isLive()) { link->setEnabled(false); return 1; }
    juce::Thread::sleep(400);

    Fm1Connection fm;
    juce::String err;
    if (!fm.open(err)) { link->setEnabled(false); return 1; }
    auto deviceSel = [&fm]() { const auto r = fm.request(fm1::TRACK); return r && !r->empty() ? pluginTrack((*r)[0]) : -1; };

    const int original = s.selected.load();
    std::printf("[inicio] plugin: %s, FM-1: %s\n", kTrackNames[original], kTrackNames[juce::jlimit(0, 3, deviceSel())]);
    const int other = (original + 1) % kTracks;
    s.selected = other; s.touch();               // a tab click
    juce::Thread::sleep(600);
    const int seen = deviceSel();
    std::printf("[pesta\xc3\xb1" "a %s] FM-1: %s\n", kTrackNames[other], seen >= 0 ? kTrackNames[seen] : "?");
    s.selected = original; s.touch();
    juce::Thread::sleep(600);
    const int back = deviceSel();
    std::printf("[pesta\xc3\xb1" "a %s] FM-1: %s\n", kTrackNames[original], back >= 0 ? kTrackNames[back] : "?");
    link->setEnabled(false);
    const bool ok = seen == other && back == original;
    std::printf("%s\n", ok ? "PISTA OK" : "PISTA FALLO");
    return ok ? 0 : 1;
}

// What USR1..USR4 hold (read only).
static int slotsInfo()
{
    Fm1Connection fm;
    juce::String err;
    std::vector<SlotInfo> slots;
    if (!fm.open(err) || !readSlots(fm, slots, err)) { std::printf("ERROR %s\n", err.toRawUTF8()); return 1; }
    for (size_t k = 0; k < slots.size(); ++k)
        std::printf("USR%zu: %s\n", k + 1, slots[k].zones ? (juce::String(slots[k].zones) + " zonas, " + slots[k].name.trim() + ", "
                                                               + juce::String(slots[k].kib) + " KiB").toRawUTF8() : "vacío");
    return 0;
}

// Uploads files to a slot as an instrument (each on the note in its name, else C4). ERASES the slot.
//   sloop_fm1_probe subir <1..4> <nombre> archivo...
static int upload(int slot, const juce::String& name, const juce::StringArray& files)
{
    std::vector<smp::Zone> zones;
    for (const auto& f : files)
    {
        std::vector<int16_t> s;
        juce::String err;
        if (!loadSound(juce::File(f), s, err)) { std::printf("ERROR %s\n", err.toRawUTF8()); return 1; }
        zones.push_back({ s, smp::noteFromName(juce::File(f).getFileNameWithoutExtension().toStdString()), -1, -1 });
    }
    std::string e;
    const auto built = smp::buildSlot(name.toStdString(), zones, e);
    if (!built) { std::printf("ERROR %s\n", e.c_str()); return 1; }
    Fm1Connection fm;
    juce::String err;
    if (!fm.open(err)) { std::printf("ERROR %s\n", err.toRawUTF8()); return 1; }
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    const bool ok = uploadSlot(fm, slot, *built, nullptr, err);
    std::printf("%s USR%d: %d zonas, %zu bytes, %.1f s  %s\n", ok ? "OK" : "ERROR", slot + 1, built->zones, built->data.size(),
                (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0, err.toRawUTF8());
    return ok ? slotsInfo() : 1;
}

// Loopback through the IAC bus: does a 300-byte SysEx arrive whole? (no FM-1 involved)
struct Catch : juce::MidiInputCallback
{
    std::vector<uint8_t> got; juce::WaitableEvent ev;
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m) override
    { if (m.isSysEx()) { got.assign(m.getSysExData(), m.getSysExData() + m.getSysExDataSize()); ev.signal(); } }
};
static int iacTest()
{
    juce::MidiDeviceInfo i, o;
    for (auto& d : juce::MidiInput::getAvailableDevices()) if (d.name.contains("IAC")) i = d;
    for (auto& d : juce::MidiOutput::getAvailableDevices()) if (d.name.contains("IAC")) o = d;
    Catch c;
    auto in = juce::MidiInput::openDevice(i.identifier, &c);
    auto out = juce::MidiOutput::openDevice(o.identifier);
    if (!in || !out) { std::printf("no IAC\n"); return 1; }
    in->start();
    int bad = 0;
    for (int size : { 20, 100, 200, 300, 600 })
    {
        std::vector<uint8_t> d((size_t) size);
        for (int k = 0; k < size; ++k) d[(size_t) k] = (uint8_t) ((k * 37 + 11) & 0x7F);
        c.ev.reset();
        out->sendMessageNow(juce::MidiMessage::createSysExMessage(d.data(), size));
        const bool arrived = c.ev.wait(1000);
        const bool same = arrived && c.got == d;
        std::printf("SysEx %d bytes: %s%s\n", size, arrived ? (same ? "OK" : "LLEGÓ DISTINTO") : "NO LLEGÓ",
                    arrived && !same ? (" (" + std::to_string(c.got.size()) + " bytes)").c_str() : "");
        bad += !same;
    }
    return bad ? 1 : 0;
}

// The PLAY bridge: presses the FM-1's PLAY twice through the live link (as its button) and listens
// on the "SLOOP Studio" MIDI port for what FL's script would get (CC 118 play / 119 stop, ch 16).
// FL's transport is played by hand here: "FL" is always the other way, so each press must go out.
static int bridgeTest()
{
    juce::MidiDeviceInfo src;
    for (auto& d : juce::MidiInput::getAvailableDevices()) if (d.name == "SLOOP Studio") src = d;
    if (src.identifier.isEmpty()) { sloop::VirtualMidiSource::get(); juce::Thread::sleep(300);
        for (auto& d : juce::MidiInput::getAvailableDevices()) if (d.name == "SLOOP Studio") src = d; }
    std::printf("[puerto] %s\n", src.identifier.isEmpty() ? "SLOOP Studio creado (esc\xc3\xba" "chalo desde otro programa)" : "SLOOP Studio");
    struct Cc : juce::MidiInputCallback
    {
        std::atomic<int> last { -1 };
        void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m) override
        { if (m.isController() && m.getChannel() == 16) last = m.getControllerNumber(); }
    } cc;
    std::unique_ptr<juce::MidiInput> in;
    if (!src.identifier.isEmpty()) { in = juce::MidiInput::openDevice(src.identifier, &cc); in->start(); }
    const bool listening = in != nullptr;

    juce::SharedResourcePointer<Fm1Live> link;
    auto& s = Session::shared();
    link->setEnabled(true);
    for (int i = 0; i < 100 && !link->isLive(); ++i) juce::Thread::sleep(50);
    if (!link->isLive() || !link->hasTransport()) { std::printf("sin enlace\n"); return 1; }
    juce::Thread::sleep(600);
    s.fm1DrivesFl = true;
    bool ok = true;
    for (int press = 0; press < 2; ++press)
    {
        const bool before = link->fm1Playing();
        s.hostPlaying = before;     // "FL" is where the FM-1 is now: the press is the FM-1's own
        cc.last = -1;
        link->pressPlay();
        juce::Thread::sleep(900);
        const int want = before ? 119 : 118;
        std::printf("[PLAY del FM-1] %s -> %s, FL recibe: %s\n", before ? "PLAY" : "STOP", link->fm1Playing() ? "PLAY" : "STOP",
                    cc.last.load() == 118 ? "CC 118 (play)" : cc.last.load() == 119 ? "CC 119 (stop)" : "nada");
        ok = ok && (!listening || cc.last.load() == want);
    }
    // FL's own PLAY must not echo: the FM-1 follows FL, nothing goes out
    s.hostPlaying = !link->fm1Playing();
    cc.last = -1;
    link->pressPlay(); juce::Thread::sleep(900);   // (the FM-1 now matches "FL")
    link->pressPlay(); juce::Thread::sleep(900);   // back as it started
    s.fm1DrivesFl = false;
    link->setEnabled(false);
    std::printf("%s\n", ok ? "PUENTE OK" : "PUENTE FALLO");
    return ok ? 0 : 1;
}

int main(int argc, char** argv)
{
    if (argc > 1 && juce::String(argv[1]) == "puente")
    {
        juce::ScopedJuceInitialiser_GUI gui;
        return bridgeTest();
    }
    if (argc > 1 && juce::String(argv[1]) == "iac")
    {
        juce::ScopedJuceInitialiser_GUI gui;
        return iacTest();
    }
    if (argc > 1 && juce::String(argv[1]) == "samples")
    {
        juce::ScopedJuceInitialiser_GUI gui;
        return slotsInfo();
    }
    if (argc > 4 && juce::String(argv[1]) == "construir")   // construir <salida> <nombre> archivo...: .hdr / .bin, no FM-1
    {
        juce::ScopedJuceInitialiser_GUI gui;
        std::vector<smp::Zone> zones;
        for (int i = 4; i < argc; ++i)
        {
            std::vector<int16_t> sm; juce::String err;
            if (!loadSound(juce::File(argv[i]), sm, err)) { std::printf("ERROR %s\n", err.toRawUTF8()); return 1; }
            zones.push_back({ sm, smp::noteFromName(juce::File(argv[i]).getFileNameWithoutExtension().toStdString()), -1, -1 });
        }
        std::string e;
        const auto built = smp::buildSlot(argv[3], zones, e);
        if (!built) { std::printf("ERROR %s\n", e.c_str()); return 1; }
        juce::File(juce::String(argv[2]) + ".hdr").replaceWithData(built->header.data(), built->header.size());
        juce::File(juce::String(argv[2]) + ".bin").replaceWithData(built->data.data(), built->data.size());
        std::printf("%zu + %zu bytes\n", built->header.size(), built->data.size());
        return 0;
    }
    if (argc > 4 && juce::String(argv[1]) == "subir")
    {
        juce::ScopedJuceInitialiser_GUI gui;
        juce::StringArray files;
        for (int i = 4; i < argc; ++i) files.add(argv[i]);
        return upload(juce::jlimit(1, 4, juce::String(argv[2]).getIntValue()) - 1, argv[3], files);
    }
    if (argc > 1 && juce::String(argv[1]) == "pista")
    {
        juce::ScopedJuceInitialiser_GUI gui;
        return trackTest();
    }
    if (argc > 1 && juce::String(argv[1]) == "transporte")
    {
        juce::ScopedJuceInitialiser_GUI gui;
        return transportTest();
    }
    if (argc > 1 && juce::String(argv[1]) == "reloj")
    {
        juce::ScopedJuceInitialiser_GUI gui;
        return clockTest();
    }
    if (argc > 1 && juce::String(argv[1]) == "escritura")
    {
        juce::ScopedJuceInitialiser_GUI gui;
        return writeRoundTrip();
    }
    if (argc > 1 && juce::String(argv[1]) == "vivo")
    {
        juce::ScopedJuceInitialiser_GUI gui;
        return live(argc > 2 ? juce::jmax(1, juce::String(argv[2]).getIntValue()) : 30);
    }

    juce::ScopedJuceInitialiser_GUI gui;   // MIDI callbacks need the message system

    std::printf("MIDI in : ");
    for (const auto& d : juce::MidiInput::getAvailableDevices()) std::printf("[%s] ", d.name.toRawUTF8());
    std::printf("\nMIDI out: ");
    for (const auto& d : juce::MidiOutput::getAvailableDevices()) std::printf("[%s] ", d.name.toRawUTF8());
    std::printf("\n");

    Session s;
    const auto r = importFromFm1(s);
    std::printf("%s %s\n\n", r.ok ? "OK" : "ERROR", r.message.toRawUTF8());
    if (!r.ok) return 1;

    static const char lv[4] = { 'N', 'g', 's', 'H' };   // NORM ghost soft HARD
    std::printf("DRUMS (%d pasos%s)\n", s.length[kDrums].load(), s.muted[kDrums].load() ? ", mute" : "");
    for (int l = 0; l < kLanes; ++l)
    {
        std::printf("  %-9s", kLaneNames[l]);
        for (int st = 0; st < kSteps; ++st)
        {
            if (!s.drumOn(st, l)) { std::printf(" .  "); continue; }
            const int roll = s.laneRoll(st, l) + 1;
            std::printf(" %c%-2s", lv[s.laneLevel(st, l)], roll > 1 ? ("x" + std::to_string(roll)).c_str() : "");
        }
        std::printf("\n");
    }
    for (int t = 1; t < kTracks; ++t)
    {
        std::printf("%s (%d pasos%s):", kTrackNames[t], s.length[t].load(), s.muted[t].load() ? ", mute" : "");
        for (int st = 0; st < kSteps; ++st)
        {
            std::string notes;
            for (int n = 0; n < 128; ++n)
                if (s.noteOn(t, st, n)) notes += (notes.empty() ? "" : "+") + std::to_string(n);
            std::printf(" %d:%s", st + 1, notes.empty() ? "-" : notes.c_str());
        }
        std::printf("\n");
    }
    return 0;
}
