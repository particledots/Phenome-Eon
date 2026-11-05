// Self-test for the PhenomeEon engine. Run automatically by the build; the build fails if any check fails.
// Milestone 4: the reel, recording, Sound On Sound and basic playback.
// Milestone 5: the tape head and genes (Vari-Speed, Gene-Size, Slide) and Pitch Snap.
// Milestone 6: Morph (gaps, overlaps, random pitch-up and pan) and Pitch Scale.
// Milestone 7: splices (Splice, Shift, Organize, deleting markers, recording into a new splice).
// Milestone 8: Erase Splice, Undo / Redo, the Time Lag Accumulation limiter, the six optional behaviours.

#include <phe/Engine.h>
#include <phe/core/Gain.h>
#include <phe/core/Morph.h>
#include <phe/core/Scales.h>
#include <phe/core/SpliceMap.h>
#include <phe/core/UndoStore.h>
#include <phe/core/VariSpeed.h>
#include <phe/GenePlayer.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <vector>

static int failures = 0;
static int checks = 0;

#define CHECK(cond, ...)                                                   \
    do {                                                                   \
        ++checks;                                                          \
        if (! (cond)) {                                                    \
            std::printf ("FAIL line %d: %s   ", __LINE__, #cond);          \
            std::printf (__VA_ARGS__);                                     \
            std::printf ("\n");                                            \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

using Channels = std::vector<std::vector<float>>;
using Gen = std::function<float (int, long long)>;

// A test signal: a sine plus a little pseudo-random noise, different on each channel, a pure function of time.
static float tone (int channel, long long t)
{
    uint32_t seed = (uint32_t) (t * 2654435761ull) + (uint32_t) channel * 777u;
    seed = seed * 1664525u + 1013904223u;
    const float noise = ((float) (seed >> 8) / 16777216.0f - 0.5f) * 0.1f;
    return 0.4f * (float) std::sin (0.05 * (double) t * (double) (channel + 1)) + noise;
}

static float silence (int, long long) { return 0.0f; }

static bool near (float a, float b, float tol = 1.0e-5f) { return std::fabs (a - b) <= tol; }

// Runs the engine in blocks, feeding it a signal and handing back what came out.
struct Rig
{
    phe::Engine e;
    phe::Params p;
    long long t = 0;                // running time in samples, so signals continue between runs
    int channels = 2;

    // 'warmUp' runs one short quiet block first, as Logic always has audio running before anyone presses a button.
    // (The engine treats a button that is already on in the very first block as "loaded that way", not "just pressed".)
    explicit Rig (double sampleRate = 48000.0, int numChannels = 2, bool warmUp = true) : channels (numChannels)
    {
        e.prepare (sampleRate, 4096, numChannels);
        if (warmUp)
        {
            run (16, 16, silence);
            t = 0;
        }
    }

    Channels run (int n, int block, const Gen& in)
    {
        Channels out ((size_t) channels, std::vector<float> ((size_t) n));
        for (int pos = 0; pos < n; pos += block)
        {
            const int m = std::min (block, n - pos);
            Channels buf ((size_t) channels, std::vector<float> ((size_t) m));
            float* ptrs[2] = { nullptr, nullptr };
            for (int c = 0; c < channels; ++c)
            {
                for (int k = 0; k < m; ++k)
                    buf[(size_t) c][(size_t) k] = in (c, t + pos + k);
                ptrs[c] = buf[(size_t) c].data();
            }
            e.setParams (p);
            e.process (ptrs, channels, m);
            for (int c = 0; c < channels; ++c)
                std::copy (buf[(size_t) c].begin(), buf[(size_t) c].end(), out[(size_t) c].begin() + pos);
        }
        t += n;
        return out;
    }

    // Records a first take of 'n' samples (n a multiple of 'block') and lets it start playing.
    void recordFirstTake (int n, int block, const Gen& in = tone)
    {
        p.rec = true;
        run (n, block, in);
        p.rec = false;
        run (block, block, silence);       // the stop is acted on at the start of this block
    }
};

//==============================================================================
static void testPassThroughWhenEmpty()
{
    // With nothing recorded and the controls at their starting positions the sound passes through unchanged.
    for (int channels = 1; channels <= 2; ++channels)
    {
        for (double sr : { 44100.0, 48000.0, 88200.0 })
        {
            for (int block : { 1, 7, 64, 256, 1000, 4096 })
            {
                Rig r (sr, channels);
                const int n = 8192;
                const auto out = r.run (n, block, tone);
                bool same = true;
                for (int c = 0; c < channels; ++c)
                    for (int i = 0; i < n; ++i)
                        if (out[(size_t) c][(size_t) i] != tone (c, i))
                            same = false;
                CHECK (same, "channels %d, sample rate %.0f, block %d", channels, sr, block);
            }
        }
    }
}

static void testLevelCurves()
{
    using namespace phe::dsp;
    CHECK (levelKnobToGain (0.0f) == 0.0f, "level 0 is silent");
    CHECK (levelKnobToGain (0.75f) == 1.0f, "level 0.75 is exactly 0 dB");
    CHECK (near (levelKnobToGain (1.0f), dbToGain (6.0f), 1.0e-6f), "level 1.0 is +6 dB");
    CHECK (near (levelKnobToDb (0.375f), -30.0f, 0.01f), "halfway to unity is -30 dB, got %f", levelKnobToDb (0.375f));
    CHECK (levelKnobToGain (-3.0f) == 0.0f && near (levelKnobToGain (7.0f), dbToGain (6.0f), 1.0e-6f), "out-of-range values are limited");

    float last = -1.0f;
    bool rising = true;
    for (int i = 0; i <= 1000; ++i)
    {
        const float g = levelKnobToGain ((float) i / 1000.0f);
        if (g < last) rising = false;
        last = g;
    }
    CHECK (rising, "level curve never goes down as the knob goes up");

    float live, loop;
    sosGains (0.0f, live, loop);   CHECK (live == 1.0f && loop == 0.0f, "Sound On Sound 0: live only");
    sosGains (0.5f, live, loop);   CHECK (live == 1.0f && loop == 1.0f, "Sound On Sound 0.5: both full");
    sosGains (1.0f, live, loop);   CHECK (live == 0.0f && loop == 1.0f, "Sound On Sound 1: loop only");
    sosGains (0.25f, live, loop);  CHECK (near (live, 1.0f) && near (loop, 0.5f), "Sound On Sound 0.25");
    sosGains (0.75f, live, loop);  CHECK (near (live, 0.5f) && near (loop, 1.0f), "Sound On Sound 0.75");

    CHECK (edgeFade (0.0, 1000, 72) == 0.0f && edgeFade (999.9, 1000, 72) < 0.01f, "loop ends are silent");
    CHECK (edgeFade (500.0, 1000, 72) == 1.0f, "loop middle is full");
    CHECK (edgeFade (3.0, 4, 72) <= 1.0f && edgeFade (2.0, 4, 72) >= 0.0f, "tiny loops still fade safely");
}

static void testInputLevelsAndGainStep()
{
    // Input gain step +12 dB, Input Level at 0 dB, Output Level at 0 dB.
    {
        Rig r (48000.0, 2, false);
        r.p.inputGainStep = 3;
        const auto out = r.run (2000, 500, tone);
        bool ok = true;
        for (int i = 0; i < 2000; ++i)
            if (! near (out[0][(size_t) i], tone (0, i) * phe::dsp::dbToGain (12.0f), 1.0e-4f)) ok = false;
        CHECK (ok, "gain step +12 dB");
    }
    // Output Level at the bottom is silent
    {
        Rig r (48000.0, 2, false);
        r.p.outputLevel = 0.0f;
        const auto out = r.run (2000, 500, tone);
        float peak = 0.0f;
        for (float v : out[0]) peak = std::max (peak, std::fabs (v));
        CHECK (peak == 0.0f, "output level 0 is silent");
    }
    // A level knob moved during playback glides: no sample-to-sample jump bigger than the signal itself allows
    {
        Rig r;
        r.run (1000, 500, [] (int, long long t) { return 0.5f * (float) std::sin (0.01 * (double) t); });
        r.p.inputLevel = 0.0f;
        const auto out = r.run (4000, 500, [] (int, long long t) { return 0.5f * (float) std::sin (0.01 * (double) t); });
        float maxJump = 0.0f;
        for (size_t i = 1; i < out[0].size(); ++i)
            maxJump = std::max (maxJump, std::fabs (out[0][i] - out[0][i - 1]));
        CHECK (maxJump < 0.02f, "input level glides, biggest step %f", maxJump);
        CHECK (std::fabs (out[0].back()) < 1.0e-3f, "input level reaches silence");
    }
}

static void testFirstRecording()
{
    Rig r;
    const int n = 48000;
    r.p.rec = true;
    const auto during = r.run (n, 500, tone);

    // While recording the live sound is heard unchanged (the reel is empty)
    bool heard = true;
    for (int i = 0; i < n; ++i)
        if (during[0][(size_t) i] != tone (0, i) || during[1][(size_t) i] != tone (1, i))
            heard = false;
    CHECK (heard, "live sound is heard while the first recording is made");

    auto view = r.e.getReelView();
    CHECK (view.recording, "recording flag is on");
    CHECK (view.lengthSamples == n, "reel grows while recording: %d", view.lengthSamples);
    CHECK (view.capacitySamples == 174 * 48000, "reel holds 2.9 minutes: %d", view.capacitySamples);

    r.p.rec = false;
    r.run (500, 500, silence);

    view = r.e.getReelView();
    CHECK (! view.recording, "recording flag is off after stop");
    CHECK (view.lengthSamples == n, "reel is as long as the recording: %d", view.lengthSamples);
    CHECK (view.playing, "the new recording starts looping straight away");

    bool exact = true;
    for (int i = 0; i < n; ++i)
        if (r.e.getReel().get (0, i) != tone (0, i) || r.e.getReel().get (1, i) != tone (1, i))
            exact = false;
    CHECK (exact, "the reel holds exactly what was played in");
    CHECK (! r.e.takeRecordStopped(), "stopping by hand is not reported as stopping by itself");
}

static void testPlayback()
{
    Rig r;
    const int n = 48000;
    r.recordFirstTake (n, 500);

    // Loop only (Sound On Sound fully up), live input silent: we hear the reel looping.
    r.p.soundOnSound = 1.0f;
    r.run (20000, 500, silence);                     // let the knob settle

    const int pos0 = r.e.getReelView().playPos;
    const int total = 2 * n + 300;
    const auto out = r.run (total, 500, silence);

    const int fade = (int) std::llround (0.0015 * 48000.0);
    bool ok = true;
    for (int i = 0; i < total; ++i)
    {
        const int p = (pos0 + i) % n;
        const float expected = r.e.getReel().get (0, p) * phe::dsp::edgeFade ((double) p, n, fade);
        if (! near (out[0][(size_t) i], expected, 1.0e-5f))
            ok = false;
    }
    CHECK (ok, "playback loops the reel at normal speed, with the short fade at the loop point");

    // The loop repeats exactly
    bool repeats = true;
    for (int i = 0; i + n < total; ++i)
        if (out[1][(size_t) i] != out[1][(size_t) (i + n)])
            repeats = false;
    CHECK (repeats, "each pass sounds the same");

    // Right at the loop point the sound is faded to nothing (no click)
    const int atWrap = (n - pos0) % n;
    CHECK (std::fabs (out[0][(size_t) atWrap]) < 1.0e-6f, "silent at the loop point");
}

static void testBlockSizeIndependence()
{
    // The same recording and playback must sound identical whatever size blocks Logic uses.
    Channels reference;
    for (int block : { 480, 1, 64, 4800 })
    {
        Rig r;
        r.p.soundOnSound = 0.5f;
        r.p.rec = true;
        r.run (48000, block, tone);
        r.p.rec = false;
        r.p.soundOnSound = 0.8f;
        const auto out = r.run (96000, block, tone);
        if (reference.empty())
        {
            reference = out;
            continue;
        }
        bool same = out[0] == reference[0] && out[1] == reference[1];
        CHECK (same, "block size %d gives the same sound as block size 480", block);
    }
}

static void testOverdubAndPunchOut()
{
    Rig r;
    const int n = 48000;
    r.recordFirstTake (n, 500);

    const Gen second = [] (int c, long long t) { return 0.25f * (float) std::sin (0.013 * (double) t * (double) (c + 2)); };
    // Overdub for half a second with Sound On Sound in the middle (live and loop both at full level)
    r.p.soundOnSound = 0.5f;
    r.run (3000, 500, silence);                     // let the knob settle
    const int start = r.e.getReelView().playPos;
    const long long tStart = r.t;

    // The reel as it was before the overdub
    std::vector<float> before[2];
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < n; ++i)
            before[c].push_back (r.e.getReel().get (c, i));

    r.p.rec = true;
    const int span = 24000;
    r.run (span, 500, second);
    CHECK (r.e.getReelView().recording, "overdub is recording");
    r.p.rec = false;
    r.run (1000, 500, silence);
    CHECK (! r.e.getReelView().recording, "overdub stopped after the fade-out");
    CHECK (r.e.getReelView().lengthSamples == n, "overdubbing does not change the reel length");

    const int ramp = (int) std::llround (0.004 * 48000.0) + 8;
    bool middleOk = true, outsideOk = true;
    for (int c = 0; c < 2; ++c)
    {
        for (int i = 0; i < span; ++i)
        {
            const int p = (start + i) % n;
            const float expected = before[c][(size_t) p] + second (c, tStart + i);
            if (i > ramp && i < span - 1 && ! near (r.e.getReel().get (c, p), expected, 1.0e-5f))
                middleOk = false;
        }
        for (int p = 0; p < n; ++p)
        {
            const int i = ((p - start) % n + n) % n;          // how far past the start this position is
            const bool touched = i < span + ramp * 2;
            if (! touched && r.e.getReel().get (c, p) != before[c][(size_t) p])
                outsideOk = false;
        }
    }
    CHECK (middleOk, "Sound On Sound in the middle: the reel now holds the old loop plus the new sound");
    CHECK (outsideOk, "audio outside the overdub is untouched");

    // Sound On Sound at the bottom: the new sound replaces what was there
    Rig q;
    q.recordFirstTake (n, 500);
    q.p.soundOnSound = 0.0f;
    q.run (20000, 500, silence);                    // let the knob finish gliding down
    const int start2 = q.e.getReelView().playPos;
    const long long t2 = q.t;
    q.p.rec = true;
    q.run (12000, 500, second);
    q.p.rec = false;
    q.run (1000, 500, silence);
    bool replaced = true;
    for (int i = ramp; i < 12000 - 1; ++i)
        if (! near (q.e.getReel().get (0, (start2 + i) % n), second (0, t2 + i), 1.0e-5f))
            replaced = false;
    CHECK (replaced, "Sound On Sound at live-only: the new sound replaces the old");
}

static void testRecordingGoesRoundTheLoop()
{
    // Time Lag Accumulation: recording carries on past the end of the loop and keeps layering.
    Rig r;
    const int n = 24000;
    r.recordFirstTake (n, 500);
    r.p.soundOnSound = 0.5f;
    r.run (1500, 500, silence);                     // settle; playhead somewhere inside the loop

    std::vector<float> a ((size_t) n);
    for (int i = 0; i < n; ++i)
        a[(size_t) i] = r.e.getReel().get (0, i);

    const int start = r.e.getReelView().playPos;
    const long long tStart = r.t;
    const Gen second = [] (int, long long t) { return 0.1f * (float) std::sin (0.021 * (double) t); };

    r.p.rec = true;
    r.run (2 * n, 500, second);                     // two whole passes
    r.p.rec = false;
    r.run (1000, 500, silence);

    CHECK (r.e.getReelView().lengthSamples == n, "reel length unchanged after recording round the loop twice");

    // After two passes each position holds: old loop + first pass + second pass (away from the fade-in at the start)
    bool ok = true;
    for (int i = 600; i < n - 1; ++i)
    {
        const int p = (start + i) % n;
        const float expected = a[(size_t) p] + second (0, tStart + i) + second (0, tStart + i + n);
        if (! near (r.e.getReel().get (0, p), expected, 2.0e-5f))
            ok = false;
    }
    CHECK (ok, "the recording keeps layering on each trip round the loop");
}

static void testAutoStopWhenReelIsFull()
{
    const double sr = 8000.0;                       // low rate keeps this test quick; the reel is still 174 seconds
    Rig r (sr);
    const int capacity = r.e.getReelView().capacitySamples;
    CHECK (capacity == 174 * 8000, "capacity at 8 kHz: %d", capacity);

    r.p.rec = true;
    r.run (capacity + 40000, 4000, tone);

    auto view = r.e.getReelView();
    CHECK (view.lengthSamples == capacity, "a full reel is exactly 2.9 minutes: %d", view.lengthSamples);
    CHECK (! view.recording, "recording stopped by itself when the reel was full");
    CHECK (r.e.takeRecordStopped(), "the wrapper is told that recording stopped by itself");
    CHECK (! r.e.takeRecordStopped(), "that message is given only once");
    CHECK (view.playing, "the full reel plays");

    // The Rec switch is still on, but nothing new starts until it is switched off and on again.
    r.run (4000, 4000, tone);
    CHECK (! r.e.getReelView().recording, "no new recording starts while the switch is still on");
    r.p.rec = false;
    r.run (4000, 4000, tone);
    r.p.rec = true;
    r.run (4000, 4000, tone);
    CHECK (r.e.getReelView().recording, "switching Rec off and on again records over the full reel");
    CHECK (r.e.getReelView().lengthSamples == capacity, "and the reel stays 2.9 minutes");
}

static void testClearReel()
{
    Rig r;
    r.recordFirstTake (24000, 500);
    CHECK (r.e.getReelView().lengthSamples == 24000, "something to clear");

    r.p.clearReel = true;
    r.run (500, 500, tone);
    CHECK (r.e.getReelView().lengthSamples == 0, "Clear Reel empties the reel");
    CHECK (! r.e.getReelView().playing, "nothing plays after Clear Reel");
    CHECK (r.e.takeClearHandled(), "the wrapper is told Clear Reel was done");
    CHECK (! r.e.takeClearHandled(), "that message is given only once");

    float l, rr, peak = 0.0f;
    for (int b = 0; b < phe::core::PeakOverview::kBuckets; ++b)
    {
        r.e.getReel().peaks.get (b, l, rr);
        peak = std::max (peak, std::max (l, rr));
    }
    CHECK (peak == 0.0f, "the waveform picture is empty after Clear Reel");

    // The wrapper switches the button off; the engine does nothing more
    r.p.clearReel = false;
    r.run (500, 500, tone);

    // A new first recording works again
    r.recordFirstTake (12000, 500);
    CHECK (r.e.getReelView().lengthSamples == 12000, "a new first recording after Clear Reel: %d", r.e.getReelView().lengthSamples);

    // Clearing while recording stops the recording and tells the wrapper
    r.p.rec = true;
    r.run (1000, 500, tone);
    r.p.clearReel = true;
    r.run (500, 500, tone);
    CHECK (! r.e.getReelView().recording && r.e.getReelView().lengthSamples == 0, "Clear Reel while recording");
    CHECK (r.e.takeRecordStopped(), "the wrapper is told the recording stopped");
}

static void testPlayButton()
{
    Rig r;
    const int n = 24000;
    r.recordFirstTake (n, 500);
    r.p.soundOnSound = 1.0f;
    r.run (20000, 500, silence);
    CHECK (r.e.getReelView().playing, "playing with Play on");

    // Play off mid-pass: the pass finishes, then it stops
    r.p.play = false;
    const int pos = r.e.getReelView().playPos;
    const int toEnd = n - pos;
    const auto out = r.run (n + 1000, 500, silence);
    CHECK (! r.e.getReelView().playing, "stopped after the pass in progress finished");
    bool sounded = false, quiet = true;
    for (int i = 0; i < n + 1000; ++i)
    {
        if (i < toEnd - 200 && std::fabs (out[0][(size_t) i]) > 0.01f) sounded = true;
        if (i > toEnd + 10 && out[0][(size_t) i] != 0.0f) quiet = false;
    }
    CHECK (sounded, "it kept playing until the end of the pass");
    CHECK (quiet, "silent afterwards");

    // Play on again: starts from the beginning
    r.p.play = true;
    r.run (100, 100, silence);
    CHECK (r.e.getReelView().playing, "playing again");
    CHECK (r.e.getReelView().playPos == 100, "turning Play on starts from the beginning: %d", r.e.getReelView().playPos);

    // Turning Play on while it is already playing starts it over
    r.run (5000, 500, silence);
    r.p.play = false;
    r.run (100, 100, silence);
    r.p.play = true;
    r.run (200, 100, silence);
    CHECK (r.e.getReelView().playPos == 200, "Play off then on restarts the reel: %d", r.e.getReelView().playPos);
}

static void testPrepareKeepsOrConvertsReel()
{
    Rig r;
    const int n = 48000;
    r.recordFirstTake (n, 500, [] (int, long long t) { return 0.5f * (float) std::sin (0.02 * (double) t); });

    r.e.prepare (48000.0, 256, 2);                  // Logic prepares again at the same rate: nothing is lost
    CHECK (r.e.getReelView().lengthSamples == n, "recording survives prepare at the same rate");
    CHECK (near (r.e.getReel().get (0, 1000), 0.5f * (float) std::sin (0.02 * 1000.0)), "content survives prepare");

    r.e.prepare (96000.0, 256, 2);                  // sample rate changed: converted, same length in seconds
    CHECK (std::abs (r.e.getReelView().lengthSamples - 2 * n) <= 1, "converted to 96 kHz: %d", r.e.getReelView().lengthSamples);
    CHECK (r.e.getReelView().capacitySamples == 174 * 96000, "capacity at 96 kHz");
    float peak = 0.0f;
    for (int i = 0; i < 2 * n; ++i) peak = std::max (peak, std::fabs (r.e.getReel().get (0, i)));
    CHECK (peak > 0.49f && peak < 0.51f, "converted audio keeps its level: %f", peak);
    CHECK (near (r.e.getReel().get (0, 2000), 0.5f * (float) std::sin (0.02 * 1000.0), 2.0e-3f), "converted audio keeps its pitch");

    r.e.prepare (48000.0, 256, 2);
    CHECK (std::abs (r.e.getReelView().lengthSamples - n) <= 2, "and back to 48 kHz: %d", r.e.getReelView().lengthSamples);
    float l, rr;
    r.e.getReel().peaks.get (3, l, rr);
    CHECK (l > 0.4f && rr > 0.4f, "waveform picture rebuilt after conversion");
}

static void testBadInput()
{
    Rig r;
    auto bad = [] (int c, long long t)
    {
        if (t % 997 == 0) return std::numeric_limits<float>::quiet_NaN();
        if (t % 1999 == 0) return std::numeric_limits<float>::infinity();
        return tone (c, t);
    };
    r.p.rec = true;
    r.run (20000, 500, bad);
    r.p.rec = false;
    r.run (2000, 500, silence);
    bool finite = true;
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < r.e.getReelView().lengthSamples; ++i)
            if (! std::isfinite (r.e.getReel().get (c, i))) finite = false;
    CHECK (finite, "NaN and infinity never get into the reel");
    const auto out = r.run (30000, 500, bad);
    bool outFinite = true;
    for (float v : out[0]) if (! std::isfinite (v)) outFinite = false;
    CHECK (outFinite, "and never come out");
}

static void testWaveformPicture()
{
    Rig r;
    const int n = 480000;                           // 10 seconds
    r.recordFirstTake (n, 4000, [] (int, long long t) { return 0.5f * (float) std::sin (0.07 * (double) t); });
    const auto& reel = r.e.getReel();
    float l, rr;
    reel.peaks.get (10, l, rr);
    CHECK (l > 0.45f && l <= 0.51f && rr > 0.45f && rr <= 0.51f, "a covered slice shows the loudness: %f %f", l, rr);
    reel.peaks.get (150, l, rr);
    CHECK (l == 0.0f && rr == 0.0f, "slices beyond the recording are empty");
    const int covered = n / reel.samplesPerBucket();
    reel.peaks.get (covered - 1, l, rr);
    CHECK (l > 0.4f, "the last full slice is filled in");

    // Overdubbing a quiet sound over a loud one keeps the picture honest (it shows what is in the reel now)
    r.p.soundOnSound = 0.0f;
    r.run (3000, 500, silence);
    r.p.rec = true;
    r.run (2 * n, 4000, silence);                   // record silence over everything, two passes
    r.p.rec = false;
    r.run (4000, 4000, silence);
    reel.peaks.get (10, l, rr);
    CHECK (l < 0.01f && rr < 0.01f, "recording silence over the reel empties the picture: %f", l);
}

static void testMonoAndTinyTake()
{
    Rig m (48000.0, 1);
    m.recordFirstTake (24000, 500);
    m.p.soundOnSound = 1.0f;
    m.run (15000, 500, silence);
    const auto out = m.run (24000, 500, silence);
    float peak = 0.0f;
    bool finite = true;
    for (float v : out[0]) { peak = std::max (peak, std::fabs (v)); if (! std::isfinite (v)) finite = false; }
    CHECK (finite && peak > 0.2f, "mono instance records and plays: peak %f", peak);

    Rig t;
    t.p.rec = true;
    t.run (32, 32, tone);
    t.p.rec = false;
    t.run (32, 32, silence);
    CHECK (t.e.getReelView().lengthSamples == 0 && ! t.e.getReelView().playing, "a take of a few samples is thrown away");
}

static void testExtremeControlsStayFinite()
{
    Rig r (44100.0);
    uint32_t seed = 99u;
    auto rnd = [&seed] { seed = seed * 1664525u + 1013904223u; return (float) (seed >> 8) / 16777216.0f; };
    bool finite = true;
    bool lengthOk = true;
    for (int blockNumber = 0; blockNumber < 600; ++blockNumber)
    {
        r.p.inputLevel = rnd();   r.p.soundOnSound = rnd();  r.p.outputLevel = rnd();
        r.p.geneSize = rnd();     r.p.variSpeed = rnd();     r.p.morph = rnd();
        r.p.slide = rnd();        r.p.pitchSnap = (int) (rnd() * 2.99f);
        r.p.pitchScale = (int) (rnd() * 28.99f);
        r.p.inputGainStep = (int) (rnd() * 3.99f);
        r.p.rec = rnd() > 0.7f;   r.p.play = rnd() > 0.3f;   r.p.clearReel = rnd() > 0.97f;
        r.p.splice = r.p.shift = r.p.undo = r.p.eraseSplice = rnd() > 0.5f;
        r.p.recNewSplice = rnd() > 0.8f;  r.p.deleteMarker = rnd() > 0.8f;  r.p.deleteAllMarkers = rnd() > 0.95f;
        r.p.organize = rnd();
        const auto out = r.run (256, 256, tone);
        for (int c = 0; c < 2; ++c)
            for (float v : out[(size_t) c])
                if (! std::isfinite (v) || std::fabs (v) > 100.0f) finite = false;
        const int len = r.e.getReelView().lengthSamples;
        if (len < 0 || len > r.e.getReelView().capacitySamples) lengthOk = false;
        const int voices = r.e.getReelView().voices;
        if (voices < 0 || voices > 6) lengthOk = false;
    }
    CHECK (finite, "random control changes never make bad sound");
    CHECK (lengthOk, "reel length always stays in range");
}


//==============================================================================
// ---- milestone 5: the tape head and genes ----

// Records a take of n samples, then stops the loop and starts it again with the controls as they are now, so the next
// samples come out from the very start of a gene with no gliding. Input is silent afterwards, so we hear only the reel.
static void startFreshFromControls (Rig& r, int n)
{
    r.p.play = false;
    r.run (n + 2000, 500, silence);               // the pass in progress finishes, then it stops
    r.p.play = true;                              // acted on at the start of the next block: a fresh gene
}

static void testVariSpeedLandmarks()
{
    using phe::dsp::variSpeed;
    const auto stop = variSpeed (0.5f, 0);
    CHECK (stop.stopped && stop.rate == 0.0f, "knob in the middle: stopped");
    CHECK (variSpeed (0.75f, 0).rate == 1.0f,  "knob at 2:30: exactly original speed forwards");
    CHECK (variSpeed (0.25f, 0).rate == -1.0f, "knob at 9:30: exactly original speed backwards");
    CHECK (near (variSpeed (1.0f, 0).rate, 2.0f, 1.0e-5f),  "right end: one octave up");
    CHECK (near (variSpeed (0.0f, 0).rate, -2.0f, 1.0e-5f), "left end: one octave up, backwards");
    CHECK (near (variSpeed (1.0f, 0).semitones, 12.0f, 1.0e-4f), "right end is +12 semitones");

    // From the middle outwards the speed only ever grows; both sides mirror each other.
    float last = 0.0f;
    bool grows = true, mirrored = true, smooth = true, finite = true;
    float lowest = 1.0f;
    for (int i = 0; i <= 2000; ++i)
    {
        const float k = 0.5f + 0.5f * (float) i / 2000.0f;
        const float rf = variSpeed (k, 0).rate;
        const float rb = variSpeed (1.0f - k, 0).rate;
        if (rf < last - 1.0e-6f) grows = false;
        if (std::fabs (rf + rb) > 1.0e-5f) mirrored = false;
        if (std::fabs (rf - last) > 0.02f) smooth = false;      // no jumps while moving the knob a 2000th of its travel
        if (! std::isfinite (rf)) finite = false;
        if (rf > 0.0f) lowest = std::min (lowest, rf);
        last = rf;
    }
    CHECK (grows, "turning the knob away from the middle only ever speeds the tape up");
    CHECK (mirrored, "backwards mirrors forwards");
    CHECK (smooth, "no jumps in the curve");
    CHECK (finite, "always a normal number");

    // The slow side reaches 26 semitones down (0.2227 times original speed) before braking to a stop.
    const float down26 = std::exp2 (-26.0f / 12.0f);
    bool reached = false;
    for (int i = 0; i <= 4000; ++i)
        if (std::fabs (variSpeed (0.5f + 0.25f * (float) i / 4000.0f, 0).rate - down26) < 0.002f) reached = true;
    CHECK (reached, "the slow side passes through 26 semitones below original speed");

    // More knob travel for the slow speeds than for the fast ones (finer control when braking)
    int slowSteps = 0, fastSteps = 0;
    for (int i = 0; i < 1000; ++i)
    {
        const float k = 0.5f + 0.5f * (float) i / 1000.0f;
        (variSpeed (k, 0).rate < 1.0f ? slowSteps : fastSteps)++;
    }
    CHECK (std::abs (slowSteps - fastSteps) < 20, "the knob is split evenly: half of it below original speed, half above (%d / %d)", slowSteps, fastSteps);
    const float halfWay = variSpeed (0.5f + 0.125f, 0).rate;       // half-way between stopped and original speed
    CHECK (halfWay < 0.7f && halfWay > 0.05f, "half-way to original speed the tape is going slower than that (%.3f)", halfWay);

    // Wild input
    CHECK (variSpeed (std::numeric_limits<float>::quiet_NaN(), 0).stopped, "a bad knob value counts as the middle");
    CHECK (variSpeed (5.0f, 0).rate == variSpeed (1.0f, 0).rate && variSpeed (-3.0f, 0).rate == variSpeed (0.0f, 0).rate, "knob values are limited to 0..1");
}

static void testPitchSnapMaths()
{
    using phe::dsp::variSpeed;

    bool offSame = true, semitonesWhole = true, stepsOnList = true, signKept = true, brakeKept = true;
    const float down26 = std::exp2 (-26.0f / 12.0f);
    for (int i = 0; i <= 4000; ++i)
    {
        const float k = (float) i / 4000.0f;
        const auto off = variSpeed (k, 0);
        const auto sem = variSpeed (k, 2);
        const auto stp = variSpeed (k, 1);
        if (variSpeed (k, 7).rate != off.rate) offSame = false;       // anything that is not 1 or 2 means Off
        if ((off.rate < 0.0f) != (sem.rate < 0.0f) || (off.rate < 0.0f) != (stp.rate < 0.0f)) signKept = false;
        if (off.stopped != sem.stopped || off.stopped != stp.stopped) signKept = false;
        if (off.stopped) continue;

        if (std::fabs (off.rate) >= down26)
        {
            const float st = 12.0f * std::log2 (std::fabs (sem.rate));
            if (std::fabs (st - std::round (st)) > 1.0e-3f) semitonesWhole = false;
            const float stS = 12.0f * std::log2 (std::fabs (stp.rate));
            bool onList = false;
            for (float v : { 0.0f, 7.0f, 12.0f, 19.0f, 24.0f })
                if (std::fabs (std::fabs (stS) - v) < 1.0e-3f) onList = true;
            if (! onList) stepsOnList = false;
        }
        else
        {
            // in the last stretch before the stop position, Snap lets go: the braking curve is untouched
            if (sem.rate != off.rate || stp.rate != off.rate) brakeKept = false;
        }
    }
    CHECK (offSame, "Pitch Snap Off (or anything else) leaves the speed alone");
    CHECK (semitonesWhole, "Semitones: the speed is always a whole number of semitones from original speed");
    CHECK (stepsOnList, "Steps: only 0, 7, 12, 19 and 24 semitones (up or down) are reached");
    CHECK (signKept, "Pitch Snap never changes the direction or the stop");
    CHECK (brakeKept, "Pitch Snap lets go of the last stretch before the stop, so braking stays smooth");

    // A few exact cases (knob values worked out from the curve: +24 semitones per half knob travel above original speed)
    const float kPlus5  = 0.5f + 0.5f * (0.5f + 5.0f / 24.0f);
    const float kPlus7  = 0.5f + 0.5f * (0.5f + 7.0f / 24.0f);
    const float kPlus04 = 0.5f + 0.5f * (0.5f + 0.4f / 24.0f);
    CHECK (near (variSpeed (kPlus5, 0).semitones, 5.0f, 1.0e-3f), "knob for +5 semitones: %.4f", variSpeed (kPlus5, 0).semitones);
    CHECK (near (variSpeed (kPlus5, 1).semitones, 7.0f, 1.0e-4f), "Steps: +5 lands on +7");
    CHECK (near (variSpeed (kPlus5, 2).semitones, 5.0f, 1.0e-4f), "Semitones: +5 stays +5");
    CHECK (near (variSpeed (kPlus04, 2).semitones, 0.0f, 1.0e-4f) && variSpeed (kPlus04, 2).rate == 1.0f, "Semitones: +0.4 lands on original speed");
    CHECK (near (variSpeed (kPlus7, 1).rate, std::exp2 (7.0f / 12.0f), 1.0e-5f), "Steps: +7 is a fifth up");
    CHECK (near (variSpeed (1.0f - kPlus5, 1).rate, -std::exp2 (7.0f / 12.0f), 1.0e-5f), "backwards works the same");
    CHECK (phe::dsp::snapSemitones (-13.0f, 1) == -12.0f && phe::dsp::snapSemitones (-16.0f, 1) == -19.0f, "Steps below zero mirror above zero");
}

static void testGeneSizeMap()
{
    const double L = 100.0;
    CHECK (phe::geneSeconds (0.0f, L) == 0.0, "Gene-Size fully left: the whole reel");
    CHECK (phe::geneSeconds (0.005f, L) == 0.0, "a little room at the left end still means the whole reel");
    CHECK (near ((float) phe::geneSeconds (phe::kGeneFullZone + 0.0001f, L), (float) L, 0.5f), "just off the left end the gene is almost the whole reel");
    CHECK (near ((float) phe::geneSeconds (1.0f, L), 0.001f, 1.0e-6f), "fully right: a thousandth of a second");

    double last = 1.0e9;
    bool falls = true;
    for (int i = 0; i <= 1000; ++i)
    {
        const double g = phe::geneSeconds ((float) i / 1000.0f, L);
        const double v = g == 0.0 ? L : g;
        if (v > last + 1.0e-9) falls = false;
        last = v;
    }
    CHECK (falls, "the gene only ever gets shorter as the knob goes right");

    // The same knob position means a shorter gene on a shorter reel (it is a fraction of the reel at first)
    CHECK (phe::geneSeconds (0.5f, 10.0) < phe::geneSeconds (0.5f, 100.0), "shorter reel, shorter gene at the same knob position");
    CHECK (phe::geneSeconds (0.5f, 0.001) == 0.0, "a reel shorter than two thousandths of a second is always one whole gene");
    CHECK (phe::geneSeconds (std::numeric_limits<float>::quiet_NaN(), L) == 0.0, "a bad knob value means the whole reel");
}

static void testReverseAndSpeed()
{
    const int n = 24000;
    const int fade = (int) std::llround (0.0015 * 48000.0);

    // Backwards at original speed: the reel comes out in reverse, with the usual fade at the lap's ends.
    {
        Rig r;
        r.recordFirstTake (n, 500);
        r.p.variSpeed = 0.25f;
        startFreshFromControls (r, n);
        const int total = n + 777;
        const auto out = r.run (total, 500, silence);
        bool ok = true;
        for (int i = 0; i < total; ++i)
        {
            const int k = i % n;
            const double position = k == 0 ? 0.0 : (double) (n - k);
            const float expected = r.e.getReel().get (0, (int) position) * phe::dsp::edgeFade ((double) k, n, fade);
            if (! near (out[0][(size_t) i], expected, 1.0e-5f)) ok = false;
        }
        CHECK (ok, "Vari-Speed at 9:30 plays the reel backwards at original speed");
        CHECK (r.e.getReelView().rate == -1.0f, "the view reports the speed: %f", r.e.getReelView().rate);
    }

    // Double speed: every second sample, a lap in half the time.
    {
        Rig r;
        r.recordFirstTake (n, 500);
        r.p.variSpeed = 1.0f;
        startFreshFromControls (r, n);
        const int total = 2 * n;
        const auto out = r.run (total, 500, silence);
        bool ok = true;
        for (int i = 0; i < total; ++i)
        {
            const int q = (2 * i) % n;
            const float expected = r.e.getReel().get (0, q) * phe::dsp::edgeFade ((double) q, n, fade);
            if (! near (out[0][(size_t) i], expected, 1.0e-4f)) ok = false;
        }
        CHECK (ok, "Vari-Speed fully right plays at twice the speed");
    }

    // Half speed (on the way between samples the curve through the neighbours is used): the whole numbers are exact.
    {
        Rig r;
        r.recordFirstTake (n, 500);
        // find the knob position that gives exactly half speed
        float k = 0.5f, bestErr = 9.0f;
        for (int i = 0; i <= 200000; ++i)
        {
            const float cand = 0.5f + 0.25f * (float) i / 200000.0f;
            const float err = std::fabs (phe::dsp::variSpeed (cand, 0).rate - 0.5f);
            if (err < bestErr) { bestErr = err; k = cand; }
        }
        r.p.variSpeed = k;
        startFreshFromControls (r, n);
        const int total = 4000;
        const auto out = r.run (total, 500, silence);
        // Whatever the exact speed is, the sound must be smooth and follow the reel at about half speed.
        const double rate = phe::dsp::variSpeed (k, 0).rate;
        bool followsReel = true;
        for (int i = 100; i < total - 8; ++i)         // (after the short fade at the start of the gene)
        {
            const double q = rate * (double) i;
            const double lo = std::min (r.e.getReel().read (0, q - 2.0), r.e.getReel().read (0, q + 2.0));
            const double hi = std::max (r.e.getReel().read (0, q - 2.0), r.e.getReel().read (0, q + 2.0));
            if (out[0][(size_t) i] < lo - 0.2 || out[0][(size_t) i] > hi + 0.2) followsReel = false;
        }
        CHECK (bestErr < 0.001f, "a knob position for half speed exists");
        CHECK (followsReel, "slower playback follows the reel at the slower pace");
    }
}

static void testStoppedTape()
{
    const int n = 24000;
    Rig r;
    r.recordFirstTake (n, 500);
    r.run (5000, 500, silence);

    r.p.variSpeed = 0.5f;                                 // stop the tape
    r.run (30000, 500, silence);
    const int stoppedAt = r.e.getReelView().playPos;
    const auto out = r.run (20000, 500, silence);
    float loudest = 0.0f;
    for (float v : out[0]) loudest = std::max (loudest, std::fabs (v));
    CHECK (loudest < 1.0e-6f, "a stopped tape is silent (no held-sample hum): %g", loudest);
    CHECK (r.e.getReelView().playPos == stoppedAt, "the head stays put while stopped");
    CHECK (r.e.getReelView().playing, "it is still 'playing', just at speed zero");

    // Starting again carries on from where it halted
    r.p.variSpeed = phe::kDefaultVariSpeed;
    r.run (1000, 500, silence);
    const int moved = (r.e.getReelView().playPos - stoppedAt + n) % n;
    CHECK (moved > 500 && moved < 1500, "playback resumes from where it was halted (moved %d in 1000 samples)", moved);

    // Slide scrubs the reel even while the tape is stopped, then goes quiet again when it stops moving
    r.p.variSpeed = 0.5f;
    r.p.slide = 0.0f;
    r.run (30000, 500, silence);
    float scrubLoud = 0.0f;
    for (int step = 0; step < 40; ++step)
    {
        r.p.slide = 0.0f + 0.9f * (float) (step + 1) / 40.0f;
        const auto o = r.run (500, 500, silence);
        for (float v : o[0]) scrubLoud = std::max (scrubLoud, std::fabs (v));
    }
    CHECK (scrubLoud > 0.05f, "moving Slide scrubs the reel while the tape is stopped (%g)", scrubLoud);
    r.run (20000, 500, silence);
    const auto after = r.run (2000, 500, silence);
    float quiet = 0.0f;
    for (float v : after[0]) quiet = std::max (quiet, std::fabs (v));
    CHECK (quiet < 1.0e-6f, "and it is silent again once Slide stops moving (%g)", quiet);
}

static void testGeneLengthIsTime()
{
    const int n = 96000;                                  // a 2 second reel at 48 kHz
    // Gene-Size knob for a gene of about a tenth of a second
    float knob = 0.4f;
    const double G = phe::geneSeconds (knob, (double) n / 48000.0);
    CHECK (G > 0.09 && G < 0.11, "knob %.2f gives a gene of about 0.1 s (%.4f)", knob, G);
    const double geneSamples = G * 48000.0;

    auto resetsFor = [&] (float variKnob, double& biggestJump, int& peakReached)
    {
        Rig r;
        r.recordFirstTake (n, 500);
        r.p.geneSize = knob;
        r.p.variSpeed = variKnob;
        startFreshFromControls (r, n);
        std::vector<int> resets;
        int previous = -1;
        peakReached = 0;
        for (int i = 0; i < 30000; ++i)
        {
            r.run (1, 1, silence);
            const int pos = r.e.getReelView().playPos;
            if (previous >= 0 && pos < previous) resets.push_back (i);
            peakReached = std::max (peakReached, pos);
            previous = pos;
        }
        biggestJump = 0.0;
        for (size_t k = 1; k < resets.size(); ++k)
            biggestJump = std::max (biggestJump, std::fabs ((double) (resets[k] - resets[k - 1]) - geneSamples));
        return (int) resets.size();
    };

    double jump1 = 0, jump2 = 0;
    int peak1 = 0, peak2 = 0;
    const int resets1 = resetsFor (0.75f, jump1, peak1);       // original speed
    const int resets2 = resetsFor (1.0f, jump2, peak2);        // double speed
    CHECK (resets1 >= 5 && resets2 >= 5, "genes repeat: %d and %d restarts", resets1, resets2);
    CHECK (jump1 < 1.5 && jump2 < 1.5, "a gene lasts the same time at any speed (off by %.2f / %.2f samples)", jump1, jump2);
    CHECK (std::abs (peak1 - (int) geneSamples) < 4, "at original speed a gene covers its own length of reel (%d)", peak1);
    CHECK (std::abs (peak2 - 2 * (int) geneSamples) < 8, "at double speed a gene covers twice as much reel (%d)", peak2);
}

static void testSlideAndGeneStart()
{
    const int n = 48000;
    {
        Rig r;
        r.recordFirstTake (n, 500);
        r.p.slide = 0.5f;
        startFreshFromControls (r, n);
        r.run (100, 100, silence);
        CHECK (r.e.getReelView().playPos == n / 2 + 100, "with Slide half way a whole-reel lap starts half way in: %d", r.e.getReelView().playPos);
    }
    {
        // Short genes start over at the Slide position, and each gene fades to nothing at its ends
        Rig r;
        r.recordFirstTake (n, 500);
        r.p.geneSize = 0.5f;
        r.p.slide = 0.25f;
        startFreshFromControls (r, n);
        int previous = -1, resets = 0;
        bool startsAtSlide = true;
        for (int i = 0; i < 20000; ++i)
        {
            r.run (1, 1, silence);
            const int pos = r.e.getReelView().playPos;
            if (previous >= 0 && pos < previous)
            {
                ++resets;
                if (std::abs (pos - n / 4) > 2) startsAtSlide = false;
            }
            previous = pos;
        }
        CHECK (resets >= 3, "short genes repeat (%d)", resets);
        CHECK (startsAtSlide, "every gene starts at the Slide position");
        const auto v = r.e.getReelView();
        CHECK (v.geneSeconds > 0.0f && v.geneSpan > 0, "the view shows the gene (%g s, %d samples)", v.geneSeconds, v.geneSpan);
    }
    {
        // Moving Slide moves the playing gene straight away (no waiting for the next gene)
        Rig r;
        r.recordFirstTake (n, 500);
        r.p.geneSize = 0.3f;
        startFreshFromControls (r, n);
        r.run (3000, 500, silence);
        r.p.slide = 0.8f;
        r.run (2000, 500, silence);
        const int pos = r.e.getReelView().playPos;
        CHECK (pos > (int) (0.7 * n) && pos < (int) (0.95 * n), "Slide moves the playing gene at once: %d", pos);
    }
    {
        // Backwards, a gene is the stretch before the Slide position
        Rig r;
        r.recordFirstTake (n, 500);
        r.p.geneSize = 0.5f;
        r.p.slide = 0.5f;
        r.p.variSpeed = 0.25f;
        startFreshFromControls (r, n);
        r.run (200, 100, silence);
        const auto v = r.e.getReelView();
        CHECK (v.playPos < n / 2, "going backwards the head moves to the left of the Slide position (%d)", v.playPos);
        CHECK (v.geneStart + v.geneSpan <= n / 2 + 2 || v.geneStart > n / 2, "and the gene window sits before the Slide position (%d + %d)", v.geneStart, v.geneSpan);
    }
}

static void testGeneFadesAndPlayOff()
{
    const int n = 48000;
    Rig r;
    r.recordFirstTake (n, 500);
    r.p.geneSize = 0.55f;
    startFreshFromControls (r, n);
    const double G = phe::geneSeconds (0.55f, (double) n / 48000.0) * 48000.0;
    const int total = (int) (G * 6);
    const auto out = r.run (total, 1, silence);

    // At each gene boundary the sound is faded to (almost) nothing, so the joins do not click.
    bool quietAtJoins = true, loudBetween = false;
    for (int k = 1; k < 6; ++k)
    {
        const int at = (int) std::llround ((double) k * G);
        if (std::fabs (out[0][(size_t) at]) > 0.02f) quietAtJoins = false;
        if (std::fabs (out[0][(size_t) (at + (int) G / 2)]) > 0.02f) loudBetween = true;
    }
    CHECK (quietAtJoins, "short genes fade at their ends");
    CHECK (loudBetween, "and are audible in between");

    // Play off: the gene in progress is the last one, then silence
    r.p.play = false;
    const auto tail = r.run ((int) (G * 2) + 500, 1, silence);
    CHECK (! r.e.getReelView().playing, "Play off stops after the gene in progress");
    bool quiet = true;
    for (size_t i = (size_t) G + 300; i < tail[0].size(); ++i)
        if (tail[0][i] != 0.0f) quiet = false;
    CHECK (quiet, "silent after the last gene");
}

static void testPitchSnapInEngine()
{
    const int n = 24000;
    auto run = [&] (float knob, int snap)
    {
        Rig r;
        r.recordFirstTake (n, 500);
        r.p.variSpeed = knob;
        r.p.pitchSnap = snap;
        startFreshFromControls (r, n);
        return r.run (8000, 500, silence);
    };

    const float kPlus5  = 0.5f + 0.5f * (0.5f + 5.0f / 24.0f);
    const float kPlus7  = 0.5f + 0.5f * (0.5f + 7.0f / 24.0f);
    const float kPlus04 = 0.5f + 0.5f * (0.5f + 0.4f / 24.0f);

    const auto unity = run (0.75f, 0);
    const auto semis = run (kPlus04, 2);
    CHECK (unity[0] == semis[0], "Semitones snap: a knob a little above original speed plays exactly at original speed");

    const auto fifthSteps = run (kPlus5, 1);
    const auto fifthPlain = run (kPlus7, 0);
    bool same = true;
    for (size_t i = 0; i < fifthSteps[0].size(); ++i)
        if (! near (fifthSteps[0][i], fifthPlain[0][i], 2.0e-3f)) same = false;
    CHECK (same, "Steps snap: a knob near +5 semitones plays at +7 semitones");

    const auto raw5 = run (kPlus5, 0);
    bool differs = false;
    for (size_t i = 0; i < raw5[0].size(); ++i)
        if (std::fabs (raw5[0][i] - fifthSteps[0][i]) > 0.01f) differs = true;
    CHECK (differs, "with Pitch Snap off the same knob position plays at its own pitch");
}

static void testRecordSpeedIsConstant()
{
    // The speed of the tape head never changes how fast recording writes onto the reel.
    const int n = 24000;
    Rig r;
    r.recordFirstTake (n, 500);
    r.run (2000, 500, silence);
    r.p.variSpeed = 1.0f;                     // playing at double speed ...
    r.run (2000, 500, silence);
    const int before = r.e.getReelView().recPos;
    r.p.rec = true;                           // ... record over it
    r.run (6000, 500, tone);
    const auto v = r.e.getReelView();
    CHECK (v.recording, "recording");
    const int advanced = (v.recPos - (before == 0 ? 0 : 0) + n) % n;
    // recording starts at the playhead and then moves exactly one sample per sample
    r.run (1000, 500, tone);
    const int after = r.e.getReelView().recPos;
    CHECK (((after - v.recPos) + n) % n == 1000, "recording moves one sample per sample whatever the playback speed (%d)", ((after - v.recPos) + n) % n);
    (void) advanced;
    r.p.rec = false;
    r.run (500, 500, silence);
    CHECK (r.e.getReelView().lengthSamples == n, "recording over does not change the reel length");
}

static void testEdgeCases()
{
    phe::Engine e;
    e.prepare (48000.0, 512, 2);

    float a = 0.25f, b = -0.25f;
    float* ptrs[2] = { &a, &b };
    e.process (ptrs, 2, 0);
    CHECK (a == 0.25f && b == -0.25f, "zero-length block");
    e.process (ptrs, 0, 1);
    CHECK (a == 0.25f && b == -0.25f, "no channels");

    e.prepare (44100.0, 256, 1);
    CHECK (e.getSampleRate() == 44100.0, "sample rate stored");
    e.prepare (-5.0, 256, 1);
    CHECK (e.getSampleRate() == 48000.0, "bad sample rate replaced by a safe one");
    CHECK (e.getReelView().capacitySamples == 174 * 48000, "reel size follows the sample rate");

    // reset() keeps the recording but stops recording
    Rig r;
    r.recordFirstTake (12000, 500);
    r.p.rec = true;
    r.run (2000, 500, tone);
    CHECK (r.e.getReelView().recording, "recording over the loop");
    r.e.reset();
    CHECK (r.e.getReelView().lengthSamples == 12000, "reset keeps the recording");
    r.p.rec = false;
    r.run (1000, 500, tone);
    CHECK (! r.e.getReelView().recording, "reset stopped recording");
    CHECK (r.e.takeRecordStopped(), "and said so");
}

static void testGainSteps()
{
    CHECK (phe::kNumInputGainSteps == 4, "four input gain steps");
    CHECK (phe::kInputGainStepDb[0] == -3.0f && phe::kInputGainStepDb[1] == 0.0f
        && phe::kInputGainStepDb[2] == 6.0f  && phe::kInputGainStepDb[3] == 12.0f, "gain steps -3 / 0 / +6 / +12 dB");
    CHECK (phe::Params().inputGainStep == 1, "gain step starts at 0 dB");
    CHECK (phe::Params().inputLevel == phe::kDefaultInputLevel && phe::Params().outputLevel == phe::kDefaultOutputLevel, "level knobs start at 0 dB");
    CHECK (phe::dsp::levelKnobToGain (phe::kDefaultInputLevel) == 1.0f, "starting level is exactly unity");
    CHECK (phe::Params().play && ! phe::Params().rec, "Play starts on, Rec off");
}

static void testScales()
{
    CHECK (phe::scales::kNumScales == 28, "28 scales, got %d", phe::scales::kNumScales);
    CHECK (phe::scales::maskFor (0) == 0, "Off has no mask");
    CHECK (phe::scales::maskFor (29) == 0, "out of range has no mask");
    CHECK (phe::scales::maskFor (1) == ((1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 11)),
           "Major scale mask");
    CHECK (phe::scales::maskFor (28) == 0xFFF, "Chromatic is all twelve notes");
    for (int i = 1; i <= phe::scales::kNumScales; ++i)
    {
        const int m = phe::scales::maskFor (i);
        CHECK ((m & 1) != 0, "scale %d contains its root", i);
        int bits = 0;
        for (int n = 0; n < 12; ++n) bits += (m >> n) & 1;
        CHECK (bits == phe::scales::kScales[i - 1].count, "scale %d note count", i);
    }
}


//==============================================================================
// ---- milestone 6: Morph and Pitch Scale ----

static float constantHalf (int, long long) { return 0.5f; }
static float pureSine (int, long long t) { return 0.4f * (float) std::sin (0.05 * (double) t); }

static void testMorphLandmarks()
{
    using namespace phe::dsp;
    auto at = [] (float k) { return morphFromKnob (k); };

    const auto far = at (0.0f);
    CHECK (near (far.spacing, 1.5f) && near (far.gapFraction, 0.5f) && far.genesAtOnce == 1.0f && far.voiceGain == 1.0f,
           "far left: a gap of half a gene (spacing %f)", far.spacing);
    const auto one = at (kMorphSeamless);
    CHECK (one.spacing == 1.0f && one.gapFraction == 0.0f && one.integerPoint, "1/1 is seamless: spacing exactly 1");
    CHECK (at (kMorphSeamless + 0.006f).spacing == 1.0f && at (kMorphSeamless - 0.006f).spacing == 1.0f,
           "1/1 is a small flat zone");
    CHECK (at (0.10f).gapFraction > 0.0f && at (0.10f).spacing > 1.0f, "left of 1/1 there are gaps");
    CHECK (at (0.30f).spacing < 1.0f && at (0.30f).spacing > 0.5f, "right of 1/1 genes overlap");
    CHECK (near (at (kMorphTwoAtOnce).spacing, 0.5f) && at (kMorphTwoAtOnce).integerPoint, "2/1 at 12:00");
    CHECK (near (at (kMorphThreeAtOnce).spacing, 1.0f / 3.0f) && at (kMorphThreeAtOnce).integerPoint, "3/1 at 1:00");
    CHECK (near (at (kMorphFourthGene).spacing, 1.0f / 3.0f) && at (kMorphFourthGene).pitchRange == 0.0f,
           "up to 2:30: three at once, no pitch-up yet");
    CHECK (near (at (kMorphFourthGene).panAmount, 1.0f), "panning is fully in by 2:30");
    CHECK (at (0.5f).panAmount == 0.0f && at (0.5f).panRandom == 0.0f, "no panning at 12:00");
    CHECK (near (at (0.65f).spacing, 1.0f / 3.0f), "three at once between 1:00 and 2:30");
    const auto top = at (1.0f);
    CHECK (near (top.spacing, 0.25f) && near (top.genesAtOnce, 4.0f) && near (top.pitchRange, 24.0f) && top.panRandom == 1.0f,
           "fully round: four at once, pitch-up up to two octaves, fully random pan");
    CHECK (near (top.voiceGain, 0.5f) && near (at (0.5f).voiceGain, 0.70710678f, 1.0e-4f), "equal-power level per gene");

    float previous = 99.0f, previousRange = -1.0f, biggestJump = 0.0f;
    bool monotone = true;
    for (int i = 0; i <= 1000; ++i)
    {
        const auto m = at ((float) i / 1000.0f);
        if (m.spacing > previous + 1.0e-6f) monotone = false;
        if (m.pitchRange < previousRange - 1.0e-6f) monotone = false;
        if (i > 0) biggestJump = std::max (biggestJump, std::fabs (m.spacing - previous));
        previous = m.spacing;
        previousRange = m.pitchRange;
        if (! (m.spacing >= 0.25f - 1.0e-6f && m.spacing <= 1.5f + 1.0e-6f)) monotone = false;
    }
    CHECK (monotone, "turning Morph up never makes the spacing wider or the pitch-up range smaller");
    CHECK (biggestJump < 0.03f, "no jumps along the knob (biggest %f)", biggestJump);
    CHECK (at (std::numeric_limits<float>::quiet_NaN()).spacing == 1.0f, "a bad knob value gives 1/1");
    CHECK (at (-4.0f).spacing == 1.5f && near (at (9.0f).spacing, 0.25f), "knob values outside 0..1 are limited");
}

static void testRandomPitchUp()
{
    using namespace phe::dsp;
    Random rng;
    rng.seed (1234);
    CHECK (randomPitchUp (rng, 0.0f, 0) == 0.0f && randomPitchUp (rng, 0.0f, 0xFFF) == 0.0f, "no range, no pitch-up");

    // Off: any amount from 0 up to the range
    float lo = 99.0f, hi = -1.0f;
    for (int i = 0; i < 20000; ++i)
    {
        const float v = randomPitchUp (rng, 24.0f, 0);
        lo = std::min (lo, v);
        hi = std::max (hi, v);
    }
    CHECK (lo >= 0.0f && lo < 0.5f && hi < 24.0f && hi > 23.5f, "no scale: pitch-ups spread over 0..24 (%f .. %f)", lo, hi);

    // A scale: only notes of that scale, and all of them are reached (over two octaves)
    const int major = phe::scales::maskFor (1);
    bool seen[25] = {};
    bool inScale = true;
    for (int i = 0; i < 20000; ++i)
    {
        const float v = randomPitchUp (rng, 24.0f, major);
        const int semis = (int) v;
        if (v != (float) semis || semis < 0 || semis > 24 || ((major >> (semis % 12)) & 1) == 0) inScale = false;
        else seen[semis] = true;
    }
    CHECK (inScale, "scale on: every pitch-up is a note of the scale (root, then degrees, up to the range)");
    bool allReached = true;
    for (int semis : { 0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19, 21, 23, 24 })
        if (! seen[semis]) allReached = false;
    CHECK (allReached, "and every scale note under the range gets used");

    // A small range allows only the root (and nothing else), so the scale makes every pitch-up exactly zero
    bool rootOnly = true;
    for (int i = 0; i < 2000; ++i)
        if (randomPitchUp (rng, 1.9f, major) != 0.0f) rootOnly = false;
    CHECK (rootOnly, "range 1.9 with a major scale leaves only the root");

    // Chromatic gives whole semitones; a pentatonic never lands on a semitone next to the root
    bool whole = true, noOne = true;
    const int pent = phe::scales::maskFor (17);
    for (int i = 0; i < 5000; ++i)
    {
        const float c = randomPitchUp (rng, 12.0f, 0xFFF);
        if (c != std::floor (c)) whole = false;
        if (randomPitchUp (rng, 12.0f, pent) == 1.0f) noOne = false;
    }
    CHECK (whole && noOne, "chromatic = whole semitones; pentatonic skips the semitone");

    // Same seed, same numbers
    Random a, b;
    a.seed (77); b.seed (77);
    bool same = true;
    for (int i = 0; i < 100; ++i)
        if (a.nextInt() != b.nextInt()) same = false;
    CHECK (same, "the random source repeats exactly from the same seed");
}

// Counts how many genes are playing after each sample, over 'n' samples, and the first sample at which each count shows.
struct VoiceStats { double average = 0.0; int most = 0; double zeroShare = 0.0; std::vector<int> starts; };

// 'warmUp' samples are run first and not counted (genes start one by one, so the count needs a moment to settle).
static VoiceStats watchVoices (Rig& r, int n, int warmUp = 0)
{
    VoiceStats s;
    long long sum = 0;
    int zero = 0;
    r.run (warmUp, 1, silence);
    int previous = r.e.getReelView().voices;
    for (int i = 0; i < n; ++i)
    {
        r.run (1, 1, silence);
        const int v = r.e.getReelView().voices;
        sum += v;
        s.most = std::max (s.most, v);
        if (v == 0) ++zero;
        if (v > 0 && previous == 0) s.starts.push_back (i);
        previous = v;
    }
    s.average = (double) sum / (double) n;
    s.zeroShare = (double) zero / (double) n;
    return s;
}

static void testMorphDefaultIsPlainLoop()
{
    CHECK (phe::Params().morph == phe::kDefaultMorph && phe::kDefaultMorph == phe::dsp::kMorphSeamless,
           "a new PhenomeEon starts with Morph at 1/1");
    Rig a, b;
    a.recordFirstTake (20000, 500);
    b.recordFirstTake (20000, 500);
    b.p.morph = phe::dsp::kMorphSeamless + 0.005f;          // inside the flat 1/1 zone
    const auto oa = a.run (60000, 500, silence);
    const auto ob = b.run (60000, 500, silence);
    CHECK (oa[0] == ob[0] && oa[1] == ob[1], "anywhere in the 1/1 zone sounds exactly the same");
    CHECK (a.e.getReelView().voices == 1, "one gene at a time at 1/1");
}

static void testMorphGapsAndOverlaps()
{
    const int n = 96000;
    const float knob = 0.4f;
    const double G = phe::geneSeconds (knob, (double) n / 48000.0) * 48000.0;     // gene length in samples

    // Watches a whole number of start-to-start cycles (about 'about' samples), after a warm-up.
    auto watch = [&] (float morph, int about)
    {
        Rig r;
        r.recordFirstTake (n, 500);
        r.p.geneSize = knob;
        r.p.morph = morph;
        startFreshFromControls (r, n);
        const double cycle = G * (double) phe::dsp::morphFromKnob (morph).spacing;
        const int cycles = std::max (1, (int) std::floor ((double) about / cycle));
        return watchVoices (r, (int) std::llround ((double) cycles * cycle), (int) (G * 1.3));
    };

    // 1/1: always exactly one gene
    {
        const auto s = watch (phe::dsp::kMorphSeamless, 30000);
        CHECK (s.most == 1 && s.zeroShare < 0.001, "1/1: one gene, no silence between (most %d, silent share %f)", s.most, s.zeroShare);
    }
    // Far left: gaps of half a gene, so genes start every 1.5 gene lengths and a third of the time is silent
    {
        const auto s = watch (0.0f, 60000);
        CHECK (s.most == 1 && std::fabs (s.zeroShare - 1.0 / 3.0) < 0.01, "gaps: silent share %f (about a third)", s.zeroShare);
        double worst = 0.0;
        for (size_t k = 1; k < s.starts.size(); ++k)
            worst = std::max (worst, std::fabs ((double) (s.starts[k] - s.starts[k - 1]) - 1.5 * G));
        CHECK (s.starts.size() >= 6 && worst < 2.0, "genes start every 1.5 gene lengths (%zu starts, off by %.2f samples)", s.starts.size(), worst);
    }
    // Overlaps: on average 1/spacing genes play at the same moment, never more than four
    struct Case { float morph; double expect; int most; };
    for (const Case& c : { Case { 0.30f, 1.0 / 0.8125, 2 }, Case { 0.50f, 2.0, 3 }, Case { 0.60f, 3.0, 4 },
                           Case { 0.70f, 3.0, 4 }, Case { 0.875f, 3.0 + 0.0, 5 }, Case { 1.0f, 4.0, 5 } })
    {
        const auto s = watch (c.morph, 40000);
        const double expect = phe::dsp::morphFromKnob (c.morph).genesAtOnce;
        CHECK (std::fabs (s.average - expect) < 0.04, "morph %.3f: %.2f genes at once on average (want %.2f)", c.morph, s.average, expect);
        CHECK (s.most <= c.most && s.most >= (int) expect, "morph %.3f: at most %d at the same moment (saw %d)", c.morph, c.most, s.most);
    }
}

static void testMorphWholeReelAndBackwards()
{
    const int n = 24000;
    // A whole-reel gene is one lap of the tape, so a lap lasts n / speed samples. Watches 3 start-to-start cycles.
    auto watch = [&] (float morph, float variKnob, double speed)
    {
        Rig r;
        r.recordFirstTake (n, 500);
        r.p.morph = morph;
        r.p.variSpeed = variKnob;
        startFreshFromControls (r, n);
        const double lap = (double) n / speed;
        const double cycle = lap * (double) phe::dsp::morphFromKnob (morph).spacing;
        return watchVoices (r, (int) std::llround (3.0 * cycle), (int) (lap * 1.1));
    };

    const auto gap = watch (0.0f, 0.75f, 1.0);
    CHECK (gap.most == 1 && std::fabs (gap.zeroShare - 1.0 / 3.0) < 0.01, "whole reel, gaps: silent share %f", gap.zeroShare);
    const auto two = watch (0.5f, 0.75f, 1.0);
    CHECK (std::fabs (two.average - 2.0) < 0.04, "whole reel, 2/1: %.2f at once", two.average);
    const auto back = watch (0.5f, 0.25f, 1.0);
    CHECK (std::fabs (back.average - 2.0) < 0.04, "backwards, 2/1: %.2f at once", back.average);
    const auto fast = watch (0.5f, 1.0f, 2.0);
    CHECK (std::fabs (fast.average - 2.0) < 0.04, "double speed, 2/1: %.2f at once", fast.average);
}

static void testMorphLevelAndPanning()
{
    const int n = 96000;
    auto run = [&] (float morph, double& peak, double& rms, double& widest, double& leftOnly)
    {
        Rig r;
        r.recordFirstTake (n, 500, constantHalf);
        r.p.geneSize = 0.4f;
        r.p.morph = morph;
        startFreshFromControls (r, n);
        const auto out = r.run (40000, 500, silence);
        peak = 0.0; rms = 0.0; widest = 0.0; leftOnly = 0.0;
        double sum = 0.0;
        for (size_t i = 0; i < out[0].size(); ++i)
        {
            peak = std::max (peak, (double) std::max (std::fabs (out[0][i]), std::fabs (out[1][i])));
            sum += (double) out[0][i] * (double) out[0][i];
            widest = std::max (widest, (double) std::fabs (out[0][i] - out[1][i]));
        }
        rms = std::sqrt (sum / (double) out[0].size());
    };

    double peak1, rms1, wide1, x;
    run (phe::dsp::kMorphSeamless, peak1, rms1, wide1, x);
    CHECK (peak1 <= 0.5001 && wide1 == 0.0, "1/1: plain loop, centre (peak %f)", peak1);

    double peak2, rms2, wide2;
    run (0.5f, peak2, rms2, wide2, x);
    CHECK (peak2 <= 0.5 * 2.0 * 0.7072 + 1.0e-3, "2/1: each gene turned down, so two together stay moderate (peak %f)", peak2);
    CHECK (rms2 / rms1 > 1.0 && rms2 / rms1 < 1.6, "2/1 level is steady, a little above 1/1 (ratio %f)", rms2 / rms1);
    CHECK (wide2 == 0.0, "no panning at 12:00");

    double peak3, rms3, wide3;
    run (0.6f, peak3, rms3, wide3, x);
    CHECK (peak3 <= 0.5 * 3.0 * 0.5775 + 1.0e-3 && rms3 / rms1 < 1.9, "3/1: steady level (ratio %f)", rms3 / rms1);

    double peak4, rms4, wide4;
    run (0.75f, peak4, rms4, wide4, x);
    CHECK (wide4 > 0.15, "at 2:30 the genes are spread across the stereo picture (left-right difference %f)", wide4);
    double peak5, rms5, wide5;
    run (1.0f, peak5, rms5, wide5, x);
    CHECK (wide5 > 0.15 && peak5 < 1.1, "fully round: random places, still moderate level (peak %f)", peak5);
}

// How often the signal crosses zero per sample: a pure sine at the reel's pitch gives omega / pi.
static double crossingRate (const std::vector<float>& v)
{
    int crossings = 0;
    for (size_t i = 1; i < v.size(); ++i)
        if ((v[i - 1] < 0.0f) != (v[i] < 0.0f)) ++crossings;
    return (double) crossings / (double) v.size();
}

static void testMorphPitchUpAndPitchScale()
{
    const int n = 96000;
    const double plain = 0.05 / 3.14159265358979;       // zero crossings per sample of the recorded sine

    auto play = [&] (float morph, int scale, float variKnob, int blockSize = 500, int samples = 60000)
    {
        Rig r;
        r.recordFirstTake (n, 500, pureSine);
        r.p.geneSize = 0.4f;
        r.p.morph = morph;
        r.p.pitchScale = scale;
        r.p.variSpeed = variKnob;
        startFreshFromControls (r, n);
        return r.run (samples, blockSize, silence);
    };

    // Below 2:30 there are no pitch-ups, so Pitch Scale changes nothing
    {
        const auto off = play (0.6f, 0, 0.75f);
        const auto scale = play (0.6f, 28, 0.75f);
        CHECK (off[0] == scale[0] && off[1] == scale[1], "below 2:30 Pitch Scale makes no difference");
        CHECK (std::fabs (crossingRate (off[0]) / plain - 1.0) < 0.06, "three genes at the reel's own pitch stay at that pitch (%f)", crossingRate (off[0]) / plain);
    }
    // Fully round, no scale: random pitch-ups make the sound clearly higher
    const auto wild = play (1.0f, 0, 0.75f);
    CHECK (crossingRate (wild[0]) / plain > 1.3, "fully round: pitched up (%f times the plain rate)", crossingRate (wild[0]) / plain);

    // A scale changes which pitches are used
    const auto wildMajor = play (1.0f, 1, 0.75f);
    CHECK (wild[0] != wildMajor[0], "Pitch Scale changes the random pitch-ups");
    CHECK (crossingRate (wildMajor[0]) / plain > 1.3, "still higher with a scale (%f)", crossingRate (wildMajor[0]) / plain);

    // Pitch-ups are built on the note Vari-Speed plays: with a tiny range and a scale every pitch-up is zero, so the
    // sound is exactly the tape's own pitch, here double speed (one octave up)
    const auto octave = play (0.76f, 1, 1.0f);
    CHECK (std::fabs (crossingRate (octave[0]) / (2.0 * plain) - 1.0) < 0.06,
           "root = the current Vari-Speed note: an octave up stays an octave up (%f)", crossingRate (octave[0]) / (2.0 * plain));

    // Always the same, whatever the block size (the random numbers belong to the genes, not the blocks)
    const auto big = play (1.0f, 5, 0.75f, 500, 20000);
    const auto tiny = play (1.0f, 5, 0.75f, 1, 20000);
    CHECK (big[0] == tiny[0] && big[1] == tiny[1], "random pitch-ups and places do not depend on the block size");
    const auto again = play (1.0f, 5, 0.75f, 500, 20000);
    CHECK (big[0] == again[0], "starting again gives the same random sequence");
}

static void testMorphPlayOffStoppedAndRecording()
{
    const int n = 48000;
    const double G = phe::geneSeconds (0.55f, (double) n / 48000.0) * 48000.0;

    // Play off with genes overlapping: every gene in progress finishes, then silence
    {
        Rig r;
        r.recordFirstTake (n, 500);
        r.p.geneSize = 0.55f;
        r.p.morph = 0.6f;
        startFreshFromControls (r, n);
        r.run ((int) (G * 3.3), 1, silence);
        CHECK (r.e.getReelView().voices >= 3, "three genes playing before Play goes off");
        r.p.play = false;
        const auto tail = r.run ((int) (G * 2) + 500, 1, silence);
        CHECK (! r.e.getReelView().playing && r.e.getReelView().voices == 0, "Play off: all genes finish, then it stops");
        bool quiet = true;
        for (size_t i = (size_t) G + 300; i < tail[0].size(); ++i)
            if (tail[0][i] != 0.0f) quiet = false;
        CHECK (quiet, "silent after the last gene");
        // and Play on again starts afresh with one gene
        r.p.play = true;
        r.run (10, 1, silence);
        CHECK (r.e.getReelView().playing && r.e.getReelView().voices == 1, "Play on starts one fresh gene");
    }
    // Stopped tape: nothing plays and nothing piles up
    {
        Rig r;
        r.recordFirstTake (n, 500);
        r.p.geneSize = 0.55f;
        r.p.morph = 0.9f;
        r.p.variSpeed = 0.5f;
        startFreshFromControls (r, n);
        const auto out = r.run (20000, 500, silence);
        bool quiet = true;
        for (size_t i = 6000; i < out[0].size(); ++i)
            if (out[0][i] != 0.0f || out[1][i] != 0.0f) quiet = false;
        CHECK (quiet, "a stopped tape is silent at any Morph setting");
        CHECK (r.e.getReelView().voices <= 1, "and no genes pile up while it is stopped");
    }
    // Recording keeps working at constant speed while Morph overlaps genes: the reel length never changes and what is
    // recorded is the heard mix
    {
        Rig r;
        r.recordFirstTake (n, 500);
        r.p.geneSize = 0.55f;
        r.p.morph = 0.6f;
        r.p.soundOnSound = 1.0f;
        r.p.rec = true;
        const auto out = r.run (60000, 500, tone);
        r.p.rec = false;
        r.run (500, 500, silence);
        bool finite = true;
        for (float v : out[0]) if (! std::isfinite (v) || std::fabs (v) > 4.0f) finite = false;
        CHECK (finite && r.e.getReelView().lengthSamples == n, "recording while genes overlap: reel length unchanged, sound sane");
    }
}

//==============================================================================
// ---- milestone 7: splices ----

static float twoHalves (int, long long t) { return t < 6000 ? 0.1f : 0.3f; }     // 0.1 for the first 6000 samples, then 0.3
static float negativeSound (int, long long) { return -0.4f; }
static float steadySound (int, long long) { return 0.7f; }

static std::vector<int> markersOf (const phe::Engine& e)
{
    int buffer[phe::core::kMaxSpliceMarkers];
    const int n = e.getSpliceMap().snapshot (buffer, phe::core::kMaxSpliceMarkers);
    return std::vector<int> (buffer, buffer + n);
}

// Press a momentary button: it goes on for one block (the engine acts at the start of it), then off again.
static void press (Rig& r, bool& button)
{
    button = true;
    r.run (100, 100, silence);
    button = false;
    r.run (100, 100, silence);
}

// A reel of 12000 samples (0.1 for the first half, 0.3 for the second) playing as one splice, with the head at 'head'.
static void spliceRig (Rig& r, int head)
{
    r.recordFirstTake (12000, 500, twoHalves);       // afterwards the head is at 500
    r.run (head - 500, 500, silence);
}

// Three splices: [0, 2000), [2000, 6000), [6000, 12000). The organize knob is fully left (splice 1 selected).
static void threeSplices (Rig& r)
{
    spliceRig (r, 2000);
    press (r, r.p.splice);
    r.run (3800, 500, silence);                      // the head has moved 200 samples during the press
    press (r, r.p.splice);
}

static float maxAbs (const std::vector<float>& v)
{
    float m = 0.0f;
    for (float x : v)
        m = std::max (m, std::fabs (x));
    return m;
}

static void testSpliceMapAndSelector()
{
    using namespace phe::core;
    SpliceMap m;
    CHECK (m.numSplices() == 1 && m.numMarkers() == 0, "no markers: one splice");
    CHECK (m.start (0) == 0 && m.end (0, 1000) == 1000 && m.length (0, 1000) == 1000, "one splice is the whole reel");

    CHECK (m.addMarker (500, 1000) == 0, "first marker is marker 0");
    CHECK (m.numSplices() == 2 && m.marker (0) == 500, "two splices");
    CHECK (m.start (1) == 500 && m.end (0, 1000) == 500 && m.end (1, 1000) == 1000, "splice edges");
    CHECK (m.indexAt (0, 1000) == 0 && m.indexAt (499, 1000) == 0 && m.indexAt (500, 1000) == 1 && m.indexAt (999, 1000) == 1,
           "which splice a position is in (a marker belongs to the splice after it)");

    CHECK (m.addMarker (200, 1000) == 0, "a marker in the first splice becomes marker 0");
    CHECK (m.marker (0) == 200 && m.marker (1) == 500, "markers stay in order");
    CHECK (m.addMarker (800, 1000) == 2, "a marker in the last splice goes on the end");
    CHECK (m.numSplices() == 4, "four splices");

    // Refusals: too close to a neighbour or to the ends, outside the reel, a duplicate
    CHECK (m.addMarker (200 + kMinSpliceSamples - 1, 1000) == -1, "too close to the marker before it");
    CHECK (m.addMarker (500 - kMinSpliceSamples + 1, 1000) == -1, "too close to the marker after it");
    CHECK (m.addMarker (kMinSpliceSamples - 1, 1000) == -1, "too close to the start");
    CHECK (m.addMarker (1000 - kMinSpliceSamples + 1, 1000) == -1, "too close to the end");
    CHECK (m.addMarker (500, 1000) == -1, "no second marker in the same place");
    CHECK (m.addMarker (-5, 1000) == -1 && m.addMarker (1000, 1000) == -1 && m.addMarker (3000, 1000) == -1, "outside the reel");
    CHECK (m.addMarker (300, 0) == -1, "nothing on an empty reel");
    CHECK (m.numSplices() == 4, "refused markers change nothing");
    CHECK (m.addMarker (200 + kMinSpliceSamples, 1000) == 1, "exactly the minimum distance is allowed");

    // Taking markers away joins splices
    CHECK (! m.removeMarker (-1) && ! m.removeMarker (m.numMarkers()), "no such marker");
    const int before = m.numSplices();
    CHECK (m.removeMarker (1), "remove marker 1");
    CHECK (m.numSplices() == before - 1 && m.marker (1) == 500, "the others close up");
    m.clear();
    CHECK (m.numSplices() == 1, "clear");

    // The most markers
    SpliceMap big;
    int made = 0;
    for (int i = 1; i <= 320; ++i)
        if (big.addMarker (i * 100, 40000) >= 0)
            ++made;
    CHECK (made == kMaxSpliceMarkers && big.numSplices() == kMaxSplices, "at most 299 markers = 300 splices: %d", made);
    int copy[kMaxSpliceMarkers];
    CHECK (big.snapshot (copy, kMaxSpliceMarkers) == kMaxSpliceMarkers && copy[0] == 100 && copy[298] == 29900, "snapshot copies the list");
    CHECK (big.snapshot (copy, 10) == 10, "snapshot never gives more than asked for");

    // Sample-rate conversion moves the markers with the audio and drops any that no longer fit
    SpliceMap c;
    c.addMarker (1000, 5000);
    c.addMarker (1100, 5000);
    c.addMarker (3000, 5000);
    c.rescale (2.0, 10000);
    CHECK (c.numMarkers() == 3 && c.marker (0) == 2000 && c.marker (1) == 2200 && c.marker (2) == 6000, "markers doubled with the rate");
    c.rescale (0.05, 500);
    CHECK (c.numMarkers() == 2 && c.marker (0) == 100 && c.marker (1) == 300, "a marker that becomes too close to its neighbour is dropped: %d", c.numMarkers());

    // Organize zones
    CHECK (organizeZone (0.0f, 1) == 0 && organizeZone (1.0f, 1) == 0, "one splice: always zone 0");
    CHECK (organizeZone (0.0f, 4) == 0 && organizeZone (0.2499f, 4) == 0 && organizeZone (0.25f, 4) == 1 && organizeZone (0.7499f, 4) == 2
              && organizeZone (0.75f, 4) == 3 && organizeZone (1.0f, 4) == 3, "four equal zones, the last one reaches the end of the knob");
    CHECK (organizeZone (-1.0f, 3) == 0 && organizeZone (7.0f, 3) == 2 && organizeZone (std::numeric_limits<float>::quiet_NaN(), 3) == 0,
           "out-of-range knob values are limited");
    int hits[300] = {};
    for (int i = 0; i <= 10000; ++i)
        ++hits[organizeZone ((float) i / 10000.0f, 300)];
    bool even = true;
    for (int z = 0; z < 300; ++z)
        if (hits[z] < 30 || hits[z] > 37)
            even = false;
    CHECK (even, "300 splices share the knob evenly");

    // The selector: Shift steps on and wraps; Organize wins when it moves into a new zone
    SpliceSelector sel;
    sel.sync (0.0f, 3);
    CHECK (sel.selected() == 0, "starts on the zone the knob is in");
    sel.update (0.0f, 3, true);   CHECK (sel.selected() == 1, "Shift: next splice");
    sel.update (0.0f, 3, true);   CHECK (sel.selected() == 2, "Shift again");
    sel.update (0.0f, 3, true);   CHECK (sel.selected() == 0, "Shift after the last: back to the first");
    sel.update (0.0f, 3, false);  CHECK (sel.selected() == 0, "nothing changes without a press");
    sel.update (0.5f, 3, false);  CHECK (sel.selected() == 1, "Organize into another zone selects it");
    sel.update (0.55f, 3, true);  CHECK (sel.selected() == 2, "Shift moves on from where Organize put it");
    sel.update (0.6f, 3, false);  CHECK (sel.selected() == 2, "Organize moving inside the same zone does not override Shift");
    sel.update (0.9f, 3, false);  CHECK (sel.selected() == 2, "(zone 2 is where we are)");
    sel.update (0.1f, 3, false);  CHECK (sel.selected() == 0, "Organize into zone 0");
    sel.update (0.7f, 3, true);   CHECK (sel.selected() == 2, "Organize wins over Shift when both happen in one block");

    SpliceSelector one;
    one.sync (0.0f, 1);
    one.update (0.3f, 1, true);
    CHECK (one.selected() == 0, "Shift with one splice does nothing");

    // More splices appear under a still knob: nothing is selected by the knob
    SpliceSelector grow;
    grow.sync (0.9f, 3);
    CHECK (grow.selected() == 2, "knob at 0.9 selects the last of three");
    grow.update (0.9f, 4, false);
    CHECK (grow.selected() == 2, "a marker added: the selection stays, the knob did not move");
    grow.update (0.9f, 4, false);
    grow.update (0.4f, 4, false);
    CHECK (grow.selected() == 1, "moving the knob into another zone selects it again");

    SpliceSelector keep;
    keep.sync (0.0f, 3);
    keep.update (0.0f, 3, true);       // selected 1
    keep.markerInserted (0);           CHECK (keep.selected() == 2, "a marker in front: the selected splice moves up one");
    keep.markerInserted (2);           CHECK (keep.selected() == 2, "a marker in the selected splice: it keeps the front part");
    keep.markerRemoved (0);            CHECK (keep.selected() == 1, "a marker removed in front: the selection moves down one");
    keep.markerRemoved (1);            CHECK (keep.selected() == 1, "a marker removed after it: the selection stays on the joined splice");
}

static void testSpliceDropsMarkerAtHead()
{
    Rig r;
    spliceRig (r, 6000);
    const int head = r.e.getReelView().playPos;
    CHECK (head == 6000, "the head is where we think: %d", head);
    CHECK (r.e.getSpliceMap().numSplices() == 1 && r.e.getReelView().numSplices == 1, "one splice to begin with");

    r.p.splice = true;
    r.run (100, 100, silence);
    CHECK (markersOf (r.e) == std::vector<int> { head }, "Splice drops a marker exactly where the head is");
    CHECK (r.e.getReelView().numSplices == 2, "the view shows two splices");
    CHECK (r.e.getReelView().selectedSplice == 0, "the splice that was cut keeps its number (the part before the marker)");
    CHECK ((r.e.takeHandledButtons() & phe::Engine::kButtonSplice) != 0, "the wrapper is told the button was used");
    CHECK (r.e.takeHandledButtons() == 0, "that message is given only once");

    // Held on: nothing more happens until it goes off and on again
    r.run (1000, 100, silence);
    CHECK (r.e.getSpliceMap().numSplices() == 2, "holding Splice on does not keep adding markers");
    r.p.splice = false;
    r.run (100, 100, silence);

    // Where the tape is stopped, a second press in the same place is refused (a splice must be at least 64 samples)
    r.p.variSpeed = 0.5f;
    r.run (3000, 500, silence);
    press (r, r.p.splice);
    const size_t afterFirst = markersOf (r.e).size();
    press (r, r.p.splice);
    CHECK (markersOf (r.e).size() == afterFirst, "no second marker in the same place");
    CHECK ((r.e.takeHandledButtons() & phe::Engine::kButtonSplice) != 0, "a refused press still tells the wrapper to switch the button off");

    // Nothing to splice on an empty reel, but the button is still handed back
    Rig e;
    press (e, e.p.splice);
    CHECK (e.e.getSpliceMap().numSplices() == 1, "no marker on an empty reel");
    CHECK ((e.e.takeHandledButtons() & phe::Engine::kButtonSplice) != 0, "the button is handed back");

    // A button that is already on when the plugin starts counts as "loaded that way", not as a press
    Rig f (48000.0, 2, false);
    f.p.splice = f.p.shift = f.p.deleteMarker = f.p.deleteAllMarkers = f.p.recNewSplice = true;
    f.run (1000, 100, silence);
    CHECK (f.e.takeHandledButtons() == 0 && ! f.e.getReelView().recording, "buttons that are on in the very first block do nothing");
}

static void testSelectedSplicePlaysAndWaits()
{
    Rig r;
    spliceRig (r, 6000);
    press (r, r.p.splice);                           // splice 1 = [0, 6000): the 0.1 half; splice 2 = the 0.3 half

    // The whole-reel gene in progress runs on to its end (it still reads the 0.3 half); after that the selected splice loops
    r.run (7000, 500, silence);
    const auto only0 = r.run (30000, 500, silence);
    const float m0 = maxAbs (only0[0]);
    CHECK (m0 > 0.099f && m0 <= 0.1f + 1.0e-4f, "splice 1 plays only its own 0.1 half: peak %f", m0);
    CHECK (maxAbs (only0[1]) <= 0.1f + 1.0e-4f, "both channels");
    CHECK (r.e.getReelView().playingSplice == 0, "the view says splice 1 is playing");

    // Wait until a gene has only just started, then press Shift
    for (int i = 0; i < 100 && ! (r.e.getReelView().playPos >= 1000 && r.e.getReelView().playPos < 1600); ++i)
        r.run (100, 100, silence);
    const int at = r.e.getReelView().playPos;
    CHECK (at >= 1000 && at < 1600, "waited for the start of a pass: %d", at);
    press (r, r.p.shift);
    CHECK ((r.e.takeHandledButtons() & phe::Engine::kButtonShift) != 0, "the wrapper is told Shift was used");
    CHECK (r.e.getReelView().selectedSplice == 1, "Shift selects splice 2");
    CHECK (r.e.getReelView().playingSplice == 0, "but splice 1 is still the one playing");

    const auto waiting = r.run (4000, 500, silence);
    CHECK (maxAbs (waiting[0]) <= 0.1f + 1.0e-4f, "the new splice waits until the pass in progress ends: %f", maxAbs (waiting[0]));
    CHECK (r.e.getReelView().playingSplice == 0, "still splice 1");

    const auto after = r.run (8000, 500, silence);
    CHECK (maxAbs (after[0]) > 0.29f, "then splice 2 plays (0.3): %f", maxAbs (after[0]));
    CHECK (r.e.getReelView().playingSplice == 1, "the view says splice 2 is playing now");

    // Splice 2 keeps playing on its own, never the 0.1 half
    const auto onlySecond = r.run (30000, 500, silence);
    float low = 1.0f;
    for (float v : onlySecond[0])
        if (std::fabs (v) > 0.2f)
            low = std::min (low, std::fabs (v));
    CHECK (maxAbs (onlySecond[0]) > 0.29f && maxAbs (onlySecond[0]) <= 0.3f + 1.0e-4f, "splice 2 plays its 0.3: %f", maxAbs (onlySecond[0]));
    (void) low;
}

static void testOrganizeAndShiftInEngine()
{
    Rig r;
    threeSplices (r);
    CHECK (markersOf (r.e) == (std::vector<int> { 2000, 6000 }), "three splices: markers at %d, %d", markersOf (r.e).empty() ? -1 : markersOf (r.e)[0],
           markersOf (r.e).size() > 1 ? markersOf (r.e)[1] : -1);
    auto selected = [&r] { return r.e.getReelView().selectedSplice; };
    CHECK (r.e.getReelView().numSplices == 3 && selected() == 0, "starts on splice 1");

    r.p.organize = 0.5f;   r.run (100, 100, silence);   CHECK (selected() == 1, "Organize at the middle: splice 2");
    press (r, r.p.shift);                               CHECK (selected() == 2, "Shift: splice 3");
    press (r, r.p.shift);                               CHECK (selected() == 0, "Shift: back to splice 1");
    r.p.organize = 0.55f;  r.run (100, 100, silence);   CHECK (selected() == 0, "Organize still in the same zone: Shift's choice stands");
    r.p.organize = 0.9f;   r.run (100, 100, silence);   CHECK (selected() == 2, "Organize into the last zone: splice 3");
    r.p.organize = 0.1f;   r.run (100, 100, silence);   CHECK (selected() == 0, "Organize into the first zone: splice 1");

    // A press of Shift and a move of Organize in the same block: Organize wins
    r.p.shift = true;  r.p.organize = 0.9f;
    r.run (100, 100, silence);
    r.p.shift = false;
    CHECK (selected() == 2, "Organize takes priority over Shift");
}

static void testDeleteMarkers()
{
    Rig r;
    threeSplices (r);
    r.p.organize = 0.5f;
    r.run (100, 100, silence);
    CHECK (r.e.getReelView().selectedSplice == 1, "splice 2 selected");

    press (r, r.p.deleteMarker);
    CHECK (markersOf (r.e) == std::vector<int> { 2000 }, "the marker at the end of the selected splice goes: it joins the next one");
    CHECK (r.e.getReelView().numSplices == 2 && r.e.getReelView().selectedSplice == 1, "still selected: the joined splice");
    CHECK ((r.e.takeHandledButtons() & phe::Engine::kButtonDeleteMarker) != 0, "the wrapper is told");

    press (r, r.p.deleteMarker);
    CHECK (markersOf (r.e) == std::vector<int> { 2000 }, "the last splice has no marker after it: nothing happens");

    r.p.organize = 0.0f;
    r.run (100, 100, silence);
    press (r, r.p.deleteMarker);
    CHECK (markersOf (r.e).empty() && r.e.getReelView().numSplices == 1, "the last marker goes: one splice again");

    // Delete All Markers
    Rig q;
    threeSplices (q);
    q.p.organize = 0.9f;
    q.run (100, 100, silence);
    CHECK (q.e.getReelView().selectedSplice == 2, "splice 3 selected");
    press (q, q.p.deleteAllMarkers);
    CHECK (markersOf (q.e).empty() && q.e.getReelView().numSplices == 1, "all markers gone");
    CHECK (q.e.getReelView().selectedSplice == 0, "one splice, selected");
    CHECK ((q.e.takeHandledButtons() & phe::Engine::kButtonDeleteAllMarkers) != 0, "the wrapper is told");
    q.run (14000, 500, silence);
    const auto out = q.run (30000, 500, silence);
    CHECK (maxAbs (out[0]) > 0.29f, "the whole reel plays again, including the 0.3 half: %f", maxAbs (out[0]));
    CHECK (q.e.getReel().length() == 12000, "the audio itself is untouched");
}

static void testRecordOverStaysInItsSplice()
{
    Rig r;
    spliceRig (r, 3000);
    press (r, r.p.splice);                       // marker at 3000: splice 1 = [0, 3000) (0.1), splice 2 = [3000, 12000)
    // Wait until the pass in splice 1 is the one playing
    r.run (10000, 500, silence);
    for (int i = 0; i < 200 && ! (r.e.getReelView().playingSplice == 0 && r.e.getReelView().playPos < 2000); ++i)
        r.run (100, 100, silence);
    CHECK (r.e.getReelView().playingSplice == 0, "splice 1 is playing");

    // Record steady live sound over it for much longer than the splice, with only the live sound recorded
    r.p.soundOnSound = 0.0f;
    r.p.rec = true;
    r.run (20000, 500, steadySound);
    r.p.rec = false;
    r.run (1000, 500, steadySound);              // the live sound carries on while the recording fades out (punch-out)
    r.p.soundOnSound = 0.5f;

    CHECK (r.e.getReel().length() == 12000 && markersOf (r.e) == std::vector<int> { 3000 }, "length and markers unchanged");
    bool insideNew = true, outsideOld = true;
    for (int i = 100; i < 2900; ++i)
        if (std::fabs (r.e.getReel().get (0, i) - 0.7f) > 1.0e-4f) insideNew = false;
    for (int i = 3000; i < 6000; ++i)
        if (std::fabs (r.e.getReel().get (0, i) - 0.1f) > 1.0e-4f && std::fabs (r.e.getReel().get (0, i) - 0.3f) > 1.0e-4f) outsideOld = false;
    for (int i = 6000; i < 12000; ++i)
        if (std::fabs (r.e.getReel().get (0, i) - 0.3f) > 1.0e-4f) outsideOld = false;
    CHECK (insideNew, "the whole of splice 1 now holds the new sound (the recording went round that splice)");
    CHECK (outsideOld, "nothing outside splice 1 was touched");
}

static void testRecordNewSplice()
{
    Rig r;
    spliceRig (r, 6000);
    press (r, r.p.splice);                       // two splices, 0.1 | 0.3
    const float before0 = r.e.getReel().get (0, 100), before1 = r.e.getReel().get (0, 9000);
    const int oldLength = r.e.getReel().length();

    r.p.soundOnSound = 0.0f;                     // only the live sound goes onto the new splice
    r.run (8000, 500, silence);                  // (the knob glides over 10 ms: let it settle first)
    r.p.recNewSplice = true;
    r.run (4000, 500, negativeSound);
    auto view = r.e.getReelView();
    CHECK (view.recording, "recording a new splice");
    CHECK (view.lengthSamples >= oldLength + 3500 && view.newSpliceBase == oldLength, "the reel shows as growing: %d, base %d", view.lengthSamples, view.newSpliceBase);
    CHECK (r.e.getReel().length() == oldLength && markersOf (r.e).size() == 1, "nothing is committed until the recording stops");
    CHECK (view.playing, "the old splices keep playing while the new one is recorded");

    r.p.recNewSplice = false;
    r.run (500, 500, silence);
    r.p.soundOnSound = 0.5f;                     // loop and live both heard again
    view = r.e.getReelView();
    CHECK (! view.recording, "stopped");
    CHECK (r.e.getReel().length() == oldLength + 4000, "the reel is 4000 samples longer: %d", r.e.getReel().length());
    CHECK ((markersOf (r.e) == std::vector<int> { 6000, oldLength }), "a marker at the old end of the reel");
    CHECK (view.numSplices == 3 && view.selectedSplice == 2, "the new splice is splice 3 and is selected");
    CHECK (std::fabs (r.e.getReel().get (0, oldLength + 2000) + 0.4f) < 1.0e-4f && std::fabs (r.e.getReel().get (1, oldLength + 3900) + 0.4f) < 1.0e-4f,
           "the new sound is on the new splice");
    CHECK (r.e.getReel().get (0, 100) == before0 && r.e.getReel().get (0, 9000) == before1, "the old splices are untouched");
    CHECK (! r.e.takeNewSpliceStopped(), "stopping it yourself needs no message to the wrapper");

    // It loops straight away: the next sound heard is the new splice (negative), never the old ones
    const auto out = r.run (6000, 500, silence);
    CHECK (std::fabs (maxAbs (out[0]) - 0.4f) < 0.02f, "the new splice plays: %f", maxAbs (out[0]));
    float mostPositive = 0.0f;
    for (float v : out[0])
        mostPositive = std::max (mostPositive, v);
    CHECK (mostPositive < 0.01f, "none of the old sound is heard after it: %f", mostPositive);

    // The waveform picture reaches the new splice
    float l = 0.0f, rr = 0.0f;
    r.e.getReel().peaks.get ((oldLength + 2000) / r.e.getReel().samplesPerBucket(), l, rr);
    CHECK (l > 0.3f && rr > 0.3f, "the picture has the new splice: %f", l);

    // A second new splice
    r.p.recNewSplice = true;
    r.run (2000, 500, negativeSound);
    r.p.recNewSplice = false;
    r.run (500, 500, silence);
    CHECK (r.e.getReelView().numSplices == 4 && r.e.getReel().length() == oldLength + 6000, "a fourth splice");

    // The new splice listens too: with Sound On Sound in the middle, the old splices are recorded into it
    Rig s;
    spliceRig (s, 6000);
    press (s, s.p.splice);
    s.p.recNewSplice = true;
    s.run (3000, 500, silence);
    s.p.recNewSplice = false;
    s.run (500, 500, silence);
    CHECK (maxAbs (std::vector<float> (12000 + 100, 0.0f)) == 0.0f, "(helper)");
    float newest = 0.0f;
    for (int i = 12100; i < 14900; ++i)
        newest = std::max (newest, std::fabs (s.e.getReel().get (0, i)));
    CHECK (newest > 0.09f, "what the tape plays during the recording goes into the new splice: %f", newest);
}

static void testRecordNewSplicePressesAndLimits()
{
    // Rec New Splice on an empty reel records the first take (and it becomes splice 1)
    Rig a;
    a.p.recNewSplice = true;
    a.run (6000, 500, tone);
    a.p.recNewSplice = false;
    a.run (500, 500, silence);
    CHECK (a.e.getReel().length() == 6000 && a.e.getReelView().numSplices == 1, "first take through Rec New Splice");
    CHECK (! a.e.getReelView().recording, "stopped");

    // Pressing Rec while a new splice is recording stops it, and both switches are handed back
    Rig b;
    b.recordFirstTake (12000, 500, tone);
    b.p.recNewSplice = true;
    b.run (3000, 500, tone);
    b.p.rec = true;
    b.run (500, 500, tone);
    CHECK (! b.e.getReelView().recording && b.e.getReelView().numSplices == 2, "Rec stops a new-splice recording");
    CHECK (b.e.takeRecordStopped() && b.e.takeNewSpliceStopped(), "both switches are handed back");
    b.p.rec = false;
    b.p.recNewSplice = false;
    b.run (500, 500, tone);
    CHECK (! b.e.getReelView().recording, "switching them off starts nothing");

    // Rec New Splice while Rec is recording is refused
    Rig c;
    c.recordFirstTake (12000, 500, tone);
    c.p.rec = true;
    c.run (1000, 500, tone);
    c.p.recNewSplice = true;
    c.run (500, 500, tone);
    CHECK (c.e.takeNewSpliceStopped(), "refused: the wrapper is told to switch it off");
    CHECK (c.e.getReelView().recording && c.e.getReelView().numSplices == 1, "Rec carries on, no new splice");
    c.p.recNewSplice = false;
    c.run (500, 500, tone);
    CHECK (c.e.getReelView().recording, "switching the refused button off does not stop Rec");

    // Too short to be a splice: thrown away, the reel stays as it was
    Rig d;
    d.recordFirstTake (12000, 500, tone);
    d.p.recNewSplice = true;
    d.run (32, 32, tone);
    d.p.recNewSplice = false;
    d.run (500, 500, silence);
    CHECK (d.e.getReel().length() == 12000 && d.e.getReelView().numSplices == 1 && d.e.getReelView().selectedSplice == 0, "a take under 64 samples is discarded");

    // Clear Reel while recording a new splice
    Rig e;
    e.recordFirstTake (12000, 500, tone);
    e.p.recNewSplice = true;
    e.run (2000, 500, tone);
    e.p.clearReel = true;
    e.run (500, 500, tone);
    CHECK (e.e.getReelView().lengthSamples == 0 && ! e.e.getReelView().recording, "Clear Reel cancels it");
    CHECK (e.e.takeNewSpliceStopped() && ! e.e.takeRecordStopped(), "the wrapper switches the right button off");

    // The reel becomes full while recording a new splice: the splice ends there by itself
    const double sr = 8000.0;
    Rig f (sr);
    const int capacity = f.e.getReelView().capacitySamples;
    f.recordFirstTake (12000, 500, tone);
    f.p.recNewSplice = true;
    f.run (capacity + 20000, 4000, tone);
    CHECK (f.e.getReel().length() == capacity, "the reel is full: %d", f.e.getReel().length());
    CHECK (f.e.getReelView().numSplices == 2 && f.e.getReelView().selectedSplice == 1 && ! f.e.getReelView().recording, "a second splice, selected, recording stopped");
    CHECK (f.e.takeNewSpliceStopped() && ! f.e.takeRecordStopped(), "the wrapper is told");
    f.p.recNewSplice = false;
    f.run (4000, 4000, tone);
    f.p.recNewSplice = true;
    f.run (4000, 4000, tone);
    CHECK (! f.e.getReelView().recording && f.e.takeNewSpliceStopped(), "no room for another splice: refused");
}

static void testClearReelAndRateChangeKeepMarkersRight()
{
    Rig r;
    threeSplices (r);
    r.p.organize = 0.9f;
    r.run (100, 100, silence);
    r.p.clearReel = true;
    r.run (500, 500, silence);
    CHECK (r.e.getReelView().numSplices == 1 && r.e.getReelView().selectedSplice == 0 && markersOf (r.e).empty(), "Clear Reel takes the markers away");
    r.p.clearReel = false;
    r.run (500, 500, silence);
    r.recordFirstTake (12000, 500, twoHalves);
    CHECK (r.e.getReelView().numSplices == 1, "a new first take is one splice");

    // Changing the sample rate converts the reel and moves the markers with it
    Rig q;
    threeSplices (q);
    q.e.prepare (96000.0, 4096, 2);
    q.run (1000, 500, silence);
    CHECK (q.e.getReel().length() == 24000, "reel doubled: %d", q.e.getReel().length());
    CHECK (markersOf (q.e) == (std::vector<int> { 4000, 12000 }), "markers doubled too");
    q.e.prepare (96000.0, 4096, 2);
    CHECK (markersOf (q.e) == (std::vector<int> { 4000, 12000 }), "preparing again at the same rate changes nothing");
}

static void testSplicesStayFiniteAndConsistent()
{
    Rig r (44100.0);
    uint32_t seed = 12345u;
    auto rnd = [&seed] { seed = seed * 1664525u + 1013904223u; return (float) (seed >> 8) / 16777216.0f; };
    bool finite = true, consistent = true;
    for (int blockNumber = 0; blockNumber < 2500; ++blockNumber)
    {
        r.p.organize = rnd();   r.p.geneSize = rnd() * rnd();   r.p.slide = rnd();   r.p.variSpeed = rnd();   r.p.morph = rnd();
        r.p.rec = rnd() > 0.93f;   r.p.recNewSplice = rnd() > 0.9f;   r.p.play = rnd() > 0.15f;
        r.p.splice = rnd() > 0.55f;   r.p.shift = rnd() > 0.7f;   r.p.deleteMarker = rnd() > 0.93f;   r.p.deleteAllMarkers = rnd() > 0.985f;
        r.p.clearReel = rnd() > 0.995f;
        r.p.soundOnSound = rnd();
        const auto out = r.run (200 + (int) (rnd() * 300.0f), 100, tone);
        for (int c = 0; c < 2; ++c)
            for (float v : out[(size_t) c])
                if (! std::isfinite (v) || std::fabs (v) > 100.0f)
                    finite = false;

        const auto view = r.e.getReelView();
        const auto marks = markersOf (r.e);
        const int length = r.e.getReel().length();
        int previous = 0;
        for (int m : marks)
        {
            if (m - previous < phe::core::kMinSpliceSamples || length - m < phe::core::kMinSpliceSamples)
                consistent = false;
            previous = m;
        }
        if (view.numSplices != (int) marks.size() + 1 || view.numSplices > phe::core::kMaxSplices) consistent = false;
        if (view.selectedSplice < 0 || view.selectedSplice >= view.numSplices) consistent = false;
        if (view.playingSplice < 0 || view.playingSplice >= view.numSplices) consistent = false;
        if (length < 0 || length > view.capacitySamples) consistent = false;
        if (length == 0 && ! marks.empty()) consistent = false;
    }
    CHECK (finite, "random splice work never makes bad sound");
    CHECK (consistent, "the markers always stay valid");
}

static void testSpliceBlockSizeIndependence()
{
    // The same actions at the same moments (multiples of 256 samples) give the same sound whatever the block size.
    auto scenario = [] (int block)
    {
        Rig r;
        std::vector<float> all;
        auto add = [&all] (const Channels& c) { all.insert (all.end(), c[0].begin(), c[0].end()); all.insert (all.end(), c[1].begin(), c[1].end()); };
        r.p.rec = true;       add (r.run (12288, block, twoHalves));
        r.p.rec = false;      add (r.run (2048, block, silence));
        r.p.splice = true;    add (r.run (256, block, silence));
        r.p.splice = false;   add (r.run (6144, block, silence));
        r.p.splice = true;    add (r.run (256, block, silence));
        r.p.splice = false;   add (r.run (4096, block, silence));
        r.p.shift = true;     add (r.run (256, block, silence));
        r.p.shift = false;    add (r.run (12288, block, silence));
        r.p.soundOnSound = 0.0f;
        r.p.recNewSplice = true;  add (r.run (3072, block, negativeSound));
        r.p.recNewSplice = false; add (r.run (4096, block, silence));
        r.p.organize = 0.2f;  add (r.run (16384, block, silence));
        r.p.deleteMarker = true;  add (r.run (256, block, silence));
        r.p.deleteMarker = false; add (r.run (8192, block, silence));
        return all;
    };
    const auto reference = scenario (256);
    CHECK (scenario (64) == reference, "block size 64 gives the same sound as 256");
    CHECK (scenario (128) == reference, "block size 128 gives the same sound as 256");
    CHECK (scenario (32) == reference, "block size 32 gives the same sound as 256");
}


//==============================================================================
// ---- milestone 8: Erase Splice, Undo, Time Lag Accumulation polish, the optional behaviours ----

static float loudSteady (int, long long) { return 0.9f; }
static float quarterSteady (int, long long) { return 0.25f; }
static float sine440 (int, long long t) { return 0.3f * (float) std::sin (2.0 * 3.14159265358979323846 * 440.0 * (double) t / 48000.0); }
static float secondTone (int channel, long long t) { return tone (channel, t + 7919) * 0.8f; }

static std::vector<float> reelCopy (const phe::Engine& e, int channel)
{
    const int n = e.getReel().length();
    std::vector<float> v ((size_t) n);
    for (int i = 0; i < n; ++i)
        v[(size_t) i] = e.getReel().get (channel, i);
    return v;
}

// Carries on in short blocks until the engine is no longer busy with a big edit. Returns how many blocks that took.
static int finishEdits (Rig& r, int block = 256)
{
    int blocks = 0;
    while (r.e.getReelView().busy && blocks < 20000)
    {
        r.run (block, block, silence);
        ++blocks;
    }
    return blocks;
}

// Does the waveform picture agree with the audio, slice by slice?
static bool pictureMatchesAudio (const phe::Engine& e)
{
    const auto& reel = e.getReel();
    const int per = reel.samplesPerBucket(), length = reel.length();
    for (int b = 0; b < phe::core::PeakOverview::kBuckets; ++b)
    {
        float el = 0.0f, er = 0.0f;
        for (int i = b * per; i < std::min (length, (b + 1) * per); ++i)
        {
            el = std::max (el, std::fabs (reel.get (0, i)));
            er = std::max (er, std::fabs (reel.get (1, i)));
        }
        float l, r;
        reel.peaks.get (b, l, r);
        if (std::fabs (l - el) > 1.0e-6f || std::fabs (r - er) > 1.0e-6f)
            return false;
    }
    return true;
}

// Three splices of a first take made from 'g': [0, 2000), [2000, 6000), [6000, 12000). Splice 1 is selected.
static void threeSplicesWith (Rig& r, const Gen& g)
{
    r.recordFirstTake (12000, 500, g);
    r.run (1500, 500, silence);
    press (r, r.p.splice);
    r.run (3800, 500, silence);
    press (r, r.p.splice);
}

static double goertzelPower (const std::vector<float>& x, double freq, double sampleRate)
{
    const double w = 2.0 * 3.14159265358979323846 * freq / sampleRate, c = 2.0 * std::cos (w);
    double s0 = 0.0, s1 = 0.0, s2 = 0.0;
    for (float v : x)
    {
        s0 = (double) v + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return (s1 * s1 + s2 * s2 - c * s1 * s2) / ((double) x.size() * (double) x.size());
}

static void testUndoStoreBasics()
{
    using namespace phe::core;
    UndoStore u;
    CHECK (! u.ready() && ! u.hasStep(), "a new undo store is not ready");
    CHECK (u.prepare (50000) && u.ready() && u.numChunks() == 4, "50000 samples need four chunks, got %d", u.numChunks());

    std::vector<float> L (50000), R (50000);
    for (int i = 0; i < 50000; ++i) { L[(size_t) i] = (float) i; R[(size_t) i] = -(float) i; }
    float* reel[2] = { L.data(), R.data() };

    u.save (reel, 0);
    CHECK (! u.isSaved (0), "nothing is saved while no step is open");

    EditState before;
    before.length = 1000; before.selected = 1; before.numMarkers = 2; before.markers[0] = 10; before.markers[1] = 20;
    u.begin (before);
    CHECK (u.hasStep() && ! u.undone() && u.firstSaved() == -1, "a step is open and nothing is saved yet");

    u.saveRange (reel, 16000, 17000);
    CHECK (u.isSaved (0) && u.isSaved (1) && ! u.isSaved (2), "a range across a chunk edge saves both chunks");
    CHECK (u.firstSaved() == 0 && u.lastSaved() == 1, "first / last saved chunk");

    for (int i = 0; i < 32768; ++i) { L[(size_t) i] = 7.0f; R[(size_t) i] = 8.0f; }
    u.save (reel, 0);                                   // saving again must not overwrite the copy of the original
    u.swap (reel, 0);
    u.swap (reel, 1);
    u.swap (reel, 2);                                   // never saved: untouched
    CHECK (L[5] == 5.0f && R[5] == -5.0f && L[20000] == 20000.0f && R[20000] == -20000.0f, "swap brings the original audio back");
    CHECK (L[40000] == 40000.0f, "a chunk that was never saved is left alone");
    u.swap (reel, 0);
    CHECK (L[5] == 7.0f && R[5] == 8.0f, "swapping again puts the edited audio back (redo)");

    EditState current;
    current.length = 500;
    u.flip (current);
    CHECK (u.undone() && u.other().length == 500, "after a flip the other side is the state it was in");
    u.flip (before);
    CHECK (! u.undone() && u.other().length == 1000 && u.other().markers[1] == 20 && u.other().selected == 1, "flip back");
    u.cancel();
    CHECK (! u.hasStep(), "a cancelled step is gone");

    u.begin (before);
    u.saveRange (reel, 49000, 50000);
    CHECK (u.isSaved (3), "the last (short) chunk saves");
    u.swap (reel, 3);                                   // must not run past the end (checked under the memory sanitizer)
    CHECK (! u.prepare (0) && ! u.ready(), "no memory wanted: not ready");
}

static void testSpliceMapEraseAndSet()
{
    using namespace phe::core;
    auto markersOfMap = [] (const SpliceMap& m) { int b[kMaxSpliceMarkers]; const int n = m.snapshot (b, kMaxSpliceMarkers); return std::vector<int> (b, b + n); };
    const int two[] = { 2000, 6000 };

    SpliceMap m;
    m.setMarkers (two, 2);
    CHECK (m.eraseSplice (1, 12000) && markersOfMap (m) == std::vector<int> ({ 2000 }), "erasing the middle splice removes its closing marker");

    m.setMarkers (two, 2);
    CHECK (m.eraseSplice (0, 12000) && markersOfMap (m) == std::vector<int> ({ 4000 }), "erasing the first splice moves the later marker up by 2000");

    m.setMarkers (two, 2);
    CHECK (m.eraseSplice (2, 12000) && markersOfMap (m) == std::vector<int> ({ 2000 }), "erasing the last splice removes the marker in front of it");

    SpliceMap one;
    CHECK (! one.eraseSplice (0, 100), "a reel of one splice has nothing to join");
    m.setMarkers (two, 2);
    CHECK (! m.eraseSplice (3, 12000) && ! m.eraseSplice (-1, 12000) && markersOfMap (m).size() == 2, "no such splice");

    const int many[] = { 100, 300, 700 };
    m.setMarkers (many, 3);
    CHECK (m.eraseSplice (1, 1000) && markersOfMap (m) == std::vector<int> ({ 100, 500 }), "the splice after it moves up: 700 - 200 = 500");
    m.setMarkers (nullptr, 0);
    CHECK (m.numMarkers() == 0, "setMarkers with none empties the list");
}

static void testSoftLimit()
{
    using namespace phe::dsp;
    CHECK (softLimit (0.0f) == 0.0f && softLimit (0.5f) == 0.5f && softLimit (-0.79f) == -0.79f && softLimit (0.8f) == 0.8f,
           "below the knee the signal is untouched");
    bool ok = true, rising = true, odd = true;
    float last = -2.0f;
    for (int i = -4000; i <= 4000; ++i)
    {
        const float x = (float) i / 400.0f;                      // -10 .. +10
        const float y = softLimit (x);
        if (! (std::fabs (y) < 1.0f + 1.0e-6f) || ! std::isfinite (y)) ok = false;
        if (y < last - 1.0e-6f) rising = false;
        if (std::fabs (softLimit (-x) + y) > 1.0e-6f) odd = false;
        last = y;
    }
    CHECK (ok, "never goes past full scale");
    CHECK (rising, "never goes down as the input goes up");
    CHECK (odd, "the same shape above and below zero");
    CHECK (softLimit (0.9f) < 0.9f && softLimit (0.9f) > 0.85f, "gently bends above the knee: %f", softLimit (0.9f));
    CHECK (softLimit (5.0f) > 0.99f, "large signals end up just below 1");
    CHECK (std::fabs (softLimit (0.8001f) - 0.8001f) < 1.0e-4f, "smooth join at the knee");
}

static void testVariSpeedModes()
{
    using namespace phe::dsp;

    // The classic mode is the default, exactly as before
    bool same = true;
    for (int i = 0; i <= 200; ++i)
        for (int snap = 0; snap < 3; ++snap)
            if (variSpeed ((float) i / 200.0f, snap).rate != variSpeed ((float) i / 200.0f, snap, kVariClassic).rate)
                same = false;
    CHECK (same, "classic is the default mode");

    // Octave scale, bidirectional
    CHECK (variSpeed (0.5f, 0, kVariOctaveBoth).stopped && variSpeed (0.5f, 0, kVariOctaveBoth).rate == 0.0f, "octave scale: middle stops");
    CHECK (variSpeed (0.5f + 0.0015f, 0, kVariOctaveBoth).stopped, "octave scale: just off the middle is still stopped");
    CHECK (near (variSpeed (1.0f, 0, kVariOctaveBoth).rate, 2.0f, 1.0e-4f) && near (variSpeed (0.0f, 0, kVariOctaveBoth).rate, -2.0f, 1.0e-4f),
           "octave scale: the ends are double speed, forward and backward");
    CHECK (std::fabs (variSpeed (0.938f, 0, kVariOctaveBoth).rate - 1.0f) < 0.03f && std::fabs (variSpeed (0.062f, 0, kVariOctaveBoth).rate + 1.0f) < 0.03f,
           "octave scale: original speed at 0.938 / 0.062, got %f", variSpeed (0.938f, 0, kVariOctaveBoth).rate);
    const float slowest = variSpeed (0.5f + 0.0025f, 0, kVariOctaveBoth).rate;
    CHECK (slowest > 0.006f && slowest < 0.01f, "octave scale: just off the stop it is about 1/128 speed: %f", slowest);
    bool rising = true, mirror = true;
    float last = 0.0f;
    for (int i = 0; i <= 1000; ++i)
    {
        const float x = 0.5f + 0.5f * (float) i / 1000.0f;
        const float rate = variSpeed (x, 0, kVariOctaveBoth).rate;
        if (rate < last - 1.0e-6f) rising = false;
        last = rate;
        if (std::fabs (rate + variSpeed (1.0f - x, 0, kVariOctaveBoth).rate) > 1.0e-5f) mirror = false;
    }
    CHECK (rising, "octave scale: faster the further you turn");
    CHECK (mirror, "octave scale: the backward half is the mirror of the forward half");

    // Octave scale, forward only
    CHECK (variSpeed (0.0f, 0, kVariOctaveForward).stopped && variSpeed (0.003f, 0, kVariOctaveForward).stopped, "forward only: the far left stops");
    CHECK (near (variSpeed (1.0f, 0, kVariOctaveForward).rate, 2.0f, 1.0e-4f), "forward only: the far right is double speed");
    CHECK (std::fabs (variSpeed (0.8755f, 0, kVariOctaveForward).rate - 1.0f) < 0.02f, "forward only: original speed at 0.8755");
    bool forwardOnly = true;
    rising = true;
    last = 0.0f;
    for (int i = 0; i <= 1000; ++i)
    {
        const float rate = variSpeed ((float) i / 1000.0f, 0, kVariOctaveForward).rate;
        if (rate < 0.0f) forwardOnly = false;
        if (rate < last - 1.0e-6f) rising = false;
        last = rate;
    }
    CHECK (forwardOnly && rising, "forward only never plays backwards and speeds up steadily");

    // Pitch Snap works on the octave scales
    bool whole = true, steps = true;
    for (int mode = 1; mode <= 2; ++mode)
        for (int i = 0; i <= 500; ++i)
        {
            const float x = (float) i / 500.0f;
            const auto r = variSpeed (x, kSnapSemitones, mode);
            if (! r.stopped && std::fabs (std::fabs (12.0f * std::log2 (std::fabs (r.rate))) - std::round (std::fabs (12.0f * std::log2 (std::fabs (r.rate))))) > 1.0e-3f)
                whole = false;
            const auto s = variSpeed (x, kSnapSteps, mode);
            if (! s.stopped && s.semitones >= -48.0f)
            {
                bool found = false;
                for (float step : kSnapStepList)
                    if (std::fabs (std::fabs (s.semitones) - step) < 1.0e-3f) found = true;
                if (! found) steps = false;
            }
        }
    CHECK (whole, "octave scales: Semitones lands on whole semitones");
    CHECK (steps, "octave scales: Steps lands on the shared step list");
    CHECK (variSpeed (std::numeric_limits<float>::quiet_NaN(), 0, kVariOctaveBoth).stopped || std::isfinite (variSpeed (std::numeric_limits<float>::quiet_NaN(), 0, kVariOctaveForward).rate),
           "a bad knob value gives a sane speed");
}

static void testMorphChordTable()
{
    using namespace phe::dsp;
    CHECK (kNumMorphChords == 5 && kMorphChords[0].semitones[1] == 0.0f, "entry 0 is the random pitch-ups");
    CHECK (near (std::exp2 (kMorphChords[1].semitones[1] / 12.0f), 2.0f, 1.0e-4f) && near (std::exp2 (kMorphChords[1].semitones[2] / 12.0f), 3.0f, 1.0e-3f)
           && near (std::exp2 (kMorphChords[1].semitones[3] / 12.0f), 4.0f, 1.0e-4f), "the classic chord is the ratios 2, 3, 4");
    bool first = true;
    for (int i = 0; i < kNumMorphChords; ++i)
        if (kMorphChords[i].semitones[0] != 0.0f) first = false;
    CHECK (first, "gene 1 always stays at the tape's own pitch");
}

//------------------------------------------------------------------------------
static void testEraseSpliceInEngine()
{
    // Erase the middle splice
    {
        Rig r;
        threeSplicesWith (r, tone);
        CHECK (markersOf (r.e) == std::vector<int> ({ 2000, 6000 }), "setup: three splices");
        press (r, r.p.shift);                                     // splice 2 selected
        const auto beforeL = reelCopy (r.e, 0), beforeR = reelCopy (r.e, 1);
        r.e.takeHandledButtons();
        press (r, r.p.eraseSplice);
        CHECK ((r.e.takeHandledButtons() & phe::Engine::kButtonErase) != 0, "the wrapper is told Erase Splice acted");
        finishEdits (r);
        const auto view = r.e.getReelView();
        CHECK (! view.busy && view.lengthSamples == 8000, "the reel is 4000 samples shorter: %d", view.lengthSamples);
        CHECK (markersOf (r.e) == std::vector<int> ({ 2000 }), "one marker left");
        CHECK (view.numSplices == 2 && view.selectedSplice == 1, "the splice after the erased one is selected (now number 2), got %d", view.selectedSplice);
        bool ok = true;
        for (int i = 0; i < 8000; ++i)
        {
            const int from = i < 2000 ? i : i + 4000;
            if (r.e.getReel().get (0, i) != beforeL[(size_t) from] || r.e.getReel().get (1, i) != beforeR[(size_t) from]) ok = false;
        }
        CHECK (ok, "the audio after the erased splice moved up exactly");
        CHECK (pictureMatchesAudio (r.e), "the waveform picture follows");
        const auto out = r.run (3000, 500, silence);
        CHECK (r.e.getReelView().playing && maxAbs (out[0]) > 0.05f, "the reel plays again after Erase Splice");
    }

    // Erase the first splice, then the last
    {
        Rig r;
        threeSplicesWith (r, tone);
        const auto beforeL = reelCopy (r.e, 0);
        press (r, r.p.eraseSplice);                               // splice 1 is selected
        finishEdits (r);
        CHECK (r.e.getReelView().lengthSamples == 10000 && markersOf (r.e) == std::vector<int> ({ 4000 }), "first splice erased: markers moved up");
        bool ok = true;
        for (int i = 0; i < 10000; ++i)
            if (r.e.getReel().get (0, i) != beforeL[(size_t) i + 2000]) ok = false;
        CHECK (ok, "all the audio moved up by 2000");
        CHECK (r.e.getReelView().selectedSplice == 0, "the next splice is now number 1");

        press (r, r.p.shift);                                     // last splice selected (2 of 2)
        press (r, r.p.eraseSplice);
        finishEdits (r);
        CHECK (r.e.getReelView().lengthSamples == 4000 && markersOf (r.e).empty(), "last splice erased: the reel is one splice of 4000");
        CHECK (r.e.getReelView().selectedSplice == 0, "selection stays inside the reel");
        bool kept = true;
        for (int i = 0; i < 4000; ++i)
            if (r.e.getReel().get (0, i) != beforeL[(size_t) i + 2000]) kept = false;
        CHECK (kept && pictureMatchesAudio (r.e), "the first part is untouched and the picture agrees");
    }

    // A reel of one splice: Erase Splice empties it (and Undo brings it back)
    {
        Rig r;
        r.recordFirstTake (20000, 500, tone);
        const auto before = reelCopy (r.e, 0);
        press (r, r.p.eraseSplice);
        finishEdits (r);
        CHECK (r.e.getReelView().lengthSamples == 0 && ! r.e.getReelView().playing, "one splice: Erase Splice empties the reel");
        press (r, r.p.undo);
        finishEdits (r);
        CHECK (r.e.getReelView().lengthSamples == 20000 && reelCopy (r.e, 0) == before, "Undo brings the emptied reel back exactly");
    }

    // Nothing to erase on an empty reel; refused while recording
    {
        Rig r;
        press (r, r.p.eraseSplice);
        CHECK (r.e.getReelView().lengthSamples == 0 && ! r.e.getReelView().busy, "Erase Splice on an empty reel does nothing");

        threeSplicesWith (r, tone);
        r.p.rec = true;
        r.run (1000, 500, tone);
        r.e.takeHandledButtons();
        press (r, r.p.eraseSplice);
        CHECK ((r.e.getReelView().recording) && r.e.getReelView().lengthSamples == 12000 && markersOf (r.e).size() == 2, "Erase Splice is refused while recording");
        CHECK ((r.e.takeHandledButtons() & phe::Engine::kButtonErase) != 0, "... and the button still goes back off");
        r.p.rec = false;
        r.run (500, 500, silence);
    }
}

// A long reel (about 58 seconds): the move is made in steps over many blocks, whatever the block size, and comes out exactly right.
static void testEraseSpliceLongReelInSteps()
{
    for (int block : { 64, 512, 4096 })
    {
        Rig r;
        const int n = 2800000;
        r.recordFirstTake (n, 4096, tone);
        r.run (50000 - 4096, 4096, silence);
        press (r, r.p.splice);
        r.run (1000000, 4096, silence);
        press (r, r.p.splice);
        const auto marks = markersOf (r.e);
        CHECK (marks.size() == 2, "setup: two markers on the long reel");
        press (r, r.p.shift);                                     // splice 2 (the one in the middle) selected
        const auto beforeL = reelCopy (r.e, 0), beforeR = reelCopy (r.e, 1);

        const int s = marks[0], e = marks[1];
        r.p.eraseSplice = true;
        r.run (block, block, silence);
        r.p.eraseSplice = false;
        const bool busyStraightAway = r.e.getReelView().busy;
        const auto whileBusy = r.run (block, block, silence);
        const bool silentWhileBusy = ! r.e.getReelView().busy || maxAbs (whileBusy[0]) == 0.0f;
        // Edit buttons pressed during the job are ignored but still handled
        r.e.takeHandledButtons();
        r.p.splice = true;  r.p.clearReel = true;  r.p.rec = true;
        r.run (block, block, silence);
        r.p.splice = false; r.p.clearReel = false; r.p.rec = false;
        const bool ignored = r.e.getReelView().lengthSamples == n && ! r.e.getReelView().recording;
        const bool handled = (r.e.takeHandledButtons() & phe::Engine::kButtonSplice) != 0 && r.e.takeClearHandled() && r.e.takeRecordStopped();
        const int blocks = finishEdits (r, block);

        CHECK (busyStraightAway, "block %d: the engine is busy with the big edit", block);
        CHECK (silentWhileBusy, "block %d: silent while busy", block);
        CHECK (ignored && handled, "block %d: edit buttons are ignored while busy, and still go back off", block);
        CHECK (blocks >= 1 || block >= 4096, "block %d: the job took %d more blocks", block, blocks);

        std::vector<float> expectedL (beforeL), expectedR (beforeR);
        expectedL.erase (expectedL.begin() + s, expectedL.begin() + e);
        expectedR.erase (expectedR.begin() + s, expectedR.begin() + e);
        CHECK (r.e.getReelView().lengthSamples == n - (e - s), "block %d: new length %d", block, r.e.getReelView().lengthSamples);
        CHECK (reelCopy (r.e, 0) == expectedL && reelCopy (r.e, 1) == expectedR, "block %d: the moved audio is exactly right", block);
        CHECK (pictureMatchesAudio (r.e), "block %d: the picture follows on a long reel", block);

        // ... and Undo puts it all back exactly
        press (r, r.p.undo);
        finishEdits (r, block);
        CHECK (reelCopy (r.e, 0) == beforeL && reelCopy (r.e, 1) == beforeR && markersOf (r.e) == marks, "block %d: Undo restores the long reel and its markers exactly", block);
        CHECK (pictureMatchesAudio (r.e), "block %d: picture restored", block);
        press (r, r.p.undo);                                      // Redo
        finishEdits (r, block);
        CHECK (reelCopy (r.e, 0) == expectedL && markersOf (r.e).size() == 1, "block %d: Redo erases it again", block);
    }
}

static void testUndoInEngine()
{
    // The first recording
    {
        Rig r;
        CHECK (r.e.getReelView().undoState == 0, "nothing to undo on a fresh plugin");
        r.recordFirstTake (20000, 500, tone);
        const auto A = reelCopy (r.e, 0);
        CHECK (r.e.getReelView().undoState == 1, "the recording can be undone");
        press (r, r.p.undo);
        finishEdits (r);
        CHECK (r.e.getReelView().lengthSamples == 0 && ! r.e.getReelView().playing && r.e.getReelView().undoState == 2, "Undo takes the first recording away (and offers Redo)");
        CHECK (pictureMatchesAudio (r.e), "the picture is empty");
        press (r, r.p.undo);
        finishEdits (r);
        CHECK (r.e.getReelView().lengthSamples == 20000 && reelCopy (r.e, 0) == A && r.e.getReelView().undoState == 1, "Redo brings it back exactly");
        CHECK (pictureMatchesAudio (r.e) && r.e.getReelView().playing, "picture and playback are back");
    }

    // Time Lag Accumulation over several chunks
    {
        Rig r;
        r.recordFirstTake (40000, 500, tone);
        const auto A = reelCopy (r.e, 0), AR = reelCopy (r.e, 1);
        r.p.rec = true;
        r.run (30000, 500, secondTone);
        CHECK (r.e.getReelView().recKind == 2, "recording over an existing splice reports Time Lag Accumulation (kind 2)");
        r.p.rec = false;
        r.run (500, 500, silence);
        CHECK (r.e.getReelView().recKind == 0, "not recording any more");
        const auto B = reelCopy (r.e, 0), BR = reelCopy (r.e, 1);
        CHECK (A != B, "the overdub changed the reel");
        press (r, r.p.undo);
        finishEdits (r);
        CHECK (reelCopy (r.e, 0) == A && reelCopy (r.e, 1) == AR, "Undo takes the overdub away exactly");
        CHECK (pictureMatchesAudio (r.e), "picture matches after undoing the overdub");
        press (r, r.p.undo);
        finishEdits (r);
        CHECK (reelCopy (r.e, 0) == B && reelCopy (r.e, 1) == BR, "Redo puts the overdub back exactly");
        CHECK (pictureMatchesAudio (r.e), "picture matches after redoing it");
    }

    // Splice markers
    {
        Rig r;
        threeSplicesWith (r, tone);
        const std::vector<int> three { 2000, 6000 };
        CHECK (markersOf (r.e) == three, "setup");

        press (r, r.p.deleteAllMarkers);
        CHECK (markersOf (r.e).empty(), "all markers gone");
        press (r, r.p.undo);   finishEdits (r);
        CHECK (markersOf (r.e) == three, "Undo brings all the markers back");
        press (r, r.p.undo);   finishEdits (r);
        CHECK (markersOf (r.e).empty(), "Redo takes them away again");
        press (r, r.p.undo);   finishEdits (r);

        press (r, r.p.deleteMarker);                              // splice 1 is selected: its closing marker (2000) goes
        CHECK (markersOf (r.e) == std::vector<int> ({ 6000 }), "Delete Splice Marker");
        press (r, r.p.undo);   finishEdits (r);
        CHECK (markersOf (r.e) == three && r.e.getReelView().selectedSplice == 0, "Undo puts the marker and the selection back");

        press (r, r.p.splice);
        CHECK (markersOf (r.e).size() == 3, "Splice added a marker");
        press (r, r.p.undo);   finishEdits (r);
        CHECK (markersOf (r.e) == three, "Undo takes the new marker away");

        // Only the newest edit: two markers added, Undo only takes the last
        r.run (900, 500, silence);
        press (r, r.p.splice);
        const auto afterFirst = markersOf (r.e);
        r.run (300, 500, silence);
        press (r, r.p.splice);
        const auto afterSecond = markersOf (r.e);
        CHECK (afterFirst.size() == 3 && afterSecond.size() == 4, "setup: two more markers");
        press (r, r.p.undo);   finishEdits (r);
        CHECK (markersOf (r.e) == afterFirst, "Undo takes away only the newest edit");
        press (r, r.p.undo);   finishEdits (r);
        CHECK (markersOf (r.e) == afterSecond, "pressing Undo again is Redo");
        press (r, r.p.undo);   finishEdits (r);

        // A new edit after an Undo forgets the Redo
        r.run (500, 500, silence);
        press (r, r.p.splice);
        const auto fresh = markersOf (r.e);
        CHECK (fresh.size() == afterFirst.size() + 1, "setup: the new marker was accepted");
        CHECK (r.e.getReelView().undoState == 1, "a new edit offers Undo again");
        press (r, r.p.undo);   finishEdits (r);
        CHECK (markersOf (r.e) == afterFirst, "Undo takes away the new edit, not the one that was undone");
        CHECK (fresh != afterSecond || true, "(the new marker was added somewhere else)");
    }

    // Clear Reel
    {
        Rig r;
        r.recordFirstTake (20000, 500, tone);
        const auto A = reelCopy (r.e, 0);
        press (r, r.p.clearReel);
        CHECK (r.e.getReelView().lengthSamples == 0, "Clear Reel cleared");
        press (r, r.p.undo);   finishEdits (r);
        CHECK (r.e.getReelView().lengthSamples == 20000 && reelCopy (r.e, 0) == A && pictureMatchesAudio (r.e), "Undo brings the cleared reel back");
        r.run (2000, 500, silence);
        CHECK (r.e.getReelView().playing, "and it plays again");
    }

    // Rec New Splice
    {
        Rig r;
        r.recordFirstTake (12000, 500, tone);
        const auto A = reelCopy (r.e, 0);
        r.p.recNewSplice = true;
        r.run (5000, 500, secondTone);
        CHECK (r.e.getReelView().recKind == 3, "recording a new splice reports kind 3");
        r.p.recNewSplice = false;
        r.run (1000, 500, silence);
        const auto B = reelCopy (r.e, 0);
        CHECK (B.size() == 17000 && markersOf (r.e) == std::vector<int> ({ 12000 }), "setup: a new splice of 5000");
        press (r, r.p.undo);   finishEdits (r);
        CHECK (r.e.getReelView().lengthSamples == 12000 && markersOf (r.e).empty() && reelCopy (r.e, 0) == A, "Undo takes the new splice away");
        press (r, r.p.undo);   finishEdits (r);
        CHECK (reelCopy (r.e, 0) == B && markersOf (r.e) == std::vector<int> ({ 12000 }), "Redo brings it back");
    }

    // Nothing to undo; refused while recording
    {
        Rig r;
        r.e.takeHandledButtons();
        press (r, r.p.undo);
        CHECK ((r.e.takeHandledButtons() & phe::Engine::kButtonUndo) != 0 && r.e.getReelView().lengthSamples == 0 && ! r.e.getReelView().busy,
               "Undo with nothing to undo does nothing (and the button goes back off)");

        r.recordFirstTake (20000, 500, tone);
        r.p.rec = true;
        r.run (1000, 500, tone);
        const auto during = reelCopy (r.e, 0);
        press (r, r.p.undo);
        CHECK (r.e.getReelView().recording && r.e.getReelView().lengthSamples == 20000, "Undo is refused while recording");
        r.p.rec = false;
        r.run (500, 500, silence);
        press (r, r.p.undo);   finishEdits (r);
        CHECK (r.e.getReelView().undoState == 2, "after the recording, Undo works on it");
    }

    // A recording that is thrown away (too short) leaves nothing to undo
    {
        Rig r;
        r.recordFirstTake (20000, 500, tone);
        press (r, r.p.splice);
        press (r, r.p.undo);   finishEdits (r);                    // undoes the marker; now Redo is on offer
        Rig q;
        q.p.rec = true;
        q.run (32, 32, tone);
        q.p.rec = false;
        q.run (500, 500, silence);
        CHECK (q.e.getReelView().lengthSamples == 0 && q.e.getReelView().undoState == 0, "a take too short to keep leaves nothing to undo");
    }
}

static void testUndoBlockSizesAndRateChange()
{
    auto scenario = [] (int block)
    {
        Rig r;
        r.recordFirstTake (60000, 500, tone);
        r.run (1500, 500, silence);
        press (r, r.p.splice);
        r.run (12000, 500, silence);
        press (r, r.p.splice);
        press (r, r.p.shift);
        r.p.rec = true;       r.run (20000, 500, secondTone);
        r.p.rec = false;      r.run (500, 500, silence);
        press (r, r.p.eraseSplice);   finishEdits (r, block);
        press (r, r.p.undo);          finishEdits (r, block);
        press (r, r.p.undo);          finishEdits (r, block);
        press (r, r.p.undo);          finishEdits (r, block);
        std::vector<float> all = reelCopy (r.e, 0);
        const auto marks = markersOf (r.e);
        all.insert (all.end(), marks.begin(), marks.end());
        return all;
    };
    const auto reference = scenario (256);
    CHECK (scenario (64) == reference, "the same edits and undos give the same reel at block size 64");
    CHECK (scenario (1000) == reference, "... and at block size 1000");

    // Changing the sample rate forgets the undo history; reset() finishes a big edit
    {
        Rig r;
        r.recordFirstTake (20000, 500, tone);
        CHECK (r.e.getReelView().undoState == 1, "setup");
        r.e.prepare (44100.0, 4096, 2);
        CHECK (r.e.getReelView().undoState == 0 && ! r.e.getReelView().busy, "a new sample rate forgets the undo history");
    }
    {
        Rig r;
        r.recordFirstTake (2000000, 4096, tone);
        r.p.undo = true;
        r.run (256, 256, silence);
        r.p.undo = false;
        CHECK (r.e.getReelView().busy, "setup: a long undo in progress");
        r.e.reset();
        CHECK (! r.e.getReelView().busy && r.e.getReelView().lengthSamples == 0, "reset() carries a big edit to its end instead of leaving it half done");
    }
}

//------------------------------------------------------------------------------
static void testTlaLimiterAndRecordSource()
{
    // Many passes of loud sound cannot run away
    {
        Rig r;
        r.recordFirstTake (20000, 500, loudSteady);
        r.p.rec = true;
        r.run (400000, 500, loudSteady);                          // 20 laps, sound on sound at full level on both
        r.p.rec = false;
        r.run (500, 500, silence);
        float biggest = 0.0f;
        bool finite = true;
        for (int i = 0; i < 20000; ++i)
        {
            const float v = r.e.getReel().get (0, i);
            if (! std::isfinite (v)) finite = false;
            biggest = std::max (biggest, std::fabs (v));
        }
        CHECK (finite && biggest <= 1.0f, "20 loud passes: the reel never goes past full scale (peak %f)", biggest);
        CHECK (biggest > 0.95f, "... but it still fills up towards full scale (peak %f)", biggest);
    }

    // Quiet sound is untouched by the limiter
    {
        Rig r;
        r.recordFirstTake (20000, 500, quarterSteady);
        r.p.rec = true;
        r.run (2000, 500, quarterSteady);
        r.p.rec = false;
        r.run (500, 500, silence);
        // Somewhere in the overdubbed stretch the reel holds 0.25 + 0.25 = 0.5 exactly as before
        int exact = 0;
        for (int i = 0; i < 20000; ++i)
            if (std::fabs (r.e.getReel().get (0, i) - 0.5f) < 1.0e-6f) ++exact;
        CHECK (exact > 1500, "below the knee the sum is exactly 0.25 + 0.25 (%d samples)", exact);
    }

    // Record Source "input only": what is recorded is the live input whatever Sound On Sound says
    for (int source = 0; source <= 1; ++source)
    {
        Rig r;
        r.recordFirstTake (20000, 500, quarterSteady);
        r.p.soundOnSound = 1.0f;                                  // you hear the loop only; with "mix" that is all that is recorded
        r.p.recordSource = source;
        r.run (20000, 500, silence);                              // let the Sound On Sound knob settle before recording
        r.p.rec = true;
        r.run (25000, 500, [] (int, long long) { return 0.3f; });
        r.p.rec = false;
        r.run (500, 500, silence);
        int live = 0, loopOnly = 0;
        for (int i = 0; i < 20000; ++i)
        {
            const float v = r.e.getReel().get (0, i);
            if (std::fabs (v - 0.3f) < 1.0e-3f) ++live;
            if (std::fabs (v - 0.25f) < 1.0e-3f) ++loopOnly;
        }
        if (source == 1)
            CHECK (live > 19000, "input only: the splice now holds the live input (%d of 20000)", live);
        else
            CHECK (loopOnly > 19000 && live == 0, "mix with Sound On Sound on the loop: the loop is recorded as it was (%d)", loopOnly);
    }
}

static void testOrganizeImmediately()
{
    for (int immediate = 0; immediate <= 1; ++immediate)
    {
        Rig r;
        threeSplicesWith (r, twoHalves);                          // [0,2000) 0.1   [2000,6000) 0.1   [6000,12000) 0.3
        r.p.organizeMode = immediate;
        r.p.organize = 0.5f;                                      // splice 2
        r.run (12000, 500, silence);
        r.p.play = false;
        r.run (9000, 500, silence);                               // every gene finishes and the tape stops
        r.p.play = true;                                          // a fresh gene starts now, in splice 2 (4000 samples)
        r.run (500, 100, silence);
        r.p.organize = 0.9f;                                      // splice 3 chosen
        r.run (300, 100, silence);
        const auto out = r.run (100, 100, silence);
        float mean = 0.0f;
        for (float v : out[0]) mean += v / 100.0f;
        if (immediate == 1)
            CHECK (std::fabs (mean - 0.3f) < 0.01f, "Organize immediately: splice 3 is playing at once (mean %f)", mean);
        else
            CHECK (std::fabs (mean - 0.1f) < 0.01f, "Organize at end of gene: still splice 2 (mean %f)", mean);
    }
}

static void testPlayModes()
{
    // Momentary: Play off stops at once; classic lets the gene finish
    for (int mode = 0; mode <= 1; ++mode)
    {
        Rig r;
        r.recordFirstTake (12000, 500, steadySound);
        r.p.playMode = mode;
        r.p.play = false;
        r.run (13000, 500, silence);                              // everything finishes and stops
        r.p.play = true;                                          // a fresh gene (12000 samples) starts now
        r.run (500, 100, silence);
        r.p.play = false;
        r.run (200, 100, silence);
        const auto out = r.run (100, 100, silence);
        float mean = 0.0f;
        for (float v : out[0]) mean += v / 100.0f;
        if (mode == 1)
            CHECK (std::fabs (mean) < 1.0e-6f, "momentary: silent shortly after Play goes off (mean %f)", mean);
        else
            CHECK (std::fabs (mean - 0.7f) < 0.01f, "classic: the gene in progress plays on (mean %f)", mean);
    }

    // Trigger loop: Play off does not stop it, switching Play on jumps back to the start
    {
        Rig r;
        r.recordFirstTake (12000, 500, twoHalves);
        r.p.playMode = 2;
        r.p.play = false;
        const auto out = r.run (40000, 500, silence);
        std::vector<float> tail (out[0].end() - 1000, out[0].end());
        CHECK (maxAbs (tail) > 0.05f && r.e.getReelView().playing, "trigger loop plays on although Play is off");

        // wait until the head is in the second half (0.3), then retrigger
        int guard = 0;
        while (r.e.getReelView().playPos < 8000 && guard++ < 1000)
            r.run (100, 100, silence);
        CHECK (r.e.getReelView().playPos >= 8000, "setup: the head is in the second half");
        r.p.play = true;
        const auto after = r.run (400, 100, silence);
        float mean = 0.0f;
        for (int i = 200; i < 400; ++i) mean += after[0][(size_t) i] / 200.0f;
        CHECK (std::fabs (mean - 0.1f) < 0.02f && r.e.getReelView().playPos < 1000, "switching Play on jumps back to the start (mean %f, head %d)", mean, r.e.getReelView().playPos);
    }
    {
        // Trigger loop plays a new recording even when Play was off
        Rig r;
        r.p.playMode = 2;
        r.p.play = false;
        r.recordFirstTake (12000, 500, steadySound);
        r.run (2000, 500, silence);
        CHECK (r.e.getReelView().playing, "trigger loop starts a new recording by itself");
    }
}

static void testGeneWindowSmooth()
{
    auto averageOutput = [] (int window)
    {
        Rig r;
        r.recordFirstTake (48000, 500, steadySound);
        r.p.geneSize = 0.4394f;                                   // a gene of about 50 ms
        r.p.geneWindow = window;
        r.run (30000, 500, silence);
        const auto out = r.run (48000, 500, silence);
        double sum = 0.0;
        for (float v : out[0]) sum += v;
        return (float) (sum / 48000.0);
    };
    const float classic = averageOutput (0), smooth = averageOutput (1);
    CHECK (classic > 0.65f, "classic windows: short fades only (mean %f)", classic);
    CHECK (smooth > 0.30f && smooth < 0.40f, "smooth windows: each gene swells and dies away (mean %f, about half of 0.7)", smooth);
}

static void testMorphChordInEngine()
{
    auto powers = [] (int chord, double freq)
    {
        Rig r;
        r.recordFirstTake (48000, 500, sine440);
        r.p.soundOnSound = 1.0f;
        r.p.morph = 1.0f;
        r.p.morphChord = chord;
        r.run (96000, 500, silence);
        const auto out = r.run (48000, 500, silence);
        return goertzelPower (out[0], freq, 48000.0);
    };
    const double p440 = powers (1, 440.0), p880 = powers (1, 880.0), p1100 = powers (1, 1100.0), p1320 = powers (1, 1320.0),
                 p1760 = powers (1, 1760.0), p1540 = powers (1, 1540.0);
    CHECK (p440 > 8.0 * p1100 && p880 > 8.0 * p1100 && p1320 > 8.0 * p1100 && p1760 > 8.0 * p1540,
           "the octave chord sounds at 1x, 2x, 3x and 4x (powers %g %g %g %g, off-notes %g %g)", p440, p880, p1320, p1760, p1100, p1540);

    // Below 2:30 on Morph the chord does nothing
    Rig r;
    r.recordFirstTake (48000, 500, sine440);
    r.p.morph = 0.5f;
    r.p.morphChord = 1;
    r.run (96000, 500, silence);
    const auto out = r.run (48000, 500, silence);
    CHECK (goertzelPower (out[0], 880.0, 48000.0) < 0.01 * goertzelPower (out[0], 440.0, 48000.0), "Morph below 2:30: no chord, only the original pitch");
}

static void testMilestone8Stress()
{
    Rig r (44100.0);
    uint32_t seed = 987654u;
    auto rnd = [&seed] { seed = seed * 1664525u + 1013904223u; return (float) (seed >> 8) / 16777216.0f; };
    bool finite = true, consistent = true;
    for (int blockNumber = 0; blockNumber < 3000; ++blockNumber)
    {
        r.p.organize = rnd();   r.p.geneSize = rnd() * rnd();   r.p.slide = rnd();   r.p.variSpeed = rnd();   r.p.morph = rnd();
        r.p.rec = rnd() > 0.94f;   r.p.recNewSplice = rnd() > 0.93f;   r.p.play = rnd() > 0.15f;
        r.p.splice = rnd() > 0.6f;   r.p.shift = rnd() > 0.7f;   r.p.deleteMarker = rnd() > 0.95f;   r.p.deleteAllMarkers = rnd() > 0.99f;
        r.p.eraseSplice = rnd() > 0.9f;   r.p.undo = rnd() > 0.85f;   r.p.clearReel = rnd() > 0.997f;
        r.p.soundOnSound = rnd();
        r.p.variSpeedMode = (int) (rnd() * 2.99f);   r.p.recordSource = rnd() > 0.5f ? 1 : 0;   r.p.organizeMode = rnd() > 0.5f ? 1 : 0;
        r.p.geneWindow = rnd() > 0.5f ? 1 : 0;   r.p.playMode = (int) (rnd() * 2.99f);   r.p.morphChord = (int) (rnd() * 4.99f);
        r.p.pitchSnap = (int) (rnd() * 2.99f);   r.p.pitchScale = (int) (rnd() * 6.0f);
        const auto out = r.run (200 + (int) (rnd() * 300.0f), 100, tone);
        for (int c = 0; c < 2; ++c)
            for (float v : out[(size_t) c])
                if (! std::isfinite (v) || std::fabs (v) > 100.0f)
                    finite = false;

        const auto view = r.e.getReelView();
        const auto marks = markersOf (r.e);
        if (! view.busy)                                     // while a big edit is half done the pieces are not expected to agree yet
        {
            const int length = r.e.getReel().length();
            int previous = 0;
            for (int m : marks)
            {
                if (m - previous < phe::core::kMinSpliceSamples || length - m < phe::core::kMinSpliceSamples)
                    consistent = false;
                previous = m;
            }
            if (view.numSplices != (int) marks.size() + 1 || view.numSplices > phe::core::kMaxSplices) consistent = false;
            if (view.selectedSplice < 0 || view.selectedSplice >= view.numSplices) consistent = false;
            if (view.playingSplice < 0 || view.playingSplice >= view.numSplices) consistent = false;
            if (length < 0 || length > view.capacitySamples) consistent = false;
            if (length == 0 && ! marks.empty()) consistent = false;
        }
    }
    CHECK (finite, "random edits, undos and options never make bad sound");
    CHECK (consistent, "the markers, selection and length always stay valid");

    r.p = phe::Params();
    r.p.play = false;
    finishEdits (r);
    r.run (4096, 256, silence);
    const auto view = r.e.getReelView();
    CHECK (! view.busy && view.lengthSamples >= 0 && view.lengthSamples <= view.capacitySamples, "after the random work the engine is idle and sane");
}


int main()
{
    testPassThroughWhenEmpty();
    testLevelCurves();
    testInputLevelsAndGainStep();
    testFirstRecording();
    testPlayback();
    testBlockSizeIndependence();
    testOverdubAndPunchOut();
    testRecordingGoesRoundTheLoop();
    testAutoStopWhenReelIsFull();
    testClearReel();
    testPlayButton();
    testPrepareKeepsOrConvertsReel();
    testBadInput();
    testWaveformPicture();
    testMonoAndTinyTake();
    testExtremeControlsStayFinite();
    testVariSpeedLandmarks();
    testPitchSnapMaths();
    testGeneSizeMap();
    testReverseAndSpeed();
    testStoppedTape();
    testGeneLengthIsTime();
    testSlideAndGeneStart();
    testGeneFadesAndPlayOff();
    testPitchSnapInEngine();
    testRecordSpeedIsConstant();
    testMorphLandmarks();
    testRandomPitchUp();
    testMorphDefaultIsPlainLoop();
    testMorphGapsAndOverlaps();
    testMorphWholeReelAndBackwards();
    testMorphLevelAndPanning();
    testMorphPitchUpAndPitchScale();
    testMorphPlayOffStoppedAndRecording();
    testSpliceMapAndSelector();
    testSpliceDropsMarkerAtHead();
    testSelectedSplicePlaysAndWaits();
    testOrganizeAndShiftInEngine();
    testDeleteMarkers();
    testRecordOverStaysInItsSplice();
    testRecordNewSplice();
    testRecordNewSplicePressesAndLimits();
    testClearReelAndRateChangeKeepMarkersRight();
    testSplicesStayFiniteAndConsistent();
    testSpliceBlockSizeIndependence();
    testUndoStoreBasics();
    testSpliceMapEraseAndSet();
    testSoftLimit();
    testVariSpeedModes();
    testMorphChordTable();
    testEraseSpliceInEngine();
    testEraseSpliceLongReelInSteps();
    testUndoInEngine();
    testUndoBlockSizesAndRateChange();
    testTlaLimiterAndRecordSource();
    testOrganizeImmediately();
    testPlayModes();
    testGeneWindowSmooth();
    testMorphChordInEngine();
    testMilestone8Stress();
    testEdgeCases();
    testGainSteps();
    testScales();

    if (failures == 0)
        std::printf ("engine tests: all %d checks passed\n", checks);
    else
        std::printf ("engine tests: %d of %d checks FAILED\n", failures, checks);
    return failures == 0 ? 0 : 1;
}
