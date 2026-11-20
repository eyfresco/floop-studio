// SPDX-License-Identifier: GPL-3.0-only
// End-to-end check of the built VST3: loads it like a host, writes a pattern through its saved
// state, plays it with a moving transport, checks the MIDI it sends, and saves a picture of the UI.
//   sloop_host_check <path to Floop Studio.vst3> <output png>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "Session.h"

struct FakePlayHead : juce::AudioPlayHead
{
    double ppq = 0, bpm = 120; bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setIsPlaying(playing); p.setPpqPosition(ppq); p.setBpm(bpm);
        return p;
    }
};

// A host's VST3 state: JUCE's host wraps the plugin's own bytes as base64 in <VST3PluginState>.
static void loadState(juce::AudioPluginInstance& plugin, const juce::ValueTree& state)
{
    juce::MemoryBlock raw;
    { juce::MemoryOutputStream os(raw, false); state.writeToStream(os); }
    juce::XmlElement xml("VST3PluginState");
    xml.createNewChildElement("IComponent")->addTextElement(raw.toBase64Encoding());
    juce::MemoryBlock wrapped;
    juce::AudioProcessor::copyXmlToBinary(xml, wrapped);
    plugin.setStateInformation(wrapped.getData(), (int) wrapped.getSize());
}

static int fail(const juce::String& m) { std::printf("FAIL: %s\n", m.toRawUTF8()); return 1; }

int main(int argc, char** argv)
{
    if (argc < 3) return fail("usage: sloop_host_check <plugin.vst3> <out.png>");
    juce::ScopedJuceInitialiser_GUI gui;

    juce::AudioPluginFormatManager fm;
    fm.addFormat(new juce::VST3PluginFormat());
    juce::OwnedArray<juce::PluginDescription> found;
    juce::VST3PluginFormat vst3;
    vst3.findAllTypesForFile(found, argv[1]);
    if (found.isEmpty()) return fail("plugin not found");
    std::printf("loaded: %s\n", found[0]->name.toRawUTF8());

    juce::String err;
    auto plugin = fm.createPluginInstance(*found[0], 48000, 512, err);
    if (!plugin) return fail("instantiate: " + err);

    // A pattern: kick on the beats, hats on the off-beats, a C minor chord on SYNTH 1 step 1.
    sloop::Session pattern;
    pattern.reset();
    for (int s : { 0, 4, 8, 12 }) pattern.setDrum(s, 0, true);
    for (int s : { 2, 6, 10, 14 }) pattern.setDrum(s, 4, true);
    for (int n : { 60, 63, 67 }) pattern.setNote(1, 0, n, true);
    juce::ValueTree state("SLOOPStudio");
    state.setProperty("version", 1, nullptr);
    state.setProperty("pattern", juce::String(pattern.serialise()), nullptr);
    state.setProperty("output", -1, nullptr);
    loadState(*plugin, state);

    FakePlayHead ph;
    plugin->setPlayHead(&ph);
    plugin->prepareToPlay(48000, 512);

    juce::AudioBuffer<float> audio(2, 512);
    int kicks = 0, hats = 0, chord = 0, offs = 0, firstKick = -1;
    for (int b = 0; b < 187; ++b)   // just under 2 s = one bar at 120 BPM (not the next downbeat)
    {
        juce::MidiBuffer midi;
        plugin->processBlock(audio, midi);
        for (const auto m : midi)
        {
            const auto msg = m.getMessage();
            if (msg.isNoteOn())
            {
                if (msg.getChannel() == 10 && msg.getNoteNumber() == 36) { if (firstKick < 0) firstKick = b * 512 + m.samplePosition; ++kicks; }
                if (msg.getChannel() == 10 && msg.getNoteNumber() == 42) ++hats;
                if (msg.getChannel() == 1) ++chord;
            }
            else if (msg.isNoteOff()) ++offs;
        }
        ph.ppq += 512.0 / 48000.0 * 2.0;   // 120 BPM: 2 quarter notes per second
    }
    std::printf("bar 1: kicks=%d hats=%d chord=%d note-offs=%d first kick at sample %d\n", kicks, hats, chord, offs, firstKick);
    if (kicks != 4 || hats != 4 || chord != 3 || firstKick != 0) return fail("unexpected MIDI");

    // Output = DRUMS only: the synth chord must not come out.
    state.setProperty("output", 0, nullptr);
    loadState(*plugin, state);
    ph.ppq = 0; ph.playing = false;
    { juce::MidiBuffer m; plugin->processBlock(audio, m); }
    ph.playing = true;
    int synthNotes = 0, drumNotes = 0;
    for (int b = 0; b < 20; ++b)
    {
        juce::MidiBuffer midi;
        plugin->processBlock(audio, midi);
        for (const auto m : midi)
            if (m.getMessage().isNoteOn()) (m.getMessage().getChannel() == 10 ? drumNotes : synthNotes)++;
        ph.ppq += 512.0 / 48000.0 * 2.0;
    }
    std::printf("DRUMS-only instance: drum notes=%d synth notes=%d\n", drumNotes, synthNotes);
    if (synthNotes != 0 || drumNotes == 0) return fail("routing");

    // Stop: everything closes.
    ph.playing = false;
    { juce::MidiBuffer m; plugin->processBlock(audio, m); }

    // Picture of the UI.
    std::unique_ptr<juce::AudioProcessorEditor> ed(plugin->createEditorIfNeeded());
    if (ed)
    {
        ed->setVisible(true);
        auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 2.0f);
        juce::File out(argv[2]);
        out.deleteFile();
        juce::FileOutputStream fos(out);
        juce::PNGImageFormat().writeImageToStream(img, fos);
        std::printf("UI snapshot: %s (%dx%d)\n", out.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
        ed.reset();
    }
    plugin->releaseResources();
    std::printf("HOST CHECK PASSED\n");
    return 0;
}
