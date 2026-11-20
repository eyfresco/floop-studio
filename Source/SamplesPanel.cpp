// SPDX-License-Identifier: GPL-3.0-only
#include "SamplesPanel.h"

namespace sloop {

namespace {
const juce::Colour kBg = ui::bg, kPanel = ui::s1, kCell = ui::s2, kText = ui::fg, kDim = ui::dim, kAccent = ui::orange,
                   kInst = ui::blue, kGood = ui::green, kBad = ui::red;
juce::String u8(const char* s) { return juce::String::fromUTF8(s); }
const char* kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
juce::String noteName(int n) { return juce::String(kNoteNames[n % 12]) + juce::String(n / 12 - 1); }
const char* kPadKeys[16] = { "F3", "G3", "A3", "B3", "C4", "D4", "E4", "F4", "G4", "A4", "B4", "C5", "D5", "E5", "F5", "G5" };
}

// --- the job ------------------------------------------------------------------------------------

void SampleJob::run()
{
    const bool wasLive = live.isEnabled();
    if (wasLive) live.pause();   // one conversation with the FM-1 at a time

    Fm1Connection fm;
    fm1::Info info;
    juce::String err;
    if (!fm.open(err) || !readInfo(fm, info, err))
    {
        message = err;
    }
    else if (toSend)
    {
        if (uploadSlot(fm, slot, *toSend, [this](float p) { progress = p; }, err))
        {
            ok = true;
            message = u8("Enviado a USR") + juce::String(slot + 1) + " (" + juce::String(toSend->zones)
                    + (toSend->zones == 1 ? u8(" sonido, ") : u8(" sonidos, "))
                    + juce::String(toSend->data.size() * 2.0 / smp::kRate, 1) + " s).";
            if (drumKit)
            {
                if (useAsDrumKit(fm, slot, info.pE0, err)) message << u8(" DRUMS toca ahora KIT = USR") << (slot + 1) << ".";
                else { message << " " << err; }
            }
        }
        else message = err;
        readSlots(fm, slots, err);
    }
    else
    {
        ok = readSlots(fm, slots, err);
        message = ok ? juce::String() : err;
    }
    if (wasLive) live.resume();
}

// --- the panel ----------------------------------------------------------------------------------

SamplesPanel::SamplesPanel(Fm1Live& l) : live(l)
{
    instButton.setButtonText("instrumento");
    readButton.setButtonText("leer fm-1");
    clearButton.setButtonText("vaciar");
    kitButton.setButtonText(u8("kit de batería"));
    addButton.setButtonText(u8("agregar archivos…"));
    useKitToggle.setButtonText(u8("Usar en DRUMS al enviar"));
    useKitToggle.setToggleState(true, juce::dontSendNotification);
    useKitToggle.setColour(juce::ToggleButton::textColourId, kDim);

    for (auto* b : { &instButton, &kitButton }) { b->setClickingTogglesState(false); addAndMakeVisible(*b); }
    instButton.onClick = [this] { mode = Mode::Instrument; refresh(); };
    kitButton.onClick = [this] { mode = Mode::Kit; refresh(); };
    for (int k = 0; k < smp::kSlots; ++k)
    {
        slotButtons[(size_t) k].onClick = [this, k] { slot = k; refresh(); };
        addAndMakeVisible(slotButtons[(size_t) k]);
    }
    readButton.setTooltip(u8("Lee qué hay en USR1–USR4 del FM-1 (solo lectura)."));
    readButton.onClick = [this] { startJob(std::nullopt); };
    addButton.onClick = [this] { chooseFiles(-1); };
    clearButton.onClick = [this] {
        if (mode == Mode::Instrument) items.clear(); else for (auto& p : pads) p.reset();
        setStatus({}, true);
        refresh();
    };
    sendButton.setColour(juce::TextButton::buttonColourId, ui::orange.darker(0.35f));
    sendButton.setColour(juce::TextButton::textColourOffId, ui::fg);
    sendButton.onClick = [this] { askToSend(); };
    nameEditor.setInputRestrictions(8, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 -_.+#");
    nameEditor.setTextToShowWhenEmpty(u8("nombre"), kDim);
    nameEditor.setFont(ui::mono(12.0f));
    for (juce::Component* c : { (juce::Component*) &readButton, (juce::Component*) &addButton, (juce::Component*) &clearButton,
                                (juce::Component*) &sendButton, (juce::Component*) &nameEditor, (juce::Component*) &useKitToggle })
        addAndMakeVisible(c);
    refresh();
}

SamplesPanel::~SamplesPanel()
{
    stopTimer();
    job.reset();
}

void SamplesPanel::visibilityChanged()
{
    // The first time the panel shows: what the slots hold (read only), if the FM-1 is there.
    if (isShowing() && !askedOnce) { askedOnce = true; startJob(std::nullopt); }
}

juce::String SamplesPanel::slotLabel(int k) const
{
    juce::String s = "usr" + juce::String(k + 1);
    if (!slotsKnown) return s;
    if (k >= (int) slots.size()) return s + u8(" · —");
    return s + u8(" · ") + (slots[(size_t) k].zones ? slots[(size_t) k].name.trim() : u8("vacío"));
}

void SamplesPanel::refresh()
{
    const bool kit = mode == Mode::Kit;
    instButton.setToggleState(!kit, juce::dontSendNotification);
    kitButton.setToggleState(kit, juce::dontSendNotification);
    instButton.setColour(juce::TextButton::buttonOnColourId, kInst.darker(0.6f));
    kitButton.setColour(juce::TextButton::buttonOnColourId, kAccent.darker(0.5f));
    for (int k = 0; k < smp::kSlots; ++k)
    {
        auto& b = slotButtons[(size_t) k];
        b.setButtonText(slotLabel(k));
        b.setToggleState(k == slot, juce::dontSendNotification);
        b.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3a4050));
        b.setEnabled(k < (slotsKnown && !slots.empty() ? (int) slots.size() : smp::kSlots));
    }
    sendButton.setButtonText(u8("enviar a usr") + juce::String(slot + 1));
    useKitToggle.setVisible(kit);
    const bool busy = job != nullptr;
    for (juce::Component* c : { (juce::Component*) &sendButton, (juce::Component*) &readButton, (juce::Component*) &addButton,
                                (juce::Component*) &clearButton })
        c->setEnabled(!busy);
    if (nameEditor.isEmpty())
    {
        if (!kit && !items.empty()) nameEditor.setText(juce::File(items.front().file).getFileNameWithoutExtension().toUpperCase().substring(0, 8), false);
    }
    repaint();
}

