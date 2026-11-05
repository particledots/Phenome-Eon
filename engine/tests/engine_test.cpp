// Self-test for the PhenomeEon engine. Run automatically by the build; the build fails if any check fails.
// Milestone 4: the reel, recording, Sound On Sound and basic playback.

#include <phe/Engine.h>
#include <phe/core/Gain.h>
#include <phe/core/Scales.h>

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
        r.p.inputGainStep = (int) (rnd() * 3.99f);
        r.p.rec = rnd() > 0.7f;   r.p.play = rnd() > 0.3f;   r.p.clearReel = rnd() > 0.97f;
        r.p.splice = r.p.shift = r.p.undo = r.p.eraseSplice = rnd() > 0.5f;
        const auto out = r.run (256, 256, tone);
        for (int c = 0; c < 2; ++c)
            for (float v : out[(size_t) c])
                if (! std::isfinite (v) || std::fabs (v) > 100.0f) finite = false;
        const int len = r.e.getReelView().lengthSamples;
        if (len < 0 || len > r.e.getReelView().capacitySamples) lengthOk = false;
    }
    CHECK (finite, "random control changes never make bad sound");
    CHECK (lengthOk, "reel length always stays in range");
}

static void testControlsNotYetUsed()
{
    // Gene-Size, Slide, Vari-Speed, Morph, Organize and the pitch menus are stored but do not change the sound yet.
    Rig a, b;
    b.p.geneSize = 1.0f; b.p.slide = 0.0f; b.p.variSpeed = 0.0f; b.p.morph = 1.0f; b.p.organize = 1.0f;
    b.p.pitchSnap = 2; b.p.pitchScale = 28; b.p.splice = b.p.shift = true;
    a.recordFirstTake (12000, 500);
    b.recordFirstTake (12000, 500);
    const auto oa = a.run (30000, 500, tone);
    const auto ob = b.run (30000, 500, tone);
    CHECK (oa[0] == ob[0] && oa[1] == ob[1], "the controls that are not used yet do not change the sound");
    CHECK (b.e.getParams().variSpeed == 0.0f && b.e.getParams().morph == 1.0f, "stored control values");
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
    testControlsNotYetUsed();
    testEdgeCases();
    testGainSteps();
    testScales();

    if (failures == 0)
        std::printf ("engine tests: all %d checks passed\n", checks);
    else
        std::printf ("engine tests: %d of %d checks FAILED\n", failures, checks);
    return failures == 0 ? 0 : 1;
}
