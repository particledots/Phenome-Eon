// Self-test for the PhenomeEon engine. Run automatically by the build; the build fails if any check fails.
// Milestone 2: the engine must pass audio through bit for bit, whatever the controls say.

#include <phe/Engine.h>
#include <phe/core/Scales.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
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

// A test signal: a sine plus a little pseudo-random noise, different on each channel.
static std::vector<float> makeSignal (int channel, int length)
{
    std::vector<float> s ((size_t) length);
    uint32_t seed = 12345u + (uint32_t) channel * 777u;
    for (int i = 0; i < length; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        const float noise = ((float) (seed >> 8) / 16777216.0f - 0.5f) * 0.2f;
        s[(size_t) i] = 0.5f * std::sin (0.05f * (float) i * (float) (channel + 1)) + noise;
    }
    return s;
}

// Runs the engine over numChannels channels of 'length' samples in blocks of 'block' samples
// and reports whether every output sample equals its input sample exactly.
static bool passesThrough (phe::Engine& e, int numChannels, int length, int block)
{
    std::vector<std::vector<float>> original, work;
    for (int c = 0; c < numChannels; ++c)
    {
        original.push_back (makeSignal (c, length));
        work.push_back (original.back());
    }

    for (int pos = 0; pos < length; pos += block)
    {
        const int n = (length - pos < block) ? (length - pos) : block;
        float* ptrs[2] = { work[0].data() + pos, numChannels > 1 ? work[1].data() + pos : nullptr };
        e.process (ptrs, numChannels, n);
    }

    for (int c = 0; c < numChannels; ++c)
        if (std::memcmp (original[(size_t) c].data(), work[(size_t) c].data(), sizeof (float) * (size_t) length) != 0)
            return false;
    return true;
}

static void testPassThrough()
{
    for (int channels = 1; channels <= 2; ++channels)
    {
        for (double sr : { 44100.0, 48000.0, 88200.0, 192000.0 })
        {
            for (int block : { 1, 7, 64, 256, 1000, 4096 })
            {
                phe::Engine e;
                e.prepare (sr, 4096, channels);
                CHECK (passesThrough (e, channels, 20000, block),
                       "channels %d, sample rate %.0f, block %d", channels, sr, block);
            }
        }
    }
}

static void testControlsDoNotChangeSound()
{
    phe::Engine e;
    e.prepare (48000.0, 512, 2);

    // Every control at an extreme, every button on.
    phe::Params p;
    p.inputLevel = 1.0f; p.soundOnSound = 0.0f; p.geneSize = 1.0f; p.slide = 1.0f;
    p.variSpeed = 0.0f;  p.morph = 1.0f;        p.organize = 1.0f; p.outputLevel = 0.0f;
    p.inputGainStep = 3; p.pitchSnap = 2; p.pitchScale = 28;
    p.rec = p.recNewSplice = p.splice = p.shift = p.play = true;
    p.deleteMarker = p.deleteAllMarkers = p.eraseSplice = p.clearReel = p.undo = true;
    e.setParams (p);
    CHECK (passesThrough (e, 2, 20000, 512), "extreme control values");

    // Controls changing in the middle of the audio.
    phe::Params q;
    q.variSpeed = 0.9f; q.morph = 0.1f;
    e.setParams (q);
    CHECK (passesThrough (e, 2, 5000, 100), "control values changed between blocks");

    // The engine remembers the values it was given.
    CHECK (e.getParams().variSpeed == 0.9f && e.getParams().morph == 0.1f, "stored control values");
}

static void testEdgeCases()
{
    phe::Engine e;
    e.prepare (48000.0, 512, 2);

    // Zero samples: must not touch anything.
    float a = 0.25f, b = -0.25f;
    float* ptrs[2] = { &a, &b };
    e.process (ptrs, 2, 0);
    CHECK (a == 0.25f && b == -0.25f, "zero-length block");

    // Special values pass through unchanged (nothing is "fixed" or filtered yet).
    float x[4] = { 0.0f, -0.0f, 1.0e-39f, 3.0f };
    float y[4] = { 0.0f, -0.0f, 1.0e-39f, 3.0f };
    float* p2[2] = { x, y };
    e.process (p2, 2, 4);
    CHECK (std::memcmp (x, y, sizeof x) == 0 && x[3] == 3.0f, "special values");

    // Preparing again (Logic restarts or changes the sample rate) is harmless.
    e.prepare (44100.0, 256, 1);
    CHECK (passesThrough (e, 1, 3000, 256), "after prepare again");
    CHECK (e.getSampleRate() == 44100.0, "sample rate stored");
    e.prepare (-5.0, 256, 1);
    CHECK (e.getSampleRate() == 48000.0, "bad sample rate replaced by a safe one");
    e.reset();
    CHECK (passesThrough (e, 1, 3000, 64), "after reset");
}

static void testGainSteps()
{
    CHECK (phe::kNumInputGainSteps == 4, "four input gain steps");
    CHECK (phe::kInputGainStepDb[0] == -3.0f && phe::kInputGainStepDb[1] == 0.0f
        && phe::kInputGainStepDb[2] == 6.0f  && phe::kInputGainStepDb[3] == 12.0f, "gain steps -3 / 0 / +6 / +12 dB");
    CHECK (phe::Params().inputGainStep == 1, "gain step starts at 0 dB");
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
    testPassThrough();
    testControlsDoNotChangeSound();
    testEdgeCases();
    testGainSteps();
    testScales();

    if (failures == 0)
        std::printf ("engine tests: all %d checks passed\n", checks);
    else
        std::printf ("engine tests: %d of %d checks FAILED\n", failures, checks);
    return failures == 0 ? 0 : 1;
}
