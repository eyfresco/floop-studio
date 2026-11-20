// SPDX-License-Identifier: GPL-3.0-only
#include "PluginEditor.h"

using namespace sloop;
namespace u = sloop::ui;

namespace {
const char* kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
juce::String noteName(int n) { return juce::String(kNoteNames[n % 12]) + juce::String(n / 12 - 1); }
juce::String u8(const char* s) { return juce::String::fromUTF8(s); }

void setupSlider(juce::Slider& s, juce::Label& l, const char* name, int lo, int hi, juce::Component& parent)
{
    s.setSliderStyle(juce::Slider::LinearBar);
    s.setRange(lo, hi, 1);
    s.setTextBoxIsEditable(false);
    s.setColour(juce::Slider::backgroundColourId, u::s2);
    s.setColour(juce::Slider::textBoxTextColourId, u::fg);
    s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    l.setText(name, juce::dontSendNotification);
    l.setFont(u::mono(11.0f));
    l.setJustificationType(juce::Justification::centredRight);
    parent.addAndMakeVisible(s);
    parent.addAndMakeVisible(l);
}

void setupLabel(juce::Label& l, const char* text, juce::Component& parent)
{
    l.setText(text, juce::dontSendNotification);
    l.setFont(u::mono(11.0f));
    l.setJustificationType(juce::Justification::centredRight);
    parent.addAndMakeVisible(l);
}
}

