// SPDX-License-Identifier: GPL-3.0-only
#include "MidiMapPanel.h"
#include <map>

namespace sloop {

namespace {
juce::String u8(const char* s) { return juce::String::fromUTF8(s); }
const char* kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
juce::String noteName(int n) { return juce::String(kNoteNames[n % 12]) + juce::String(n / 12 - 1); }
bool isBlack(int n) { const int k = n % 12; return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; }
constexpr int kLowKey = 53, kHighKey = 79;   // F3 .. G5: the FM-1's 27 keys

struct ButtonDef { const char* name; const char* info; };
// REMOTE's order: CC 102 + index (GUIDE.md "REMOTE")
const ButtonDef kButtons[14] = {
    { "FX", "tocar: páginas FX · mantener: la capa de los 27 efectos punch-in (las teclas los disparan)" },
    { "SEL", "tocar: escala y acordes · mantener: su capa" },
    { "ENV", "tocar: la envolvente de la pista" },
    { "LFO", "tocar: el LFO de la pista" },
    { "EDIT", "tocar: el motor · mantener: su capa · + OCT−/OCT+: deshacer / rehacer" },
    { "GLO", "tocar: global · mantener: mute, solo, fill, tap tempo con las teclas" },
    { "HOME", "tocar: la pantalla TRACKS · mantener: el menú del FM-1" },
    { "SAVE", "tocar: SONG / páginas SAVE · mantener: la capa de canción (secciones A–D)" },
    { "ARP", "tocar: el arpegiador · mantener: repetir notas" },
    { "SEQ", "tocar: los pasos · mantener: editar pasos con las teclas" },
    { "PLAY", "arranca / detiene las 4 pistas" },
    { "REC", "tocando: grabar · parado: armar · mantener: borrar la pista" },
    { "OCT-", "octava abajo · en DRUMS, mantenido: golpes ghost" },
    { "OCT+", "octava arriba · en DRUMS, mantenido: golpes hard" },
};
}

MidiMapPanel::MidiMapPanel(Fm1Live& l, Session& s) : live(l), session(s)
{
    remoteButton.setClickingTogglesState(false);
    remoteButton.setColour(juce::TextButton::buttonOnColourId, ui::orange);
    remoteButton.setTooltip(u8("Activa REMOTE en el FM-1 (GLO > MIDI 3) en el canal 16: los CC 102–115 pulsan sus 14 botones. "
                               "Al apagarlo, el FM-1 vuelve a como lo tenías."));
    remoteButton.onClick = [this] { live.setRemote(live.remoteChannel() == 0); };
    addAndMakeVisible(remoteButton);
    build();
    timerCallback();   // (the button's text at once)
    startTimerHz(30);
}

void MidiMapPanel::build()
{
    controls.clear();
    for (const char* k : { "MASTER", "SELECT", "ALGORITHM", "PRESETS" })
    {
        static const std::map<juce::String, const char*> info {
            { "MASTER", "el volumen (y el nivel del audio USB)" },
            { "SELECT", "tempo · la página anterior / siguiente · DRUMS: cuadrícula / kit" },
            { "ALGORITHM", "la pista seleccionada: 1 · 2 · 3 (synths) · 4 (drums)" },
            { "PRESETS", "el sonido de la pista, o el kit de batería" } };
        controls.push_back({ Knob, k, u8(info.at(k)) + u8(" · no envía MIDI con SLOOP") });
    }
    for (int k = 1; k <= 4; ++k)
        controls.push_back({ Knob, "K" + juce::String(k),
                             u8("la perilla ") + juce::String(k) + u8(" de la página en pantalla · no envía MIDI; con fm-1 vivo, sonido la ve") });
    for (int b = 0; b < 14; ++b)
        controls.push_back({ Button, kButtons[b].name, u8(kButtons[b].info), Fm1Live::kRemoteCc0 + b });
    for (int n = kLowKey; n <= kHighKey; ++n)
        controls.push_back({ Key, noteName(n), {}, n, isBlack(n) });
}

void MidiMapPanel::resized()
{
    auto r = getLocalBounds();
    barArea = r.removeFromTop(22);
    r.removeFromTop(6);
    infoArea = r.removeFromBottom(30);
    r.removeFromBottom(4);
    bodyArea = r;
    remoteButton.setBounds(barArea.removeFromLeft(150));

    // The FM-1 as drawn in M-VAVE's manual (980 x 585): every control at its place, in fractions.
    const float aspect = 980.0f / 585.0f;
    auto area = bodyArea.toFloat();
    float w = area.getWidth(), h = w / aspect;
    if (h > area.getHeight()) { h = area.getHeight(); w = h * aspect; }
    device = juce::Rectangle<float>(w, h).withCentre(area.getCentre());
    auto at = [this](float cx, float cy, float rw, float rh) {
        return juce::Rectangle<float>(rw * device.getWidth(), rh * device.getHeight())
            .withCentre({ device.getX() + cx * device.getWidth(), device.getY() + cy * device.getHeight() });
    };

    // knobs: MASTER SELECT / PRESETS ALGORITHM (left), KNOB1..4 (right of the screen)
    const float kd = 0.046f, kdh = kd * aspect;
    controls[0].r = at(0.084f, 0.161f, kd, kdh);   // MASTER
    controls[1].r = at(0.191f, 0.161f, kd, kdh);   // SELECT
    controls[2].r = at(0.191f, 0.333f, kd, kdh);   // ALGORITHM
    controls[3].r = at(0.084f, 0.333f, kd, kdh);   // PRESETS
    const float kx[4] = { 0.562f, 0.674f, 0.786f, 0.897f };
    for (int i = 0; i < 4; ++i) controls[(size_t) (4 + i)].r = at(kx[i], 0.161f, kd, kdh);

    // the 12 buttons in their box (FX SEL ENV LFO EDIT GLO / HOME SAVE ARP SEQ PLAY REC), OCT- / OCT+ left
    const float bx[6] = { 0.574f, 0.638f, 0.700f, 0.761f, 0.823f, 0.885f };
    for (int b = 0; b < 12; ++b) controls[(size_t) (8 + b)].r = at(bx[b % 6], b < 6 ? 0.344f : 0.449f, 0.053f, 0.080f);
    controls[20].r = at(0.101f, 0.467f, 0.059f, 0.056f);   // OCT-
    controls[21].r = at(0.176f, 0.467f, 0.059f, 0.056f);   // OCT+

    // the keys: 11 above (3 · 2 · 3 · 2 · 1, the black keys), 16 below (the white keys), as pills
    const float blackX[11] = { 0.110f, 0.165f, 0.220f, 0.332f, 0.388f, 0.500f, 0.555f, 0.611f, 0.722f, 0.778f, 0.889f };
    int white = 0, black = 0;
    for (int n = kLowKey; n <= kHighKey; ++n)
    {
        auto& c = controls[(size_t) (22 + n - kLowKey)];
        if (isBlack(n)) c.r = at(blackX[black++], 0.672f, 0.047f, 0.150f);
        else c.r = at(0.0837f + 0.0557f * (float) white++, 0.845f, 0.047f, 0.157f);
    }
}

int MidiMapPanel::controlAt(juce::Point<float> p) const
{
    for (int pass = 0; pass < 2; ++pass)   // black keys first: they lie over the white ones
        for (int i = 0; i < (int) controls.size(); ++i)
        {
            const auto& c = controls[(size_t) i];
            if ((pass == 0) == (c.kind == Key && c.black) && c.r.contains(p)) return i;
        }
    return -1;
}

void MidiMapPanel::mouseMove(const juce::MouseEvent& e)
{
    const int i = controlAt(e.position);
    if (i != hover) { hover = i; repaint(); }
}

void MidiMapPanel::mouseDown(const juce::MouseEvent& e)
{
    pressed = controlAt(e.position);
    if (pressed < 0) return;
    const auto& c = controls[(size_t) pressed];
    const uint8_t ch = (uint8_t) (Fm1Live::kRemoteChannel - 1);
    if (c.kind == Button && live.remoteChannel())
        live.sendMidi({ (uint8_t) (0xB0 | ch), (uint8_t) c.midi, 127 });   // held while the mouse is down
    else if (c.kind == Key && live.isLive())
        live.sendMidi({ (uint8_t) (0x90 | ch), (uint8_t) c.midi, 100 });   // OTHER = SEL: the selected track plays it
    repaint();
}

void MidiMapPanel::mouseUp(const juce::MouseEvent&)
{
    if (pressed >= 0)
    {
        const auto& c = controls[(size_t) pressed];
        const uint8_t ch = (uint8_t) (Fm1Live::kRemoteChannel - 1);
        if (c.kind == Button && live.remoteChannel()) live.sendMidi({ (uint8_t) (0xB0 | ch), (uint8_t) c.midi, 0 });
        else if (c.kind == Key && live.isLive()) live.sendMidi({ (uint8_t) (0x80 | ch), (uint8_t) c.midi, 0 });
    }
    pressed = -1;
    repaint();
}

void MidiMapPanel::timerCallback()
{
    const int rc = live.remoteChannel();
    const juce::String txt = rc ? u8("remote: canal ") + juce::String(rc) : u8("remote: apagado");
    if (remoteButton.getButtonText() != txt) remoteButton.setButtonText(txt);
    remoteButton.setToggleState(rc != 0, juce::dontSendNotification);
    remoteButton.setEnabled(live.isLive());
    if (const auto n = live.noteRevision(); n != seenNotes) { seenNotes = n; repaint(); }
}

void MidiMapPanel::paint(juce::Graphics& g)
{
    g.fillAll(ui::bg);
    const int sel = session.selected.load();
    const bool drums = sel == kDrums, isLive = live.isLive(), remote = live.remoteChannel() != 0;

    g.setFont(ui::mono(11.0f));
    g.setColour(isLive ? ui::dim : ui::yellow);
    juce::String bar = isLive ? u8("botones: CC 102–115 con remote · teclas: 53–79 en octava 0 · perillas: sin MIDI")
                              : u8("activa fm-1 vivo para tocar y pulsar el FM-1 desde aquí; el mapa sirve igual de referencia");
    if (isLive && live.lastNote() >= 0)
    {
        const int ln = live.lastNote();
        bar = u8("última nota del FM-1: ") + noteName(ln & 127) + " · " + juce::String(ln & 127) + u8(" · canal ")
            + juce::String((ln >> 8) & 31) + (ln & (1 << 16) ? juce::String() : u8(" (suelta)"));
    }
    g.setColour(isLive ? (live.lastNote() >= 0 ? ui::fg2 : ui::dim) : ui::yellow);
    g.drawText(bar, barArea.withTrimmedLeft(166), juce::Justification::centredLeft, true);

    auto at = [this](float x0, float y0, float x1, float y1) {
        return juce::Rectangle<float>::leftTopRightBottom(device.getX() + x0 * device.getWidth(), device.getY() + y0 * device.getHeight(),
                                                          device.getX() + x1 * device.getWidth(), device.getY() + y1 * device.getHeight());
    };
    const float u = device.getWidth() / 980.0f;   // one pixel of the manual's drawing

    // the body, the screen, the boxes of the buttons, of OCT and of the keys
    g.setColour(ui::s1);
    g.fillRoundedRectangle(device, 40.0f * u);
    g.setColour(ui::s3);
    g.drawRoundedRectangle(device.reduced(1.0f), 40.0f * u, 1.5f);
    auto box = [&](juce::Rectangle<float> r, float radius) {
        g.setColour(ui::bg.withAlpha(0.45f));
        g.fillRoundedRectangle(r, radius * u);
        g.setColour(ui::line.brighter(0.3f));
        g.drawRoundedRectangle(r, radius * u, 1.0f);
    };
    box(at(0.533f, 0.278f, 0.931f, 0.510f), 10.0f);   // the 12 buttons
    box(at(0.061f, 0.426f, 0.216f, 0.509f), 6.0f);    // OCT- OCT+
    box(at(0.039f, 0.573f, 0.961f, 0.936f), 22.0f);   // the keys
    {
        const auto scr = at(0.260f, 0.077f, 0.495f, 0.470f);
        g.setColour(ui::bg);
        g.fillRoundedRectangle(scr, 16.0f * u);
        g.setColour(ui::s3);
        g.drawRoundedRectangle(scr, 16.0f * u, 1.0f);
        g.setColour(ui::track(sel));
        g.setFont(ui::mono(11.0f, true));
        g.drawText(juce::String(kTrackNames[sel]).toLowerCase(), scr.reduced(10, 8), juce::Justification::topLeft);
        g.setColour(ui::dim);
        g.setFont(ui::mono(9.5f));
        g.drawFittedText(u8("pantalla\ndel FM-1"), scr.reduced(10, 8).toNearestInt(), juce::Justification::bottomRight, 2);
    }

    for (int i = 0; i < (int) controls.size(); ++i)
    {
        const auto& c = controls[(size_t) i];
        const bool hot = i == hover, down = i == pressed;
        if (c.kind == Knob)
        {
            const bool kn = c.name.startsWith("K");
            const int k = kn ? c.name.getTrailingIntValue() : 0;
            const auto col = kn ? ui::track(k == 4 ? 0 : k) : ui::fg2;
            g.setColour(hot ? ui::s3 : ui::s2);
            g.fillEllipse(c.r);
            g.setColour(col);
            g.drawEllipse(c.r.reduced(1.0f), 2.0f);
            g.setColour(col.withAlpha(0.8f));
            g.fillEllipse(c.r.withSizeKeepingCentre(c.r.getWidth() * 0.18f, c.r.getWidth() * 0.18f)
                              .translated(0, -c.r.getHeight() * 0.28f));
            g.setColour(hot ? ui::fg : ui::dim);
            g.setFont(ui::mono(9.5f));
            const juce::String label = kn ? "knob" + juce::String(k) : c.name.toLowerCase();
            g.drawText(label, juce::Rectangle<float>(c.r.getX() - 30, c.r.getY() - 16, c.r.getWidth() + 60, 13),
                       juce::Justification::centred);
        }
        else if (c.kind == Button)
        {
            const bool play = c.name == "PLAY", rec = c.name == "REC";
            g.setColour(down ? ui::orange : hot ? ui::s3 : ui::s2);
            g.fillRoundedRectangle(c.r, 6.0f * u);
            g.setColour(play ? ui::green : rec ? ui::red : ui::s3);
            g.drawRoundedRectangle(c.r.reduced(0.5f), 6.0f * u, 1.0f);
            auto t = c.r.toNearestInt();
            g.setColour(down ? juce::Colours::black : ui::fg);
            g.setFont(ui::mono(10.5f, true));
            g.drawText(play ? juce::String("play") : c.name.toLowerCase(), t.removeFromTop(t.getHeight() / 2 + 2),
                       juce::Justification::centredBottom);
            g.setColour(down ? juce::Colours::black : (remote ? ui::orange : ui::dim));
            g.setFont(ui::mono(9.0f));
            g.drawText(juce::String(c.midi), t, juce::Justification::centredTop);
        }
        else
        {
            // The FM-1 sends the note it plays (its octave and transpose move it): lit is the key
            // that note falls on, folded into the keyboard's two octaves.
            bool held = false;
            for (int n = c.midi % 12; n < 128 && !held; n += 12)
                if (live.noteHeld(n))
                {
                    int f = n;
                    while (f < kLowKey) f += 12;
                    while (f > kHighKey) f -= 12;
                    held = f == c.midi || f + 12 == c.midi;   // (F3..G5 has some notes twice: both light)
                }
            const auto col = ui::track(sel);
            juce::Colour fill = held || down ? col : hot ? ui::s3 : ui::s2;
            const float rad = c.r.getWidth() * 0.5f;
            g.setColour(fill);
            g.fillRoundedRectangle(c.r, rad);
            g.setColour(c.black ? ui::s3 : ui::s3.brighter(0.3f));
            g.drawRoundedRectangle(c.r.reduced(0.5f), rad, 1.0f);
            // the groove in the middle of each key, as on the FM-1
            g.setColour((held || down ? juce::Colours::black : ui::bg).withAlpha(0.7f));
            g.fillRoundedRectangle(c.r.withSizeKeepingCentre(c.r.getWidth() * 0.16f, c.r.getHeight() * 0.42f), 2.0f);
            const juce::Colour ink = held || down ? juce::Colours::black : ui::fg2;
            g.setColour(ink);
            g.setFont(ui::mono(9.0f));
            auto lab = juce::Rectangle<float>(c.r.getX() - 6, c.r.getBottom() - 15 - (c.black ? 0 : 0), c.r.getWidth() + 12, 12);
            g.drawText(juce::String(c.midi), lab, juce::Justification::centred);
            g.setColour(ink.withAlpha(0.8f));
            g.drawText(c.name, lab.translated(0, -c.r.getHeight() + 18), juce::Justification::centred);
            if (drums && !c.black)   // the white key's drum sound (one per white key from F3)
            {
                int white = 0;
                for (int n = kLowKey; n < c.midi; ++n) white += isBlack(n) ? 0 : 1;
                g.setColour(held || down ? juce::Colours::black : ui::orange);
                g.setFont(ui::mono(8.5f));
                g.drawFittedText(juce::String(kLaneNames[(size_t) (white % kLanes)]).toLowerCase(),
                                 c.r.reduced(1, 0).withTrimmedTop(18).withTrimmedBottom(c.r.getHeight() * 0.5f + 4).toNearestInt(),
                                 juce::Justification::centred, 2, 0.6f);
            }
        }
    }

    // what the hovered control is
    auto info = infoArea;
    g.setFont(ui::mono(11.5f));
    if (hover >= 0)
    {
        const auto& c = controls[(size_t) hover];
        juce::String head = c.kind == Button ? c.name + u8(" · CC ") + juce::String(c.midi) + u8(" (canal ") + juce::String(Fm1Live::kRemoteChannel) + ")"
                          : c.kind == Key ? c.name + u8(" · nota ") + juce::String(c.midi)
                          : c.name;
        juce::String body = c.info;
        if (c.kind == Key)
        {
            body = drums ? u8("en DRUMS toca su sonido; ") : juce::String();
            body += u8("53–79 es la tecla en octava 0: el FM-1 envía la nota que toca (OCT−/OCT+ y transpose la mueven; "
                       "en DRUMS, la nota GM de su sonido) · con FX CH, esta nota dispara un efecto · clic: la toca en el FM-1");
        }
        if (c.kind == Button) body += remote ? u8(" · clic: lo pulsa en el FM-1 (mantener = mantener)") : u8(" · activa remote para pulsarlo desde aquí");
        g.setColour(ui::fg);
        g.drawText(head, info.removeFromTop(15), juce::Justification::centredLeft, true);
        g.setColour(ui::dim);
        g.drawText(body, info, juce::Justification::centredLeft, true);
    }
    else
    {
        g.setColour(ui::dim);
        g.drawText(u8("pasa el mouse sobre un control para ver su MIDI y qué hace"), info, juce::Justification::centredLeft);
    }
}

} // namespace sloop
