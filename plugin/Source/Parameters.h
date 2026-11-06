// The list of controls Logic can see and automate: every control in the approved panel plan.
//
// The short ids below are what Logic stores in saved projects and automation, so they must not be
// renamed once people start using the plugin. New controls get new ids added at the end.
//
// All of them are in use by milestone 10 (32 in all).

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

    // Firmware-option menus (milestone 8). Each starts at 0 = the normal behaviour, so nothing changes until one is chosen.
    inline constexpr const char* variSpeedMode = "variSpeedMode";   // Classic / Octave scale (both ways) / Octave scale (forward only)
    inline constexpr const char* recordSource  = "recordSource";    // Mix (as set by Sound On Sound) / Input only
    inline constexpr const char* organizeMode  = "organizeMode";    // New splice at end of gene / immediately
    inline constexpr const char* geneWindow    = "geneWindow";      // Short fades / Smooth windows
    inline constexpr const char* playMode      = "playMode";        // Classic / Momentary / Trigger loop
    inline constexpr const char* morphChord    = "morphChord";      // Random pitch-ups / four chord choices

    // Clock features (milestone 10), driven by Logic's tempo. Clock Mode, Play Retrigger and Rec Sync start Off.
    inline constexpr const char* clockMode       = "clockMode";         // Off / Hybrid / Gene Shift / Time Stretch
    inline constexpr const char* clockRate       = "clockRate";         // how far apart the ticks are: 2 bars ... 1/32
    inline constexpr const char* clocksPerSplice = "clocksPerSplice";   // Time Stretch: ticks per trip through the splice
    inline constexpr const char* retrigger       = "retrigger";         // Play Retrigger: Off / every 1 ... 64 clocks
    inline constexpr const char* recSync         = "recSync";           // Rec and Rec New Splice: Off / On the clock

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}