SloopEditor::SloopEditor(SloopProcessor& p)
    : AudioProcessorEditor(p), proc(p), session(p.session)
{
    setLookAndFeel(&look);

    // --- header ---
    clockBox.addItem("reloj: libre", 1);
    clockBox.addItem(u8("reloj: fl → fm-1"), 2);
    clockBox.addItem(u8("reloj: fm-1 → fl"), 3);
    clockBox.setTooltip(u8("Quién manda el tempo y PLAY/STOP.\n"
                           "fl → fm-1: FL manda su reloj al FM-1 (pone SYNC = USB en el FM-1; arranca en la barra de compás).\n"
                           "fm-1 → fl: el FM-1 manda su reloj (SYNC = INT, CLOCK = ON). En FL: Options > MIDI Settings > "
                           "External sync = MIDI clock, con el puerto Felucca. Necesita fm-1 vivo.\n"
                           "Al cambiar, el FM-1 vuelve a sus ajustes de antes."));
    clockBox.onChange = [this] { chooseClock(clockBox.getSelectedId()); };

    liveButton.setColour(juce::TextButton::buttonOnColourId, u::green);
    liveButton.setTooltip(u8("Edita el FM-1 en vivo, como el SLOOP Editor: lo que cambies aquí se escribe en el FM-1 "
                             "y lo que cambies en el FM-1 aparece aquí."));
    liveButton.onClick = [this] {
        if (proc.live->isEnabled()) proc.live->setEnabled(false);
        else askToGoLive();
        refreshControls();
    };

    // PLAY: the FM-1's PLAY (a plugin cannot start FL's transport; FL's own PLAY drives the clock).
    playButton.setButtonText(u8("▶ fm-1"));
    playButton.setColour(juce::TextButton::buttonOnColourId, u::green);
    playButton.setTooltip(u8("PLAY / STOP del FM-1, como su botón PLAY (con fm-1 vivo)."));
    playButton.onClick = [this] { proc.live->pressPlay(); };

    // REC: with the FM-1 live, the FM-1's own REC; otherwise Floop Studio records the keys itself.
    recButton.setColour(juce::TextButton::buttonOnColourId, u::red);
    recButton.onClick = [this] {
        if (proc.live->isLive() && proc.live->hasTransport())
        {
            if (proc.live->recBlockedBySong())
            {
                notice = u8("el FM-1 está tocando su canción: detenla para grabar");
                noticeUntil = juce::Time::getMillisecondCounter() + 3000;
                repaint();
            }
            else proc.live->pressRec();
        }
        else { session.recording = !session.recording.load(); refreshControls(); }
    };

    outputBox.addItem("salida: todas", 1);
    for (int t : { 1, 2, 3, kDrums })   // the FM-1's order: tracks 1..3 the synths, 4 the drums
        outputBox.addItem("salida: " + juce::String(kTrackNames[t]).toLowerCase(), t + 2);
    outputBox.onChange = [this] { proc.outputTrack = outputBox.getSelectedId() - 2; repaint(); };
    outputBox.setTooltip(u8("FL da un solo puerto MIDI por plugin. Para mandar cada pista a otro instrumento, "
                            "carga una instancia por pista y elige aquí cuál envía."));
    for (juce::Component* c : { (juce::Component*) &clockBox, (juce::Component*) &liveButton, (juce::Component*) &playButton,
                                (juce::Component*) &recButton, (juce::Component*) &outputBox })
        addAndMakeVisible(c);

    // --- tabs ---
    patternTab.setButtonText(u8("patrón"));
    soundTab.setButtonText(u8("sonido"));
    soundTab.setTooltip(u8("El sonido de la pista seleccionada en el FM-1: motor, presets y perillas."));
    soundsTab.setButtonText(u8("samples"));
    soundsTab.setTooltip(u8("Tus samples e instrumentos y kits de batería para USR1–USR4 del FM-1."));
    mapTab.setButtonText("mapa midi");
    mapTab.setTooltip(u8("El FM-1 con el MIDI de cada tecla y botón; tócalo y púlsalo desde aquí."));
    creditsTab.setButtonText(u8("créditos"));
    for (auto* b : { &patternTab, &soundTab, &soundsTab, &mapTab, &creditsTab })
    {
        b->setColour(juce::TextButton::buttonOnColourId, u::s3);
        b->setColour(juce::TextButton::textColourOnId, u::fg);
        addAndMakeVisible(*b);
    }
    patternTab.onClick = [this] { setView(ViewPattern); };
    soundTab.onClick = [this] { setView(ViewSound); };
    soundsTab.onClick = [this] { setView(ViewSamples); };
    mapTab.onClick = [this] { setView(ViewMap); };
    creditsTab.onClick = [this] { setView(ViewCredits); };

    // --- patrón ---
    setupSlider(lenSlider, lenLabel, "pasos", 1, kSteps, *this);
    setupSlider(gateSlider, gateLabel, "gate", 5, 100, *this);
    setupSlider(velSlider, velLabel, "vel", 1, 127, *this);
    setupSlider(chSlider, chLabel, "ch", 1, 16, *this);
    lenSlider.onValueChange = [this] { session.length[selected()] = (uint8_t) lenSlider.getValue(); session.touch(); };
    lenSlider.setTooltip(u8("Largo del patrón de la pista: 1 a 64 pasos (4 compases)."));
    gateSlider.onValueChange = [this] { session.gate[selected()] = (uint8_t) gateSlider.getValue(); session.touch(); };
    velSlider.onValueChange = [this] { session.velocity[selected()] = (uint8_t) velSlider.getValue(); session.touch(); };
    chSlider.onValueChange = [this] { session.channel[selected()] = (uint8_t) chSlider.getValue(); session.touch(); };

    setupLabel(levelLabel, "nivel", *this);
    setupLabel(rollLabel, "roll", *this);
    setupLabel(octLabel, "octava", *this);
    static const int levelOrder[4] = { LvGhost, LvSoft, LvNorm, LvHard };   // quiet to loud, as the editor
    for (int i = 0; i < 4; ++i)
    {
        auto& lb = levelButtons[(size_t) i];
        lb.setButtonText(juce::String(kLevelNames[(size_t) levelOrder[i]]).toLowerCase());
        lb.onClick = [this, i] { brushLevel = levelOrder[i]; refreshControls(); };
        auto& rb = rollButtons[(size_t) i];
        rb.setButtonText("x" + juce::String(i + 1));
        rb.onClick = [this, i] { brushRoll = i; refreshControls(); };
        addAndMakeVisible(lb);
        addAndMakeVisible(rb);
    }
    clearButton.setButtonText("limpiar");
    clearButton.onClick = [this] { session.clearTrack(selected()); };
    octDown.onClick = [this] { auto& b = baseNote[selected()]; b = juce::jmax(0, b - 12); repaint(); };
    octUp.onClick = [this] { auto& b = baseNote[selected()]; b = juce::jmin(127 - kSynthRows + 1, b + 12); repaint(); };
    flPlayToggle.setButtonText(u8("play fm-1 → fl"));
    flPlayToggle.setTooltip(u8("Con fm-1 vivo: el PLAY / STOP del FM-1 arranca y detiene FL. Necesita el script "
                               "\"Floop Studio\" en FL (MIDI Settings: entrada \"Floop Studio\", Controller type \"Floop Studio\")."));
    flPlayToggle.onClick = [this] { session.fm1DrivesFl = flPlayToggle.getToggleState(); };
    for (int pg = 0; pg < 4; ++pg)
    {
        auto& b = pageButtons[(size_t) pg];
        b.setButtonText(juce::String(pg * kPageSteps + 1) + u8("–") + juce::String((pg + 1) * kPageSteps));
        b.setColour(juce::TextButton::buttonOnColourId, u::s3);
        b.setColour(juce::TextButton::textColourOnId, u::fg);
        b.onClick = [this, pg] { page = pg; zoomAll = false; refreshControls(); };
        addAndMakeVisible(b);
    }
    zoomButton.setButtonText("todo");
    zoomButton.setTooltip(u8("Ver los 64 pasos a la vez (y volver a una página)."));
    zoomButton.setColour(juce::TextButton::buttonOnColourId, u::s3);
    zoomButton.setColour(juce::TextButton::textColourOnId, u::fg);
    zoomButton.onClick = [this] { zoomAll = !zoomAll; refreshControls(); };
    addAndMakeVisible(zoomButton);
    for (juce::Component* c : { (juce::Component*) &clearButton, (juce::Component*) &octDown, (juce::Component*) &octUp,
                                (juce::Component*) &flPlayToggle })
        addAndMakeVisible(c);

    samples = std::make_unique<sloop::SamplesPanel>(*proc.live);
    addChildComponent(*samples);
    soundPanel = std::make_unique<sloop::SoundPanel>(*proc.live);
    addChildComponent(*soundPanel);
    mapPanel = std::make_unique<sloop::MidiMapPanel>(*proc.live, session);
    addChildComponent(*mapPanel);
    creditsPanel = std::make_unique<sloop::CreditsPanel>();
    addChildComponent(*creditsPanel);

    setSize(860, 520);
    setView(ViewPattern);
    refreshControls();
    startTimerHz(30);
}

