// SPDX-License-Identifier: GPL-3.0-only
// Floop Studio's look, after the SLOOP web editor: black, grey cells, a monospaced font, lowercase
// labels, flat square-ish buttons, and the editor's track colours (track 1 blue, 2 green, 3 yellow,
// drums orange: web/editor.html --t1..--t4).
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace sloop::ui {

// web/editor.html :root
const juce::Colour bg { 0xff000000 }, s1 { 0xff0b0b0e }, s2 { 0xff1a1a1e }, s3 { 0xff36363c }, line { 0xff232329 },
                   dim { 0xff76767e }, fg { 0xfff2f2f2 }, fg2 { 0xffc4c4cc }, red { 0xffff2c34 }, green { 0xff1ecc70 },
                   yellow { 0xffffc618 }, orange { 0xffff621a }, blue { 0xff287cff };

// The plugin's tracks: 0 DRUMS (the editor's track 4), 1..3 SYNTH 1..3 (tracks 1..3).
inline juce::Colour track(int t)
{
    static const juce::Colour c[4] = { orange, blue, green, yellow };
    return c[juce::jlimit(0, 3, t)];
}

inline juce::FontOptions mono(float size, bool bold = false)
{
#if JUCE_MAC
    const char* face = "Menlo";
#else
    const char* face = "Consolas";
#endif
    return juce::FontOptions(face, size, bold ? juce::Font::bold : juce::Font::plain);
}

class Look : public juce::LookAndFeel_V4
{
public:
    Look()
    {
        setColour(juce::ResizableWindow::backgroundColourId, bg);
        setColour(juce::TextButton::buttonColourId, s2);
        setColour(juce::TextButton::buttonOnColourId, orange);
        setColour(juce::TextButton::textColourOffId, fg2);
        setColour(juce::TextButton::textColourOnId, juce::Colours::black);
        setColour(juce::ComboBox::backgroundColourId, s2);
        setColour(juce::ComboBox::outlineColourId, line);
        setColour(juce::ComboBox::textColourId, fg);
        setColour(juce::ComboBox::arrowColourId, dim);
        setColour(juce::PopupMenu::backgroundColourId, s1);
        setColour(juce::PopupMenu::textColourId, fg2);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, s3);
        setColour(juce::PopupMenu::highlightedTextColourId, fg);
        setColour(juce::Label::textColourId, dim);
        setColour(juce::TextEditor::backgroundColourId, s2);
        setColour(juce::TextEditor::outlineColourId, line);
        setColour(juce::TextEditor::textColourId, fg);
        setColour(juce::ToggleButton::textColourId, fg2);
        setColour(juce::ToggleButton::tickColourId, orange);
        setColour(juce::ToggleButton::tickDisabledColourId, dim);
        setColour(juce::AlertWindow::backgroundColourId, s1);
        setColour(juce::AlertWindow::textColourId, fg);
        setColour(juce::AlertWindow::outlineColourId, s3);
        setColour(juce::TooltipWindow::backgroundColourId, s2);
        setColour(juce::TooltipWindow::textColourId, fg);
        setColour(juce::TooltipWindow::outlineColourId, s3);
    }

    juce::Font getTextButtonFont(juce::TextButton&, int h) override { return juce::Font(mono(juce::jmin(12.0f, h * 0.55f))); }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return juce::Font(mono(12.0f)); }
    juce::Font getPopupMenuFont() override { return juce::Font(mono(12.0f)); }
    juce::Font getLabelFont(juce::Label& l) override { return juce::Font(mono(l.getFont().getHeight())); }
    juce::Font getAlertWindowMessageFont() override { return juce::Font(mono(13.0f)); }
    juce::Font getAlertWindowTitleFont() override { return juce::Font(mono(14.0f, true)); }

    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour& base, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced(0.5f);
        auto c = b.getToggleState() ? b.findColour(juce::TextButton::buttonOnColourId) : base;
        if (!b.isEnabled()) c = c.withMultipliedAlpha(0.45f);
        else if (down) c = c.brighter(0.15f);
        else if (over) c = c.brighter(0.07f);
        g.setColour(c);
        g.fillRoundedRectangle(r, 2.0f);
        if (!b.getToggleState())
        {
            g.setColour(line);
            g.drawRoundedRectangle(r, 2.0f, 1.0f);
        }
    }

    void drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        const auto r = juce::Rectangle<float>(0, 0, (float) w, (float) h).reduced(0.5f);
        g.setColour(box.findColour(juce::ComboBox::backgroundColourId).withMultipliedAlpha(box.isEnabled() ? 1.0f : 0.5f));
        g.fillRoundedRectangle(r, 2.0f);
        g.setColour(line);
        g.drawRoundedRectangle(r, 2.0f, 1.0f);
        juce::Path arrow;
        const float x = (float) w - 12.0f, y = (float) h * 0.5f;
        arrow.addTriangle(x - 4.0f, y - 2.0f, x + 4.0f, y - 2.0f, x, y + 3.0f);
        g.setColour(dim);
        g.fillPath(arrow);
    }

    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds(6, 1, box.getWidth() - 24, box.getHeight() - 2);
        label.setFont(getComboBoxFont(box));
    }
};

} // namespace sloop::ui
