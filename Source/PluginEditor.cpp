// SPDX-License-Identifier: GPL-3.0-only
#include "PluginEditor.h"

using namespace sloop;

namespace {
const juce::Colour kBg { 0xff15171b }, kPanel { 0xff1f2228 }, kCellOff { 0xff2a2e36 },
                   kCellBeat { 0xff333844 }, kText { 0xffe9ebef }, kDim { 0xff8a909c };
const char* kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

juce::String noteName(int n) { return juce::String(kNoteNames[n % 12]) + juce::String(n / 12 - 1); }

void setupSlider(juce::Slider& s, juce::Label& l, const juce::String& name, int lo, int hi, juce::Component& parent)
{
    s.setSliderStyle(juce::Slider::LinearBar);
    s.setRange(lo, hi, 1);
    s.setTextBoxIsEditable(false);
    s.setColour(juce::Slider::trackColourId, juce::Colour(0xff3a4050));
    s.setColour(juce::Slider::textBoxTextColourId, kText);
    s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    l.setText(name, juce::dontSendNotification);
    l.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    l.setColour(juce::Label::textColourId, kDim);
    parent.addAndMakeVisible(s);
    parent.addAndMakeVisible(l);
}
}

juce::Colour SloopEditor::trackColour(int t)
{
    static const juce::Colour c[kTracks] = { juce::Colour(0xffff8a3d), juce::Colour(0xff3dd6ff),
                                             juce::Colour(0xffff4fa3), juce::Colour(0xffb6f24a) };
    return c[juce::jlimit(0, kTracks - 1, t)];
}

