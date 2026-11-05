#include "StyledPanel.h"
#include <cmath>

using namespace PheStyle;

namespace
{
    float textWidth (const juce::Font& font, const juce::String& s)
    {
        return juce::GlyphArrangement::getStringWidth (font, s);
    }

    juce::String minutesSeconds (double seconds)
    {
        const int total = (int) std::floor (seconds);
        return juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
    }

    juce::String percentText (double value)
    {
        return juce::String (juce::roundToInt (value * 100.0)) + " %";
    }

    // ---- layout, in the fixed 1200 x 800 space (measured from the approved mockup) ----
    constexpr int kMargin = 27;
    const juce::Rectangle<int> kReelFileBox  { 349, 34, 551, 63 };
    const juce::Rectangle<int> kClockBox     { 924, 34, 249, 63 };
    const juce::Rectangle<int> kReelDisplay  { kMargin, 118, 1146, 217 };
    constexpr int kCardTop = 352, kCardWidth = 139, kCardHeight = 289, kCardStep = 144;
    constexpr int kButtonTop = 678, kButtonHeight = 63, kButtonGap = 9;
    constexpr int kLeftGroupX = 27, kLeftGroupW = 498, kRightGroupX = 549, kRightGroupW = 624;
}

//==============================================================================
void ReelDisplay::setFormat (double newRate, int newChannels)
{
    if (newRate != sampleRate || newChannels != channels)
    {
        sampleRate = newRate;
        channels = newChannels;
        repaint();
    }
}

