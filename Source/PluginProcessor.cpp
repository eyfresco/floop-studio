// SPDX-License-Identifier: GPL-3.0-only
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace sloop;

SloopProcessor::SloopProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    for (auto& s : playStep) s = -1;
    sloop::VirtualMidiSource::get();   // the "Floop Studio" MIDI port exists as soon as the plugin is loaded
}

void SloopProcessor::prepareToPlay(double sampleRate, int)
{
    rate = sampleRate;
    outBuffer.ensureSize(16384);
}

int SloopProcessor::laneForKey(int note)
{
    static const int whiteOf[12] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };   // C C# D D# E F F# G G# A A# B
    auto white = [](int n) { return (n / 12) * 7 + whiteOf[n % 12]; };
    const int lane = (white(note) - white(53)) % kLanes;   // F3 = lane 0 (kick)
    return lane < 0 ? lane + kLanes : lane;
}

void SloopProcessor::handleLiveInput(const juce::MidiBuffer& in, juce::MidiBuffer& out,
                                     const Transport& t, int numSamples)
{
    const int sel = session.selected.load();
    const bool mine = (trackMask() >> sel) & 1u;
    const uint8_t ch = (uint8_t) (session.channel[sel].load() - 1);
    const double samplesPerPpq = t.bpm > 0 ? rate * 60.0 / t.bpm : 0.0;

    for (const auto meta : in)
    {
        const auto m = meta.getMessage();
        const int pos = juce::jlimit(0, juce::jmax(0, numSamples - 1), meta.samplePosition);

        if (m.isNoteOn())
        {
            midiInCount.fetch_add(1);
            const int key = m.getNoteNumber();
            const int lane = laneForKey(key);
            const int note = sel == kDrums ? session.laneNote[lane].load() : key;

            // REC: write the key into the step nearest to when it was played.
            if (session.recording.load() && t.playing && samplesPerPpq > 0)
            {
                const double ppq = t.ppq + pos / samplesPerPpq;
                const int idx = session.stepIndex(sel, (int64_t) std::llround(ppq / SeqEngine::kStepPpq));
                if (sel == kDrums) session.setDrum(idx, lane, true);
                else session.setNote(sel, idx, key, true);
            }

            if (mine)
            {
                const int srcCh = m.getChannel() - 1;
                liveChannel[srcCh][key] = (uint8_t) (ch + 1);
                liveNote[srcCh][key] = (uint8_t) note;
                out.addEvent(juce::MidiMessage::noteOn(ch + 1, note, m.getVelocity()), pos);
            }
        }
        else if (m.isNoteOff())
        {
            const int srcCh = m.getChannel() - 1, key = m.getNoteNumber();
            if (liveChannel[srcCh][key])   // close it where it was opened, even if the track changed since
            {
                out.addEvent(juce::MidiMessage::noteOff(liveChannel[srcCh][key], liveNote[srcCh][key]), pos);
                liveChannel[srcCh][key] = 0;
            }
        }
        else if (mine && (m.isController() || m.isPitchWheel() || m.isChannelPressure() || m.isAftertouch()))
        {
            auto copy = m;
            copy.setChannel(ch + 1);   // the FM-1's knobs, bend and pressure play the selected track
            out.addEvent(copy, pos);
        }
    }
}

void SloopProcessor::processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    audio.clear();
    const int n = audio.getNumSamples();

    Transport t;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            t.playing = pos->getIsPlaying();
            t.ppq = pos->getPpqPosition().orFallback(0.0);
            t.bpm = pos->getBpm().orFallback(120.0);
            if (clock->isEnabled())
            {
                const auto sig = pos->getTimeSignature().orFallback(juce::AudioPlayHead::TimeSignature {});
                clock->publish(t.playing, t.ppq, t.bpm, sig.numerator, sig.denominator);
            }
        }

    auto& out = outBuffer;   // a member, sized in prepareToPlay: no allocation here
    out.clear();
    events.clear();

    // The output changed (DRUMS only -> all tracks...): close what this instance had started.
    const int o = outputTrack.load();
    if (o != lastOutput) { engine.allNotesOff(0, events); lastOutput = o; }

    handleLiveInput(midi, out, t, n);
    engine.process(session, t, n, rate, trackMask(), events);

    for (int i = 0; i < events.count; ++i)
    {
        const auto& e = events.items[i];
        out.addEvent(juce::MidiMessage(e.status, e.data1, e.data2), e.sample);
    }
    midi.swapWith(out);

    hostPlaying = t.playing;
    session.hostPlaying = t.playing;
    for (int tr = 0; tr < kTracks; ++tr) playStep[tr] = t.playing ? engine.currentStep(tr) : -1;
}

void SloopProcessor::processBlockBypassed(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    audio.clear();
    events.clear();
    engine.allNotesOff(0, events);
    midi.clear();
    for (int i = 0; i < events.count; ++i)
        midi.addEvent(juce::MidiMessage(events.items[i].status, events.items[i].data1, events.items[i].data2), 0);
}

void SloopProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    juce::ValueTree state("SLOOPStudio");
    state.setProperty("version", 1, nullptr);
    state.setProperty("pattern", juce::String(session.serialise()), nullptr);
    state.setProperty("output", outputTrack.load(), nullptr);
    juce::MemoryOutputStream os(dest, false);
    state.writeToStream(os);
}

void SloopProcessor::setStateInformation(const void* data, int size)
{
    const auto state = juce::ValueTree::readFromData(data, (size_t) size);
    if (!state.isValid() || !state.hasType("SLOOPStudio")) return;
    session.deserialise(state.getProperty("pattern").toString().toStdString());
    outputTrack = juce::jlimit(-1, kTracks - 1, (int) state.getProperty("output", AllTracks));
}

juce::AudioProcessorEditor* SloopProcessor::createEditor() { return new SloopEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SloopProcessor(); }