SloopEditor::SloopEditor(SloopProcessor& p)
    : AudioProcessorEditor(p), proc(p), session(p.session)
{
    outputLabel.setText("SALIDA DE ESTA INSTANCIA", juce::dontSendNotification);
    outputLabel.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    outputLabel.setColour(juce::Label::textColourId, kDim);
    addAndMakeVisible(outputLabel);

    outputBox.addItem("Todas las pistas", 1);
    for (int t = 0; t < kTracks; ++t) outputBox.addItem(juce::String("Solo ") + kTrackNames[t], t + 2);
    outputBox.setSelectedId(proc.outputTrack.load() + 2, juce::dontSendNotification);
    outputBox.onChange = [this] { proc.outputTrack = outputBox.getSelectedId() - 2; repaint(); };
    outputBox.setTooltip(juce::String(juce::CharPointer_UTF8(
        "FL da un solo puerto MIDI por plugin. Para mandar cada pista a otro instrumento, "
        "carga una instancia por pista y elige aqu\xc3\xad cu\xc3\xa1l env\xc3\xad" "a.")));
    addAndMakeVisible(outputBox);

    // REC: with the FM-1 live, the FM-1's own REC; otherwise SLOOP Studio records the keys itself.
    recButton.setClickingTogglesState(false);
    recButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffe5383b));
    recButton.onClick = [this] {
        if (proc.live->isLive() && proc.live->hasTransport())
        {
            if (proc.live->recBlockedBySong())
            {
                notice = juce::String::fromUTF8("El FM-1 está tocando su canción: detenla para grabar");
                noticeUntil = juce::Time::getMillisecondCounter() + 3000;
                repaint();
            }
            else proc.live->pressRec();
        }
        else { session.recording = !session.recording.load(); refreshControls(); }
    };
    addAndMakeVisible(recButton);

    // PLAY: the FM-1's PLAY (a plugin cannot start FL's transport; FL's own PLAY drives the clock).
    playButton.setButtonText(juce::String::fromUTF8("▶ FM-1"));
    playButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff16a34a));
    playButton.setTooltip(juce::String::fromUTF8("PLAY / STOP del FM-1, como su botón PLAY (con FM-1 EN VIVO)."));
    playButton.onClick = [this] { proc.live->pressPlay(); };
    addAndMakeVisible(playButton);

    syncButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff2563eb));
    syncButton.setTooltip(juce::String::fromUTF8(
        "Manda el tempo y PLAY/STOP de FL al FM-1 (reloj MIDI) y pone SYNC = USB en el FM-1. "
        "El FM-1 arranca en la primera barra de compás de FL."));
    syncButton.onClick = [this] { proc.clock->setEnabled(!proc.clock->isEnabled()); refreshControls(); };
    addAndMakeVisible(syncButton);

    clearButton.onClick = [this] { session.clearTrack(selected()); };

    liveButton.setClickingTogglesState(false);
    liveButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff16a34a));
    liveButton.setTooltip(juce::String::fromUTF8(
        "Edita el FM-1 en vivo, como el SLOOP Editor: lo que cambies aquí se escribe en el FM-1 "
        "y lo que cambies en el FM-1 aparece aquí."));
    liveButton.onClick = [this] {
        if (proc.live->isEnabled()) proc.live->setEnabled(false);
        else askToGoLive();
        refreshControls();
    };
    addAndMakeVisible(liveButton);
    addAndMakeVisible(clearButton);

    octDown.onClick = [this] { auto& b = baseNote[selected()]; b = juce::jmax(0, b - 12); repaint(); };
    octUp.onClick = [this] { auto& b = baseNote[selected()]; b = juce::jmin(127 - kSynthRows + 1, b + 12); repaint(); };
    addAndMakeVisible(octDown);
    addAndMakeVisible(octUp);

    setupSlider(lenSlider, lenLabel, "PASOS", 1, kSteps, *this);
    setupSlider(gateSlider, gateLabel, "GATE %", 5, 100, *this);
    setupSlider(velSlider, velLabel, "VELOCIDAD", 1, 127, *this);
    setupSlider(chSlider, chLabel, "CANAL MIDI", 1, 16, *this);
    lenSlider.onValueChange = [this] { session.length[selected()] = (uint8_t) lenSlider.getValue(); session.touch(); };
    gateSlider.onValueChange = [this] { session.gate[selected()] = (uint8_t) gateSlider.getValue(); session.touch(); };
    velSlider.onValueChange = [this] { session.velocity[selected()] = (uint8_t) velSlider.getValue(); session.touch(); };
    chSlider.onValueChange = [this] { session.channel[selected()] = (uint8_t) chSlider.getValue(); session.touch(); };

    patternTab.setButtonText(juce::String::fromUTF8("PATRÓN"));
    soundsTab.setButtonText(juce::String::fromUTF8("SONIDOS"));
    soundsTab.setTooltip(juce::String::fromUTF8("Tus samples e instrumentos y kits de batería para USR1–USR4 del FM-1."));
    for (auto* b : { &patternTab, &soundsTab })
    {
        b->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3a4050));
        addAndMakeVisible(*b);
    }
    patternTab.onClick = [this] { setView(false); };
    soundsTab.onClick = [this] { setView(true); };
    flPlayToggle.setButtonText(juce::String::fromUTF8("PLAY del FM-1 → FL"));
    flPlayToggle.setColour(juce::ToggleButton::textColourId, juce::Colour(0xff8a909c));
    flPlayToggle.setTooltip(juce::String::fromUTF8(
        "Con FM-1 EN VIVO: el PLAY / STOP del FM-1 arranca y detiene FL. Necesita el script "
        "\"SLOOP Studio\" en FL (MIDI Settings: entrada \"SLOOP Studio\", Controller type \"SLOOP Studio\")."));
    flPlayToggle.onClick = [this] { session.fm1DrivesFl = flPlayToggle.getToggleState(); };
    addAndMakeVisible(flPlayToggle);
    samples = std::make_unique<sloop::SamplesPanel>(*proc.live);
    addChildComponent(*samples);

    setSize(1100, 640);
    setView(false);
    refreshControls();
    startTimerHz(30);
}

SloopEditor::~SloopEditor() { stopTimer(); }   // the live link keeps running with the editor closed

