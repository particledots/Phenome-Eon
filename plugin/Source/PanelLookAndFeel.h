// The look of the styled panel (milestone 3): colours, fonts, and how knobs, buttons and menus are drawn.
// It only decides appearance. Nothing here touches the sound or the control values.
//
// The colours and shapes follow the approved mockup (chat 2): charcoal panel, amber for values and selection,
// teal for the waveform. Fonts are Space Grotesk (labels) and IBM Plex Mono (readouts), both free open-licence
// fonts, embedded in the plugin so it looks the same on every Mac.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace PheStyle
{
    // Colours from the approved mockup
    inline const juce::Colour background { 0xff16181b };   // charcoal
    inline const juce::Colour card       { 0xff1b1e22 };   // knob cards and boxes
    inline const juce::Colour cardEdge   { 0xff2a2e33 };
    inline const juce::Colour well       { 0xff111316 };   // reel display background
    inline const juce::Colour control    { 0xff1f2328 };   // idle button / menu fill
    inline const juce::Colour controlHot { 0xff282c32 };   // button / menu fill under the mouse
    inline const juce::Colour controlEdge{ 0xff363b41 };
    inline const juce::Colour amber      { 0xffe8a33d };   // values and selection
    inline const juce::Colour teal       { 0xff4fb3a9 };   // waveform
    inline const juce::Colour text       { 0xffe9e6df };
    inline const juce::Colour muted      { 0xffa8a59d };
    inline const juce::Colour recRed     { 0xffd9534f };
    inline const juce::Colour tapeForward{ 0xff6fc26b };   // Vari-Speed arc and read-out while the tape plays forwards (version 1.0.1)
    inline const juce::Colour tapeBackward { 0xffd9534f }; // ... and backwards (the same red as REC)
    inline const juce::Colour tapeStopped  { 0xffa8a59d }; // ... and stopped (the muted grey)

    // A knob's arc is amber unless its slider carries this property (a colour stored as a 64-bit number).
    inline constexpr const char* arcColourProperty = "arcColour";

    // The two embedded font families. "sans" is Space Grotesk; "mono" is IBM Plex Mono.
    juce::Font sans (float height, bool bold = false, float extraTracking = 0.0f);
    juce::Font mono (float height, bool medium = false);
}

class PanelLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PanelLookAndFeel();

    // Knobs: a dark dial with an amber arc showing the value and a pointer (the arc takes the colour in the slider's
    // "arcColour" property when it has one; only Vari-Speed does).
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPosProportional,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    // Buttons: flat rounded buttons; amber when on (red for REC), greyed when not usable yet.
    // A button with the property "recDot" set gets a red dot in front of its text.
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool isMouseOver, bool isButtonDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool isMouseOver, bool isButtonDown) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    // Menus
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override;
};
