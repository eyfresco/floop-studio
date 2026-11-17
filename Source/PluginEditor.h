// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "PluginProcessor.h"
#include "SamplesPanel.h"

class SloopEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SloopEditor(SloopProcessor&);
    ~SloopEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;

private:
    SloopProcessor& proc;
    sloop::Session& session;

    juce::ComboBox outputBox;
    juce::Label outputLabel;
    juce::TextButton liveButton { "FM-1 EN VIVO" }, syncButton { "SYNC FL" }, playButton;
    void askToGoLive();

    // PATRÓN (the step grid) or SONIDOS (samples and drum kits for the FM-1's USR slots)
    juce::TextButton patternTab, soundsTab;
    juce::ToggleButton flPlayToggle;   // the FM-1's PLAY / STOP drive FL (Session::fm1DrivesFl)
    std::unique_ptr<sloop::SamplesPanel> samples;
    bool soundsView = false;

public:
    void setView(bool sounds);   // (public for Tests/UiSnapshot.cpp)

private:
    juce::String notice;              // a short message of the editor's own (e.g. the 4-note limit)
    juce::uint32 noticeUntil = 0;

    juce::TextButton recButton { "REC" }, clearButton { "LIMPIAR" }, octDown { "OCT -" }, octUp { "OCT +" };
    juce::Slider lenSlider, gateSlider, velSlider, chSlider;
    juce::Label lenLabel, gateLabel, velLabel, chLabel;

    juce::Rectangle<int> headerArea, tabsArea, hintArea, gridArea, footerArea;
    std::array<juce::Rectangle<int>, sloop::kTracks> tabRects, muteRects;

    int baseNote[sloop::kTracks] { 0, 48, 48, 36 };   // lowest row of each synth's 2-octave grid
    static constexpr int kSynthRows = 24;
    static constexpr int kLabelWidth = 92;

    uint32_t seenRevision = ~0u;
    int seenSteps[sloop::kTracks] { -2, -2, -2, -2 };
    uint32_t seenMidiIn = 0;
    int seenLiveState = -1, seenTransport = -1;
    juce::String seenLiveMessage;
    int midiLight = 0;
    bool paintValue = true;
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
    static juce::Colour trackColour(int t);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SloopEditor)
};
