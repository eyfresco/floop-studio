// SPDX-License-Identifier: GPL-3.0-only
// MAPA MIDI: the FM-1 drawn with its real layout (M-VAVE's manual), every control with its MIDI under SLOOP.
//   - the 27 keys F3..G5: notes 53..79 (on DRUMS the white keys are the 16 sounds); lit when the
//     FM-1 plays them, and a click plays the FM-1 (channel 16: OTHER = SEL plays the selected track)
//   - the 14 buttons: REMOTE (GLO > MIDI 3), CC 102..115 press them; a click presses the FM-1's
//   - the knobs: no MIDI under SLOOP (the plugin sees them through FM-1 EN VIVO)
// sloop-fm1 GUIDE.md "REMOTE", README "The controls".
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "FM1Live.h"
#include "SloopLook.h"

namespace sloop {

class MidiMapPanel : public juce::Component, private juce::Timer
{
public:
    MidiMapPanel(Fm1Live& live, Session& session);
    ~MidiMapPanel() override { stopTimer(); }

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override { hover = -1; repaint(); }
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    enum Kind { Button, Knob, Key };
    struct Control
    {
        Kind kind;
        juce::String name, info;
        int midi = -1;                 // the CC (buttons) or the note (keys)
        bool black = false;
        juce::Rectangle<float> r;
    };

    Fm1Live& live;
    Session& session;
    std::vector<Control> controls;
    int hover = -1, pressed = -1;
    uint32_t seenNotes = ~0u;
    juce::TextButton remoteButton;
    juce::Rectangle<int> barArea, bodyArea, infoArea;
    juce::Rectangle<float> device;   // the FM-1 drawn, at the manual's proportions

    void build();
    int controlAt(juce::Point<float>) const;
    void timerCallback() override;
};

} // namespace sloop
