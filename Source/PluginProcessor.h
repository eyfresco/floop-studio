// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "SeqEngine.h"
#include "FM1Live.h"
#include "FM1Clock.h"

// SLOOP Studio: a 4-track step sequencer and MIDI controller for FL Studio. It makes no sound:
// its MIDI goes out through FL's routing to the instruments the user picks (FPC, FLEX, Serum...).
class SloopProcessor : public juce::AudioProcessor
{
public:
    // Which tracks this instance sends out: all of them, or a single one (see Session.h for why).
    enum Output { AllTracks = -1 };

    SloopProcessor();

    void prepareToPlay(double sampleRate, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override { return true; }
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SLOOP Studio"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    sloop::Session& session = sloop::Session::shared();
    juce::SharedResourcePointer<sloop::Fm1Live> live;   // one FM-1 link for every instance
    juce::SharedResourcePointer<sloop::Fm1Clock> clock; // FL's tempo and PLAY / STOP -> the FM-1

    std::atomic<int> outputTrack { AllTracks };
    uint32_t trackMask() const
    {
        const int o = outputTrack.load();
        return o < 0 ? 0xFu : (1u << o);
    }

    // For the UI.
    std::atomic<bool> hostPlaying { false };
    std::array<std::atomic<int>, sloop::kTracks> playStep {};
    std::atomic<uint32_t> midiInCount { 0 };   // blinks the "FM-1" light when notes arrive

    // The drum lane a key plays: one lane per white key from F3, as on the FM-1 under SLOOP; a black
    // key plays the white key left of it.
    static int laneForKey(int note);

private:
    sloop::SeqEngine engine;
    sloop::EventList events;
    juce::MidiBuffer outBuffer;
    double rate = 44100.0;
    int lastOutput = AllTracks;
    uint8_t liveChannel[16][128] {};   // live notes: the channel each sounding key went out on (+1)
    uint8_t liveNote[16][128] {};      // ... and the note it became (drum keys map to lanes)

    void handleLiveInput(const juce::MidiBuffer& in, juce::MidiBuffer& out,
                         const sloop::Transport& t, int numSamples);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SloopProcessor)
};
