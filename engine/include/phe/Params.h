// The control values the engine understands: one plain struct, no plugin code.
//
// The plugin wrapper fills this in from the controls (sliders now, the styled panel later, Logic automation)
// and hands it to the engine. The engine never sees sliders, Logic or JUCE. A later plugin can have its own
// Params and reuse everything else.
//
// Knobs run 0.0 to 1.0. What each position means (seconds, semitones, percent) is decided inside the engine
// when each control gets its sound, in later milestones. In milestone 2 the engine ignores all of this.

#pragma once

namespace phe
{
    struct Params
    {
        // Knobs, 0..1 (the panel's eight knobs, in panel order)
        float inputLevel   = 0.5f;
        float soundOnSound = 0.5f;
        float geneSize     = 0.5f;
        float slide        = 0.5f;
        float variSpeed    = 0.5f;
        float morph        = 0.5f;
        float organize     = 0.5f;
        float outputLevel  = 0.5f;

        // Menus
        int inputGainStep = 1;     // 0..3 = -3, 0, +6, +12 dB (see kInputGainStepDb)
        int pitchSnap     = 0;     // 0 Off, 1 Steps, 2 Semitones
        int pitchScale    = 0;     // 0 Off, 1..28 = the scales in core/Scales.h

        // Buttons (on = true). Whether each one is held, toggled or reacts to a click is decided
        // when it gets its function.
        bool rec = false, recNewSplice = false, splice = false, shift = false, play = false;
        bool deleteMarker = false, deleteAllMarkers = false, eraseSplice = false, clearReel = false, undo = false;
    };

    inline constexpr int   kNumInputGainSteps = 4;
    inline constexpr float kInputGainStepDb[kNumInputGainSteps] = { -3.0f, 0.0f, 6.0f, 12.0f };
}