void SamplesPanel::resized()
{
    auto r = getLocalBounds();
    topArea = r.removeFromTop(50);
    bottomArea = r.removeFromBottom(34);
    bodyArea = r.reduced(0, 6);

    auto row1 = topArea.removeFromTop(22);
    instButton.setBounds(row1.removeFromLeft(100));
    row1.removeFromLeft(2);
    kitButton.setBounds(row1.removeFromLeft(120));
    row1.removeFromLeft(16);
    readButton.setBounds(row1.removeFromRight(84));
    row1.removeFromRight(6);
    for (int k = smp::kSlots - 1; k >= 0; --k)
    {
        slotButtons[(size_t) k].setBounds(row1.removeFromRight(118));
        row1.removeFromRight(2);
    }
    topArea.removeFromTop(6);
    auto row2 = topArea.removeFromTop(22);
    nameEditor.setBounds(row2.removeFromLeft(110));
    row2.removeFromLeft(6);
    addButton.setBounds(row2.removeFromLeft(150));
    row2.removeFromLeft(6);
    clearButton.setBounds(row2.removeFromLeft(64));
    meterArea = row2.removeFromRight(300);

    auto b = bottomArea.reduced(0, 6);
    sendButton.setBounds(b.removeFromLeft(120));
    b.removeFromLeft(12);
    useKitToggle.setBounds(b.removeFromLeft(190));

    const int cols = 4, gap = 6;
    const int pw = (bodyArea.getWidth() - gap * (cols - 1)) / cols, ph = (bodyArea.getHeight() - gap * 3) / 4;
    for (int i = 0; i < 16; ++i)
        padRects[(size_t) i] = { bodyArea.getX() + (i % cols) * (pw + gap), bodyArea.getY() + (i / cols) * (ph + gap), pw, ph };
}

