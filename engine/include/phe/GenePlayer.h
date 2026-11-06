// The tape head and the gene player (milestone 5; Morph and Pitch Scale added in milestone 6; splices in milestone 7).
//
// Plain language: the reel is a strip of tape, cut into splices by markers. The play head reads the selected splice at
// the speed Vari-Speed asks for (forwards, backwards, faster, slower, stopped). What it plays at any moment is a "gene":
// a window cut out of that splice. (With no markers the splice is the whole reel.)
//
//   Slide      says where the gene starts in the splice (0 = the start of the splice).
//   Gene-Size  says how long the gene lasts. Fully left it is the whole splice (one lap). Turned up it gets shorter and
//              shorter, down to a thousandth of a second. A gene's length is a length of TIME, so changing the speed
//              does not change it; a faster tape just covers more of the reel inside the same time.
//   A new gene starts every so often, always from the Slide position (so a short gene repeats like a grain loop;
//   Slide chooses which grain). Morph decides how often: with a gap between genes, exactly as each one ends (1/1, a
//   seamless loop), or earlier, so that two, three and then four copies of the gene play at the same time, each one
//   started a little later than the one before. Far round the Morph knob every new gene also gets a random pitch-up
//   (limited to the notes of the Pitch Scale, if one is chosen) and a random place in the stereo picture.
//   Each gene has a very short fade at both ends so the joins do not click.
//
// Each gene remembers the splice it was started in. When another splice is selected (Organize, Shift) the genes already
// playing finish first, and the next gene to start is in the new splice. That is the "waits until the splice or gene
// ends" behaviour of the module.
//
// While the tape is stopped (Vari-Speed in the middle) nothing plays. Moving Slide while stopped still scrubs.
//
// Clock features (milestone 10). The engine hands the player a tick now and then (from Logic's tempo, see core/HostClock.h):
//   Gene Shift    every tick moves on to the next gene of the splice (one gene length further along) and starts it at once;
//                 the gene then repeats, as usual, until the next tick. Needs a Gene-Size shorter than the whole splice.
//   Time Stretch  the place in the splice where each new gene starts travels through the splice at the pace the clock sets
//                 (the whole splice lasts a chosen number of ticks), whatever Vari-Speed says. Vari-Speed then changes only
//                 the pitch. Also needs a Gene-Size shorter than the whole splice.
//   Retrigger     on the chosen ticks the loop is pulled back to the Slide position and starts again.
// With no clock feature chosen none of this does anything, and the sound is exactly what it was before.
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
            int spliceStart = 0;            // the splice the next gene starts in: where it begins in the reel (samples) ...
            int spliceLength = 0;           // ... and how long it is (0 = the whole reel, the same as no markers)
            bool smoothWindow = false;      // Gene Window option: short genes fade in and out over their whole length
            int chordMode = 0;              // Morph Chord option: 0 = random pitch-ups, otherwise an entry of dsp::kMorphChords
            int clockFeature = 0;           // 0 none, 1 Gene Shift, 2 Time Stretch (only used while genes are shorter than the splice)
        };

        // What the clock says at one sample (nothing in particular when no clock is passed to step()).
        struct ClockIn
        {
            bool shiftTick = false;         // a tick, and Gene Shift is the feature in use
            bool retriggerTick = false;     // a tick on which the loop is pulled back to the Slide position
            double stretchPhase = 0.0;      // 0..1: how far through its trip the splice is (Time Stretch)
        };

        // What came out of one step. 'heard' has the gene fades; 'record' is what goes back onto the reel when
        // recording over it (the same, except that a whole-reel lap keeps its fade out of the recording).
        struct Out { float heardL = 0.0f, heardR = 0.0f, recordL = 0.0f, recordR = 0.0f; };

        // For the window: where the current gene is.
        struct View
        {
            double windowStart = 0.0;       // samples, the left edge of the gene's window in the reel
            double windowSpan = 0.0;        // samples the window covers (may run past the end of its splice and wrap round inside it)
            double spliceStart = 0.0;       // the splice the newest gene is in (samples) ...
            double spliceLength = 0.0;      // ... and its length
            double geneSeconds = 0.0;       // 0 = whole reel
            float rate = 0.0f;
            int voices = 0;                 // how many genes are playing at this moment
        };

        void prepare (double sampleRate);

        void setTargets (const Targets& t) { targets = t; }

        // Starts a fresh gene from the Slide position (in the selected splice), with the controls exactly as they are
        // now (no gliding in).
        void start (const Reel& reel);
        void stop();
        bool isPlaying() const { return playing; }

        // Every gene now playing fades out over a millisecond or two and none starts again (Play Mode "momentary").
        void release();

        // Moves straight to the selected splice (used when a new splice has just been recorded): the genes now playing
        // fade out over a millisecond or two instead of cutting off, and a fresh gene starts in the selected splice.
        void switchSplice (const Reel& reel);

        // One output sample from the reel, then moves the head on. Does nothing and returns silence when stopped.
        // 'clk' (optional) is what the clock says for this sample.
        Out step (const Reel& reel, const ClockIn* clk = nullptr);

        // The position in the reel (in samples) the newest gene is reading now (the Slide position when none plays).
        double headPosition (const Reel& reel) const;

        View view (const Reel& reel) const;

    private:
        // One gene that is playing.
        struct Voice
        {
            bool active = false;
            double q = 0.0;          // how far this gene's head has moved since it began (reel samples, signed; includes its pitch)
            double qBase = 0.0;      // the same at the tape's own speed (measures a whole-splice lap, whatever the pitch)
            double start = 0.0;      // the splice this gene was started in: first sample ...
            double len = 1.0;        // ... and length in samples
            double age = 0.0;        // time since it began (samples); only runs while the tape moves
            double scanBase = 0.0;   // where in the splice this gene started, on top of Slide (Gene Shift / Time Stretch; else 0)
            float mult = 1.0f;       // this gene's own speed factor (the random pitch-up)
            float gainL = 1.0f, gainR = 1.0f;     // its place in the stereo picture and its share of the level
            bool releasing = false;               // fading out (a new splice took over)
            float releaseGain = 1.0f;
        };
        static constexpr int kNumVoices = dsp::kMaxGeneVoices + 2;    // two spare, for the moment when genes change over

        double wrap (double position, double length) const;
        void spawnVoice (double overshoot, bool whole, const Reel& reel);
        void targetSplice (const Reel& reel, double& start, double& len) const;
        int activeVoices() const;
        double baseFor (double len) const;                  // where a gene started now would begin, beyond Slide (clock features)
        void restartFromSlide (const Reel& reel);           // Play Retrigger
        void shiftToNextGene (const Reel& reel, double spliceLength);   // Gene Shift

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

        // Clock features
        double shiftOffset = 0.0;       // Gene Shift: how far the genes have been shifted along the splice (reel samples)
        double stretchPhaseNow = 0.0;   // Time Stretch: the latest phase the clock gave
        bool justStarted = false;       // the gene just started: a tick on the very same sample must not move it again
    };
}