void ReelDisplay::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float w = b.getWidth(), h = b.getHeight();
    constexpr float statusH = 34.0f, rulerH = 25.0f, numbersH = 22.0f;
    const float waveTop = statusH + rulerH;
    auto xFor = [w] (double seconds) { return (float) (seconds / kReelSeconds) * w; };

    // What is shown. In the plugin the reel is empty until milestone 4; the demo is for the preview tool only.
    const double reelLength = demo ? 100.0 : 0.0;
    const std::vector<double> markers = demo ? std::vector<double> { 22.7, 45.4, 61.9, 78.6 } : std::vector<double>{};
    const int currentSplice = demo ? 2 : -1;          // 0-based
    const double playhead = demo ? 52.1 : 0.0;

    g.setColour (well);
    g.fillRoundedRectangle (b, 8.0f);

    juce::Path clip;
    clip.addRoundedRectangle (b, 8.0f);
    g.saveState();
    g.reduceClipRegion (clip);

    // ---- status line ----
    {
        struct Item { juce::String label, value; juce::Colour valueColour; };
        const std::vector<Item> items {
            { "REEL",     minutesSeconds (reelLength) + " / " + minutesSeconds (kReelSeconds), text },
            { "SPLICE",   demo ? "3 / 5" : "-",    amber },
            { "GENE",     demo ? "4.5 s" : "-",    amber },
            { "PLAYHEAD", minutesSeconds (playhead) + "." + juce::String ((int) std::floor ((playhead - std::floor (playhead)) * 10.0)), text },
        };

        const auto labelFont = sans (14.5f, false, 0.10f);
        const auto valueFont = mono (16.5f);
        float x = 14.0f;
        for (auto& item : items)
        {
            g.setFont (labelFont);
            g.setColour (muted);
            const float lw = textWidth (labelFont, item.label);
            g.drawText (item.label, juce::Rectangle<float> (x, 0.0f, lw + 2.0f, statusH), juce::Justification::centredLeft, false);
            x += lw + 12.0f;

            g.setFont (valueFont);
            g.setColour (item.valueColour);
            const float vw = textWidth (valueFont, item.value);
            g.drawText (item.value, juce::Rectangle<float> (x, 0.0f, vw + 2.0f, statusH), juce::Justification::centredLeft, false);
            x += vw + 36.0f;
        }

        juce::String format;
        if (sampleRate > 0.0)
        {
            const double kHz = sampleRate / 1000.0;
            const bool whole = std::abs (kHz - std::round (kHz)) < 0.001;
            format = (channels == 1 ? "mono" : "stereo") + juce::String (", ") + (whole ? juce::String ((int) std::round (kHz)) : juce::String (kHz, 1)) + " kHz";
        }
        g.setFont (mono (15.0f));
        g.setColour (muted);
        g.drawText (format, juce::Rectangle<float> (w - 300.0f, 0.0f, 282.0f, statusH), juce::Justification::centredRight, false);
    }

    g.setColour (cardEdge);
    g.drawHorizontalLine ((int) statusH, 0.0f, w);

    // ---- ruler ----
    {
        g.setFont (mono (14.5f));
        for (int sec = 0; sec <= 150; sec += 30)
        {
            const float x = xFor (sec);
            g.setColour (muted.withAlpha (0.7f));
            g.drawVerticalLine ((int) std::round (x), waveTop - 7.0f, waveTop);
            g.setColour (muted);
            g.drawText (minutesSeconds (sec), juce::Rectangle<float> (x + 5.0f, statusH + 1.0f, 60.0f, rulerH - 3.0f),
                        juce::Justification::centredLeft, false);
        }
        g.setColour (muted);
        g.drawText (minutesSeconds (kReelSeconds), juce::Rectangle<float> (w - 66.0f, statusH + 1.0f, 60.0f, rulerH - 3.0f),
                    juce::Justification::centredRight, false);
    }

    g.setColour (cardEdge);
    g.drawHorizontalLine ((int) waveTop, 0.0f, w);

    // ---- waveform lanes ----
    const float lanesH = h - waveTop - numbersH;
    const float laneH = lanesH * 0.5f;
    const float centreL = waveTop + laneH * 0.5f;
    const float centreR = waveTop + laneH * 1.5f;

    // Current splice tint (behind everything else)
    auto spliceStart = [&] (int i) { return i <= 0 ? 0.0 : markers[(size_t) i - 1]; };
    auto spliceEnd   = [&] (int i) { return i < (int) markers.size() ? markers[(size_t) i] : reelLength; };
    if (currentSplice >= 0)
    {
        g.setColour (amber.withAlpha (0.10f));
        g.fillRect (juce::Rectangle<float> (xFor (spliceStart (currentSplice)), waveTop, xFor (spliceEnd (currentSplice)) - xFor (spliceStart (currentSplice)), h - waveTop));
    }

    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawHorizontalLine ((int) centreL, 0.0f, xFor (reelLength));
    g.drawHorizontalLine ((int) centreR, 0.0f, xFor (reelLength));
    g.setFont (sans (14.0f, true, 0.1f));
    g.setColour (muted);
    g.drawText ("L", juce::Rectangle<float> (6.0f, waveTop + 4.0f, 18.0f, 18.0f), juce::Justification::centredLeft, false);
    g.drawText ("R", juce::Rectangle<float> (6.0f, waveTop + laneH + 4.0f, 18.0f, 18.0f), juce::Justification::centredLeft, false);

    if (demo)
    {
        auto drawLane = [&] (float centre, double phase)
        {
            auto envelope = [phase] (double t)
            {
                const double a = std::abs (std::sin (t * 0.17 + phase));
                const double c = 0.5 + 0.5 * std::sin (t * 0.6 + phase * 2.0);
                return juce::jlimit (0.12, 1.0, 0.18 + 0.62 * std::pow (a, 1.4) + 0.18 * c);
            };
            const float amplitude = laneH * 0.36f;
            const float xEnd = xFor (reelLength);
            juce::Path p;
            p.startNewSubPath (0.0f, centre);
            for (float x = 0.0f; x <= xEnd; x += 2.0f)
                p.lineTo (x, centre - amplitude * (float) envelope ((double) x / w * kReelSeconds));
            for (float x = xEnd; x >= 0.0f; x -= 2.0f)
                p.lineTo (x, centre + amplitude * (float) envelope ((double) x / w * kReelSeconds));
            p.closeSubPath();
            g.setColour (teal);
            g.fillPath (p);
        };
        drawLane (centreL, 0.3);
        drawLane (centreR, 0.9);

        // Current gene (a box over the waveform)
        const juce::Rectangle<float> gene (xFor (50.7), waveTop + 5.0f, xFor (55.3) - xFor (50.7), h - waveTop - numbersH + 8.0f);
        g.setColour (amber.withAlpha (0.22f));
        g.fillRect (gene);
        g.setColour (amber);
        g.drawRect (gene, 1.5f);
    }

    // Free reel time: hatched
    {
        const float x0 = xFor (reelLength);
        const juce::Rectangle<float> free (x0, waveTop + 1.0f, w - x0, h - waveTop - 1.0f);
        g.saveState();
        g.reduceClipRegion (free.toNearestInt());
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        const float span = free.getHeight();
        for (float s = x0 - span; s < w; s += 9.0f)
            g.drawLine (s, h, s + span, waveTop, 1.0f);
        g.restoreState();

        if (free.getWidth() > 140.0f)
        {
            g.setFont (mono (15.0f));
            g.setColour (muted);
            g.drawText ("free reel time", free.withTrimmedBottom (numbersH), juce::Justification::centred, false);
        }
    }

    // Splice markers and their numbers
    {
        const float dashes[] = { 4.0f, 3.0f };
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        for (double m : markers)
            g.drawDashedLine (juce::Line<float> (xFor (m), waveTop, xFor (m), h), dashes, 2, 1.5f);

        if (demo)
        {
            g.setFont (mono (14.5f));
            for (int i = 0; i <= (int) markers.size(); ++i)
            {
                const float x0 = xFor (spliceStart (i)), x1 = xFor (spliceEnd (i));
                g.setColour (i == currentSplice ? amber : muted);
                g.drawText (juce::String (i + 1), juce::Rectangle<float> (x0, h - numbersH + 2.0f, x1 - x0, numbersH - 4.0f),
                            juce::Justification::centred, false);
            }
        }
    }

    // Playhead
    if (demo)
    {
        const float x = xFor (playhead);
        g.setColour (juce::Colours::white);
        g.drawLine (x, waveTop, x, h, 1.5f);
        juce::Path tri;
        tri.addTriangle (x - 5.5f, waveTop - 1.0f, x + 5.5f, waveTop - 1.0f, x, waveTop + 8.0f);
        g.fillPath (tri);
    }

    g.restoreState();
    g.setColour (cardEdge);
    g.drawRoundedRectangle (b.reduced (0.5f), 8.0f, 1.0f);
}