juce::Rectangle<int> SamplesPanel::rowRect(int i) const
{
    const int top = bodyArea.getY() + 22, h = juce::jmin(24, (bodyArea.getHeight() - 22) / 16);
    return { bodyArea.getX(), top + i * h, bodyArea.getWidth(), h };
}

std::vector<std::pair<int, int>> SamplesPanel::itemKeys() const
{
    std::vector<int> order((size_t) items.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = (int) i;
    std::stable_sort(order.begin(), order.end(), [this](int a, int b) { return items[(size_t) a].root < items[(size_t) b].root; });
    std::vector<int> roots;
    for (int i : order) roots.push_back(items[(size_t) i].root);
    const auto split = smp::keySplit(roots);
    std::vector<std::pair<int, int>> out(items.size());
    for (size_t j = 0; j < order.size(); ++j) out[(size_t) order[j]] = split[j];
    return out;
}

int SamplesPanel::usedBytes() const
{
    if (mode == Mode::Kit) return smp::kitBytes(pads);
    int b = 0;
    for (const auto& it : items) b += ((int) it.samples.size() + 1) >> 1;
    return b;
}

int SamplesPanel::padAt(juce::Point<int> p) const
{
    for (int i = 0; i < 16; ++i) if (padRects[(size_t) i].contains(p)) return i;
    return -1;
}

void SamplesPanel::paint(juce::Graphics& g)
{
    g.fillAll(kBg);

    // meter: seconds used of what a slot holds
    {
        const double used = usedBytes() * 2.0 / smp::kRate;
        const bool over = used > smp::kMaxSeconds;
        auto m = meterArea.reduced(0, 9);
        auto bar = m.removeFromLeft(170);
        g.setColour(kCell);
        g.fillRoundedRectangle(bar.toFloat(), 3.0f);
        g.setColour(over ? kBad : (mode == Mode::Kit ? kAccent : kInst));
        g.fillRoundedRectangle(bar.withWidth((int) (bar.getWidth() * juce::jmin(1.0, used / smp::kMaxSeconds))).toFloat(), 3.0f);
        g.setColour(over ? kBad : kDim);
        g.setFont(ui::mono(12.0f));
        g.drawText(juce::String(used, 1) + " s de " + juce::String(smp::kMaxSeconds, 1) + " s"
                       + (over && mode == Mode::Kit ? u8(" · se recortará") : juce::String()),
                   meterArea.withTrimmedLeft(180), juce::Justification::centredLeft);
    }

    if (mode == Mode::Instrument)
    {
        g.setColour(kPanel);
        g.fillRoundedRectangle(bodyArea.toFloat(), 2.0f);
        auto head = bodyArea.withHeight(22).reduced(10, 0);
        g.setColour(kDim);
        g.setFont(ui::mono(11.0f, true));
        const int w = bodyArea.getWidth();
        g.drawText("archivo", head.withWidth(w * 40 / 100), juce::Justification::centredLeft);
        g.drawText("nota", head.withX(bodyArea.getX() + w * 45 / 100).withWidth(110), juce::Justification::centredLeft);
        g.drawText("teclas", head.withX(bodyArea.getX() + w * 62 / 100).withWidth(120), juce::Justification::centredLeft);
        g.drawText("largo", head.withX(bodyArea.getX() + w * 78 / 100).withWidth(80), juce::Justification::centredLeft);
        if (items.empty())
        {
            g.setColour(dragging ? kInst : kDim);
            g.setFont(ui::mono(14.0f));
            g.drawFittedText(u8("Suelta aquí tus WAV / AIFF / MP3 (varios a la vez), o agregar archivos…\n"
                                "Cada archivo suena en su nota (pon la nota en el nombre: PIANO_C4.wav, o ajústala aquí);\n"
                                "las teclas de en medio tocan el más cercano. Hasta 16 archivos, unos 7 s en total.\n"
                                "En el FM-1: una pista de synth con el motor SAMPLE y SET = USR1–USR4."),
                               bodyArea.reduced(30), juce::Justification::centred, 6);
        }
        const auto keys = itemKeys();
        for (int i = 0; i < (int) items.size(); ++i)
        {
            const auto rr = rowRect(i);
            const auto& it = items[(size_t) i];
            g.setColour(i % 2 ? kPanel : kCell.withAlpha(0.5f));
            g.fillRect(rr);
            g.setColour(kInst.withRotatedHue((float) i / 16.0f));
            g.fillRect(rr.withWidth(3));
            g.setColour(kText);
            g.setFont(juce::FontOptions(juce::jmin(13.0f, rr.getHeight() * 0.65f)));
            g.drawText(juce::File(it.file).getFileName(), rr.withTrimmedLeft(10).withWidth(w * 40 / 100 - 10), juce::Justification::centredLeft, true);
            const int nx = bodyArea.getX() + w * 45 / 100;
            g.setColour(kCell.brighter(0.2f));
            g.fillRect(nx, rr.getY() + 2, 20, rr.getHeight() - 4);
            g.fillRect(nx + 70, rr.getY() + 2, 20, rr.getHeight() - 4);
            g.setColour(kText);
            g.drawText("-", nx, rr.getY(), 20, rr.getHeight(), juce::Justification::centred);
            g.drawText("+", nx + 70, rr.getY(), 20, rr.getHeight(), juce::Justification::centred);
            g.drawText(noteName(it.root), nx + 22, rr.getY(), 46, rr.getHeight(), juce::Justification::centred);
            g.setColour(kDim);
            g.drawText(noteName(keys[(size_t) i].first) + u8("–") + noteName(keys[(size_t) i].second),
                       bodyArea.getX() + w * 62 / 100, rr.getY(), 140, rr.getHeight(), juce::Justification::centredLeft);
            g.drawText(juce::String(it.samples.size() / (double) smp::kRate, 2) + " s", bodyArea.getX() + w * 78 / 100, rr.getY(), 80,
                       rr.getHeight(), juce::Justification::centredLeft);
            g.setColour(kDim);
            g.drawText(u8("✕"), rr.withLeft(rr.getRight() - 30), juce::Justification::centred);
        }
    }
    else
    {
        for (int i = 0; i < 16; ++i)
        {
            const auto pr = padRects[(size_t) i];
            const auto& p = pads[(size_t) i];
            g.setColour(kPanel);
            g.fillRoundedRectangle(pr.toFloat(), 2.0f);
            g.setColour(i == dragPad ? kAccent : juce::Colour(0xff2f343d));
            g.drawRoundedRectangle(pr.toFloat().reduced(0.5f), 2.0f, i == dragPad ? 2.0f : 1.0f);
            auto t = pr.reduced(10, 6);
            auto head = t.removeFromTop(18);
            g.setFont(ui::mono(12.0f, true));
            g.setColour(kAccent);
            g.drawText(juce::String(kLaneNames[(size_t) i]).toLowerCase(), head, juce::Justification::centredLeft);
            g.setColour(kDim);
            g.drawText(kPadKeys[i], head.withTrimmedRight(p ? 22 : 0), juce::Justification::centredRight);
            if (p)
            {
                g.drawText(u8("✕"), head.withLeft(head.getRight() - 18), juce::Justification::centred);
                // the waveform, as it will be sent (its length after the fit)
                auto wave = t.removeFromTop(t.getHeight() - 16).reduced(0, 2);
                g.setColour(kCell);
                g.fillRect(wave);
                const int n = smp::padLength(*p);
                g.setColour(kAccent.withAlpha(0.8f));
                const int W = juce::jmax(1, wave.getWidth());
                for (int x = 0; x < W; ++x)
                {
                    const int a = (int) ((juce::int64) x * n / W), b = juce::jmax(a + 1, (int) ((juce::int64) (x + 1) * n / W));
                    int pk = 0;
                    for (int j = a; j < b && j < (int) p->samples.size(); ++j) pk = juce::jmax(pk, std::abs((int) p->samples[(size_t) j]));
                    const float h = (float) pk / 32768.0f * wave.getHeight() * 0.5f;
                    g.drawVerticalLine(wave.getX() + x, wave.getCentreY() - h, wave.getCentreY() + h);
                }
                g.setColour(kText);
                g.setFont(ui::mono(11.0f));
                g.drawText(juce::File(p->file).getFileName() + u8(" · ") + juce::String(n / (double) smp::kRate, 2) + " s", t,
                           juce::Justification::centredLeft, true);
            }
            else
            {
                g.setColour(kDim.withAlpha(0.7f));
                g.setFont(ui::mono(11.0f));
                g.drawFittedText(u8("Suelta un archivo\no haz clic"), t, juce::Justification::centred, 2);
            }
        }
    }

    // bottom: progress or status
    auto info = bottomArea.withTrimmedLeft(mode == Mode::Kit ? 380 : 170).reduced(0, 8);
    if (job && jobSends)
    {
        auto bar = info.removeFromLeft(260).withSizeKeepingCentre(260, 8);
        g.setColour(kCell);
        g.fillRoundedRectangle(bar.toFloat(), 3.0f);
        g.setColour(kAccent);
        g.fillRoundedRectangle(bar.withWidth((int) (bar.getWidth() * job->progress.load())).toFloat(), 3.0f);
        g.setColour(kDim);
        g.setFont(ui::mono(12.0f));
        g.drawText(u8("  Enviando al FM-1…"), info, juce::Justification::centredLeft);
    }
    else if (status.isNotEmpty())
    {
        g.setColour(statusOk ? kGood : kBad);
        g.setFont(ui::mono(12.0f));
        g.drawFittedText(status, info, juce::Justification::centredLeft, 2);
    }
}

void SamplesPanel::mouseDown(const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    if (mode == Mode::Kit)
    {
        const int i = padAt(p);
        if (i < 0) return;
        const auto pr = padRects[(size_t) i].reduced(10, 6);
        if (pads[(size_t) i] && p.y < pr.getY() + 18 && p.x > pr.getRight() - 18) { pads[(size_t) i].reset(); refresh(); return; }
        chooseFiles(i);
        return;
    }
    const int w = bodyArea.getWidth(), nx = bodyArea.getX() + w * 45 / 100;
    for (int i = 0; i < (int) items.size(); ++i)
    {
        const auto rr = rowRect(i);
        if (!rr.contains(p)) continue;
        if (p.x > rr.getRight() - 30) { items.erase(items.begin() + i); refresh(); return; }
        const int step = e.mods.isShiftDown() ? 12 : 1;   // Shift: an octave
        if (p.x >= nx && p.x < nx + 20) items[(size_t) i].root = juce::jmax(0, items[(size_t) i].root - step);
        else if (p.x >= nx + 70 && p.x < nx + 90) items[(size_t) i].root = juce::jmin(127, items[(size_t) i].root + step);
        repaint();
        return;
    }
    if (items.empty() && bodyArea.contains(p)) chooseFiles(-1);
}

void SamplesPanel::chooseFiles(int forPad)
{
    chooser = std::make_unique<juce::FileChooser>(forPad >= 0 ? u8("Sonido para ") + kLaneNames[(size_t) forPad]
                                                              : u8("Elige tus sonidos"),
                                                  juce::File(), "*.wav;*.aif;*.aiff;*.mp3;*.flac;*.ogg;*.m4a;*.caf");
    const int flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                    | (forPad < 0 ? juce::FileBrowserComponent::canSelectMultipleItems : 0);
    juce::Component::SafePointer<SamplesPanel> safe(this);
    chooser->launchAsync(flags, [safe, forPad](const juce::FileChooser& fc) {
        if (safe && !fc.getResults().isEmpty()) safe->addFiles(fc.getResults(), forPad);
    });
}

void SamplesPanel::addFiles(const juce::Array<juce::File>& files, int pad)
{
    juce::StringArray problems;
    int added = 0;
    for (const auto& f : files)
    {
        if (!isSoundFile(f.getFullPathName())) continue;
        std::vector<int16_t> s;
        juce::String err;
        if (!loadSound(f, s, err)) { problems.add(err); continue; }
        if (mode == Mode::Instrument)
        {
            if ((int) items.size() >= smp::kZones) { problems.add(u8("Ya hay 16 archivos: es el máximo de un espacio.")); break; }
            items.push_back({ f.getFullPathName(), std::move(s), smp::noteFromName(f.getFileNameWithoutExtension().toStdString()) });
        }
        else
        {
            const int lane = pad >= 0 && files.size() == 1 ? pad : smp::kitPlace(pads, f.getFileName().toStdString());
            if (lane < 0) { problems.add(u8("Los 16 pads están ocupados.")); break; }
            smp::Pad p;
            p.samples = std::move(s);
            p.file = f.getFullPathName().toStdString();
            pads[(size_t) lane] = std::move(p);
        }
        ++added;
    }
    if (!problems.isEmpty()) setStatus(problems.joinIntoString(" "), false);
    else if (added) setStatus({}, true);
    refresh();
}

bool SamplesPanel::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& f : files) if (isSoundFile(f)) return true;
    return false;
}

