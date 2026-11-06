// The control values the engine understands: one plain struct, no plugin code.
//
// The plugin wrapper fills this in from the controls (sliders now, the styled panel later, Logic automation)
// and hands it to the engine. The engine never sees sliders, Logic or JUCE. A later plugin can have its own
// Params and reuse everything else.
//
// Knobs run 0.0 to 1.0. What each position means (decibels, seconds, semitones, percent) is decided inside the
// engine when each control gets its sound. Milestone 4 gave meaning to Input Level, Sound On Sound and Output
// Level, to the Input Gain Step menu, and to the Rec, Play and Clear Reel buttons. Milestone 5 adds Vari-Speed,
// Gene-Size, Slide and the Pitch Snap menu. Milestone 6 adds Morph and the Pitch Scale menu. Milestone 7 adds Organize
// and the Rec New Splice, Splice, Shift, Delete Splice Marker and Delete All Markers buttons. Milestone 8 adds Erase Splice
// and Undo, and six menus for optional behaviours taken from the module's later firmware options (Vari-Speed Mode, Record
// Source, Organize Timing, Gene Window, Play Mode, Morph Chord). The zero setting of every one of them is the normal behaviour.
// Milestone 10 adds the optional clock features, driven by Logic's tempo (see core/HostClock.h): Clock Mode, Clock Rate,
// Clocks per Splice, Play Retrigger and Rec Sync. Clock Mode, Play Retrigger and Rec Sync start Off, so nothing changes until
// one of them is chosen.

#pragma once

#include "core/HostClock.h"

namespace phe
{
    // Starting positions (what a new PhenomeEon opens with). The plugin wrapper uses the same numbers.
    inline constexpr float kDefaultInputLevel   = 0.75f;   // 0 dB (no change)
    inline constexpr float kDefaultSoundOnSound = 0.5f;    // live input and reel loop both at full level
    inline constexpr float kDefaultOutputLevel  = 0.75f;   // 0 dB (no change)
    inline constexpr bool  kDefaultPlay         = true;    // like the module with nothing patched to Play: the reel loops
    inline constexpr float kDefaultGeneSize     = 0.0f;    // fully left: a gene is the whole reel
    inline constexpr float kDefaultSlide        = 0.0f;    // fully left: genes start at the start of the reel
    inline constexpr float kDefaultVariSpeed    = 0.75f;   // original speed, forwards (the "2:30" position)
    inline constexpr float kDefaultMorph        = 0.18f;   // exactly 1/1: genes follow each other with no gap or overlap
    inline constexpr float kDefaultOrganize     = 0.0f;    // fully left: the first splice

    struct Params
    {
        // Knobs, 0..1 (the panel's eight knobs, in panel order)
        float inputLevel   = kDefaultInputLevel;
        float soundOnSound = kDefaultSoundOnSound;
        float geneSize     = kDefaultGeneSize;
        float slide        = kDefaultSlide;
        float variSpeed    = kDefaultVariSpeed;
        float morph        = kDefaultMorph;
        float organize     = kDefaultOrganize;
        float outputLevel  = kDefaultOutputLevel;

        // Menus
        int inputGainStep = 1;     // 0..3 = -3, 0, +6, +12 dB (see kInputGainStepDb)
        int pitchSnap     = 0;     // 0 Off, 1 Steps, 2 Semitones (lands Vari-Speed on notes)
        int pitchScale    = 0;     // 0 Off, 1..28 = the scales in core/Scales.h (limits Morph's random pitch-up genes)

        // Optional behaviours (milestone 8). 0 is always the normal behaviour.
        int variSpeedMode = 0;     // 0 classic curve, 1 octave scale (bidirectional), 2 octave scale (forward only)
        int recordSource  = 0;     // 0 record what you hear (Sound On Sound mix), 1 record the live input only
        int organizeMode  = 0;     // 0 a newly chosen splice starts when the gene in progress ends, 1 straight away
        int geneWindow    = 0;     // 0 short click-free fades, 1 smooth windows over the whole gene (short genes)
        int playMode      = 0;     // 0 Play lets the gene finish, 1 momentary (stops at once), 2 trigger loop (always plays;
                                   //   switching Play on jumps back to the start)
        int morphChord    = 0;     // 0 random pitch-ups past 2:30, 1.. fixed chords (see core/Morph.h)

        // Clock features (milestone 10). The clock is Logic's tempo (see core/HostClock.h).
        int clockMode       = 0;   // 0 Off, 1 Hybrid (Morph decides: up to 2/1 Gene Shift, above Time Stretch), 2 Gene Shift only, 3 Time Stretch only
        int clockRate       = core::kDefaultClockDivision;     // 0.. index into core::kClockDivisions (how far apart the ticks are)
        int clocksPerSplice = core::kDefaultStretchClocks;     // 0.. index into core::kStretchClocks (Time Stretch: ticks per trip through the splice)
        int retrigger       = 0;   // 0 Off, 1.. index into core::kRetriggerClocks (every how many ticks Play restarts from Slide)
        int recSync         = 0;   // 0 Off, 1 Rec and Rec New Splice start and stop on the next tick

        // Buttons (on = true).
        //   rec:        on = recording, off = not recording (a switch).
        //   play:       on = the reel loops; turning it on starts a fresh gene from the Slide position; turning it
        //               off lets the gene in progress finish and then stops.
        //   clearReel:  acts the moment it goes on (the plugin switches it back off by itself).
        //   recNewSplice: a switch like rec: on = recording a new splice onto the end of the reel, off = stop.
        //   splice, shift, deleteMarker, deleteAllMarkers: act the moment they go on (the plugin switches them back off).
        //   eraseSplice: acts the moment it goes on: the selected splice is removed from the reel (the audio after it moves up).
        //   undo:        acts the moment it goes on: puts back what the last edit changed; pressed again it does the edit again.
        //   Both are refused while recording, and the plugin switches them back off by itself.
        bool rec = false, recNewSplice = false, splice = false, shift = false, play = kDefaultPlay;
        bool deleteMarker = false, deleteAllMarkers = false, eraseSplice = false, clearReel = false, undo = false;
    };

    inline constexpr int   kNumInputGainSteps = 4;
    inline constexpr float kInputGainStepDb[kNumInputGainSteps] = { -3.0f, 0.0f, 6.0f, 12.0f };
}
