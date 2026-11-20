// SPDX-License-Identifier: GPL-3.0-only
#include "SoundPanel.h"

namespace sloop {

namespace {
const juce::Colour kBg = ui::bg, kCard = ui::s1, kCell = ui::s3, kText = ui::fg, kDim = ui::dim;
juce::String u8(const char* s) { return juce::String::fromUTF8(s); }
juce::Colour trackColour(int t) { return ui::track(t); }
}

SoundPanel::SoundPanel(Fm1Live& l) : live(l)
{
    engineLabel.setText("motor", juce::dontSendNotification);
    presetLabel.setText("preset", juce::dontSendNotification);
    for (auto* lb : { &engineLabel, &presetLabel })
    {
        lb->setFont(ui::mono(11.0f, true));
        lb->setColour(juce::Label::textColourId, kDim);
        addAndMakeVisible(*lb);
    }
    engineBox.onChange = [this] {
        const int e = engineBox.getSelectedId() - 1;
        if (e >= 0 && e != snd.engine && !snd.drums) live.chooseEngine(e);
    };
    presetBox.onChange = [this] {
        const int p = presetBox.getSelectedId() - 1;
        if (p >= 0 && p != snd.preset) live.choosePreset(p);
    };
    userLabel.setText("usuario", juce::dontSendNotification);
    userLabel.setFont(ui::mono(11.0f, true));
    userLabel.setColour(juce::Label::textColourId, kDim);
    addAndMakeVisible(userLabel);
    userLoad.setButtonText("cargar");
    userSave.setButtonText(u8("guardar…"));
    userLoad.setTooltip(u8("Carga en la pista seleccionada el preset de usuario elegido (como SAVE > USER > LOAD en el FM-1)."));
    userSave.setTooltip(u8("Guarda el sonido de la pista seleccionada en el preset de usuario elegido, en la memoria del FM-1."));
    userLoad.onClick = [this] { if (userBox.getSelectedId() > 0) live.loadUserPreset(userBox.getSelectedId() - 1); };
    userSave.onClick = [this] { askToStore(); };
    for (juce::Component* c : { (juce::Component*) &userBox, (juce::Component*) &userLoad, (juce::Component*) &userSave })
        addAndMakeVisible(c);
    prevPreset.onClick = [this] { if (snd.preset > 0) live.choosePreset(snd.preset - 1); };
    nextPreset.onClick = [this] { if (snd.preset + 1 < (int) snd.presets.size()) live.choosePreset(snd.preset + 1); };
    for (juce::Component* c : { (juce::Component*) &engineBox, (juce::Component*) &presetBox,
                                (juce::Component*) &prevPreset, (juce::Component*) &nextPreset })
        addAndMakeVisible(c);
    startTimerHz(20);
}

void SoundPanel::askToStore()
{
    const int slot = userBox.getSelectedId() - 1;
    if (slot < 0) return;
    const bool used = slot < (int) snd.user.size() && snd.user[(size_t) slot].used;
    juce::String suggestion = used ? juce::String(snd.user[(size_t) slot].name)
                                   : (snd.drums || snd.engine >= (int) snd.engines.size() ? juce::String("SONIDO")
                                                                                          : juce::String(snd.engines[(size_t) snd.engine]));
    auto* w = new juce::AlertWindow(u8("Guardar en U") + juce::String(slot + 1).paddedLeft('0', 2),
                                    used ? u8("Reemplaza «") + juce::String(snd.user[(size_t) slot].name) + u8("» en la memoria del FM-1.")
                                         : u8("Guarda el sonido de esta pista en la memoria del FM-1."),
                                    juce::MessageBoxIconType::QuestionIcon, this);
    w->addTextEditor("name", suggestion.substring(0, 12), u8("Nombre (hasta 12 letras)"));
    w->addButton("Guardar", 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Cancelar", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    juce::Component::SafePointer<SoundPanel> safe(this);
    w->enterModalState(true, juce::ModalCallbackFunction::create([safe, w, slot](int ok) {
        if (!ok || !safe) return;
        juce::String name = w->getTextEditorContents("name").toUpperCase().retainCharacters(
            " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[]^_").substring(0, 12).trim();
        safe->live.storeUserPreset(slot, name.toStdString());
    }), true);
}

void SoundPanel::timerCallback()
{
    if (testMode) return;
    if (const auto n = live.takeSoundNotice(); n.isNotEmpty())
    {
        notice = n;
        noticeUntil = juce::Time::getMillisecondCounter() + 4000;
        repaint();
    }
    else if (noticeUntil && juce::Time::getMillisecondCounter() > noticeUntil) { noticeUntil = 0; notice.clear(); repaint(); }
    const bool on = live.isLive();
    const auto rev = live.soundRevision();
    if (rev != seenRev || (!on && snd.valid))
    {
        seenRev = rev;
        snd = on ? live.sound() : Fm1Live::Sound {};
        refresh();
    }
}

void SoundPanel::refresh()
{
    const bool ok = snd.valid;
    engineBox.clear(juce::dontSendNotification);
    if (snd.drums) engineBox.addItem("DRUMS (kit)", 1000);
    else for (int i = 0; i < (int) snd.engines.size(); ++i) engineBox.addItem(snd.engines[(size_t) i], i + 1);
    engineBox.setSelectedId(snd.drums ? 1000 : snd.engine + 1, juce::dontSendNotification);
    presetBox.clear(juce::dontSendNotification);
    for (int i = 0; i < (int) snd.presets.size(); ++i) presetBox.addItem(snd.presets[(size_t) i], i + 1);
    presetBox.setSelectedId(snd.preset + 1, juce::dontSendNotification);
    engineBox.setEnabled(ok && !snd.drums);
    const int userSel = userBox.getSelectedId();
    userBox.clear(juce::dontSendNotification);
    for (int i = 0; i < (int) snd.user.size(); ++i)
    {
        const auto& u = snd.user[(size_t) i];
        const juce::String eng = u.used && u.engine < (int) snd.engines.size() ? juce::String(" (") + snd.engines[(size_t) u.engine] + ")" : juce::String();
        userBox.addItem("U" + juce::String(i + 1).paddedLeft('0', 2) + "  " + (u.used ? juce::String(u.name) + eng : u8("— vacío")), i + 1);
    }
    userBox.setSelectedId(userSel > 0 ? userSel : 1, juce::dontSendNotification);
    userBox.setEnabled(ok && !snd.drums && !snd.user.empty());
    userLoad.setEnabled(ok && !snd.drums && !snd.user.empty());
    userSave.setEnabled(ok && !snd.drums && !snd.user.empty());
    const bool presets = ok && !snd.drums && !snd.presets.empty();
    for (juce::Component* c : { (juce::Component*) &presetBox, (juce::Component*) &prevPreset, (juce::Component*) &nextPreset })
        c->setEnabled(presets);
    layoutCards();
    repaint();
}

void SoundPanel::resized()
{
    auto r = getLocalBounds();
    topArea = r.removeFromTop(22);
    r.removeFromTop(8);
    gridArea = r;
    auto t = topArea;
    engineLabel.setBounds(t.removeFromLeft(44));
    engineBox.setBounds(t.removeFromLeft(120));
    t.removeFromLeft(10);
    presetLabel.setBounds(t.removeFromLeft(50));
    prevPreset.setBounds(t.removeFromLeft(22));
    t.removeFromLeft(2);
    presetBox.setBounds(t.removeFromLeft(160));
    t.removeFromLeft(2);
    nextPreset.setBounds(t.removeFromLeft(22));
    userSave.setBounds(t.removeFromRight(76));
    t.removeFromRight(4);
    userLoad.setBounds(t.removeFromRight(60));
    t.removeFromRight(4);
    userBox.setBounds(t.removeFromRight(150));
    userLabel.setBounds(t.removeFromRight(58));
    noticeArea = gridArea.removeFromBottom(18);
    layoutCards();
}

void SoundPanel::layoutCards()
{
    cards.clear();
    knobs.clear();
    if (!snd.valid || gridArea.isEmpty()) return;
    const auto& pg = pages();
    const int cols = snd.drums ? 3 : 6, rows = ((int) pg.size() + cols - 1) / cols, gap = 6;
    const int cw = (gridArea.getWidth() - gap * (cols - 1)) / cols;
    const int ch = juce::jmin(snd.drums ? 170 : 140, (gridArea.getHeight() - gap * (rows - 1)) / rows);
    for (int i = 0; i < (int) pg.size(); ++i)
    {
        const juce::Rectangle<int> card(gridArea.getX() + (i % cols) * (cw + gap), gridArea.getY() + (i / cols) * (ch + gap), cw, ch);
        cards.push_back({ card, &pg[(size_t) i] });
        auto body = card.reduced(6).withTrimmedTop(18);
        const int kw = body.getWidth() / 4;
        for (int k = 0; k < 4; ++k)
            if (pg[(size_t) i].ids[k] != snd::kNone)
                knobs.push_back({ pg[(size_t) i].scope, pg[(size_t) i].ids[k], body.withX(body.getX() + k * kw).withWidth(kw) });
    }
}

const snd::Desc* SoundPanel::descOf(const Knob& k) const
{
    const auto it = snd.desc.find(Fm1Live::soundKey(k.scope, k.id));
    return it == snd.desc.end() ? nullptr : &it->second;
}

int SoundPanel::valueOf(const Knob& k) const
{
    const auto it = snd.values.find(Fm1Live::soundKey(k.scope, k.id));
    return it == snd.values.end() ? 0 : it->second;
}

void SoundPanel::setValue(const Knob& k, int v)
{
    const auto* d = descOf(k);
    if (!d || d->fixed()) return;
    v = juce::jlimit(d->min, d->max, v);
    if (v == valueOf(k)) return;
    snd.values[Fm1Live::soundKey(k.scope, k.id)] = v;
    if (!testMode) live.setSoundParam(k.scope, k.id, v);
    repaint(k.bounds);
}

int SoundPanel::knobAt(juce::Point<int> p) const
{
    for (int i = 0; i < (int) knobs.size(); ++i) if (knobs[(size_t) i].bounds.contains(p)) return i;
    return -1;
}

void SoundPanel::mouseDown(const juce::MouseEvent& e)
{
    dragging = knobAt(e.getPosition());
    if (dragging < 0) return;
    dragStartValue = valueOf(knobs[(size_t) dragging]);
    dragStartY = e.getPosition().y;
}

void SoundPanel::mouseDrag(const juce::MouseEvent& e)
{
    if (dragging < 0) return;
    const auto& k = knobs[(size_t) dragging];
    const auto* d = descOf(k);
    if (!d) return;
    const int range = d->max - d->min;
    // Enums: one step every 12 px; ranges: the whole range over 160 px (Shift: 4x finer).
    const double px = d->fmt == snd::F_ENUM || range <= 16 ? 12.0 : 160.0 / juce::jmax(1, range);
    const double fine = e.mods.isShiftDown() ? 4.0 : 1.0;
    setValue(k, dragStartValue + (int) std::lround((dragStartY - e.getPosition().y) / (px * fine)));
}

void SoundPanel::mouseUp(const juce::MouseEvent&) { dragging = -1; }

void SoundPanel::mouseDoubleClick(const juce::MouseEvent& e)
{
    const int i = knobAt(e.getPosition());
    if (i < 0) return;
    if (const auto* d = descOf(knobs[(size_t) i])) setValue(knobs[(size_t) i], d->def);   // back to the default
}

void SoundPanel::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int i = knobAt(e.getPosition());
    if (i < 0 || w.deltaY == 0.0f) return;
    setValue(knobs[(size_t) i], valueOf(knobs[(size_t) i]) + (w.deltaY > 0 ? 1 : -1));
}

void SoundPanel::paint(juce::Graphics& g)
{
    g.fillAll(kBg);
    if (!snd.valid)
    {
        g.setColour(kDim);
        g.setFont(ui::mono(14.0f));
        g.drawFittedText(live.isEnabled()
                             ? u8("Leyendo el sonido del FM-1…")
                             : u8("Activa FM-1 EN VIVO (arriba) para ver y editar el sonido de la pista seleccionada en el FM-1:\n"
                                  "su motor, sus presets y sus páginas de perillas, como en el SLOOP Editor."),
                         gridArea.reduced(40), juce::Justification::centred, 3);
        return;
    }
    if (notice.isNotEmpty())
    {
        g.setColour(juce::Colour(0xfffacc15));
        g.setFont(ui::mono(12.0f));
        g.drawText(notice, noticeArea, juce::Justification::centredRight);
    }
    const auto col = trackColour(snd.track);
    for (const auto& [r, pg] : cards)
    {
        g.setColour(kCard);
        g.fillRoundedRectangle(r.toFloat(), 2.0f);
        g.setColour(ui::line);
        g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 2.0f, 1.0f);
        g.setColour(col.withAlpha(0.9f));
        g.setFont(ui::mono(11.0f, true));
        const juce::String title = (pg->engine ? juce::String(pg->ids[0] == snd::P_E0 ? snd.edit1 : snd.edit2) : juce::String(pg->title)).toLowerCase();
        g.drawText(title, r.reduced(8, 4).withHeight(14), juce::Justification::centredLeft);
    }
    for (const auto& k : knobs)
    {
        const auto* d = descOf(k);
        auto b = k.bounds;
        const int v = valueOf(k);
        const juce::String label = d ? juce::String(d->label).toLowerCase() : juce::String("?");
        // compact, as the FM-1's screen: "266ms", "3.37Hz", "LP31%"
        const juce::String value = d ? juce::String::fromUTF8(snd::formatValue(*d, v).c_str()).removeCharacters(" ") : juce::String();
        auto text = b.removeFromBottom(26);
        const float size = (float) juce::jmin(b.getWidth() - 8, b.getHeight() - 2);
        const auto c = b.toFloat().withSizeKeepingCentre(size, size);
        const float a0 = juce::MathConstants<float>::pi * 1.25f, a1 = juce::MathConstants<float>::pi * 2.75f;
        const bool fixed = !d || d->fixed();
        const float norm = d && d->max > d->min ? (float) (v - d->min) / (float) (d->max - d->min) : 0.0f;
        juce::Path track, arc;
        track.addCentredArc(c.getCentreX(), c.getCentreY(), size / 2 - 3, size / 2 - 3, 0, a0, a1, true);
        g.setColour(kCell);
        g.strokePath(track, juce::PathStrokeType(3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        if (!fixed)
        {
            const bool bipolar = d->min < 0 && d->max > 0;
            const float zero = bipolar ? (float) -d->min / (float) (d->max - d->min) : 0.0f;
            const float from = a0 + (a1 - a0) * juce::jmin(zero, norm), to = a0 + (a1 - a0) * juce::jmax(zero, norm);
            arc.addCentredArc(c.getCentreX(), c.getCentreY(), size / 2 - 3, size / 2 - 3, 0, from, juce::jmax(to, from + 0.01f), true);
            g.setColour(col);
            g.strokePath(arc, juce::PathStrokeType(3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            const float ang = a0 + (a1 - a0) * norm;
            g.setColour(kText);
            g.drawLine(c.getCentreX() + std::sin(ang) * size * 0.12f, c.getCentreY() - std::cos(ang) * size * 0.12f,
                       c.getCentreX() + std::sin(ang) * (size / 2 - 7), c.getCentreY() - std::cos(ang) * (size / 2 - 7), 2.0f);
        }
        g.setColour(fixed ? kDim.withAlpha(0.5f) : kDim);
        g.setFont(ui::mono(10.0f, true));
        g.drawText(label, text.removeFromTop(12), juce::Justification::centred, true);
        g.setColour(fixed ? kDim.withAlpha(0.5f) : kText);
        g.setFont(ui::mono(9.5f));
        g.drawFittedText(value, text, juce::Justification::centred, 1, 0.8f);
    }
}

} // namespace sloop
