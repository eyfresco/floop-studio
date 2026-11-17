// SPDX-License-Identifier: GPL-3.0-only
// Renders the editor to PNG (DRUMS and SYNTH 1 views) to check the layout without a host.
//   sloop_ui_snapshot <out-prefix>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::String prefix = argc > 1 ? argv[1] : "ui";
    SloopProcessor proc;
    auto& s = proc.session;
    for (int st : { 0, 4, 8, 12 }) s.setDrum(st, 0, true);
    for (int st : { 2, 6, 10, 14 }) s.setDrum(st, 4, true);
    for (int st : { 4, 12 }) s.setDrum(st, 3, true);
    s.setDrum(7, 2, true); s.setDrum(15, 5, true);
    for (int n : { 60, 63, 67 }) s.setNote(1, 0, n, true);
    s.setNote(1, 6, 58, true); s.setNote(1, 8, 55, true); s.setNote(1, 11, 62, true);
    s.length[1] = 12;
    s.muted[3] = 1;
    proc.playStep[0] = 4; proc.playStep[1] = 4;
    auto shot = [&](const juce::String& name) {
        SloopEditor ed(proc);
        auto img = ed.createComponentSnapshot(ed.getLocalBounds(), true, 1.0f);
        juce::File f(prefix + name + ".png");
        f.deleteFile();
        juce::FileOutputStream os(f);
        juce::PNGImageFormat().writeImageToStream(img, os);
        std::printf("%s\n", f.getFullPathName().toRawUTF8());
    };
    s.selected = 0; shot("_drums");
    s.selected = 1; shot("_synth1");
    {
        SloopEditor ed(proc);
        ed.setView(true);
        auto img = ed.createComponentSnapshot(ed.getLocalBounds(), true, 1.0f);
        juce::File f(prefix + "_sonidos.png");
        f.deleteFile();
        juce::FileOutputStream os(f);
        juce::PNGImageFormat().writeImageToStream(img, os);
        std::printf("%s\n", f.getFullPathName().toRawUTF8());
    }
    return 0;
}
