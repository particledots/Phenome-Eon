// The plain slider window (milestone 2). One plain control for every parameter, laid out in a fixed
// space and scaled to the window, so it can be resized. The styled panel (milestone 3) replaces this
// window but uses the same parameter ids, so saved projects and automation keep working.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

#include <memory>
#include <vector>

// Everything in the window, laid out in a fixed 900 x 600 space. The editor scales it to the window size.
class PhenomeEonSliderPanel : public juce::Component
{
public:
    static constexpr int kWidth  = 900;
    static constexpr int kHeight = 600;

    explicit PhenomeEonSliderPanel (juce::AudioProcessorValueTreeState&);
    ~PhenomeEonSliderPanel() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

    // The ids of all parameters that have a control in this window (used by the check that none is missing).
    const std::vector<juce::String>& controlledParameterIds() const { return controlIds; }

private:
    using SliderAttachment   = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment   = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    // In each row the attachment is declared after the control, so it is destroyed first.
    struct SliderRow  { juce::Label label; juce::Slider slider; std::unique_ptr<SliderAttachment> attachment; };
    struct ChoiceRow  { juce::Label label; juce::ComboBox box;  std::unique_ptr<ComboBoxAttachment> attachment; };
    struct ButtonItem { juce::TextButton button;                std::unique_ptr<ButtonAttachment> attachment; };

    void addSlider (const char* id, const char* text);
    void addChoice (const char* id, const char* text);
    void addButton (const char* id, const char* text);

    juce::AudioProcessorValueTreeState& state;
    std::vector<juce::String> controlIds;
    std::vector<std::unique_ptr<SliderRow>>  sliders;
    std::vector<std::unique_ptr<ChoiceRow>>  choices;
    std::vector<std::unique_ptr<ButtonItem>> buttons;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhenomeEonSliderPanel)
};

class PhenomeEonEditor : public juce::AudioProcessorEditor
{
public:
    explicit PhenomeEonEditor (PhenomeEonProcessor&);
    ~PhenomeEonEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::LookAndFeel_V4 lookAndFeel;      // declared before the panel, so it outlives it
    PhenomeEonSliderPanel panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhenomeEonEditor)
};
