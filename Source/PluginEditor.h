// SPDX-License-Identifier: GPL-3.0-only
// Floop Studio's window, laid out like the SLOOP web editor, in rows:
//   header   sloop studio · fl / midi lights ·  sync fl · fm-1 vivo · ▶ fm-1 · rec · salida
//   tracks   drums · synth 1 · synth 2 · synth 3 (select, mute)
//   tabs     patrón · sonido · samples                      the FM-1 link's state
//   content  patrón: steps / gate / vel / ch, the drum brushes (level, roll) or OCT, the grid
//            sonido: SoundPanel · samples: SamplesPanel
#pragma once
#include "PluginProcessor.h"
#include "SamplesPanel.h"
#include "SoundPanel.h"
#include "MidiMapPanel.h"
#include "CreditsPanel.h"
#include "SloopLook.h"

class SloopEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SloopEditor(SloopProcessor&);
    ~SloopEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;

    enum { ViewPattern, ViewSound, ViewSamples, ViewMap, ViewCredits };
    void setView(int v);   // (public for Tests/UiSnapshot.cpp)
    sloop::SoundPanel* soundForTest() { return soundPanel.get(); }
    void showSteps(int pg, bool all) { page = pg; zoomAll = all; refreshControls(); }   // (tests)

private:
    SloopProcessor& proc;
    sloop::Session& session;
    sloop::ui::Look look;

    // header
    // reloj: libre / fl -> fm-1 (Fm1Clock: FL's clock to the FM-1) / fm-1 -> fl (the FM-1's CLOCK OUT, FL's External sync)
    juce::ComboBox clockBox;
    void chooseClock(int mode);
    juce::TextButton liveButton { "fm-1 vivo" }, playButton, recButton { "rec" };
    juce::ComboBox outputBox;
    void askToGoLive();

    // tabs and the panels behind them
    juce::TextButton patternTab, soundTab, soundsTab, mapTab, creditsTab;
    std::unique_ptr<sloop::CreditsPanel> creditsPanel;
    std::unique_ptr<sloop::MidiMapPanel> mapPanel;
    std::unique_ptr<sloop::SoundPanel> soundPanel;
    std::unique_ptr<sloop::SamplesPanel> samples;
    int view = ViewPattern;

    // patrón: the track's settings, the drum brushes, OCT, clear, FM-1 PLAY -> FL
    juce::Slider lenSlider, gateSlider, velSlider, chSlider;
    juce::Label lenLabel, gateLabel, velLabel, chLabel, levelLabel, rollLabel, octLabel;
    std::array<juce::TextButton, 4> levelButtons, rollButtons;
    juce::TextButton clearButton, octDown { "-" }, octUp { "+" };
    juce::ToggleButton flPlayToggle;
    // 64 steps: a page of 16 (1-16 ... 49-64), or all 64 at once (todo)
    std::array<juce::TextButton, 4> pageButtons;
    juce::TextButton zoomButton;
    int page = 0, seenPlayPage = -1;
    bool zoomAll = false;
    int firstStep() const { return zoomAll ? 0 : page * sloop::kPageSteps; }
    int visibleSteps() const { return zoomAll ? sloop::kSteps : sloop::kPageSteps; }
    int brushLevel = sloop::LvNorm, brushRoll = 0;   // what a new drum hit gets (the editor's Level / Roll)

    juce::String notice;              // a short message of the editor's own (e.g. the 4-note limit)
    juce::uint32 noticeUntil = 0;

    juce::Rectangle<int> headerArea, tracksArea, tabsArea, toolA, toolB, numbersArea, gridArea, hintArea, contentArea;
    std::array<juce::Rectangle<int>, sloop::kTracks> tabRects, muteRects;

    int baseNote[sloop::kTracks] { 0, 48, 48, 36 };   // lowest row of each synth's 2-octave grid
    static constexpr int kSynthRows = 24;
    static constexpr int kLabelWidth = 64;

    uint32_t seenRevision = ~0u;
    int seenSteps[sloop::kTracks] { -2, -2, -2, -2 };
    uint32_t seenMidiIn = 0;
    int seenLiveState = -1, seenTransport = -1;
    juce::String seenLiveMessage;
    int midiLight = 0;
    int paintMode = 0;                // a drag: 1 paints the brush, 0 clears (drums); notes: 1 on, 0 off
    juce::Point<int> lastCell { -1, -1 };

    int selected() const { return session.selected.load(); }
    int rowCount() const { return selected() == sloop::kDrums ? sloop::kLanes : kSynthRows; }
    juce::Rectangle<int> cellBounds(int row, int step) const;
    juce::Point<int> cellAt(juce::Point<int>) const;   // (step, row), or (-1, -1)
    bool cellOn(int row, int step) const;
    void setCell(int row, int step, bool on);
    int noteForRow(int row) const { return baseNote[selected()] + (kSynthRows - 1 - row); }

    void refreshControls();
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SloopEditor)
};