SloopEditor::~SloopEditor()   // the live link keeps running with the editor closed
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void SloopEditor::askToGoLive()
{
    bool empty = true;
    for (int st = 0; st < kSteps && empty; ++st)
    {
        empty = session.drum[st].load() == 0;
        for (int t = 1; t < kTracks && empty; ++t)
            empty = session.notesLo[t][st].load() == 0 && session.notesHi[t][st].load() == 0;
    }
    if (empty) { proc.live->setEnabled(true); refreshControls(); return; }

    juce::Component::SafePointer<SloopEditor> safe(this);
    juce::AlertWindow::showOkCancelBox(
        juce::MessageBoxIconType::QuestionIcon, "fm-1 vivo",
        u8("Al conectar, Floop Studio toma el patrón que tiene el FM-1 y reemplaza el actual "
           "(las 4 pistas). Desde ahí, lo que edites aquí se escribe en el FM-1."),
        "Conectar", "Cancelar", this,
        juce::ModalCallbackFunction::create([safe](int ok) {
            if (ok && safe) { safe->proc.live->setEnabled(true); safe->refreshControls(); }
        }));
}

void SloopEditor::chooseClock(int mode)
{
    // one master at a time: switching one way off before the other on (each puts its FM-1 settings back)
    if (mode != 2) proc.clock->setEnabled(false);
    if (mode != 3) proc.live->setClockOut(false);
    if (mode == 2) proc.clock->setEnabled(true);
    if (mode == 3)
    {
        if (!proc.live->isEnabled())
        {
            notice = u8("activa fm-1 vivo para que el FM-1 mande su reloj");
            noticeUntil = juce::Time::getMillisecondCounter() + 3000;
        }
        else
        {
            proc.live->setClockOut(true);
            session.fm1DrivesFl = false;   // FL follows the FM-1's START / STOP itself: no script needed
            notice = u8("en FL: options > midi settings > external sync = midi clock (puerto felucca)");
            noticeUntil = juce::Time::getMillisecondCounter() + 6000;
        }
    }
    refreshControls();
}

void SloopEditor::setView(int v)
{
    view = v;
    patternTab.setToggleState(v == ViewPattern, juce::dontSendNotification);
    soundTab.setToggleState(v == ViewSound, juce::dontSendNotification);
    soundsTab.setToggleState(v == ViewSamples, juce::dontSendNotification);
    mapTab.setToggleState(v == ViewMap, juce::dontSendNotification);
    mapPanel->setVisible(v == ViewMap);
    creditsTab.setToggleState(v == ViewCredits, juce::dontSendNotification);
    creditsPanel->setVisible(v == ViewCredits);
    samples->setVisible(v == ViewSamples);
    soundPanel->setVisible(v == ViewSound);
    refreshControls();
    repaint();
}

