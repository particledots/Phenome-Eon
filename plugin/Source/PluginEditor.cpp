#include "PluginEditor.h"

namespace
{
    const juce::Colour kBackground (0xff16181b);   // charcoal, as in the approved mockup
    const juce::Colour kAmber      (0xffe8a33d);
    const juce::Colour kText       (0xffe9e6df);
    const juce::Colour kMuted      (0xffa8a59d);

    juce::Font font (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
    }

    // Layout, in the fixed 900 x 600 space.
    constexpr int kLeft = 24, kLabelWidth = 130, kSliderWidth = 330, kTextBoxWidth = 80;
    constexpr int kRowHeight = 40, kFirstRowY = 78;
    constexpr int kButtonX = 616, kButtonWidth = 260, kButtonHeight = 34, kButtonGap = 8;
}

PhenomeEonSliderPanel::PhenomeEonSliderPanel (juce::AudioProcessorValueTreeState& s)
    : state (s)
{
    // The eight knobs, in panel order.
    addSlider (PheParams::inputLevel,   "Input Level");
    addSlider (PheParams::soundOnSound, "Sound On Sound");
    addSlider (PheParams::geneSize,     "Gene-Size");
    addSlider (PheParams::slide,        "Slide");
    addSlider (PheParams::variSpeed,    "Vari-Speed");
    addSlider (PheParams::morph,        "Morph");
    addSlider (PheParams::organize,     "Organize");
    addSlider (PheParams::outputLevel,  "Output Level");

    // The three menus.
    addChoice (PheParams::inputGain,  "Input Gain Step");
    addChoice (PheParams::pitchSnap,  "Pitch Snap");
    addChoice (PheParams::pitchScale, "Pitch Scale");

    // The buttons.
    addButton (PheParams::rec,              "REC");
    addButton (PheParams::recNewSplice,     "REC NEW SPLICE");
    addButton (PheParams::splice,           "SPLICE");
    addButton (PheParams::shift,            "SHIFT");
    addButton (PheParams::play,             "PLAY");
    addButton (PheParams::deleteMarker,     "DELETE SPLICE MARKER");
    addButton (PheParams::deleteAllMarkers, "DELETE ALL MARKERS");
    addButton (PheParams::eraseSplice,      "ERASE SPLICE");
    addButton (PheParams::clearReel,        "CLEAR REEL");
    addButton (PheParams::undo,             "UNDO");

    setSize (kWidth, kHeight);
}

void PhenomeEonSliderPanel::addSlider (const char* id, const char* text)
{
    auto row = std::make_unique<SliderRow>();
    row->label.setText (text, juce::dontSendNotification);
    row->label.setFont (font (15.0f));
    row->label.setColour (juce::Label::textColourId, kText);
    row->slider.setSliderStyle (juce::Slider::LinearHorizontal);
    row->slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, kTextBoxWidth, 24);
    row->slider.setNumDecimalPlacesToDisplay (3);
    row->slider.setColour (juce::Slider::thumbColourId, kAmber);
    row->slider.setColour (juce::Slider::trackColourId, kAmber.withAlpha (0.55f));
    addAndMakeVisible (row->label);
    addAndMakeVisible (row->slider);
    row->attachment = std::make_unique<SliderAttachment> (state, id, row->slider);
    controlIds.push_back (id);
    sliders.push_back (std::move (row));
}

void PhenomeEonSliderPanel::addChoice (const char* id, const char* text)
{
    auto row = std::make_unique<ChoiceRow>();
    row->label.setText (text, juce::dontSendNotification);
    row->label.setFont (font (15.0f));
    row->label.setColour (juce::Label::textColourId, kText);
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (id)))
        row->box.addItemList (p->choices, 1);
    addAndMakeVisible (row->label);
    addAndMakeVisible (row->box);
    row->attachment = std::make_unique<ComboBoxAttachment> (state, id, row->box);
    controlIds.push_back (id);
    choices.push_back (std::move (row));
}

