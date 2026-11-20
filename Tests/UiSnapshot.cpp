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
        ed.setView(SloopEditor::ViewSamples);
        auto img = ed.createComponentSnapshot(ed.getLocalBounds(), true, 1.0f);
        juce::File f(prefix + "_sonidos.png");
        f.deleteFile();
        juce::FileOutputStream os(f);
        juce::PNGImageFormat().writeImageToStream(img, os);
        std::printf("%s\n", f.getFullPathName().toRawUTF8());
    }
    {   // 64 steps: DRUMS 64 long, page 49-64 and all 64 at once
        s.selected = 0;
        s.length[0] = 64;
        for (int st = 16; st < 64; st += 4) s.setDrum(st, 0, true);
        for (int st = 50; st < 64; st += 2) s.setDrum(st, 4, true);
        s.setDrum(60, 2, true);
        proc.playStep[0] = 53;
        for (int mode = 0; mode < 2; ++mode)
        {
            SloopEditor ed(proc);
            ed.showSteps(3, mode == 1);
            auto img = ed.createComponentSnapshot(ed.getLocalBounds(), true, 1.0f);
            juce::File f(prefix + (mode ? "_64todo.png" : "_64pag4.png"));
            f.deleteFile();
            juce::FileOutputStream os(f);
            juce::PNGImageFormat().writeImageToStream(img, os);
            std::printf("%s\n", f.getFullPathName().toRawUTF8());
        }
        s.length[0] = 16;
        proc.playStep[0] = 4;
    }
    {   // CRÉDITOS
        SloopEditor ed(proc);
        ed.setView(SloopEditor::ViewCredits);
        auto img = ed.createComponentSnapshot(ed.getLocalBounds(), true, 1.0f);
        juce::File f(prefix + "_creditos.png");
        f.deleteFile();
        juce::FileOutputStream os(f);
        juce::PNGImageFormat().writeImageToStream(img, os);
        std::printf("%s\n", f.getFullPathName().toRawUTF8());
    }
    {   // MAPA MIDI, with DRUMS selected (the white keys show their sounds)
        s.selected = 0;
        SloopEditor ed(proc);
        ed.setView(SloopEditor::ViewMap);
        auto img = ed.createComponentSnapshot(ed.getLocalBounds(), true, 1.0f);
        juce::File f(prefix + "_mapa.png");
        f.deleteFile();
        juce::FileOutputStream os(f);
        juce::PNGImageFormat().writeImageToStream(img, os);
        std::printf("%s\n", f.getFullPathName().toRawUTF8());
    }
    {   // SONIDO with a made-up ANALOG sound (no FM-1 needed)
        sloop::Fm1Live::Sound fake;
        fake.valid = true; fake.track = 1; fake.engine = 0; fake.preset = 3;
        fake.engines = { "ANALOG", "FM", "FM6", "PHASE", "LOFI", "SAMPLE", "VOICE", "TRIO", "DRAWBAR", "GRAIN", "PHYS", "NOISE" };
        fake.presets = { "INIT", "SUPERSAW", "G-FUNK LD", "FAT BASS", "PAD" };
        fake.edit1 = "OSC"; fake.edit2 = "FILTER";
        int n = 0;
        for (const auto& pg : sloop::snd::synthPages())
            for (int id : pg.ids)
            {
                if (id == sloop::snd::kNone) continue;
                sloop::snd::Desc d; d.id = id; d.label = "P" + std::to_string(id); d.max = 127; d.def = 64;
                if (id == sloop::snd::P_ATK || id == sloop::snd::P_DEC || id == sloop::snd::P_REL) { d.fmt = sloop::snd::F_TIME; d.label = id == 1 ? "ATK" : id == 2 ? "DEC" : "REL"; }
                if (id == sloop::snd::P_SUS) { d.fmt = sloop::snd::F_PCT; d.label = "SUS"; }
                if (id == sloop::snd::P_LRATE) { d.fmt = sloop::snd::F_LFOHZ; d.label = "RATE"; }
                if (id == sloop::snd::P_TFLT) { d.fmt = sloop::snd::F_FILT; d.min = -64; d.max = 63; d.label = "FILT"; }
                if (id == sloop::snd::P_E0 + 4) { d.fmt = sloop::snd::F_CUTOFF; d.label = "CUT"; }
                if (id == sloop::snd::P_VOICE) { d.fmt = sloop::snd::F_ENUM; d.max = 3; d.names = { "POLY", "MONO", "LEGATO", "UNISON" }; d.label = "VCE"; }
                if (id == sloop::snd::P_PAN) { d.min = -64; d.max = 63; d.fmt = sloop::snd::F_BIPCT; d.label = "PAN"; }
                fake.desc[sloop::Fm1Live::soundKey(0, id)] = d;
                fake.values[sloop::Fm1Live::soundKey(0, id)] = (n++ * 37) % 128;
            }
        fake.values[sloop::Fm1Live::soundKey(0, sloop::snd::P_VOICE)] = 1;
        fake.values[sloop::Fm1Live::soundKey(0, sloop::snd::P_TFLT)] = -20;
        fake.values[sloop::Fm1Live::soundKey(0, sloop::snd::P_PAN)] = 10;
        s.selected = 1;
        SloopEditor ed(proc);
        ed.setView(SloopEditor::ViewSound);
        ed.soundForTest()->showForTest(fake);
        auto img = ed.createComponentSnapshot(ed.getLocalBounds(), true, 1.0f);
        juce::File f(prefix + "_sonido.png");
        f.deleteFile();
        juce::FileOutputStream os(f);
        juce::PNGImageFormat().writeImageToStream(img, os);
        std::printf("%s\n", f.getFullPathName().toRawUTF8());
    }
    return 0;
}