void SloopEditor::askToGoLive()
{
    bool empty = true;
    for (int st = 0; st < kSteps && empty; ++st)
    {
        empty = session.drum[st].load() == 0;
        for (int t = 1; t < kTracks && empty; ++t)
            empty = session.notesLo[t][st].load() == 0 && session.notesHi[t][st].load() == 0;
    }
    auto& live = *proc.live;
    if (empty) { live.setEnabled(true); refreshControls(); return; }

    juce::Component::SafePointer<SloopEditor> safe(this);
    juce::AlertWindow::showOkCancelBox(
        juce::MessageBoxIconType::QuestionIcon, "FM-1 en vivo",
        juce::String::fromUTF8("Al conectar, SLOOP Studio toma el patrón que tiene el FM-1 y reemplaza el actual "
                               "(las 4 pistas). Desde ahí, lo que edites aquí se escribe en el FM-1."),
        "Conectar", "Cancelar", this,
        juce::ModalCallbackFunction::create([safe](int ok) {
            if (ok && safe) { safe->proc.live->setEnabled(true); safe->refreshControls(); }
        }));
}

void SloopEditor::setView(bool sounds)
{
    soundsView = sounds;
    patternTab.setToggleState(!sounds, juce::dontSendNotification);
    soundsTab.setToggleState(sounds, juce::dontSendNotification);
    for (juce::Component* c : { (juce::Component*) &lenSlider, (juce::Component*) &gateSlider, (juce::Component*) &velSlider,
                                (juce::Component*) &chSlider, (juce::Component*) &lenLabel, (juce::Component*) &gateLabel,
                                (juce::Component*) &velLabel, (juce::Component*) &chLabel, (juce::Component*) &clearButton,
                                (juce::Component*) &flPlayToggle })
        c->setVisible(!sounds);
    samples->setVisible(sounds);
    refreshControls();
    repaint();
}

void SloopEditor::refreshControls()
{
    const int t = selected();
    lenSlider.setValue(session.length[t].load(), juce::dontSendNotification);
    gateSlider.setValue(session.gate[t].load(), juce::dontSendNotification);
    velSlider.setValue(session.velocity[t].load(), juce::dontSendNotification);
    chSlider.setValue(session.channel[t].load(), juce::dontSendNotification);
    const bool fmLive = proc.live->isLive() && proc.live->hasTransport();
    recButton.setToggleState(fmLive ? proc.live->fm1Recording() : session.recording.load(), juce::dontSendNotification);
    recButton.setTooltip(fmLive ? juce::String::fromUTF8("REC del FM-1, como su botón REC.")
                                : juce::String::fromUTF8("Graba en SLOOP Studio lo que tocas en el FM-1 (con FL en PLAY)."));
    playButton.setEnabled(fmLive);
    playButton.setToggleState(fmLive && proc.live->fm1Playing(), juce::dontSendNotification);
    syncButton.setToggleState(proc.clock->isEnabled(), juce::dontSendNotification);
    flPlayToggle.setToggleState(session.fm1DrivesFl.load(), juce::dontSendNotification);
    outputBox.setSelectedId(proc.outputTrack.load() + 2, juce::dontSendNotification);
    octDown.setVisible(t != kDrums && !soundsView);
    octUp.setVisible(t != kDrums && !soundsView);
    for (auto* s : { &lenSlider, &gateSlider, &velSlider, &chSlider })
        s->setColour(juce::Slider::trackColourId, trackColour(t).withAlpha(0.45f));
}