void SloopEditor::refreshControls()
{
    const int t = selected();
    const bool pattern = view == ViewPattern, drums = t == kDrums;
    lenSlider.setValue(session.length[t].load(), juce::dontSendNotification);
    gateSlider.setValue(session.gate[t].load(), juce::dontSendNotification);
    velSlider.setValue(session.velocity[t].load(), juce::dontSendNotification);
    chSlider.setValue(session.channel[t].load(), juce::dontSendNotification);
    for (auto* s : { &lenSlider, &gateSlider, &velSlider, &chSlider })
        s->setColour(juce::Slider::trackColourId, u::track(t).withAlpha(0.55f));

    for (juce::Component* c : { (juce::Component*) &lenSlider, (juce::Component*) &gateSlider, (juce::Component*) &velSlider,
                                (juce::Component*) &chSlider, (juce::Component*) &lenLabel, (juce::Component*) &gateLabel,
                                (juce::Component*) &velLabel, (juce::Component*) &chLabel, (juce::Component*) &clearButton,
                                (juce::Component*) &flPlayToggle })
        c->setVisible(pattern);
    static const int levelOrder[4] = { LvGhost, LvSoft, LvNorm, LvHard };
    for (int i = 0; i < 4; ++i)
    {
        levelButtons[(size_t) i].setVisible(pattern && drums);
        levelButtons[(size_t) i].setToggleState(levelOrder[i] == brushLevel, juce::dontSendNotification);
        rollButtons[(size_t) i].setVisible(pattern && drums);
        rollButtons[(size_t) i].setToggleState(i == brushRoll, juce::dontSendNotification);
    }
    levelLabel.setVisible(pattern && drums);
    rollLabel.setVisible(pattern && drums);
    for (juce::Component* c : { (juce::Component*) &octLabel, (juce::Component*) &octDown, (juce::Component*) &octUp })
        c->setVisible(pattern && !drums);
    {   // pages: the shown one lit; past the track's length dimmed; the playhead's page outlined
        const int len = session.length[t].load(), play = proc.playStep[t].load();
        for (int pg = 0; pg < 4; ++pg)
        {
            auto& b = pageButtons[(size_t) pg];
            b.setVisible(pattern);
            b.setToggleState(!zoomAll && pg == page, juce::dontSendNotification);
            const bool used = pg * kPageSteps < len, playing = play >= 0 && play / kPageSteps == pg;
            b.setColour(juce::TextButton::textColourOffId, used ? u::fg2 : u::dim.withAlpha(0.6f));
            b.setColour(juce::TextButton::buttonColourId, playing ? u::track(t).withAlpha(0.35f) : u::s2);
        }
        zoomButton.setVisible(pattern);
        zoomButton.setToggleState(zoomAll, juce::dontSendNotification);
    }

    const bool fmLive = proc.live->isLive() && proc.live->hasTransport();
    recButton.setToggleState(fmLive ? proc.live->fm1Recording() : session.recording.load(), juce::dontSendNotification);
    recButton.setTooltip(fmLive ? u8("REC del FM-1, como su botón REC.")
                                : u8("Graba en Floop Studio lo que tocas en el FM-1 (con FL en PLAY)."));
    playButton.setEnabled(fmLive);
    playButton.setToggleState(fmLive && proc.live->fm1Playing(), juce::dontSendNotification);
    clockBox.setSelectedId(proc.clock->isEnabled() ? 2 : proc.live->clockOut() ? 3 : 1, juce::dontSendNotification);
    liveButton.setToggleState(proc.live->isEnabled(), juce::dontSendNotification);
    flPlayToggle.setToggleState(session.fm1DrivesFl.load(), juce::dontSendNotification);
    outputBox.setSelectedId(proc.outputTrack.load() + 2, juce::dontSendNotification);
    repaint();
}

