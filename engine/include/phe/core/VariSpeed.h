// Tape-head speed: the Vari-Speed knob curve and Pitch Snap. Plain C++, no JUCE. Reusable by later plugins.
//
// The knob (0..1) has "12:00" in the middle, like a tape machine's speed control:
//
//   0.50          stopped
//   0.75 / 0.25   exactly original speed, forward / backward   (the "2:30" and "9:30" positions)
//   1.00 / 0.00   the top end: one octave up (+12 semitones), forward / backward
//   between 0.50 and 0.75 (and 0.25 and 0.50): slower than original. The pitch falls down to 26 semitones below
//                 original speed and then the tape brakes smoothly to a stop. More knob travel is spent on the slow
//                 speeds than on the fast ones, so braking by hand or by automation feels smooth.
//
// This is our own curve, drawn through the landmarks above; it can be retuned by ear without touching anything else.
//
// Pitch Snap (Off / Steps / Semitones) makes the knob land on musical notes instead of smooth speeds. Steps uses the
// same list as the other particledots plugins (octaves and fifths: 0, 7, 12, 19, 24, ... mirrored below zero), so on
// this knob the resting places are -24 -19 -12 -7 0 +7 +12. Snap works across the pitched range (+12 down to -26
// semitones); in the last stretch before the stop position it lets go, so braking to a stop stays smooth.

#pragma once

#include <algorithm>
#include <cmath>

namespace phe::dsp
{
    inline constexpr float kVariSpeedUpSemitones   = 12.0f;
    inline constexpr float kVariSpeedDownSemitones = 26.0f;
    inline constexpr float kVariSpeedStopZone      = 0.004f;   // knob distance from the middle that counts as stopped (of 0..1 half-range)
    inline constexpr float kVariSpeedUnityZone     = 0.004f;   // knob distance from "original speed" that counts as exactly 1x

    enum PitchSnapMode { kSnapOff = 0, kSnapSteps = 1, kSnapSemitones = 2 };

    // The resting places of Steps mode (the shared particledots list; mirrored below zero).
    inline constexpr float kSnapStepList[] = { 0.0f, 7.0f, 12.0f, 19.0f, 24.0f, 31.0f, 36.0f, 43.0f, 48.0f };

    inline float snapSemitones (float semitones, int mode)
    {
        if (mode == kSnapSemitones)
            return std::round (semitones);
        if (mode != kSnapSteps)
            return semitones;

        const float a = std::fabs (semitones);
        float best = kSnapStepList[0];
        for (float step : kSnapStepList)
            if (std::fabs (a - step) < std::fabs (a - best))
                best = step;
        return semitones < 0.0f ? -best : best;
    }

    struct VariSpeedResult
    {
        float rate = 1.0f;          // signed: tape-speed multiplier, negative = backwards, 0 = stopped
        float semitones = 0.0f;     // pitch change of the speed (0 at original speed); meaningless when stopped
        bool  stopped = false;
        bool  snapped = false;      // Pitch Snap moved the speed to a note
    };

    // The speed the knob asks for, with Pitch Snap applied. 'pitchSnap': 0 Off, 1 Steps, 2 Semitones.
    inline VariSpeedResult variSpeed (float knob, int pitchSnap)
    {
        VariSpeedResult r;
        const float x = std::min (1.0f, std::max (0.0f, std::isfinite (knob) ? knob : 0.5f));
        const float offset = 2.0f * x - 1.0f;                // -1 .. +1, 0 in the middle
        const float d = std::fabs (offset);                  // 0 (stopped) .. 1 (the end of the travel)
        const float sign = offset >= 0.0f ? 1.0f : -1.0f;

        if (d < kVariSpeedStopZone)
        {
            r.rate = 0.0f;
            r.stopped = true;
            r.semitones = -kVariSpeedDownSemitones;
            return r;
        }

        if (std::fabs (d - 0.5f) < kVariSpeedUnityZone)
        {
            r.rate = sign;                                   // exactly original speed
            r.semitones = 0.0f;
            return r;
        }

        float raw, semitones;
        if (d >= 0.5f)
        {
            semitones = 2.0f * kVariSpeedUpSemitones * (d - 0.5f);        // 0 .. +12, even steps of pitch along the knob
            raw = std::exp2 (semitones / 12.0f);
        }
        else
        {
            // From original speed (t = 0) to the stop position (t = 1): the pitch falls evenly to -26 semitones,
            // while a brake (1 - t^8) brings the speed smoothly down to zero at the very end.
            const float t = (0.5f - d) / (0.5f - kVariSpeedStopZone);
            const float brake = 1.0f - std::pow (t, 8.0f);
            raw = std::exp2 (-kVariSpeedDownSemitones * t / 12.0f) * brake;
            semitones = 12.0f * std::log2 (std::max (raw, 1.0e-6f));
        }

        r.semitones = semitones;
        r.rate = sign * raw;

        if ((pitchSnap == kSnapSteps || pitchSnap == kSnapSemitones)
            && semitones <= kVariSpeedUpSemitones + 0.001f && semitones >= -kVariSpeedDownSemitones)
        {
            const float snapped = snapSemitones (semitones, pitchSnap);
            r.semitones = snapped;
            r.rate = sign * std::exp2 (snapped / 12.0f);
            r.snapped = true;
        }
        return r;
    }

    inline float variSpeedRate (float knob, int pitchSnap) { return variSpeed (knob, pitchSnap).rate; }
}
