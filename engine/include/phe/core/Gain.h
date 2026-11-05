// Small reusable level helpers: decibels, the level-knob curve, the Sound On Sound mix law and loop-edge fades.
// Plain C++, no JUCE. Later plugins can use these as they are.

#pragma once

#include <algorithm>
#include <cmath>

namespace phe::dsp
{
    inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }

    // The curve of the Input Level and Output Level knobs (position 0..1):
    //   0.00        silent
    //   0.00..0.75  falls from -60 dB up to 0 dB (the very bottom fades to true silence)
    //   0.75        0 dB (no change)
    //   0.75..1.00  rises to +6 dB
    // Our own curve; the knob positions can be retuned by ear later without touching anything else.
    inline float levelKnobToGain (float position)
    {
        const float x = std::min (1.0f, std::max (0.0f, position));
        if (x <= 0.0f)
            return 0.0f;

        const float db = x <= 0.75f ? -60.0f * (1.0f - x / 0.75f)
                                    :   6.0f * (x - 0.75f) / 0.25f;
        float gain = dbToGain (db);
        if (x < 0.02f)
            gain *= x / 0.02f;
        return gain;
    }

    // The same thing in decibels, for read-outs. Returns -120 for silence.
    inline float levelKnobToDb (float position)
    {
        const float g = levelKnobToGain (position);
        return g <= 1.0e-6f ? -120.0f : 20.0f * std::log10 (g);
    }

    // Sound On Sound mix law (knob 0..1): how much of the live input and how much of the reel loop is used.
    //   0.0  live input only        0.5  both at full level        1.0  reel loop only
    // At the middle both are at full level, so a loop can be layered over without it fading away.
    inline void sosGains (float position, float& liveGain, float& loopGain)
    {
        const float s = std::min (1.0f, std::max (0.0f, position));
        liveGain = std::min (1.0f, 2.0f * (1.0f - s));
        loopGain = std::min (1.0f, 2.0f * s);
    }

    // A gentle safety limit for what is written back onto tape when recording over audio that is already there (Time Lag
    // Accumulation). Below 0.8 (about -2 dB) the signal passes exactly as it is; above that it bends smoothly towards, but
    // never reaches, 1.0. Pass after pass can then pile up without the reel ever running away or going past full scale.
    inline float softLimit (float x)
    {
        constexpr float knee = 0.8f, room = 1.0f - knee;
        const float a = std::fabs (x);
        if (! (a > knee))
            return x;
        const float y = knee + room * std::tanh ((a - knee) / room);
        return x < 0.0f ? -y : y;
    }

    // A very short fade at each end of a stretch of 'length' samples, so a loop does not click where its end
    // meets its start. 'position' is where we are inside the stretch. Returns 0..1.
    inline float edgeFade (double position, int length, int fadeLength)
    {
        if (fadeLength * 2 > length)
            fadeLength = length / 2;
        if (fadeLength <= 0)
            return 1.0f;

        const double fromStart = position;
        const double fromEnd   = (double) length - position;
        const double nearest   = std::min (fromStart, fromEnd);
        if (nearest >= (double) fadeLength)
            return 1.0f;

        const float t = (float) (std::max (0.0, nearest) / (double) fadeLength);
        return t * t * (3.0f - 2.0f * t);          // smooth S-shaped fade
    }
}
