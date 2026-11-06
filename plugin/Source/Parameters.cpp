#include "Parameters.h"
#include <phe/Params.h>
#include <phe/core/Scales.h>
#include <phe/core/Morph.h>
#include <phe/core/HostClock.h>

namespace PheParams
{

static std::unique_ptr<juce::AudioParameterFloat> knob (const char* id, const char* name, float defaultValue)
{
    return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 },
                                                        name,
                                                        juce::NormalisableRange<float> (0.0f, 1.0f, 0.0f),
                                                        defaultValue);
}

static std::unique_ptr<juce::AudioParameterBool> button (const char* id, const char* name, bool defaultValue = false)
{
    return std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, defaultValue);
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // The eight knobs. The starting positions of the ones that do something now (Input Level, Sound On Sound,
    // Gene-Size, Slide, Vari-Speed, Morph, Organize, Output Level) come from the engine's list, so there is one source for them.
    layout.add (knob (inputLevel,   "Input Level",    phe::kDefaultInputLevel));
    layout.add (knob (soundOnSound, "Sound On Sound", phe::kDefaultSoundOnSound));
    layout.add (knob (geneSize,     "Gene-Size",      phe::kDefaultGeneSize));
    layout.add (knob (slide,        "Slide",          phe::kDefaultSlide));
    layout.add (knob (variSpeed,    "Vari-Speed",     phe::kDefaultVariSpeed));
    layout.add (knob (morph,        "Morph",          phe::kDefaultMorph));
    layout.add (knob (organize,     "Organize",       phe::kDefaultOrganize));
    layout.add (knob (outputLevel,  "Output Level",   phe::kDefaultOutputLevel));

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
    layout.add (button (play,             "Play", phe::kDefaultPlay));
    layout.add (button (deleteMarker,     "Delete Splice Marker"));
    layout.add (button (deleteAllMarkers, "Delete All Markers"));
    layout.add (button (eraseSplice,      "Erase Splice"));
    layout.add (button (clearReel,        "Clear Reel"));
    layout.add (button (undo,             "Undo"));

    // Firmware-option menus (milestone 8): appended, so older saved projects still load (they get the normal behaviour).
    auto choice = [&layout] (const char* id, const char* name, juce::StringArray names)
    {
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, names, 0));
    };
    choice (variSpeedMode, "Vari-Speed Mode", { "Classic", "Octave scale (both ways)", "Octave scale (forward only)" });
    choice (recordSource,  "Record Source",   { "Mix (Sound On Sound)", "Input only" });
    choice (organizeMode,  "Organize Timing", { "At end of gene", "Immediately" });
    choice (geneWindow,    "Gene Window",     { "Short fades", "Smooth windows" });
    choice (playMode,      "Play Mode",       { "Classic", "Momentary", "Trigger loop" });
    juce::StringArray chordNames;
    for (int i = 0; i < phe::dsp::kNumMorphChords; ++i)
        chordNames.add (phe::dsp::kMorphChords[i].name);
    choice (morphChord,    "Morph Chord",     chordNames);

    // Clock features (milestone 10): appended, so older saved projects still load (they get everything Off).
    choice (clockMode, "Clock Mode", { "Off", "Hybrid", "Gene Shift", "Time Stretch" });

    juce::StringArray rateNames;
    for (int i = 0; i < phe::core::kNumClockDivisions; ++i)
        rateNames.add (phe::core::kClockDivisions[i].name);
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { clockRate, 1 }, "Clock Rate", rateNames,
                                                              phe::core::kDefaultClockDivision));

    juce::StringArray stretchNames;
    for (int i = 0; i < phe::core::kNumStretchClocks; ++i)
        stretchNames.add (juce::String (phe::core::kStretchClocks[i]) + (phe::core::kStretchClocks[i] == 1 ? " clock" : " clocks"));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { clocksPerSplice, 1 }, "Clocks per Splice", stretchNames,
                                                              phe::core::kDefaultStretchClocks));

    juce::StringArray retriggerNames;
    for (int i = 0; i < phe::core::kNumRetriggers; ++i)
    {
        const int every = phe::core::kRetriggerClocks[i];
        retriggerNames.add (every == 0 ? juce::String ("Off") : (every == 1 ? juce::String ("Every clock") : "Every " + juce::String (every) + " clocks"));
    }
    choice (retrigger, "Play Retrigger", retriggerNames);
    choice (recSync,   "Rec Sync",       { "Off", "On the clock" });

    return layout;
}

} // namespace PheParams