void SloopEditor::timerCallback()
{
    bool dirty = false;
    {   // the live link's state and message (another instance may have switched it)
        const auto st = (int) proc.live->state();
        const auto m = proc.live->message();
        if (st != seenLiveState || m != seenLiveMessage) { seenLiveState = st; seenLiveMessage = m; refreshControls(); }
        if (noticeUntil && juce::Time::getMillisecondCounter() > noticeUntil) { noticeUntil = 0; notice.clear(); dirty = true; }
        // PLAY / REC / SYNC lights follow the FM-1 and the clock, whoever switched them
        const int transportBits = (proc.live->fm1Playing() ? 1 : 0) | (proc.live->fm1Recording() ? 2 : 0)
                                | (proc.live->isLive() && proc.live->hasTransport() ? 4 : 0) | (proc.clock->isEnabled() ? 8 : 0)
                                | (proc.live->clockOut() ? 1 << 10 : 0)
                                | (session.recording.load() ? 16 : 0) | (proc.clock->waitingForBar() ? 32 : 0)
                                | ((int) proc.clock->state() << 6) | (proc.live->isEnabled() ? 1 << 9 : 0);
        if (transportBits != seenTransport) { seenTransport = transportBits; refreshControls(); }
    }
    if (const auto r = session.revision.load(); r != seenRevision) { seenRevision = r; refreshControls(); }
    for (int t = 0; t < kTracks; ++t)
        if (const int s = proc.playStep[t].load(); s != seenSteps[t]) { seenSteps[t] = s; dirty = true; }
    if (const int pp = proc.playStep[selected()].load() / kPageSteps; pp != seenPlayPage) { seenPlayPage = pp; refreshControls(); }
    if (const auto m = proc.midiInCount.load(); m != seenMidiIn) { seenMidiIn = m; midiLight = 6; dirty = true; }
    else if (midiLight > 0) { --midiLight; dirty = true; }
    if (dirty) repaint();
}

void SloopEditor::resized()
{
    auto r = getLocalBounds().reduced(12, 8);
    headerArea = r.removeFromTop(28);
    r.removeFromTop(8);
    tracksArea = r.removeFromTop(32);
    r.removeFromTop(8);
    tabsArea = r.removeFromTop(24);
    r.removeFromTop(8);
    contentArea = r;

    // header, right to left: salida · rec · ▶ fm-1 · fm-1 vivo · sync fl
    auto h = headerArea;
    outputBox.setBounds(h.removeFromRight(120));
    h.removeFromRight(8);
    recButton.setBounds(h.removeFromRight(42));
    h.removeFromRight(4);
    playButton.setBounds(h.removeFromRight(60));
    h.removeFromRight(4);
    liveButton.setBounds(h.removeFromRight(78));
    h.removeFromRight(4);
    clockBox.setBounds(h.removeFromRight(124));

    const int gap = 6, tw = (tracksArea.getWidth() - gap * (kTracks - 1)) / kTracks;
    for (int t = 0; t < kTracks; ++t)
    {
        const int slot = (t + kTracks - 1) % kTracks;   // the FM-1's order: synth 1, 2, 3, then drums (track 4)
        tabRects[(size_t) t] = { tracksArea.getX() + slot * (tw + gap), tracksArea.getY(), tw, tracksArea.getHeight() };
        muteRects[(size_t) t] = tabRects[(size_t) t].withTrimmedLeft(tw - 28).reduced(4, 6);
    }

    auto tb = tabsArea;
    patternTab.setBounds(tb.removeFromLeft(76));
    tb.removeFromLeft(4);
    soundTab.setBounds(tb.removeFromLeft(76));
    tb.removeFromLeft(4);
    soundsTab.setBounds(tb.removeFromLeft(76));
    tb.removeFromLeft(4);
    mapTab.setBounds(tb.removeFromLeft(86));
    tb.removeFromLeft(4);
    creditsTab.setBounds(tb.removeFromLeft(80));

    samples->setBounds(contentArea);
    soundPanel->setBounds(contentArea);
    mapPanel->setBounds(contentArea);
    creditsPanel->setBounds(contentArea);

    // patrón: two tool rows, the step numbers, the grid, a hint line
    auto c = contentArea;
    toolA = c.removeFromTop(22);
    c.removeFromTop(6);
    toolB = c.removeFromTop(22);
    c.removeFromTop(8);
    hintArea = c.removeFromBottom(14);
    c.removeFromBottom(4);
    numbersArea = c.removeFromTop(14);
    gridArea = c;

    auto a = toolA;
    auto place = [&a](juce::Label& l, juce::Slider& s, int lw) {
        l.setBounds(a.removeFromLeft(lw));
        a.removeFromLeft(4);
        s.setBounds(a.removeFromLeft(70));
        a.removeFromLeft(12);
    };
    place(lenLabel, lenSlider, 40);
    place(gateLabel, gateSlider, 34);
    place(velLabel, velSlider, 28);
    place(chLabel, chSlider, 20);
    clearButton.setBounds(a.removeFromRight(70));

    auto b = toolB;
    levelLabel.setBounds(b.removeFromLeft(40));
    b.removeFromLeft(4);
    for (auto& lb : levelButtons) { lb.setBounds(b.removeFromLeft(50)); b.removeFromLeft(2); }
    b.removeFromLeft(14);
    rollLabel.setBounds(b.removeFromLeft(34));
    b.removeFromLeft(4);
    for (auto& rb : rollButtons) { rb.setBounds(b.removeFromLeft(32)); b.removeFromLeft(2); }
    auto o = toolB;
    octLabel.setBounds(o.removeFromLeft(48));
    o.removeFromLeft(4);
    octDown.setBounds(o.removeFromLeft(26));
    o.removeFromLeft(2);
    octUp.setBounds(o.removeFromLeft(26));
    flPlayToggle.setBounds(toolB.withTrimmedLeft(toolB.getWidth() - 128));
    {   // pages + todo, left of the FL toggle
        auto pgs = toolB.withTrimmedRight(134);
        zoomButton.setBounds(pgs.removeFromRight(40));
        pgs.removeFromRight(4);
        for (int pg = 3; pg >= 0; --pg) { pageButtons[(size_t) pg].setBounds(pgs.removeFromRight(44)); pgs.removeFromRight(2); }
    }
}