//==============================================================================
GainStepSelector::GainStepSelector (juce::AudioProcessorValueTreeState& state, const char* parameterId)
{
    parameter = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (parameterId));
    jassert (parameter != nullptr);
    if (parameter == nullptr)
        return;

    for (auto& choice : parameter->choices)
        labels.add (choice.replace (" dB", ""));

    attachment = std::make_unique<juce::ParameterAttachment> (*parameter, [this] (float newValue)
    {
        selected = juce::jlimit (0, juce::jmax (0, labels.size() - 1), juce::roundToInt (newValue));
        repaint();
    });
    attachment->sendInitialUpdate();
}

juce::Rectangle<float> GainStepSelector::segment (int index) const
{
    const float gap = 3.0f;
    const int n = juce::jmax (1, labels.size());
    const float segW = ((float) getWidth() - gap * (float) (n - 1)) / (float) n;
    return { (segW + gap) * (float) index, 0.0f, segW, (float) getHeight() };
}

int GainStepSelector::indexAt (juce::Point<int> p) const
{
    for (int i = 0; i < labels.size(); ++i)
        if (segment (i).contains (p.toFloat()))
            return i;
    return -1;
}

void GainStepSelector::paint (juce::Graphics& g)
{
    for (int i = 0; i < labels.size(); ++i)
    {
        const auto r = segment (i);
        const bool on = i == selected;
        g.setColour (on ? amber : (i == hovered ? controlHot : control));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (on ? amber.brighter (0.3f) : controlEdge);
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
        g.setColour (on ? background : text);
        g.setFont (sans (14.5f, true));
        g.drawText (labels[i], r, juce::Justification::centred, false);
    }
}

