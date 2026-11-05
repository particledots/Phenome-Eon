// The styled panel (milestone 3), laid out like the approved mockup.
//
// Every control here is attached to the same parameter ids as the plain-slider window (see Parameters.h), so
// saved projects and automation work the same whichever window is showing. The panel only draws and edits
// control values; it makes no sound and it does not look inside the engine.
//
// The panel is laid out in a fixed 1200 x 800 space; the editor scales it to the window.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PanelLookAndFeel.h"
#include "Parameters.h"

#include <functional>
#include <memory>
#include <vector>

//==============================================================================
// The reel display: ruler, two waveform lanes, splice markers, current gene and playhead.
// Milestone 3 draws the empty reel only (nothing recorded yet): an empty ruler and the whole length hatched as
// free reel time. The drawing of waveform, markers, gene box and playhead is already here, so the preview tool
// can compare it with the mockup; milestone 4 onwards feeds it real reel data.
class ReelDisplay : public juce::Component
{
public:
    static constexpr double kReelSeconds = 174.0;      // 2.9 minutes, shown as 2:54 on the ruler

    ReelDisplay() = default;

    void paint (juce::Graphics&) override;

    // The audio format shown at the right of the status line (0 = not known yet).
    void setFormat (double sampleRate, int numChannels);

    // Only the preview tool (not the plugin) turns this on: draws made-up content like the mockup.
    void showDemoContent (bool shouldShow) { demo = shouldShow; repaint(); }

private:
    double sampleRate = 0.0;
    int channels = 0;
    bool demo = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReelDisplay)
};

//==============================================================================
// The 4-step input gain selector under Input Level (-3 / 0 / +6 / +12 dB), attached to the "inputGain" choice.
class GainStepSelector : public juce::Component
{
public:
    GainStepSelector (juce::AudioProcessorValueTreeState&, const char* parameterId);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    // Choose a step (what a click does); used by the automatic checks too.
    void select (int index);

private:
    int indexAt (juce::Point<int>) const;
    juce::Rectangle<float> segment (int index) const;

    juce::AudioParameterChoice* parameter = nullptr;
    juce::StringArray labels;
    int selected = 0;
    int hovered = -1;
    std::unique_ptr<juce::ParameterAttachment> attachment;     // declared last: destroyed first

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GainStepSelector)
};

//==============================================================================
// One knob with its title, its value readout, and (for some) extra things underneath.
class KnobCard : public juce::Component
{
public:
    enum class Size { small, large };

    // "format" turns the knob position (0..1) into the text under the knob. Until a control has its sound
    // (later milestones) the default is a plain percentage.
    KnobCard (juce::AudioProcessorValueTreeState&, const char* parameterId, const juce::String& title, Size,
              std::function<juce::String (double)> format = {});

    void paint (juce::Graphics&) override;
    void resized() override;

    // A small piece of text drawn inside the card, and a child control placed inside the card.
    void addNote (const juce::String& text, juce::Rectangle<int> area, juce::Justification);
    void addExtra (juce::Component&, juce::Rectangle<int> area);

    const char* getParameterId() const { return parameterId; }

private:
    struct Note  { juce::String text; juce::Rectangle<int> area; juce::Justification justification; };
    struct Extra { juce::Component* component; juce::Rectangle<int> area; };

    const char* parameterId;
    juce::String title;
    Size size;
    std::function<juce::String (double)> format;
    std::vector<Note> notes;
    std::vector<Extra> extras;

    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;   // after the slider: destroyed first

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KnobCard)
};

//==============================================================================
class StyledPanel : public juce::Component, private juce::Timer
{
public:
    static constexpr int kWidth  = 1200;
    static constexpr int kHeight = 800;

    // "processor" is only asked for the audio format (sample rate, channels) shown in the reel display.
    StyledPanel (juce::AudioProcessorValueTreeState&, juce::AudioProcessor& processor, const juce::String& versionText);
    ~StyledPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Called when the "Plain sliders" button is pressed; the editor swaps windows.
    std::function<void()> onSwitchToPlain;

    // The ids of all parameters that have a control in this window (used by the check that none is missing).
    const std::vector<juce::String>& controlledParameterIds() const { return controlIds; }

    ReelDisplay& getReelDisplay() { return reel; }

private:
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment   = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct ChoiceItem { juce::ComboBox box; std::unique_ptr<ComboBoxAttachment> attachment; };
    struct ButtonItem { juce::TextButton button; std::unique_ptr<ButtonAttachment> attachment; };

    KnobCard& addKnob (const char* id, const juce::String& title, KnobCard::Size, std::function<juce::String (double)> format = {});
    ChoiceItem& makeChoice (const char* id);
    void addPanelButton (const char* id, const juce::String& text, bool isRecButton = false);
    void timerCallback() override;

    juce::AudioProcessorValueTreeState& state;
    juce::AudioProcessor& host;
    juce::String versionText;
    std::vector<juce::String> controlIds;

    ReelDisplay reel;
    std::vector<std::unique_ptr<KnobCard>> cards;
    std::unique_ptr<GainStepSelector> gainSelector;
    std::vector<std::unique_ptr<ChoiceItem>> choices;
    std::vector<std::unique_ptr<ButtonItem>> buttons;

    // Reel file buttons: shown as in the mockup but not usable until milestone 9.
    juce::TextButton loadWav { "Load WAV" }, saveWav { "Save WAV" }, reelFolder { "Reel folder" };
    juce::TextButton plainSliders { "PLAIN SLIDERS" };

    double shownRate = -1.0;
    int shownChannels = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StyledPanel)
};