void SamplesPanel::fileDragMove(const juce::StringArray&, int x, int y)
{
    const int i = mode == Mode::Kit ? padAt({ x, y }) : -1;
    if (i != dragPad) { dragPad = i; repaint(); }
}

void SamplesPanel::filesDropped(const juce::StringArray& paths, int x, int y)
{
    juce::Array<juce::File> files;
    for (const auto& p : paths) files.add(juce::File(p));
    const int pad = mode == Mode::Kit ? padAt({ x, y }) : -1;
    dragPad = -1;
    dragging = false;
    addFiles(files, pad);
}

void SamplesPanel::askToSend()
{
    if (job) return;
    std::vector<smp::Zone> zones;
    juce::String note;
    if (mode == Mode::Instrument)
        for (const auto& it : items) zones.push_back({ it.samples, it.root, -1, -1 });
    else
    {
        if (smp::kitBytes(pads) > smp::kMaxData && smp::kitFit(pads))
            note = u8("Los sonidos más largos se recortaron para que el kit quepa. ");
        zones = smp::kitZones(pads);
    }
    if (zones.empty()) { setStatus(u8("Agrega al menos un sonido."), false); return; }

    juce::String name = nameEditor.getText().trim();
    if (name.isEmpty()) name = mode == Mode::Kit ? "KIT" : "SAMPLE";
    std::string err;
    auto built = smp::buildSlot(name.toStdString(), std::move(zones), err);
    if (!built) { setStatus(juce::String::fromUTF8(err.c_str()), false); return; }

    const juce::String target = "USR" + juce::String(slot + 1);
    juce::String what;
    if (slotsKnown && slot < (int) slots.size())
        what = slots[(size_t) slot].zones ? u8("Esto borra lo que hay en ") + target + " (" + slots[(size_t) slot].name.trim() + u8(") en el FM-1.")
                                          : target + u8(" está vacío en el FM-1.");
    else
        what = u8("No sé qué hay en ") + target + u8(": si tiene algo, se borra.");
    if (mode == Mode::Kit && useKitToggle.getToggleState())
        what << u8(" Después, DRUMS tocará KIT = ") << target << ".";
    if (note.isNotEmpty()) what = note + what;

    auto slotData = std::make_shared<smp::Slot>(std::move(*built));
    juce::Component::SafePointer<SamplesPanel> safe(this);
    juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::WarningIcon, u8("Enviar a ") + target, what,
                                       u8("Enviar"), u8("Cancelar"), this,
                                       juce::ModalCallbackFunction::create([safe, slotData](int ok) {
                                           if (ok && safe) safe->startJob(*slotData);
                                       }));
}

void SamplesPanel::startJob(std::optional<smp::Slot> data)
{
    if (job) return;
    jobSends = data.has_value();
    job = std::make_unique<SampleJob>(live, slot, std::move(data), mode == Mode::Kit && useKitToggle.getToggleState());
    if (!jobSends) setStatus(u8("Leyendo el FM-1…"), true);
    job->startThread();
    startTimerHz(15);
    refresh();
}

void SamplesPanel::timerCallback()
{
    if (!job) { stopTimer(); return; }
    if (job->isThreadRunning()) { repaint(); return; }
    if (!job->slots.empty()) { slots = job->slots; slotsKnown = true; }
    if (job->message.isNotEmpty() || jobSends) setStatus(job->message, job->ok);
    else setStatus({}, true);
    job.reset();
    stopTimer();
    refresh();
}

} // namespace sloop