void GainStepSelector::select (int index)
{
    if (index >= 0 && index < labels.size() && attachment != nullptr)
        attachment->setValueAsCompleteGesture ((float) index);
}

void GainStepSelector::mouseDown (const juce::MouseEvent& e)
{
    select (indexAt (e.getPosition()));
}

void GainStepSelector::mouseMove (const juce::MouseEvent& e)
{
    const int i = indexAt (e.getPosition());
    if (i != hovered)
    {
        hovered = i;
        repaint();
    }
}

void GainStepSelector::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

//==============================================================================
KnobCard::KnobCard (juce::AudioProcessorValueTreeState& state, const char* id, const juce::String& cardTitle, Size s,
                    std::function<juce::String (double)> formatter)
    : parameterId (id), title (cardTitle), size (s), format (formatter ? std::move (formatter) : percentText)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setTitle (cardTitle);
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (slider);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, id, slider);
}

void KnobCard::addNote (const juce::String& text, juce::Rectangle<int> area, juce::Justification j)
{
    notes.push_back ({ text, area, j });
}

void KnobCard::addExtra (juce::Component& c, juce::Rectangle<int> area)
{
    addAndMakeVisible (c);
    c.setBounds (area);
    extras.push_back ({ &c, area });
}

void KnobCard::resized()
{
    const int d = size == Size::large ? 116 : 82;
    slider.setBounds (juce::Rectangle<int> (d, d).withCentre ({ getWidth() / 2, 98 }));
    for (auto& e : extras)
        e.component->setBounds (e.area);
}

void KnobCard::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (card);
    g.fillRoundedRectangle (b, 8.0f);
    g.setColour (cardEdge);
    g.drawRoundedRectangle (b, 8.0f, 1.0f);

    g.setColour (text);
    g.setFont (sans (16.0f, true, 0.06f));
    g.drawText (title, juce::Rectangle<int> (0, 9, getWidth(), 22), juce::Justification::centred, false);

    g.setColour (amber);
    g.setFont (mono (18.0f));
    g.drawText (format (slider.getValue()), juce::Rectangle<int> (0, 163, getWidth(), 24), juce::Justification::centred, false);

    g.setColour (muted);
    g.setFont (sans (13.5f, false, 0.02f));
    for (auto& n : notes)
        g.drawFittedText (n.text, n.area, n.justification, 3, 1.0f);
}

