// SPDX-License-Identifier: GPL-3.0-only
// CRÉDITOS: who made what Floop Studio stands on, and the licences.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "SloopLook.h"

namespace sloop {

class CreditsPanel : public juce::Component
{
public:
    CreditsPanel()
    {
        auto link = [this](juce::HyperlinkButton& b, const char* text, const char* url) {
            b.setButtonText(juce::String::fromUTF8(text));
            b.setURL(juce::URL(url));
            b.setFont(juce::Font(ui::mono(11.0f)), false, juce::Justification::centredLeft);
            b.setColour(juce::HyperlinkButton::textColourId, ui::blue.brighter(0.3f));
            addAndMakeVisible(b);
        };
        link(igLink, "instagram: @heyfresco", "https://instagram.com/heyfresco");
        link(xLink, "x: @heyfresco", "https://x.com/heyfresco");
        link(sloopLink, "github.com/isod89/sloop-fm1", "https://github.com/isod89/sloop-fm1");
        link(feluccaLink, "github.com/hugelton/Felucca", "https://github.com/hugelton/Felucca");
        link(juceLink, "juce.com", "https://juce.com");
        link(gplLink, "gnu.org/licenses/gpl-3.0", "https://www.gnu.org/licenses/gpl-3.0.html");
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced(4);
        // links sit right of their paragraph's title line (see paint)
        igLink.setBounds(r.getX() + 340, r.getY() + kAuthorY, 190, 16);
        xLink.setBounds(r.getX() + 540, r.getY() + kAuthorY, 160, 16);
        sloopLink.setBounds(r.getX() + 340, r.getY() + kSloopY, 260, 16);
        feluccaLink.setBounds(r.getX() + 340, r.getY() + kFeluccaY, 260, 16);
        juceLink.setBounds(r.getX() + 340, r.getY() + kJuceY, 260, 16);
        gplLink.setBounds(r.getX() + 340, r.getY() + kLicenceY, 260, 16);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(ui::bg);
        auto r = getLocalBounds().reduced(4);
        auto u8 = [](const char* s) { return juce::String::fromUTF8(s); };

        auto title = [&](int y, const juce::String& t, juce::Colour c) {
            g.setColour(c);
            g.setFont(ui::mono(13.0f, true));
            g.drawText(t, r.getX(), r.getY() + y, 330, 16, juce::Justification::centredLeft);
        };
        auto body = [&](int y, const juce::String& t, int lines = 2) {
            g.setColour(ui::fg2);
            g.setFont(ui::mono(11.0f));
            g.drawFittedText(t, r.getX(), r.getY() + y, r.getWidth(), 15 * lines, juce::Justification::topLeft, lines, 1.0f);
        };

        // header
        g.setFont(ui::mono(22.0f, true));
        g.setColour(ui::orange);
        g.drawText("floop", r.getX(), r.getY(), 80, 30, juce::Justification::centredLeft);
        g.setColour(ui::fg);
        g.drawText("studio", r.getX() + 78, r.getY(), 110, 30, juce::Justification::centredLeft);
        g.setFont(ui::mono(13.0f));
        g.setColour(ui::fg2);
        g.drawText("by @HeyFresco", r.getX() + 190, r.getY() + 2, 200, 28, juce::Justification::centredLeft);
        g.setColour(ui::dim);
        g.setFont(ui::mono(11.0f));
        g.drawText(u8("para FL Studio · secuenciador, controlador y editor en vivo del M-VAVE FM-1 con SLOOP · v0.1"),
                   r.getX(), r.getY() + 30, r.getWidth(), 16, juce::Justification::centredLeft);

        title(kAuthorY, "@HeyFresco", ui::fg);
        body(kAuthorY + 18, u8("Creador de Floop Studio: la idea, la dirección, el diseño de la interfaz y todas las pruebas "
                               "con el FM-1 en FL Studio. Si algo de aquí te sirve para hacer música, es por él."));
        title(kSloopY, "SLOOP", ui::orange);
        body(kSloopY + 18, u8("Firmware groovebox para el FM-1, de isod89 y su comunidad (GPL-3.0). Floop Studio usa su protocolo "
                              "del editor (SysEx), su formato de samples, sus páginas de sonido y el diseño del SLOOP Editor."));
        title(kFeluccaY, "Felucca", ui::blue);
        body(kFeluccaY + 18, u8("De Leo Kuroshita (@kurogedelic) / Hügelton Instruments: los motores, el secuenciador y el "
                                "editor sobre los que está hecho SLOOP (GPL-3.0)."));
        title(kJuceY, "JUCE 8", ui::green);
        body(kJuceY + 18, u8("Raw Material Software: el framework de audio y plugins (VST3 / AU), bajo AGPLv3 o licencia comercial."), 1);
        title(kLicenceY, "Licencia", ui::yellow);
        body(kLicenceY + 18, u8("El código de Floop Studio es GPL-3.0, como SLOOP, y se distribuye sin garantía. M-VAVE y FM-1 son "
                                "marcas de sus dueños; FL Studio es de Image-Line. Floop Studio no está afiliado con ellos."));
        g.setColour(ui::dim);
        g.setFont(ui::mono(10.5f));
        g.drawText(u8("Gracias a todos los que hacen y prueban SLOOP: casi todo lo que hace este plugin existe gracias a su trabajo."),
                   r.getX(), r.getBottom() - 16, r.getWidth(), 16, juce::Justification::centredLeft);
    }

private:
    static constexpr int kAuthorY = 62, kSloopY = 122, kFeluccaY = 182, kJuceY = 236, kLicenceY = 282;
    juce::HyperlinkButton igLink, xLink, sloopLink, feluccaLink, juceLink, gplLink;
};

} // namespace sloop