juce::Rectangle<int> SloopEditor::cellBounds(int row, int step) const
{
    const auto area = gridArea.withTrimmedLeft(kLabelWidth);
    const float w = area.getWidth() / (float) visibleSteps(), h = area.getHeight() / (float) rowCount();
    return juce::Rectangle<float>(area.getX() + (step - firstStep()) * w, area.getY() + row * h, w, h).toNearestInt();
}

juce::Point<int> SloopEditor::cellAt(juce::Point<int> p) const
{
    const auto area = gridArea.withTrimmedLeft(kLabelWidth);
    if (!area.contains(p)) return { -1, -1 };
    const int step = firstStep() + (int) ((p.x - area.getX()) * visibleSteps() / (float) area.getWidth());
    const int row = (int) ((p.y - area.getY()) * rowCount() / (float) area.getHeight());
    return { juce::jlimit(firstStep(), firstStep() + visibleSteps() - 1, step), juce::jlimit(0, rowCount() - 1, row) };
}

bool SloopEditor::cellOn(int row, int step) const
{
    const int t = selected();
    return t == kDrums ? session.drumOn(step, row) : session.noteOn(t, step, noteForRow(row));
}

void SloopEditor::setCell(int row, int step, bool on)
{
    const int t = selected();
    if (t == kDrums)
    {
        session.setDrum(step, row, on);   // (a new hit starts at NORM x1)
        if (on && brushLevel != LvNorm) session.setLaneLevel(step, row, brushLevel);
        if (on && brushRoll) session.setLaneRoll(step, row, brushRoll);
        return;
    }

    // Live with the FM-1: a step holds 4 notes at most there, so the 5th is refused here.
    if (on && proc.live->isEnabled() && !session.noteOn(t, step, noteForRow(row)))
    {
        int count = 0;
        for (int n = 0; n < 128; ++n) count += session.noteOn(t, step, n) ? 1 : 0;
        if (count >= 4)
        {
            notice = u8("el FM-1 guarda hasta 4 notas por paso");
            noticeUntil = juce::Time::getMillisecondCounter() + 2500;
            repaint();
            return;
        }
    }
    session.setNote(t, step, noteForRow(row), on);
}

void SloopEditor::mouseDown(const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    for (int t = 0; t < kTracks; ++t)
    {
        if (muteRects[(size_t) t].contains(p)) { session.muted[t] = session.muted[t].load() ? 0 : 1; session.touch(); return; }
        if (tabRects[(size_t) t].contains(p)) { session.selected = t; session.touch(); return; }
    }
    if (view != ViewPattern) return;
    const auto c = cellAt(p);
    if (c.x < 0) return;

    if (selected() == kDrums)
    {
        const bool on = cellOn(c.y, c.x);
        // Shift+click on a hit: one level louder (GHOST, SOFT, NORM, HARD), as in the SLOOP editor.
        if (on && e.mods.isShiftDown())
        {
            static const int louder[4] = { LvHard, LvSoft, LvNorm, LvGhost };   // index: level -> next
            session.setLaneLevel(c.x, c.y, louder[session.laneLevel(c.x, c.y)]);
            lastCell = { -1, -1 };
            return;
        }
        // Click: a hit with the brush's level and roll; a hit that already has them is cleared.
        const bool same = on && session.laneLevel(c.x, c.y) == brushLevel && session.laneRoll(c.x, c.y) == brushRoll;
        paintMode = same ? 0 : 1;
        setCell(c.y, c.x, paintMode == 1);
        lastCell = c;
        return;
    }
    paintMode = cellOn(c.y, c.x) ? 0 : 1;
    setCell(c.y, c.x, paintMode == 1);
    lastCell = c;
}