//==============================================================================
StyledPanel::StyledPanel (juce::AudioProcessorValueTreeState& s, juce::AudioProcessor& processor, const juce::String& version)
    : state (s), host (processor), versionText (version)
{
    // ---- the eight knobs, in panel order ----
    auto& input = addKnob (PheParams::inputLevel, "INPUT LEVEL", KnobCard::Size::small);
    addKnob (PheParams::soundOnSound, "SOUND ON SOUND", KnobCard::Size::small);
    addKnob (PheParams::geneSize,     "GENE-SIZE",      KnobCard::Size::small);
    addKnob (PheParams::slide,        "SLIDE",          KnobCard::Size::small);
    auto& vari  = addKnob (PheParams::variSpeed, "VARI-SPEED", KnobCard::Size::large);
    auto& morph = addKnob (PheParams::morph,     "MORPH",      KnobCard::Size::large);
    addKnob (PheParams::organize,    "ORGANIZE",     KnobCard::Size::small);
    addKnob (PheParams::outputLevel, "OUTPUT LEVEL", KnobCard::Size::small);

    // Under Input Level: the 4-step gain selector
    gainSelector = std::make_unique<GainStepSelector> (state, PheParams::inputGain);
    controlIds.push_back (PheParams::inputGain);
    input.addNote ("INPUT GAIN STEP\n(dB)", { 7, 194, 130, 36 }, juce::Justification::topLeft);
    input.addExtra (*gainSelector, { 6, 233, 127, 31 });

    // Under Vari-Speed: Pitch Snap
    auto& snap = makeChoice (PheParams::pitchSnap);
    vari.addNote ("PITCH SNAP", { 0, 193, kCardWidth, 22 }, juce::Justification::centred);
    vari.addExtra (snap.box, { 7, 220, 125, 32 });
    vari.addNote ("Off / Steps / Semitones", { 0, 256, kCardWidth, 20 }, juce::Justification::centred);

    // Under Morph: Pitch Scale
    auto& scale = makeChoice (PheParams::pitchScale);
    morph.addNote ("PITCH SCALE", { 0, 186, kCardWidth, 22 }, juce::Justification::centred);
    morph.addExtra (scale.box, { 7, 212, 125, 33 });
    morph.addNote ("Off + 28 scales (random\npitch-up genes)", { 1, 249, 137, 38 }, juce::Justification::centred);

    // ---- buttons ----
    addPanelButton (PheParams::rec,              "REC", true);
    addPanelButton (PheParams::recNewSplice,     "REC NEW SPLICE");
    addPanelButton (PheParams::splice,           "SPLICE");
    addPanelButton (PheParams::shift,            "SHIFT");
    addPanelButton (PheParams::play,             "PLAY");
    addPanelButton (PheParams::deleteMarker,     "DELETE SPLICE MARKER");
    addPanelButton (PheParams::deleteAllMarkers, "DELETE ALL MARKERS");
    addPanelButton (PheParams::eraseSplice,      "ERASE SPLICE");
    addPanelButton (PheParams::clearReel,        "CLEAR REEL");
    addPanelButton (PheParams::undo,             "UNDO");

    // ---- reel file buttons (not usable until milestone 9) ----
    for (auto* b : { &loadWav, &saveWav, &reelFolder })
    {
        b->setEnabled (false);
        addAndMakeVisible (*b);
    }

    // ---- the way back to the plain-slider window ----
    plainSliders.onClick = [this] { if (onSwitchToPlain) onSwitchToPlain(); };
    addAndMakeVisible (plainSliders);

    addAndMakeVisible (reel);

    setSize (kWidth, kHeight);

    timerCallback();
    startTimerHz (2);
}

StyledPanel::~StyledPanel()
{
    stopTimer();
}

KnobCard& StyledPanel::addKnob (const char* id, const juce::String& title, KnobCard::Size size,
                                std::function<juce::String (double)> format)
{
    auto card = std::make_unique<KnobCard> (state, id, title, size, std::move (format));
    addAndMakeVisible (*card);
    controlIds.push_back (id);
    cards.push_back (std::move (card));
    return *cards.back();
}

StyledPanel::ChoiceItem& StyledPanel::makeChoice (const char* id)
{
    auto item = std::make_unique<ChoiceItem>();
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (id)))
        item->box.addItemList (p->choices, 1);
    item->box.setTitle (state.getParameter (id) != nullptr ? state.getParameter (id)->getName (64) : juce::String());
    item->attachment = std::make_unique<ComboBoxAttachment> (state, id, item->box);
    controlIds.push_back (id);
    choices.push_back (std::move (item));
    return *choices.back();
}

void StyledPanel::addPanelButton (const char* id, const juce::String& text, bool isRecButton)
{
    auto item = std::make_unique<ButtonItem>();
    item->button.setButtonText (text);
    item->button.setClickingTogglesState (true);
    if (isRecButton)
        item->button.getProperties().set ("recDot", true);
    addAndMakeVisible (item->button);
    item->attachment = std::make_unique<ButtonAttachment> (state, id, item->button);
    controlIds.push_back (id);
    buttons.push_back (std::move (item));
}

void StyledPanel::timerCallback()
{
    const double rate = host.getSampleRate();
    const int channels = host.getTotalNumInputChannels();
    if (rate != shownRate || channels != shownChannels)
    {
        shownRate = rate;
        shownChannels = channels;
        reel.setFormat (rate, channels);
    }
}

