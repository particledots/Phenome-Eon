// The plugin window. It can show either the styled panel (milestone 3, the default) or the plain slider
// window (milestone 2), which stays available as a fallback. Both have one control for every parameter and
// use the same parameter ids, so saved projects and automation work the same whichever is showing.
//
// Each is laid out in its own fixed space and scaled to the window, keeping its shape (both are 3:2).

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "PanelLookAndFeel.h"
#include "StyledPanel.h"

#include <functional>

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

    // Called when the "Styled panel" button is pressed; the editor swaps windows.
    std::function<void()> onSwitchToStyled;

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
    juce::TextButton styledPanelButton { "Styled panel" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhenomeEonSliderPanel)
};

class PhenomeEonEditor : public juce::AudioProcessorEditor
{
public:
    explicit PhenomeEonEditor (PhenomeEonProcessor&);
    ~PhenomeEonEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    enum class View { styled, plain };
    View getView() const { return styled != nullptr ? View::styled : View::plain; }
    void showView (View);

    // For the automatic checks: the window that is showing, and the ids it controls.
    const std::vector<juce::String>& controlledParameterIds() const;

private:
    void requestView (View);        // switches after the current click has finished
    void layOutPanel();

    PhenomeEonProcessor& owner;
    juce::LookAndFeel_V4 plainLook;        // declared before the panels, so they outlive them
    PanelLookAndFeel styledLook;
    std::unique_ptr<StyledPanel> styled;
    std::unique_ptr<PhenomeEonSliderPanel> plain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhenomeEonEditor)
};