void SloopEditor::timerCallback()
{
    bool dirty = false;
    {   // the live link's state and message (another instance may have switched it)
        const bool on = proc.live->isEnabled();
        if (liveButton.getToggleState() != on) liveButton.setToggleState(on, juce::dontSendNotification);
        const auto st = (int) proc.live->state();
        const auto m = proc.live->message();
        if (st != seenLiveState || m != seenLiveMessage) { seenLiveState = st; seenLiveMessage = m; dirty = true; }
        if (noticeUntil && juce::Time::getMillisecondCounter() > noticeUntil) { noticeUntil = 0; notice.clear(); dirty = true; }
        // PLAY / REC / SYNC lights follow the FM-1 and the clock, whoever switched them
        const int transportBits = (proc.live->fm1Playing() ? 1 : 0) | (proc.live->fm1Recording() ? 2 : 0)
                                | (proc.live->isLive() && proc.live->hasTransport() ? 4 : 0) | (proc.clock->isEnabled() ? 8 : 0)
                                | (session.recording.load() ? 16 : 0) | (proc.clock->waitingForBar() ? 32 : 0)
                                | ((int) proc.clock->state() << 6);
        if (transportBits != seenTransport) { seenTransport = transportBits; refreshControls(); dirty = true; }
    }
    if (const auto r = session.revision.load(); r != seenRevision) { seenRevision = r; refreshControls(); dirty = true; }
    for (int t = 0; t < kTracks; ++t)
        if (const int s = proc.playStep[t].load(); s != seenSteps[t]) { seenSteps[t] = s; dirty = true; }
    if (const auto m = proc.midiInCount.load(); m != seenMidiIn) { seenMidiIn = m; midiLight = 6; dirty = true; }
    else if (midiLight > 0) { --midiLight; dirty = true; }
    if (dirty) repaint();
}

void SloopEditor::resized()
{
    auto r = getLocalBounds().reduced(16, 0);
    headerArea = r.removeFromTop(64);
    tabsArea = r.removeFromTop(66);
    hintArea = r.removeFromTop(30);
    {
        auto tabs = hintArea.withSizeKeepingCentre(hintArea.getWidth(), 24);
        patternTab.setBounds(tabs.removeFromLeft(96));
        tabs.removeFromLeft(4);
        soundsTab.setBounds(tabs.removeFromLeft(96));
    }
    footerArea = r.removeFromBottom(70);
    gridArea = r.reduced(0, 4).withTrimmedBottom(16);   // room for the step numbers
    if (samples) samples->setBounds(r.getUnion(footerArea).withTrimmedTop(4));

    auto h = headerArea.reduced(0, 14);
    auto box = h.removeFromRight(210);
    outputLabel.setBounds(box.removeFromTop(14));
    outputBox.setBounds(box);
    h.removeFromRight(12);
    recButton.setBounds(h.removeFromRight(64));
    h.removeFromRight(8);
    playButton.setBounds(h.removeFromRight(70));
    h.removeFromRight(8);
    liveButton.setBounds(h.removeFromRight(124));
    h.removeFromRight(8);
    syncButton.setBounds(h.removeFromRight(84));

    const int tabW = tabsArea.getWidth() / kTracks;
    for (int t = 0; t < kTracks; ++t)
    {
        tabRects[t] = juce::Rectangle<int>(tabsArea.getX() + t * tabW, tabsArea.getY(), tabW, tabsArea.getHeight()).reduced(4, 6);
        muteRects[t] = tabRects[t].withTrimmedLeft(tabRects[t].getWidth() - 34).withSizeKeepingCentre(24, 22).translated(-6, 0);
    }

    auto f = footerArea.reduced(0, 12);
    auto place = [&f](juce::Slider& s, juce::Label& l, int w) {
        auto c = f.removeFromLeft(w);
        l.setBounds(c.removeFromTop(16));
        s.setBounds(c.removeFromTop(26));
        f.removeFromLeft(14);
    };
    place(lenSlider, lenLabel, 150);
    place(gateSlider, gateLabel, 150);
    place(velSlider, velLabel, 150);
    place(chSlider, chLabel, 150);
    flPlayToggle.setBounds(f.removeFromLeft(190).withTrimmedTop(16).withHeight(26));
    auto right = f.withTrimmedTop(16);
    clearButton.setBounds(right.removeFromRight(90).withHeight(26));
    right.removeFromRight(10);
    octUp.setBounds(right.removeFromRight(70).withHeight(26));
    right.removeFromRight(6);
    octDown.setBounds(right.removeFromRight(70).withHeight(26));
}

juce::Rectangle<int> SloopEditor::cellBounds(int row, int step) const
{
    const auto area = gridArea.withTrimmedLeft(kLabelWidth);
    const float w = area.getWidth() / (float) kSteps, h = area.getHeight() / (float) rowCount();
    return juce::Rectangle<float>(area.getX() + step * w, area.getY() + row * h, w, h).toNearestInt();
}

