// Morph: how genes follow each other, and Pitch Scale for the random pitch-up genes.
// Plain C++, no JUCE. Reusable by later plugins. Our own curves, drawn through the landmarks in the manual and meant to
// be tuned by ear (only this file needs to change).
//
// Plain language. A gene is one window of the reel. A new gene is started every so often; "spacing" is how long that
// "every so often" is, as a fraction of one gene's length:
//
//   spacing more than 1   gaps of silence between genes (the far left of the knob)
//   spacing exactly 1     genes follow each other with no gap and no overlap ("1/1", a seamless loop)
//   spacing less than 1   the next gene starts before the last one has ended, so they overlap: 1/2 gives two at a
//                         time ("2/1"), 1/3 gives three ("3/1"), 1/4 gives four.
//
// Knob map (the knob runs 0..1; hardware clock positions in brackets, 7:00 = 0, 12:00 = 0.5, 5:00 = 1):
//   0.00         biggest gap: the next gene starts half a gene length after the last one ended   [7:00]
//   0.18         exactly 1/1, seamless                                                          [about 8:30 to 9:00]
//   0.18 - 0.50  overlap grows until two genes play at a time (2/1)                             [12:00]
//   0.50 - 0.60  more overlap until three play at a time, equally spaced (3/1)                  [1:00]
//   0.60 - 0.75  three at a time; panning fades in over the stretch from 0.55 to 0.75           [2:30]
//   0.75 - 1.00  a fourth gene joins and the spacing tightens to a quarter gene; every gene gets a random pitch-up
//                and a random pan position, both growing as the knob goes round
//
// Pitch Scale (Off + 28 scales) decides which pitch-ups are possible. Off: any amount up to the range. A scale: only
// notes of that scale, counted up from the note Vari-Speed is playing now (the root). Because every pitch-up is "so many
// semitones above what the tape is doing", the root is automatically the current Vari-Speed note.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace phe::dsp
{
    inline constexpr float kMorphSeamless      = 0.18f;    // knob position of 1/1
    inline constexpr float kMorphTwoAtOnce     = 0.50f;    // knob position of 2/1
    inline constexpr float kMorphThreeAtOnce   = 0.60f;    // knob position of 3/1
    inline constexpr float kMorphFourthGene    = 0.75f;    // beyond this: a fourth gene, random pitch and pan
    inline constexpr float kMorphIntegerZone   = 0.008f;   // knob distance around 1/1, 2/1, 3/1 that counts as exactly that
    inline constexpr float kMorphMaxGap        = 0.5f;     // biggest gap, as a fraction of a gene length
    inline constexpr float kMorphMaxPitchUp    = 24.0f;    // semitones: the highest pitch-up at the end of the knob
    inline constexpr int   kMaxGeneVoices      = 4;        // the most genes that play at the same moment (more are never needed)

    struct MorphSettings
    {
        float spacing = 1.0f;           // start-to-start distance of genes as a fraction of one gene's length
        float voiceGain = 1.0f;         // each gene is turned down when several play at once (so the sum stays steady)
        float panAmount = 0.0f;         // 0..1: how far genes are spread across the stereo picture
        float panRandom = 0.0f;         // 0..1: how much of that spread is random instead of a fixed rotation
        float pitchRange = 0.0f;        // semitones: random pitch-up goes from 0 up to this
        float genesAtOnce = 1.0f;       // 1 / spacing when overlapping (1 or more), 1 for gaps: for read-outs
        float gapFraction = 0.0f;       // size of the gap as a fraction of a gene (0 when there is none)
        bool  integerPoint = false;     // sitting on exactly 1/1, 2/1 or 3/1
    };

    inline float smooth01 (float x, float lo, float hi)
    {
        if (x <= lo) return 0.0f;
        if (x >= hi) return 1.0f;
        const float t = (x - lo) / (hi - lo);
        return t * t * (3.0f - 2.0f * t);
    }

    inline MorphSettings morphFromKnob (float knob)
    {
        MorphSettings m;
        float k = std::min (1.0f, std::max (0.0f, std::isfinite (knob) ? knob : kMorphSeamless));

        // Exactly 1/1, 2/1 and 3/1 are small flat zones, so they can be found by feel and by automation.
        for (float point : { kMorphSeamless, kMorphTwoAtOnce, kMorphThreeAtOnce })
            if (std::fabs (k - point) <= kMorphIntegerZone)
            {
                k = point;
                m.integerPoint = true;
            }

        if (k < kMorphSeamless)
        {
            m.gapFraction = kMorphMaxGap * (1.0f - k / kMorphSeamless);
            m.spacing = 1.0f + m.gapFraction;
        }
        else if (k <= kMorphTwoAtOnce)
            m.spacing = 1.0f - 0.5f * (k - kMorphSeamless) / (kMorphTwoAtOnce - kMorphSeamless);
        else if (k <= kMorphThreeAtOnce)
            m.spacing = 0.5f - (0.5f - 1.0f / 3.0f) * (k - kMorphTwoAtOnce) / (kMorphThreeAtOnce - kMorphTwoAtOnce);
        else if (k <= kMorphFourthGene)
            m.spacing = 1.0f / 3.0f;
        else
            m.spacing = 1.0f / 3.0f - (1.0f / 3.0f - 0.25f) * (k - kMorphFourthGene) / (1.0f - kMorphFourthGene);

        m.genesAtOnce = std::max (1.0f, 1.0f / m.spacing);
        m.voiceGain = 1.0f / std::sqrt (m.genesAtOnce);       // equal power: several genes together are not louder

        m.panAmount = smooth01 (k, 0.55f, kMorphFourthGene);
        const float beyond = std::max (0.0f, (k - kMorphFourthGene) / (1.0f - kMorphFourthGene));   // 0..1 past 2:30
        m.panRandom = beyond;
        m.pitchRange = kMorphMaxPitchUp * beyond;
        return m;
    }

    // A tiny fast random number source for the audio thread (no memory, no locks). Same numbers every run.
    class Random
    {
    public:
        void seed (uint32_t s) { state = s != 0 ? s : 0x9E3779B9u; }
        uint32_t nextInt()
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return state;
        }
        float next01() { return (float) (nextInt() >> 8) / 16777216.0f; }          // 0 <= x < 1
        float nextSigned() { return 2.0f * next01() - 1.0f; }                      // -1 <= x < 1
    private:
        uint32_t state = 0x9E3779B9u;
    };

    // A random pitch-up in semitones (0 up to 'range'). With a scale (12-bit mask, bit n = n semitones above the root;
    // 0 = Off) it is one of the notes of the scale, over as many octaves as the range reaches; the root itself (0) is
    // always possible. With no scale it is any amount.
    inline float randomPitchUp (Random& rng, float range, int scaleMask)
    {
        if (! (range > 0.0f))
            return 0.0f;

        if ((scaleMask & 0xFFF) == 0)
            return range * rng.next01();

        // Count the scale notes that fit under the range (across octaves), then pick one of them.
        int candidates[12 * 3];
        int count = 0;
        for (int octave = 0; octave < 3 && count < 12 * 3; ++octave)
            for (int n = 0; n < 12 && count < 12 * 3; ++n)
                if ((scaleMask >> n) & 1)
                {
                    const int semis = octave * 12 + n;
                    if ((float) semis <= range)
                        candidates[count++] = semis;
                }
        if (count == 0)
            return 0.0f;
        return (float) candidates[rng.nextInt() % (uint32_t) count];
    }
}
