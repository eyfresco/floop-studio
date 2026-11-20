// SPDX-License-Identifier: GPL-3.0-only
// SONIDO: the selected track's sound on the FM-1, like the SLOOP Editor's Sound page. The engine,
// its presets, and the FM-1's own pages of four knobs (EDIT 1 / 2 of the engine, ENV, LFO, FX,
// INSERT, VOICE, ARP, SEL...). Every knob goes to the FM-1 at once (FM-1 EN VIVO), and a knob
// turned on the FM-1 moves here.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "FM1Live.h"
#include "SloopLook.h"

namespace sloop {

class SoundPanel : public juce::Component, private juce::Timer
{
public:
    explicit SoundPanel(Fm1Live& live);
    ~SoundPanel() override { stopTimer(); }

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // (Tests/UiSnapshot.cpp: draw a sound without an FM-1)
    void showForTest(const Fm1Live::Sound& s) { snd = s; testMode = true; refresh(); }

private:
    struct Knob { int scope = 0, id = snd::kNone; juce::Rectangle<int> bounds; };

    Fm1Live& live;
    Fm1Live::Sound snd;
    uint32_t seenRev = ~0u;
    bool testMode = false;

    juce::ComboBox engineBox, presetBox;
    juce::Label engineLabel, presetLabel;
    juce::TextButton prevPreset { "<" }, nextPreset { ">" };
    juce::ComboBox userBox;
    juce::Label userLabel;
    juce::TextButton userLoad, userSave;
    juce::String notice;
    juce::uint32 noticeUntil = 0;
    void askToStore();

    juce::Rectangle<int> topArea, gridArea, noticeArea;
    std::vector<std::pair<juce::Rectangle<int>, const snd::Page*>> cards;
    std::vector<Knob> knobs;
    int dragging = -1, dragStartValue = 0, dragStartY = 0;

    void timerCallback() override;
    void refresh();
    void layoutCards();
    int knobAt(juce::Point<int>) const;
    const snd::Desc* descOf(const Knob&) const;
    int valueOf(const Knob&) const;
    void setValue(const Knob&, int v);
    const std::vector<snd::Page>& pages() const { return snd.drums ? snd::drumPages() : snd::synthPages(); }
};

} // namespace sloop
