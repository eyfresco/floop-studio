// SPDX-License-Identifier: GPL-3.0-only
// SONIDOS: your own sounds into the FM-1's USR1..USR4, like the SLOOP Editor's Samples and Drum kit
// pages. INSTRUMENTO: up to 16 files, each on its note, the keys in between play the nearest one.
// KIT DE BATERÍA: one sound per drum pad (the 16 white keys). Then "ENVIAR A USRn".
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "SampleTools.h"
#include "FM1Live.h"
#include "SloopLook.h"

namespace sloop {

// Talks to the FM-1 off the message thread: reads the slots, or uploads one (pausing the live link).
class SampleJob : public juce::Thread
{
public:
    SampleJob(Fm1Live& l, int slotToSend, std::optional<smp::Slot> data, bool setDrumKit)
        : juce::Thread("SLOOP samples"), live(l), slot(slotToSend), toSend(std::move(data)), drumKit(setDrumKit) {}
    ~SampleJob() override { stopThread(15000); }

    void run() override;

    std::atomic<float> progress { 0.0f };
    // Results (read after the thread ended).
    bool ok = false;
    juce::String message;
    std::vector<SlotInfo> slots;

private:
    Fm1Live& live;
    int slot;
    std::optional<smp::Slot> toSend;   // none: only read the slots
    bool drumKit;
};

class SamplesPanel : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit SamplesPanel(Fm1Live& live);
    ~SamplesPanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void visibilityChanged() override;

    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int x, int y) override;
    void fileDragMove(const juce::StringArray&, int x, int y) override;
    void fileDragExit(const juce::StringArray&) override { dragPad = -1; dragging = false; repaint(); }
    void fileDragEnter(const juce::StringArray&, int, int) override { dragging = true; repaint(); }

private:
    enum class Mode { Instrument, Kit };
    struct Item { juce::String file; std::vector<int16_t> samples; int root = 60; };

    Fm1Live& live;
    Mode mode = Mode::Instrument;
    std::vector<Item> items;
    std::array<std::optional<smp::Pad>, 16> pads;
    int slot = 0;
    std::vector<SlotInfo> slots;
    bool slotsKnown = false, askedOnce = false;

    juce::TextButton instButton { "INSTRUMENTO" }, kitButton, readButton { "LEER FM-1" }, addButton, clearButton { "VACIAR" },
        sendButton;
    std::array<juce::TextButton, smp::kSlots> slotButtons;
    juce::TextEditor nameEditor;
    juce::ToggleButton useKitToggle;

    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<SampleJob> job;
    bool jobSends = false;
    juce::String status;
    bool statusOk = true;
    int dragPad = -1;
    bool dragging = false;

    juce::Rectangle<int> topArea, bodyArea, bottomArea, meterArea;
    std::array<juce::Rectangle<int>, 16> padRects;

    void timerCallback() override;
    void refresh();
    void setStatus(const juce::String& s, bool ok) { status = s; statusOk = ok; repaint(); }

    void chooseFiles(int forPad);
    void addFiles(const juce::Array<juce::File>& files, int pad = -1);
    int padAt(juce::Point<int>) const;
    juce::Rectangle<int> rowRect(int i) const;
    std::vector<std::pair<int, int>> itemKeys() const;   // (lo, hi) per item, as the slot will split them
    int usedBytes() const;
    void askToSend();
    void startJob(std::optional<smp::Slot> data);
    juce::String slotLabel(int k) const;
};

} // namespace sloop