void StyledPanel::paint (juce::Graphics& g)
{
    g.fillAll (background);

    // Title
    g.setColour (text);
    g.setFont (sans (43.0f, true, -0.01f));
    g.drawText ("PhenomeEon", juce::Rectangle<int> (kMargin, 32, 320, 50), juce::Justification::centredLeft, false);
    g.setColour (muted);
    g.setFont (sans (15.0f, false, 0.10f));
    g.drawText ("TAPE-SPLICE & MICROSOUND LOOPER", juce::Rectangle<int> (kMargin, 73, 400, 22), juce::Justification::centredLeft, false);

    // Reel file box
    g.setColour (card);
    g.fillRoundedRectangle (kReelFileBox.toFloat(), 8.0f);
    g.setColour (cardEdge);
    g.drawRoundedRectangle (kReelFileBox.toFloat().reduced (0.5f), 8.0f, 1.0f);
    g.setColour (muted);
    g.setFont (sans (14.5f, false, 0.10f));
    g.drawText ("REEL FILE", juce::Rectangle<int> (kReelFileBox.getX() + 14, kReelFileBox.getY(), 90, kReelFileBox.getHeight()),
                juce::Justification::centredLeft, false);
    g.setFont (mono (15.0f));
    g.drawText ("not active yet", juce::Rectangle<int> (750, kReelFileBox.getY(), 145, kReelFileBox.getHeight()),
                juce::Justification::centredLeft, false);

    // Clock box (space kept for the optional clock features, milestone 10)
    {
        juce::Path box;
        box.addRoundedRectangle (kClockBox.toFloat().reduced (0.5f), 8.0f);
        juce::Path dashed;
        const float dashes[] = { 4.0f, 3.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, box, dashes, 2);
        g.setColour (cardEdge.brighter (0.15f));
        g.fillPath (dashed);
        g.setColour (muted);
        g.setFont (sans (14.5f, false, 0.10f));
        g.drawText ("CLOCK (LATER)", juce::Rectangle<int> (kClockBox.getX() + 16, kClockBox.getY() + 5, 215, 20), juce::Justification::centredLeft, false);
        g.setFont (sans (14.5f));
        g.drawFittedText ("Off by default. Space kept\nfor Logic tempo features.",
                          juce::Rectangle<int> (kClockBox.getX() + 16, kClockBox.getY() + 25, kClockBox.getWidth() - 20, 36),
                          juce::Justification::topLeft, 2, 1.0f);
    }

    // Section labels over the button rows
    g.setColour (muted);
    g.setFont (sans (15.0f, false, 0.10f));
    g.drawText ("RECORD & PLAY", juce::Rectangle<int> (kLeftGroupX, 652, 300, 22), juce::Justification::centredLeft, false);
    g.drawText ("EDIT", juce::Rectangle<int> (kRightGroupX, 652, 300, 22), juce::Justification::centredLeft, false);

    // Footer
    g.setFont (mono (14.0f));
    g.drawText ("PhenomeEon v" + versionText + "  -  styled panel  -  the controls do not change the sound yet",
                juce::Rectangle<int> (kMargin, 757, 900, 28), juce::Justification::centredLeft, false);
}

void StyledPanel::resized()
{
    reel.setBounds (kReelDisplay);

    for (size_t i = 0; i < cards.size(); ++i)
        cards[i]->setBounds (kMargin + (int) i * kCardStep, kCardTop, kCardWidth, kCardHeight);

    // Two groups of five buttons
    auto place = [this] (size_t first, int groupX, int groupW)
    {
        const float w = ((float) groupW - 4.0f * (float) kButtonGap) / 5.0f;
        for (size_t i = 0; i < 5; ++i)
        {
            const float x = (float) groupX + (float) i * (w + (float) kButtonGap);
            buttons[first + i]->button.setBounds (juce::Rectangle<float> (x, (float) kButtonTop, w, (float) kButtonHeight).toNearestInt());
        }
    };
    place (0, kLeftGroupX, kLeftGroupW);
    place (5, kRightGroupX, kRightGroupW);

    // Reel file buttons
    const int by = kReelFileBox.getY() + 14;
    loadWav.setBounds (440, by, 88, 35);
    saveWav.setBounds (541, by, 88, 35);
    reelFolder.setBounds (642, by, 96, 35);

    plainSliders.setBounds (kWidth - kMargin - 140, 757, 140, 28);
}