void PhenomeEonSliderPanel::addButton (const char* id, const char* text)
{
    auto item = std::make_unique<ButtonItem>();
    item->button.setButtonText (text);
    item->button.setClickingTogglesState (true);
    item->button.setColour (juce::TextButton::buttonOnColourId, kAmber);
    item->button.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    addAndMakeVisible (item->button);
    item->attachment = std::make_unique<ButtonAttachment> (state, id, item->button);
    controlIds.push_back (id);
    buttons.push_back (std::move (item));
}

void PhenomeEonSliderPanel::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);

    g.setColour (kText);
    g.setFont (font (26.0f, true));
    g.drawText ("PhenomeEon", kLeft, 14, 400, 34, juce::Justification::centredLeft);

    g.setColour (kMuted);
    g.setFont (font (13.0f));
    g.drawText ("Plain slider window (milestone 2). Audio passes straight through; the controls do not change the sound yet.",
                kLeft, 46, kWidth - 2 * kLeft, 20, juce::Justification::centredLeft);

    g.setColour (kAmber);
    g.setFont (font (12.0f, true));
    g.drawText ("BUTTONS", kButtonX, kFirstRowY - 22, kButtonWidth, 18, juce::Justification::centredLeft);
    g.drawText ("KNOBS AND MENUS", kLeft, kFirstRowY - 22, 300, 18, juce::Justification::centredLeft);
}

void PhenomeEonSliderPanel::resized()
{
    int y = kFirstRowY;
    for (auto& row : sliders)
    {
        row->label.setBounds (kLeft, y, kLabelWidth, 30);
        row->slider.setBounds (kLeft + kLabelWidth, y, kSliderWidth + kTextBoxWidth, 30);
        y += kRowHeight;
    }

    y += 14;
    for (auto& row : choices)
    {
        row->label.setBounds (kLeft, y, kLabelWidth, 30);
        row->box.setBounds (kLeft + kLabelWidth, y, 260, 30);
        y += kRowHeight;
    }

    // Two groups of buttons: the five main ones, then the five that delete or undo.
    int by = kFirstRowY;
    int index = 0;
    for (auto& item : buttons)
    {
        if (index == 5)
            by += 18;
        item->button.setBounds (kButtonX, by, kButtonWidth, kButtonHeight);
        by += kButtonHeight + kButtonGap;
        ++index;
    }
}

//==============================================================================
PhenomeEonEditor::PhenomeEonEditor (PhenomeEonProcessor& p)
    : juce::AudioProcessorEditor (p),
      panel (p.getValueTreeState())
{
    lookAndFeel.setColourScheme (juce::LookAndFeel_V4::getDarkColourScheme());
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (panel);

    // Resizable like Ceo: the panel is laid out in a fixed space and scaled to the window,
    // keeping its shape. Smallest about 60%, largest 180%.
    setResizable (true, true);
    setResizeLimits ((int) (PhenomeEonSliderPanel::kWidth * 0.6), (int) (PhenomeEonSliderPanel::kHeight * 0.6),
                     (int) (PhenomeEonSliderPanel::kWidth * 1.8), (int) (PhenomeEonSliderPanel::kHeight * 1.8));
    if (auto* aspect = getConstrainer())
        aspect->setFixedAspectRatio ((double) PhenomeEonSliderPanel::kWidth / (double) PhenomeEonSliderPanel::kHeight);

    setSize (PhenomeEonSliderPanel::kWidth, PhenomeEonSliderPanel::kHeight);
}

PhenomeEonEditor::~PhenomeEonEditor()
{
    setLookAndFeel (nullptr);
}

void PhenomeEonEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);
}

void PhenomeEonEditor::resized()
{
    panel.setBounds (0, 0, PhenomeEonSliderPanel::kWidth, PhenomeEonSliderPanel::kHeight);
    panel.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) PhenomeEonSliderPanel::kWidth));
}
