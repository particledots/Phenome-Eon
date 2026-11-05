// The styled panel (milestone 3), laid out like the approved mockup.
//
// Every control here is attached to the same parameter ids as the plain-slider window (see Parameters.h), so
// saved projects and automation work the same whichever window is showing. The panel only draws and edits
// control values; it makes no sound and it does not look inside the engine.
//
// The panel is laid out in a fixed 1200 x 800 space; the editor scales it to the window.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <phe/Engine.h>
#include "PanelLookAndFeel.h"
#include "Parameters.h"

#include <functional>
#include <memory>
#include <vector>

//==============================================================================
// The reel display: ruler, two waveform lanes, splice markers, current gene and playhead.
// It shows the real reel (read-only, from the engine): the recorded length, a waveform picture of what is on the reel,
// the playhead, the current gene (the amber box, milestone 5), the splice markers with their numbers (milestone 7: the
// selected splice is tinted amber, a splice that is still playing while another is waiting to take over is marked in
// white) and, while recording, the record head (and, while a new splice is recorded, a marker where it begins).
class ReelDisplay : public juce::Component
{
public:
    static constexpr double kReelSeconds = 174.0;      // 2.9 minutes, shown as 2:54 on the ruler

    ReelDisplay() = default;

    void paint (juce::Graphics&) override;

    // The audio format shown at the right of the status line (0 = not known yet).
    void setFormat (double sampleRate, int numChannels);

    // Where the reel data comes from (the engine, read-only). Without it the display shows an empty reel.
    void setSource (const phe::Engine* engine);

    // Looks at the reel again and redraws if anything changed. Called by a timer.
    void refresh();

    // Only the preview tool (not the plugin) turns this on: draws made-up content like the mockup.
    void showDemoContent (bool shouldShow) { demo = shouldShow; repaint(); }

private:
    double sampleRate = 0.0;
    int channels = 0;
    bool demo = false;

    const phe::Engine* engine = nullptr;
    phe::Engine::ReelView view;
    std::vector<int> markerSamples;      // the splice markers (samples), copied from the engine

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

    // Changes the text of a note added earlier (0 = the first one added); redraws only if it changed.
    void setNoteText (size_t index, const juce::String& text);
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
// The Options sheet (milestone 8): the six firmware-option menus, each with a plain-language line of explanation.
// It covers the panel while open; the menus are attached to the same parameters as in the plain-slider window.
class OptionsSheet : public juce::Component
{
public:
    OptionsSheet();

    // Adds one row: a title, the menu (owned by the panel) and a line explaining what the choices do.
    void addRow (const juce::String& title, juce::ComboBox& box, const juce::String& explanation);

    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override {}      // clicks on the sheet do not reach the panel underneath

private:
    struct Row { juce::String title, explanation; juce::ComboBox* box; };
    std::vector<Row> rows;
    juce::TextButton close { "CLOSE" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OptionsSheet)
};

//==============================================================================
class StyledPanel : public juce::Component, private juce::Timer
{
public:
    static constexpr int kWidth  = 1200;
    static constexpr int kHeight = 800;

    // "processor" is asked for the audio format (sample rate, channels) shown in the reel display; "engine" is only
    // looked at (read-only) to draw the reel.
    StyledPanel (juce::AudioProcessorValueTreeState&, juce::AudioProcessor& processor, const phe::Engine& engine,
                 const juce::String& versionText);
    ~StyledPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Called when the "Plain sliders" button is pressed; the editor swaps windows.
    std::function<void()> onSwitchToPlain;

    // The ids of all parameters that have a control in this window (used by the check that none is missing).
    const std::vector<juce::String>& controlledParameterIds() const { return controlIds; }

    ReelDisplay& getReelDisplay() { return reel; }

    // The Options sheet (open / close), also used by the automatic checks.
    void showOptions (bool shouldShow);
    bool optionsShowing() const { return options.isVisible(); }

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
    juce::TextButton optionsButton { "OPTIONS" };
    OptionsSheet options;
    KnobCard* variCard = nullptr;
    int shownVariMode = -1;
    int shownUndoState = -1;

    double shownRate = -1.0;
    int shownChannels = -1;

    // The Organize and Gene-Size read-outs depend on the splices (how many, how long), so they are redrawn when those change.
    const phe::Engine* engineForReadouts = nullptr;
    KnobCard* organizeCard = nullptr;
    KnobCard* geneCard = nullptr;
    long long shownSpliceStamp = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StyledPanel)
};
