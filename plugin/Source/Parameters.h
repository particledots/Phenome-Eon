// The list of controls Logic can see and automate: every control in the approved panel plan.
//
// The short ids below are what Logic stores in saved projects and automation, so they must not be
// renamed once people start using the plugin. New controls get new ids added at the end.
//
// In milestone 2 none of these change the sound yet; each gets its function in a later milestone.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace PheParams
{
    // The eight knobs, in panel order (0.0 to 1.0)
    inline constexpr const char* inputLevel   = "inputLevel";
    inline constexpr const char* soundOnSound = "soundOnSound";
    inline constexpr const char* geneSize     = "geneSize";
    inline constexpr const char* slide        = "slide";
    inline constexpr const char* variSpeed    = "variSpeed";
    inline constexpr const char* morph        = "morph";
    inline constexpr const char* organize     = "organize";
    inline constexpr const char* outputLevel  = "outputLevel";

    // Menus: the 4-step input gain under Input Level, Pitch Snap under Vari-Speed, Pitch Scale under Morph
    inline constexpr const char* inputGain    = "inputGain";      // -3 / 0 / +6 / +12 dB
    inline constexpr const char* pitchSnap    = "pitchSnap";      // Off / Steps / Semitones
    inline constexpr const char* pitchScale   = "pitchScale";     // Off + 28 scales

    // Buttons (on/off, automatable)
    inline constexpr const char* rec              = "rec";
    inline constexpr const char* recNewSplice     = "recNewSplice";
    inline constexpr const char* splice           = "splice";
    inline constexpr const char* shift            = "shift";
    inline constexpr const char* play             = "play";
    inline constexpr const char* deleteMarker     = "deleteMarker";
    inline constexpr const char* deleteAllMarkers = "deleteAllMarkers";
    inline constexpr const char* eraseSplice      = "eraseSplice";
    inline constexpr const char* clearReel        = "clearReel";
    inline constexpr const char* undo             = "undo";

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}
