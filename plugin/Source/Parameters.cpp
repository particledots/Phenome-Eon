#include "Parameters.h"
#include <phe/Params.h>
#include <phe/core/Scales.h>

namespace PheParams
{

static std::unique_ptr<juce::AudioParameterFloat> knob (const char* id, const char* name, float defaultValue)
{
    return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 },
                                                        name,
                                                        juce::NormalisableRange<float> (0.0f, 1.0f, 0.0f),
                                                        defaultValue);
}

static std::unique_ptr<juce::AudioParameterBool> button (const char* id, const char* name)
{
    return std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, false);
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // The eight knobs. The starting positions are placeholders; each is set properly when the control gets its sound.
    layout.add (knob (inputLevel,   "Input Level",    0.5f));
    layout.add (knob (soundOnSound, "Sound On Sound", 0.5f));
    layout.add (knob (geneSize,     "Gene-Size",      0.5f));
    layout.add (knob (slide,        "Slide",          0.5f));
    layout.add (knob (variSpeed,    "Vari-Speed",     0.5f));
    layout.add (knob (morph,        "Morph",          0.5f));
    layout.add (knob (organize,     "Organize",       0.5f));
    layout.add (knob (outputLevel,  "Output Level",   0.5f));

    // Input gain step: -3 / 0 / +6 / +12 dB, starting at 0 dB (the engine's table is the single source of these numbers).
    juce::StringArray gainNames;
    for (int i = 0; i < phe::kNumInputGainSteps; ++i)
    {
        const float db = phe::kInputGainStepDb[i];
        gainNames.add ((db > 0.0f ? "+" : "") + juce::String ((int) db) + " dB");
    }
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { inputGain, 1 },
                                                              "Input Gain Step", gainNames, 1));

    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { pitchSnap, 1 },
                                                              "Pitch Snap", juce::StringArray { "Off", "Steps", "Semitones" }, 0));

    juce::StringArray scaleNames;
    scaleNames.add ("Off");
    for (int i = 0; i < phe::scales::kNumScales; ++i)
        scaleNames.add (phe::scales::kScales[i].name);
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { pitchScale, 1 },
                                                              "Pitch Scale", scaleNames, 0));

    layout.add (button (rec,              "Rec"));
    layout.add (button (recNewSplice,     "Rec New Splice"));
    layout.add (button (splice,           "Splice"));
    layout.add (button (shift,            "Shift"));
    layout.add (button (play,             "Play"));
    layout.add (button (deleteMarker,     "Delete Splice Marker"));
    layout.add (button (deleteAllMarkers, "Delete All Markers"));
    layout.add (button (eraseSplice,      "Erase Splice"));
    layout.add (button (clearReel,        "Clear Reel"));
    layout.add (button (undo,             "Undo"));

    return layout;
}

} // namespace PheParams