juce::Point<int> SloopEditor::cellAt(juce::Point<int> p) const
{
    const auto area = gridArea.withTrimmedLeft(kLabelWidth);
    if (!area.contains(p)) return { -1, -1 };
    const int step = (int) ((p.x - area.getX()) * kSteps / (float) area.getWidth());
    const int row = (int) ((p.y - area.getY()) * rowCount() / (float) area.getHeight());
    return { juce::jlimit(0, kSteps - 1, step), juce::jlimit(0, rowCount() - 1, row) };
}

bool SloopEditor::cellOn(int row, int step) const
{
    const int t = selected();
    return t == kDrums ? session.drumOn(step, row) : session.noteOn(t, step, noteForRow(row));
}

void SloopEditor::setCell(int row, int step, bool on)
{
    const int t = selected();
    if (t == kDrums) { session.setDrum(step, row, on); return; }

    // Live with the FM-1: a step holds 4 notes at most there, so the 5th is refused here.
    if (on && proc.live->isEnabled() && !session.noteOn(t, step, noteForRow(row)))
    {
        int count = 0;
        for (int n = 0; n < 128; ++n) count += session.noteOn(t, step, n) ? 1 : 0;
        if (count >= 4)
        {
            notice = juce::String::fromUTF8("El FM-1 guarda hasta 4 notas por paso");
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
        if (muteRects[t].contains(p)) { session.muted[t] = session.muted[t].load() ? 0 : 1; session.touch(); return; }
        if (tabRects[t].contains(p)) { session.selected = t; session.touch(); return; }
    }
    const auto c = cellAt(p);
    if (c.x < 0) return;

    // DRUMS, on a hit: Shift+click = one level louder (GHOST, SOFT, NORM, HARD), as in the SLOOP
    // editor; Alt+click = one more roll (x1..x4).
    if (selected() == kDrums && cellOn(c.y, c.x) && (e.mods.isShiftDown() || e.mods.isAltDown()))
    {
        if (e.mods.isShiftDown())
        {
            static const int louder[4] = { LvHard, LvSoft, LvNorm, LvGhost };   // index: level -> next
            session.setLaneLevel(c.x, c.y, louder[session.laneLevel(c.x, c.y)]);
        }
        else session.setLaneRoll(c.x, c.y, (session.laneRoll(c.x, c.y) + 1) % 4);
        lastCell = { -1, -1 };   // no drag painting after a level / roll change
        return;
    }

    paintValue = !cellOn(c.y, c.x);
    setCell(c.y, c.x, paintValue);
    lastCell = c;
}

void SloopEditor::mouseDrag(const juce::MouseEvent& e)
{
    const auto c = cellAt(e.getPosition());
    if (c.x < 0 || c == lastCell || lastCell.x < 0) return;
    setCell(c.y, c.x, paintValue);
    lastCell = c;
}

void SloopEditor::paint(juce::Graphics& g)
{
    g.fillAll(kBg);
    const int sel = selected();

    // Header
    g.setColour(kText);
    g.setFont(juce::FontOptions(24.0f, juce::Font::bold));
    g.drawText("SLOOP STUDIO", headerArea.withTrimmedTop(10).withHeight(28), juce::Justification::left);
    g.setColour(kDim);
    g.setFont(juce::FontOptions(12.0f));
    g.drawText(juce::String(juce::CharPointer_UTF8("Secuenciador y controlador MIDI  \xc2\xb7  FL Studio  \xc2\xb7  "
                                                   "control f\xc3\xadsico opcional con FM-1")),
               headerArea.withTrimmedTop(38).withHeight(16), juce::Justification::left);

    // Slider wells (LinearBar draws only the filled part)
    g.setColour(kCellOff);
    for (auto* s : { &lenSlider, &gateSlider, &velSlider, &chSlider })
        g.fillRoundedRectangle(s->getBounds().toFloat(), 4.0f);

    // Transport / FM-1 lights, left of the REC button
    auto lights = juce::Rectangle<int>(syncButton.getX() - 186, headerArea.getY() + 18, 180, 28);
    auto light = [&g](juce::Rectangle<int> r, bool on, juce::Colour c, const char* label) {
        g.setColour(on ? c : juce::Colour(0xff3a3f4a));
        g.fillEllipse(r.removeFromLeft(10).withSizeKeepingCentre(10, 10).toFloat());
        g.setColour(kDim);
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText(label, r.withTrimmedLeft(6), juce::Justification::centredLeft);
    };
    light(lights.removeFromLeft(80), proc.hostPlaying.load(), juce::Colour(0xff4ade80), "PLAY FL");
    light(lights.removeFromLeft(100), midiLight > 0, juce::Colour(0xfffacc15), "MIDI IN");

    // Track tabs
    for (int t = 0; t < kTracks; ++t)
    {
        const auto r = tabRects[t].toFloat();
        const auto col = trackColour(t);
        const bool sending = (proc.trackMask() >> t) & 1u;
        g.setColour(t == sel ? col.withAlpha(0.18f) : kPanel);
        g.fillRoundedRectangle(r, 8.0f);
        g.setColour(t == sel ? col : juce::Colour(0xff2f343d));
        g.drawRoundedRectangle(r.reduced(0.5f), 8.0f, t == sel ? 2.0f : 1.0f);
        g.setColour(col);
        g.fillRoundedRectangle(r.withWidth(4.0f).reduced(0, 10), 2.0f);

        auto text = tabRects[t].reduced(14, 8);
        g.setColour(kText.withAlpha(sending ? 1.0f : 0.4f));
        g.setFont(juce::FontOptions(15.0f, juce::Font::bold));
        g.drawText(kTrackNames[t], text.removeFromTop(22), juce::Justification::left);
        g.setColour(kDim.withAlpha(sending ? 1.0f : 0.5f));
        g.setFont(juce::FontOptions(11.0f));
        const juce::String dot(juce::CharPointer_UTF8("  \xc2\xb7  "));
        juce::String sub = "CH " + juce::String(session.channel[t].load()) + dot
                         + juce::String(session.length[t].load()) + " pasos";
        if (!sending) sub += dot + juce::String(juce::CharPointer_UTF8("no sale aqu\xc3\xad"));
        g.drawText(sub, text, juce::Justification::topLeft);

        const bool muted = session.muted[t].load();
        g.setColour(muted ? juce::Colour(0xffe5383b) : juce::Colour(0xff2f343d));
        g.fillRoundedRectangle(muteRects[t].toFloat(), 4.0f);
        g.setColour(muted ? juce::Colours::white : kDim);
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText("M", muteRects[t], juce::Justification::centred);
    }

    // Hint (left) and the FM-1 import status (right), between the tabs and the grid
    g.setFont(juce::FontOptions(11.0f));
    g.setColour(kDim);
    if (!soundsView)
    g.drawText(juce::String::fromUTF8(sel == kDrums
                   ? "Clic: golpe   \xc2\xb7   Shift+clic: m\xc3\xa1s fuerte (GHOST \xe2\x86\x92 SOFT \xe2\x86\x92 NORM \xe2\x86\x92 HARD)   \xc2\xb7   Alt+clic: roll x1\xe2\x80\x93x4"
                   : "Clic: nota (acordes permitidos)   \xc2\xb7   OCT -/+ mueve las dos octavas"),
               hintArea.withTrimmedLeft(212), juce::Justification::centredLeft);
    {
        using S = sloop::Fm1Live::State;
        const auto st = proc.live->state();
        juce::String text = notice.isNotEmpty() ? notice : proc.live->message();
        if (notice.isEmpty() && proc.clock->isEnabled())
        {
            const auto cm = proc.clock->waitingForBar() ? juce::String::fromUTF8("FM-1 entra en el próximo compás")
                                                        : proc.clock->message();
            text = text.isEmpty() ? cm : text + juce::String::fromUTF8("  ·  ") + cm;
        }
        juce::Colour c = notice.isNotEmpty() ? juce::Colour(0xfffacc15)
                       : st == S::Live ? juce::Colour(0xff4ade80)
                       : st == S::Error ? juce::Colour(0xffe5383b) : juce::Colour(0xfffacc15);
        if (text.isNotEmpty())
        {
            g.setColour(c);
            g.drawText(text, hintArea.withTrimmedRight(4), juce::Justification::centredRight);
        }
    }

    if (soundsView) return;   // the SONIDOS panel covers the rest

    // Grid
    g.setColour(kPanel);
    g.fillRoundedRectangle(gridArea.toFloat().expanded(4), 8.0f);
    const auto col = trackColour(sel);
    const int len = session.length[sel].load();
    const int play = proc.playStep[sel].load();
    const int rows = rowCount();

    for (int row = 0; row < rows; ++row)
    {
        const auto rr = cellBounds(row, 0);
        auto label = juce::Rectangle<int>(gridArea.getX(), rr.getY(), kLabelWidth - 8, rr.getHeight());
        juce::String name;
        bool black = false;
        if (sel == kDrums) name = kLaneNames[row];
        else
        {
            const int n = noteForRow(row);
            black = juce::String(kNoteNames[n % 12]).contains("#");
            name = noteName(n);
        }
        if (black)
        {
            g.setColour(juce::Colour(0xff1a1d22));
            g.fillRect(label.withRight(cellBounds(row, kSteps - 1).getRight()));
        }
        g.setColour(name.startsWith("C") && !black && sel != kDrums ? kText : kDim);
        g.setFont(juce::FontOptions(juce::jmin(12.0f, rr.getHeight() * 0.7f), juce::Font::bold));
        g.drawText(name, label.withTrimmedLeft(8), juce::Justification::centredLeft);
    }

    for (int step = 0; step < kSteps; ++step)
    {
        if (step == play)
        {
            auto colRect = cellBounds(0, step).getUnion(cellBounds(rows - 1, step));
            g.setColour(juce::Colours::white.withAlpha(0.07f));
            g.fillRect(colRect);
        }
        for (int row = 0; row < rows; ++row)
        {
            const auto c = cellBounds(row, step).reduced(2).toFloat();
            const bool on = cellOn(row, step);
            const bool inLen = step < len;
            const bool drum = sel == kDrums;
            const int level = on && drum ? session.laneLevel(step, row) : LvNorm;
            static const float levelAlpha[4] = { 0.85f, 0.35f, 0.6f, 1.0f };   // NORM GHOST SOFT HARD
            juce::Colour fill = on ? col.withMultipliedAlpha(levelAlpha[level])
                                   : (step / 4) % 2 == 0 ? kCellOff : kCellBeat;
            if (!inLen) fill = fill.withMultipliedAlpha(0.25f);
            if (on && step == play) fill = fill.brighter(0.5f);
            g.setColour((step / 4) % 2 == 0 ? kCellOff : kCellBeat);
            g.fillRoundedRectangle(c, 3.0f);
            g.setColour(fill);
            g.fillRoundedRectangle(c, 3.0f);
            if (on && drum)
            {
                if (level == LvHard)
                {
                    g.setColour(juce::Colours::white.withAlpha(0.8f));
                    g.drawRoundedRectangle(c.reduced(0.5f), 3.0f, 1.2f);
                }
                const int hits = session.laneRoll(step, row) + 1;   // a roll: one slice per hit
                g.setColour(kBg.withAlpha(0.85f));
                for (int k = 1; k < hits; ++k)
                {
                    const float x = c.getX() + c.getWidth() * k / hits;
                    g.fillRect(x - 1.0f, c.getY() + 3.0f, 2.0f, c.getHeight() - 6.0f);
                }
            }
        }
    }

    // Step numbers under the grid
    g.setFont(juce::FontOptions(10.0f));
    for (int step = 0; step < kSteps; ++step)
    {
        const auto b = cellBounds(rows - 1, step);
        g.setColour(step == play ? col : kDim.withAlpha(0.7f));
        g.drawText(juce::String(step + 1), b.withY(b.getBottom() + 2).withHeight(12), juce::Justification::centred);
    }
}
