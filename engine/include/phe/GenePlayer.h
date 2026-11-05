// The tape head and the gene player (milestone 5; Morph and Pitch Scale added in milestone 6).
//
// Plain language: the reel is a strip of tape. The play head reads it at the speed Vari-Speed asks for (forwards,
// backwards, faster, slower, stopped). What it plays at any moment is a "gene": a window cut out of the reel.
//
//   Slide      says where the gene starts in the reel (0 = the start of the reel).
//   Gene-Size  says how long the gene lasts. Fully left it is the whole reel (one lap). Turned up it gets shorter and
//              shorter, down to a thousandth of a second. A gene's length is a length of TIME, so changing the speed
//              does not change it; a faster tape just covers more of the reel inside the same time.
//   A new gene starts every so often, always from the Slide position (so a short gene repeats like a grain loop;
//   Slide chooses which grain). Morph decides how often: with a gap between genes, exactly as each one ends (1/1, a
//   seamless loop), or earlier, so that two, three and then four copies of the gene play at the same time, each one
//   started a little later than the one before. Far round the Morph knob every new gene also gets a random pitch-up
//   (limited to the notes of the Pitch Scale, if one is chosen) and a random place in the stereo picture.
//   Each gene has a very short fade at both ends so the joins do not click.
//
// While the tape is stopped (Vari-Speed in the middle) nothing plays. Moving Slide while stopped still scrubs.
//
// Plain C++17. No JUCE. Knows nothing about knobs or the plugin: it is handed numbers.

#pragma once

#include "Reel.h"
#include "core/Morph.h"

#include <algorithm>
#include <cmath>

namespace phe
{
    // Gene-Size knob (0..1) to the length of a gene in seconds, for a splice that lasts 'spliceSeconds'.
    // Returns 0 for "the whole splice" (the knob fully left, with a little room so it can be reached exactly).
    // Otherwise the length falls smoothly (in equal musical-style steps along the knob) from the whole splice down to
    // kMinGeneSeconds. Our own curve; tune by ear.
    inline constexpr float  kGeneFullZone   = 0.01f;
    inline constexpr double kMinGeneSeconds = 0.001;

    inline double geneSeconds (float knob, double spliceSeconds)
    {
        const float k = std::min (1.0f, std::max (0.0f, std::isfinite (knob) ? knob : 0.0f));
        if (k <= kGeneFullZone || spliceSeconds <= kMinGeneSeconds * 2.0)
            return 0.0;

        const double u = (double) (k - kGeneFullZone) / (double) (1.0f - kGeneFullZone);
        return spliceSeconds * std::pow (kMinGeneSeconds / spliceSeconds, u);
    }

    class GenePlayer
    {
    public:
        struct Targets
        {
            float rate = 1.0f;              // signed tape speed (from Vari-Speed + Pitch Snap); 0 = stopped
            double geneSeconds = 0.0;       // length of a gene in seconds; 0 = the whole reel (one lap)
            float slide = 0.0f;             // 0..1: where the gene starts in the reel
            bool playWanted = true;         // Play switch: when off the genes in progress are the last ones
            dsp::MorphSettings morph;       // how genes follow each other (from the Morph knob)
            int scaleMask = 0;              // Pitch Scale as a 12-note mask (0 = Off); see core/Scales.h
        };

        // What came out of one step. 'heard' has the gene fades; 'record' is what goes back onto the reel when
        // recording over it (the same, except that a whole-reel lap keeps its fade out of the recording).
        struct Out { float heardL = 0.0f, heardR = 0.0f, recordL = 0.0f, recordR = 0.0f; };

        // For the window: where the current gene is.
        struct View
        {
            double windowStart = 0.0;       // samples, the left edge of the gene's window in the reel
            double windowSpan = 0.0;        // samples the window covers (may run past the end and wrap round)
            double geneSeconds = 0.0;       // 0 = whole reel
            float rate = 0.0f;
            int voices = 0;                 // how many genes are playing at this moment
        };

        void prepare (double sampleRate);

        void setTargets (const Targets& t) { targets = t; }

        // Starts a fresh gene from the Slide position, with the controls exactly as they are now (no gliding in).
        void start();
        void stop();
        bool isPlaying() const { return playing; }

        // One output sample from the reel, then moves the head on. Does nothing and returns silence when stopped.
        Out step (const Reel& reel);

        // The position in the reel (in samples) the newest gene is reading now (the Slide position when none plays).
        double headPosition (const Reel& reel) const;

        View view (const Reel& reel) const;

    private:
        // One gene that is playing.
        struct Voice
        {
            bool active = false;
            double q = 0.0;          // how far this gene's head has moved since it began (reel samples, signed; includes its pitch)
            double qBase = 0.0;      // the same at the tape's own speed (measures a whole-reel lap, whatever the pitch)
            double age = 0.0;        // time since it began (samples); only runs while the tape moves
            float mult = 1.0f;       // this gene's own speed factor (the random pitch-up)
            float gainL = 1.0f, gainR = 1.0f;     // its place in the stereo picture and its share of the level
        };
        static constexpr int kNumVoices = dsp::kMaxGeneVoices + 2;    // two spare, for the moment when genes change over

        double wrap (double position, double length) const;
        void spawnVoice (double overshoot, bool whole);
        int activeVoices() const;

        double sampleRate = 48000.0;
        float rateGlide = 0.01f, slideGlide = 0.01f, gateGlide = 0.01f;
        int fadeSamples = 72;

        Targets targets;
        bool playing = false;

        Voice voices[kNumVoices];
        int newest = 0;                 // the voice started last
        double clock = 0.0;             // time to the next gene: whole-reel genes count distance (signed), shorter ones count time
        long spawnCount = 0;            // genes started so far (rotates the fixed stereo positions)
        dsp::Random rng;
        double rateNow = 0.0, slideNow = 0.0, gate = 1.0;
    };
}