void SloopEditor::mouseDrag(const juce::MouseEvent& e)
{
    if (view != ViewPattern) return;
    const auto c = cellAt(e.getPosition());
    if (c.x < 0 || c == lastCell || lastCell.x < 0) return;
    setCell(c.y, c.x, paintMode == 1);
    lastCell = c;
}

void SloopEditor::paint(juce::Graphics& g)
{
    g.fillAll(u::bg);
    const int sel = selected();

    // --- header: the name, the FL / MIDI lights ---
    {
        auto h = headerArea;
        g.setFont(u::mono(15.0f, true));
        g.setColour(u::orange);
        g.drawText("floop", h.removeFromLeft(52), juce::Justification::centredLeft);
        g.setColour(u::fg);
        g.drawText("studio", h.removeFromLeft(62), juce::Justification::centredLeft);
        g.setColour(u::dim);
        g.setFont(u::mono(11.0f));
        g.drawText("by @HeyFresco", h.removeFromLeft(100), juce::Justification::centredLeft);
        h.removeFromLeft(10);
        auto light = [&g, &h](bool on, juce::Colour c, const char* label) {
            auto r = h.removeFromLeft(58);
            g.setColour(on ? c : u::s3);
            g.fillEllipse(r.removeFromLeft(8).withSizeKeepingCentre(8, 8).toFloat());
            g.setColour(u::dim);
            g.setFont(u::mono(11.0f));
            g.drawText(label, r.withTrimmedLeft(5), juce::Justification::centredLeft);
        };
        light(proc.hostPlaying.load(), u::green, "fl");
        light(midiLight > 0, u::yellow, "midi");
    }

    // the bars of pasos / gate / vel / ch (LinearBar draws only the filled part)
    if (view == ViewPattern)
        for (auto* s : { &lenSlider, &gateSlider, &velSlider, &chSlider })
        {
            g.setColour(u::s2);
            g.fillRoundedRectangle(s->getBounds().toFloat(), 2.0f);
        }

    // --- tracks ---
    for (int t = 0; t < kTracks; ++t)
    {
        const auto r = tabRects[(size_t) t];
        const auto col = u::track(t);
        const bool on = t == sel, sending = (proc.trackMask() >> t) & 1u, muted = session.muted[t].load();
        g.setColour(on ? col.withAlpha(0.14f) : u::s1);
        g.fillRoundedRectangle(r.toFloat(), 2.0f);
        g.setColour(on ? col : u::line);
        g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 2.0f, 1.0f);
        g.setColour(col);
        g.fillRect(r.withWidth(3).reduced(0, 6));

        auto text = r.reduced(10, 0).withTrimmedRight(28);
        g.setFont(u::mono(12.0f, true));
        g.setColour((on ? u::fg : u::fg2).withAlpha(sending ? 1.0f : 0.45f));
        g.drawText(juce::String(kTrackNames[t]).toLowerCase(), text.removeFromLeft(text.getWidth() / 2 + 6), juce::Justification::centredLeft);
        g.setFont(u::mono(10.5f));
        g.setColour(u::dim.withAlpha(sending ? 1.0f : 0.5f));
        g.drawText("ch " + juce::String(session.channel[t].load()) + " " + juce::String(session.length[t].load()) + "p"
                       + (sending ? juce::String() : juce::String(" ·")),
                   text, juce::Justification::centredRight);

        const auto m = muteRects[(size_t) t].toFloat();
        g.setColour(muted ? u::red : u::s2);
        g.fillRoundedRectangle(m, 2.0f);
        g.setColour(muted ? juce::Colours::black : u::dim);
        g.setFont(u::mono(11.0f, true));
        g.drawText("m", muteRects[(size_t) t], juce::Justification::centred);
    }

    // --- tabs row: the FM-1 link's state (right) ---
    {
        using S = sloop::Fm1Live::State;
        const auto st = proc.live->state();
        juce::String text = notice.isNotEmpty() ? notice : proc.live->message();
        if (notice.isEmpty() && proc.clock->isEnabled())
        {
            const auto cm = proc.clock->waitingForBar() ? u8("el FM-1 entra en el próximo compás") : proc.clock->message();
            text = text.isEmpty() ? cm : text + u8("  ·  ") + cm;
        }
        const juce::Colour c = notice.isNotEmpty() ? u::yellow : st == S::Live ? u::green : st == S::Error ? u::red : u::yellow;
        g.setFont(u::mono(11.0f));
        g.setColour(text.isEmpty() ? u::dim : c);
        g.drawText(text.isEmpty() ? u8("fm-1 sin conectar") : text.toLowerCase(), tabsArea.withTrimmedLeft(440),
                   juce::Justification::centredRight, true);
    }

    if (view != ViewPattern) return;   // SONIDO / SAMPLES cover the rest

    // --- patrón: the grid ---
    const auto col = u::track(sel);
    const int len = session.length[sel].load();
    const int play = proc.playStep[sel].load();
    const int rows = rowCount();
    const bool drums = sel == kDrums;

    g.setFont(u::mono(10.5f));
    const int first = firstStep(), last = first + visibleSteps();
    const int every = zoomAll ? 16 : 4;   // the numbers: 1 · 5 · 9 · 13 on a page, 1 · 17 · 33 · 49 zoomed out
    for (int step = first; step < last; ++step)
    {
        if (step % every && step != play) continue;
        const auto b = cellBounds(0, step);
        g.setColour(step == play ? col : u::dim);
        g.drawText(juce::String(step + 1), b.getX() + 2, numbersArea.getY(), b.getWidth(), numbersArea.getHeight(),
                   juce::Justification::centredLeft);
    }

    for (int row = 0; row < rows; ++row)
    {
        const auto rr = cellBounds(row, 0);
        auto label = juce::Rectangle<int>(gridArea.getX(), rr.getY(), kLabelWidth - 6, rr.getHeight());
        juce::String name;
        bool black = false;
        if (drums) name = juce::String(kLaneNames[(size_t) row]).toLowerCase();
        else
        {
            const int n = noteForRow(row);
            black = juce::String(kNoteNames[n % 12]).contains("#");
            name = noteName(n);
        }
        g.setColour(!drums && name.startsWith("C") && !black ? u::fg : u::fg2.withAlpha(black ? 0.5f : 0.85f));
        g.setFont(u::mono(juce::jmin(11.5f, rr.getHeight() * 0.72f)));
        g.drawText(name, label, juce::Justification::centredLeft);
    }

    static const float levelAlpha[4] = { 0.9f, 0.38f, 0.62f, 1.0f };   // NORM GHOST SOFT HARD
    for (int step = first; step < last; ++step)
    {
        const bool beat = step % 4 == 0;
        for (int row = 0; row < rows; ++row)
        {
            const auto c = cellBounds(row, step).reduced(1).toFloat();
            const bool on = cellOn(row, step);
            const bool blackRow = !drums && juce::String(kNoteNames[noteForRow(row) % 12]).contains("#");
            juce::Colour base = beat ? u::s3 : u::s2;
            if (blackRow) base = base.darker(0.35f);
            if (step >= len) base = base.withMultipliedAlpha(0.35f);
            g.setColour(base);
            g.fillRect(c);
            if (on)
            {
                const int level = drums ? session.laneLevel(step, row) : LvNorm;
                juce::Colour fill = col.withMultipliedAlpha(levelAlpha[level]);
                if (step >= len) fill = fill.withMultipliedAlpha(0.35f);
                if (step == play) fill = fill.brighter(0.45f);
                g.setColour(fill);
                g.fillRect(c);
                if (drums)
                {
                    if (level == LvHard)
                    {
                        g.setColour(u::fg.withAlpha(0.85f));
                        g.drawRect(c, 1.0f);
                    }
                    const int hits = session.laneRoll(step, row) + 1;   // a roll: one slice per hit
                    g.setColour(u::bg.withAlpha(0.8f));
                    for (int k = 1; k < hits; ++k)
                        g.fillRect(c.getX() + c.getWidth() * k / hits - 1.0f, c.getY() + 2.0f, 2.0f, c.getHeight() - 4.0f);
                }
            }
            else if (step == play)
            {
                g.setColour(u::fg.withAlpha(0.07f));
                g.fillRect(c);
            }
        }
    }

    if (zoomAll)   // the bar lines between the 4 bars of 16
        for (int bar = 1; bar < kSteps / kPageSteps; ++bar)
        {
            const auto b = cellBounds(0, bar * kPageSteps);
            g.setColour(u::fg2.withAlpha(0.5f));
            g.fillRect(b.getX() - 1, gridArea.getY(), 2, gridArea.getHeight());
        }

    g.setFont(u::mono(10.5f));
    g.setColour(u::dim);
    g.drawText(drums ? u8("clic: golpe con el nivel y roll elegidos · otra vez: lo quita · shift+clic: más fuerte · teclas blancas desde F3")
                     : u8("clic: nota (hasta 4 por paso con el FM-1) · octava -/+ mueve las dos octavas"),
               hintArea, juce::Justification::centredLeft, true);
}
