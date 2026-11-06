// Milestone 11: CPU measurements and the long-session ("soak") test for the PhenomeEon engine. Plain C++, no JUCE.
//
//   phe_stress_test bench              how hard the engine works in the heavy cases (percent of one processor core, worst block)
//   phe_stress_test soak HOURS [SEED] [--mono]   HOURS of simulated audio, run faster than real time, with the controls and buttons
//                                      used at random the whole time (what a long Logic session might do, and then some)
//   phe_stress_test quick              a short soak (a few minutes of audio) that the normal build runs every time
//
// Add --noftz to leave the processor's "flush tiny numbers to zero" switch off (the plugin switches it on, like JUCE's
// ScopedNoDenormals; running both ways shows whether tiny-number slowdowns could ever matter).
//
// The soak fails (exit code 1) if the output is ever not-a-number or infinite or absurdly loud, if the reel ever holds a
// not-a-number, or if memory use keeps climbing after the first hours.

#include <phe/Engine.h>
#include <phe/core/HostClock.h>
#include <phe/core/Morph.h>
#include <phe/core/Scales.h>
#include <phe/core/SpliceMap.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(__SSE__) || defined(__x86_64__)
 #include <xmmintrin.h>
 #include <pmmintrin.h>
 #define PHE_HAVE_FTZ 1
#endif

using Clock = std::chrono::steady_clock;

static void setFlushToZero (bool on)
{
#if PHE_HAVE_FTZ
    _MM_SET_FLUSH_ZERO_MODE (on ? _MM_FLUSH_ZERO_ON : _MM_FLUSH_ZERO_OFF);
    _MM_SET_DENORMALS_ZERO_MODE (on ? _MM_DENORMALS_ZERO_ON : _MM_DENORMALS_ZERO_OFF);
#else
    (void) on;
#endif
}

static long residentKB()
{
    long pages = 0, rss = 0;
    if (FILE* f = std::fopen ("/proc/self/statm", "r"))
    {
        if (std::fscanf (f, "%ld %ld", &pages, &rss) != 2) rss = 0;
        std::fclose (f);
    }
    return rss * 4;
}

struct Rng
{
    uint64_t s;
    explicit Rng (uint64_t seed) : s (seed * 2862933555777941757ull + 3037000493ull) { for (int i = 0; i < 4; ++i) next(); }
    uint32_t next()
    {
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;
        return (uint32_t) (s >> 16);
    }
    float uni() { return (float) (next() & 0xFFFFFF) / 16777216.0f; }
    int range (int n) { return (int) (next() % (uint32_t) n); }
    bool chance (int percent) { return range (100) < percent; }
};

// Feeds the engine blocks of sound and keeps the numbers we care about.
struct Harness
{
    phe::Engine e;
    phe::Params p;
    double sr = 48000.0;
    long long t = 0;
    int source = 0;                // 0 tone, 1 silence, 2 loud square, 3 loud tone
    int numCh = 2;                 // 2 = stereo, 1 = mono (the plugin accepts both)
    bool nanNext = false;

    // What the host says
    phe::core::HostInfo host;
    bool hostPlaying = false;
    double hostPpq = 0.0;

    // Numbers
    bool badOutput = false;
    double maxAbs = 0.0;
    double worstBlockMs = 0.0, worstBlockRatio = 0.0;   // slowest block, and slowest block as a share of its own duration
    double totalMs = 0.0;
    long long totalSamples = 0;
    int momentaryAge[8] = {};
    struct SlowBlock { long long index; int n; double ms; bool busy; int recKind; };
    std::vector<SlowBlock> slowBlocks;      // the first blocks that took over half a millisecond AND over 30% of their own time, and what was going on
    long long blockCount = 0, slowCount = 0;

    std::vector<float> bufL, bufR;

    explicit Harness (double rate = 48000.0, int channelCount = 2)
    {
        sr = rate;
        numCh = channelCount;
        e.prepare (sr, 4096, numCh);
        bufL.assign (65536, 0.0f);
        bufR.assign (65536, 0.0f);
        host.tempoValid = true;
        host.bpm = 120.0;
        host.beatsPerBar = 4.0;
        // Logic always has audio running before anyone presses a button: one short quiet block first.
        float* ptrs[2] = { bufL.data(), bufR.data() };
        e.setHost (host);
        e.setParams (p);
        e.process (ptrs, numCh, 16);
        t = 0;
    }

    void prepare (double rate)
    {
        sr = rate;
        e.prepare (sr, 4096, numCh);
    }

    float sample (int channel, long long time) const
    {
        switch (source)
        {
            case 1: return 0.0f;
            case 2: return ((time / 25) & 1) ? 0.95f : -0.95f;
            case 3: return 0.99f * (float) std::sin (0.031 * (double) time * (double) (channel + 1));
            default:
            {
                uint32_t seed = (uint32_t) (time * 2654435761ull) + (uint32_t) channel * 777u;
                seed = seed * 1664525u + 1013904223u;
                const float noise = ((float) (seed >> 8) / 16777216.0f - 0.5f) * 0.1f;
                return 0.4f * (float) std::sin (0.05 * (double) time * (double) (channel + 1)) + noise;
            }
        }
    }

    // One block. Times the engine only.
    void block (int n)
    {
        for (int k = 0; k < n; ++k)
        {
            bufL[(size_t) k] = sample (0, t + k);
            bufR[(size_t) k] = sample (1, t + k);
        }
        if (nanNext)
        {
            bufL[0] = std::nanf ("");
            if (n > 2) bufR[2] = INFINITY;
            nanNext = false;
        }

        host.playing = hostPlaying;
        host.ppqValid = hostPlaying;
        host.ppq = hostPpq;
        float* ptrs[2] = { bufL.data(), bufR.data() };

        const auto viewBefore = e.getReelView();
        const auto t0 = Clock::now();
        e.setHost (host);
        e.setParams (p);
        e.process (ptrs, numCh, n);
        const double ms = std::chrono::duration<double, std::milli> (Clock::now() - t0).count();

        ++blockCount;
        if (ms > 0.5 && ms > 0.3 * 1000.0 * (double) n / sr)
        {
            ++slowCount;
            if (slowBlocks.size() < 40) slowBlocks.push_back ({ blockCount, n, ms, viewBefore.busy, viewBefore.recKind });
        }
        totalMs += ms;
        totalSamples += n;
        worstBlockMs = std::max (worstBlockMs, ms);
        worstBlockRatio = std::max (worstBlockRatio, ms / (1000.0 * (double) n / sr));
        for (int k = 0; k < n; ++k)
        {
            const float a = bufL[(size_t) k], b = numCh > 1 ? bufR[(size_t) k] : 0.0f;
            if (! std::isfinite (a) || ! std::isfinite (b)) badOutput = true;
            else maxAbs = std::max (maxAbs, (double) std::max (std::fabs (a), std::fabs (b)));
        }
        t += n;
        if (hostPlaying) hostPpq += (double) n * host.bpm / 60.0 / sr;

        wrapper();
    }

    // What the plugin wrapper does 20 times a second: switch buttons back off when the engine acted or stopped by itself.
    void wrapper()
    {
        if (e.takeRecordStopped()) p.rec = false;
        if (e.takeNewSpliceStopped()) p.recNewSplice = false;
        if (e.takeClearHandled()) p.clearReel = false;
        const int mask = e.takeHandledButtons();
        if (mask & phe::Engine::kButtonSplice) p.splice = false;
        if (mask & phe::Engine::kButtonShift) p.shift = false;
        if (mask & phe::Engine::kButtonDeleteMarker) p.deleteMarker = false;
        if (mask & phe::Engine::kButtonDeleteAllMarkers) p.deleteAllMarkers = false;
        if (mask & phe::Engine::kButtonErase) p.eraseSplice = false;
        if (mask & phe::Engine::kButtonUndo) p.undo = false;
    }

    void run (long long samples, int blockSize)
    {
        long long done = 0;
        while (done < samples)
        {
            const int n = (int) std::min<long long> (blockSize, samples - done);
            block (n);
            done += n;
        }
    }

    // Releases every momentary button (a press that the engine refused would otherwise stay on).
    void releaseButtons()
    {
        p.splice = p.shift = p.deleteMarker = p.deleteAllMarkers = p.eraseSplice = p.clearReel = p.undo = false;
    }

    double percentCpu() const { return totalSamples > 0 ? 100.0 * totalMs / (1000.0 * (double) totalSamples / sr) : 0.0; }

    void resetTimers()
    {
        totalMs = 0.0;
        totalSamples = 0;
        worstBlockMs = 0.0;
        worstBlockRatio = 0.0;
    }

    bool reelHoldsOnlyNumbers() const
    {
        const auto& reel = e.getReel();
        const int len = reel.length();
        for (int c = 0; c < 2; ++c)
        {
            const float* d = reel.channel (c);
            if (d == nullptr) continue;
            for (int i = 0; i < len; ++i)
                if (! std::isfinite (d[i])) return false;
        }
        return true;
    }
};

// ------------------------------------------------------------------------------------------------ benchmark

// Fills the reel to its full 2.9 minutes with sound and puts markers in it, so every case is the worst case for the reel.
static void fillReel (Harness& h, int markers)
{
    h.p = phe::Params();
    h.p.play = false;
    h.p.rec = true;
    h.source = 0;
    h.hostPlaying = false;
    long long guard = 0;
    while (h.p.rec && guard < (long long) (200.0 * h.sr))
    {
        h.block (512);
        guard += 512;
    }
    h.p.rec = false;
    h.p.play = true;
    h.run (4800, 256);
    for (int i = 0; i < markers; ++i)
    {
        h.run ((long long) (h.sr * (3.0 + (double) i)), 512);
        h.p.splice = true;
        h.run (512, 512);
        h.releaseButtons();
        h.run (512, 512);
    }
    h.p.play = true;
}

struct Case
{
    const char* name;
    void (*set) (phe::Params&);
    int blockSize;
    bool hostPlays;
    int source;
};

static void caseDefault (phe::Params&) {}
static void caseBusy (phe::Params& p)
{
    p.morph = 0.9f; p.variSpeed = 0.9f; p.geneSize = 0.55f; p.slide = 0.3f; p.pitchSnap = 2; p.pitchScale = 5;
}
static void caseFourGenesSmooth (phe::Params& p)
{
    p.morph = 1.0f; p.geneSize = 0.8f; p.variSpeed = 0.97f; p.geneWindow = 1; p.pitchScale = 12;
}
static void caseChord (phe::Params& p)
{
    p.morph = 1.0f; p.geneSize = 0.65f; p.morphChord = 2; p.variSpeed = 0.3f;
}
static void caseTla (phe::Params& p)
{
    p.rec = true; p.soundOnSound = 0.5f; p.morph = 0.5f; p.geneSize = 0.6f;
}
static void caseClock (phe::Params& p)
{
    p.clockMode = 2; p.clockRate = 7; p.retrigger = 2; p.geneSize = 0.7f; p.morph = 0.4f; p.recSync = 1;
}
static void caseStretch (phe::Params& p)
{
    p.clockMode = 3; p.clockRate = 5; p.clocksPerSplice = 2; p.geneSize = 0.75f; p.morph = 0.85f; p.geneWindow = 1;
}
static void caseWorst (phe::Params& p)
{
    p.morph = 1.0f; p.geneSize = 0.85f; p.variSpeed = 1.0f; p.geneWindow = 1; p.pitchSnap = 2; p.pitchScale = 7;
    p.clockMode = 3; p.clockRate = 9; p.clocksPerSplice = 0; p.retrigger = 1; p.rec = true; p.soundOnSound = 0.6f;
}
static void caseSilentKnobs (phe::Params& p)
{
    p.outputLevel = 0.0f; p.inputLevel = 0.0f; p.variSpeed = 0.5f; p.morph = 0.9f; p.geneSize = 0.7f;
}

static int runBench()
{
    std::printf ("PhenomeEon engine CPU benchmark (full 2.9-minute reel, 48 kHz stereo)\n");
    std::printf ("Percent of ONE processor core of this computer; lower is better. 'worst block' = the slowest single block\n");
    std::printf ("as a share of the time that block has to be finished in (over 100%% would be a dropout).\n\n");

    Harness h (48000.0);
    fillReel (h, 6);
    std::printf ("reel: %d samples, %d splices\n\n", h.e.getReel().length(), h.e.getSpliceMap().numSplices());

    const Case cases[] = {
        { "plain 1/1 loop (the starting settings)", caseDefault, 256, false, 0 },
        { "busy: 3-4 genes, +random pitch, Pitch Snap + Scale", caseBusy, 256, false, 0 },
        { "four genes, smooth windows, Morph and speed at maximum", caseFourGenesSmooth, 256, false, 0 },
        { "octave-chord genes, backwards slow", caseChord, 256, false, 0 },
        { "Time Lag overdub running over the reel", caseTla, 256, false, 0 },
        { "Gene Shift + Retrigger + Rec Sync, clock following Logic", caseClock, 256, true, 0 },
        { "Time Stretch, smooth windows, clock following Logic", caseStretch, 256, true, 0 },
        { "everything at once (worst case)", caseWorst, 256, true, 0 },
        { "everything at once, block size 64", caseWorst, 64, true, 0 },
        { "everything at once, block size 1024", caseWorst, 1024, true, 0 },
        { "everything at once, loud input", caseWorst, 256, true, 3 },
        { "output and input knobs at zero, silent input (tiny-number test)", caseSilentKnobs, 256, false, 1 },
    };

    int worstOverHundred = 0;
    double worstPct = 0.0;
    for (const auto& c : cases)
    {
        h.p = phe::Params();
        c.set (h.p);
        h.source = c.source;
        h.hostPlaying = c.hostPlays;
        h.hostPpq = 0.0;
        h.run ((long long) (h.sr * 5.0), c.blockSize);     // settle
        h.resetTimers();
        h.run ((long long) (h.sr * 60.0), c.blockSize);
        std::printf ("%-72s %6.2f %%   worst block %6.2f %%\n", c.name, h.percentCpu(), 100.0 * h.worstBlockRatio);
        worstPct = std::max (worstPct, h.percentCpu());
        if (h.worstBlockRatio > 1.0) ++worstOverHundred;
        h.p.rec = false;
        h.releaseButtons();
        h.run (4800, 256);
        if (h.badOutput) { std::printf ("FAIL: output was not a number\n"); return 1; }
    }

    // Big edits and file hand-overs on the full reel: how long is the slowest block while they run, at several buffer sizes?
    std::printf ("\nBig jobs on the full reel (the engine does them a little at a time, sized to the block):\n");
    for (const int bs : { 32, 64, 256, 1024 })
    {
        h.p = phe::Params();
        h.source = 0;
        h.hostPlaying = false;
        h.run ((long long) (h.sr * 2.0), bs);
        const double blockMs = 1000.0 * (double) bs / h.sr;
        auto report = [&] (const char* what, int blocks, size_t frames)
        {
            std::printf ("  block %4d: %-14s %6d blocks = %5.2f s of audio, worst block %6.2f %% of its time%s\n", bs, what, blocks,
                         (double) blocks * blockMs / 1000.0, 100.0 * h.worstBlockRatio, frames ? "" : "");
        };
        const int limit = 400000;
        h.worstBlockRatio = 0.0;
        h.p.eraseSplice = true;
        int blocks = 0;
        while (h.e.getReelView().busy || blocks < 2) { h.block (bs); ++blocks; if (blocks > limit) break; }
        h.releaseButtons();
        report ("Erase Splice", blocks, 0);
        h.worstBlockRatio = 0.0;
        h.p.undo = true;
        blocks = 0;
        while (h.e.getReelView().busy || blocks < 2) { h.block (bs); ++blocks; if (blocks > limit) break; }
        h.releaseButtons();
        report ("Undo", blocks, 0);
        h.run (2048, bs);

        h.worstBlockRatio = 0.0;
        phe::Engine::ReelAudio out;
        uint32_t stamp = 0;
        blocks = 0;
        bool ok = h.e.requestExport();
        auto res = phe::Engine::IoResult::none;
        while (ok && res == phe::Engine::IoResult::none && blocks < limit) { h.block (bs); ++blocks; res = h.e.pollExport (out, stamp); }
        report ("Save (copy)", blocks, out.frames());
        h.worstBlockRatio = 0.0;
        blocks = 0;
        std::string why;
        ok = ok && h.e.submitImport (std::move (out), why);
        auto ires = phe::Engine::IoResult::none;
        while (ok && ires == phe::Engine::IoResult::none && blocks < limit) { h.block (bs); ++blocks; ires = h.e.pollImport (stamp); }
        report ("Load (write)", blocks, 0);
        h.run (4096, bs);
        if (h.worstBlockRatio > 1.0) ++worstOverHundred;
    }

    std::printf ("\nHeaviest case: %.2f %% of one core.   Cases with a block over 100%%: %d\n", worstPct, worstOverHundred);
    std::printf ("Memory in use now: %.0f MB\n", (double) residentKB() / 1024.0);
    return h.badOutput ? 1 : 0;
}

// ------------------------------------------------------------------------------------------------ soak

struct Soak
{
    Harness h;
    Rng rng;
    long long actions[16] = {};
    bool exportPending = false, importPending = false;
    int walker[2] = { 0, 4 };       // two knobs that wander slowly all the time
    float walkTarget[2] = { 0.5f, 0.5f };
    int exportsDone = 0, importsDone = 0, rateChanges = 0, nanBlocks = 0, jobsRefused = 0;

    Soak (uint64_t seed, double rate, int channelCount) : h (rate, channelCount), rng (seed) {}

    float& knob (int i)
    {
        switch (i)
        {
            case 0: return h.p.inputLevel;
            case 1: return h.p.soundOnSound;
            case 2: return h.p.geneSize;
            case 3: return h.p.slide;
            case 4: return h.p.variSpeed;
            case 5: return h.p.morph;
            case 6: return h.p.organize;
            default: return h.p.outputLevel;
        }
    }

    void randomKnob()
    {
        const int i = rng.range (8);
        float v = rng.uni();
        const int r = rng.range (10);
        if (r == 0) v = 0.0f; else if (r == 1) v = 1.0f;       // the ends get used a lot
        if (i == 0 || i == 7) v = 0.2f + 0.8f * v;              // levels are rarely turned fully down for long
        knob (i) = v;
        // Never leave a level at zero for long: sound must be heard now and then.
    }

    void doAction()
    {
        auto& p = h.p;
        const int a = rng.range (100);
        if (a < 30) { randomKnob(); actions[0]++; }
        else if (a < 38) { p.rec = ! p.rec; actions[1]++; }
        else if (a < 42) { p.recNewSplice = ! p.recNewSplice; actions[2]++; }
        else if (a < 46) { p.play = ! p.play; actions[3]++; }
        else if (a < 53) { p.splice = true; actions[4]++; }
        else if (a < 57) { p.shift = true; actions[5]++; }
        else if (a < 60) { p.deleteMarker = true; actions[6]++; }
        else if (a < 62) { p.deleteAllMarkers = true; actions[7]++; }
        else if (a < 65) { p.eraseSplice = true; actions[8]++; }
        else if (a < 66) { p.clearReel = true; actions[9]++; }
        else if (a < 72) { p.undo = true; actions[10]++; }
        else if (a < 80)
        {
            p.pitchSnap = rng.range (3);
            p.pitchScale = rng.range (phe::scales::kNumScales + 1);
            p.inputGainStep = rng.range (4);
            p.variSpeedMode = rng.range (3);
            p.recordSource = rng.range (2);
            p.organizeMode = rng.range (2);
            p.geneWindow = rng.range (2);
            p.playMode = rng.range (3);
            p.morphChord = rng.range (phe::dsp::kNumMorphChords);
            actions[11]++;
        }
        else if (a < 86)
        {
            p.clockMode = rng.range (4);
            p.clockRate = rng.range (phe::core::kNumClockDivisions);
            p.clocksPerSplice = rng.range (phe::core::kNumStretchClocks);
            p.retrigger = rng.range (phe::core::kNumRetriggers);
            p.recSync = rng.range (2);
            actions[12]++;
        }
        else if (a < 90)
        {
            static const double signatures[] = { 3.0, 4.0, 5.0, 6.0, 7.0 };
            h.host.tempoValid = rng.chance (90);
            h.host.bpm = 20.0 + 280.0 * (double) rng.uni();
            h.host.beatsPerBar = signatures[rng.range (5)];
            h.hostPlaying = rng.chance (60);
            if (rng.chance (50)) h.hostPpq = 1000.0 * (double) rng.uni();
            actions[13]++;
        }
        else if (a < 93)
        {
            h.source = rng.range (4);
            if (rng.chance (20)) { h.nanNext = true; ++nanBlocks; }
            actions[14]++;
        }
        else if (a < 95) { exportRequest(); }
        else if (a < 97) { importRandom(); }
        else if (a < 98 && rng.chance (50)) { changeRate(); }
        else
        {
            // Nothing: let the sound run. (The reel also needs sound in it, so an empty reel gets a recording started.)
        }

        if (h.e.reelLength() == 0 && rng.chance (40) && ! p.rec) p.rec = true;
    }

    void exportRequest()
    {
        if (exportPending) return;
        if (h.e.requestExport()) exportPending = true;
        else ++jobsRefused;
    }

    void importRandom()
    {
        if (importPending) return;
        phe::Engine::ReelAudio a;
        a.sampleRate = h.e.reelSampleRate();
        const int cap = h.e.reelCapacity();
        int n = 100 + rng.range (std::min (cap - 100, rng.chance (10) ? cap - 100 : 400000));
        n = std::min (n, cap);
        a.ch[0].resize ((size_t) n);
        a.ch[1].resize ((size_t) n);
        const float f0 = 0.002f + 0.05f * rng.uni(), f1 = 0.002f + 0.05f * rng.uni();
        for (int i = 0; i < n; ++i)
        {
            a.ch[0][(size_t) i] = 0.5f * std::sin (f0 * (float) i);
            a.ch[1][(size_t) i] = 0.5f * std::sin (f1 * (float) i);
        }
        const int m = rng.range (8);
        for (int i = 0; i < m; ++i) a.markers.push_back (rng.range (n));
        std::sort (a.markers.begin(), a.markers.end());
        phe::core::tidyMarkers (a.markers, n);
        std::string why;
        if (h.e.submitImport (std::move (a), why)) importPending = true;
        else ++jobsRefused;
    }

    void changeRate()
    {
        static const double rates[] = { 44100.0, 48000.0, 88200.0 };
        const double r = rates[rng.range (3)];
        if (r == h.sr) return;
        h.prepare (r);
        h.hostPpq = h.hostPpq;      // unchanged
        ++rateChanges;
        // A job in flight is dropped by the engine when the rate changes; its hand-over slot is collected below.
    }

    void collectJobs()
    {
        if (exportPending)
        {
            phe::Engine::ReelAudio out;
            uint32_t stamp = 0;
            const auto res = h.e.pollExport (out, stamp);
            if (res != phe::Engine::IoResult::none) { exportPending = false; ++exportsDone; }
        }
        if (importPending)
        {
            uint32_t stamp = 0;
            const auto res = h.e.pollImport (stamp);
            if (res != phe::Engine::IoResult::none) { importPending = false; ++importsDone; }
        }
    }

    // One stretch of audio between two actions.
    void step()
    {
        static const int sizes[] = { 32, 64, 128, 256, 256, 480, 512, 512, 1000, 1024, 2048, 4096 };
        int blockSize = sizes[rng.range (12)];
        if (rng.chance (2)) { static const int big[] = { 8192, 16384, 65536 }; blockSize = big[rng.range (3)]; }
        long long len = (long long) (h.sr * (0.02 + 2.5 * (double) rng.uni() * (double) rng.uni()));
        if (rng.chance (3)) { blockSize = 1; len = std::min<long long> (len, 1500); }
        else if (rng.chance (3)) { blockSize = 7; len = std::min<long long> (len, 20000); }

        doAction();
        long long done = 0;
        while (done < len)
        {
            const int n = (int) std::min<long long> (blockSize, len - done);
            // Two knobs wander all the time, a little each block.
            for (int w = 0; w < 2; ++w)
            {
                float& k = knob (walker[w]);
                if (rng.chance (1)) walkTarget[w] = rng.uni();
                k += (walkTarget[w] - k) * 0.002f * (float) n / 64.0f;
            }
            h.block (n);
            done += n;
            collectJobs();
            // A button the engine refused would stay on for ever; the real wrapper switches such buttons off as well.
            if (rng.chance (2)) h.releaseButtons();
        }
        h.releaseButtons();
    }
};

static int runSoak (double hours, uint64_t seed, bool quiet, int channelCount = 2)
{
    Soak s (seed, 48000.0, channelCount);
    const auto wall0 = Clock::now();
    const long long hourSamples = (long long) (3600.0 * 48000.0);
    double audioHoursDone = 0.0;
    long long nextReport = 0;
    long rssNow = 0;
    std::vector<long> rssByHour;
    int reports = 0;
    bool failed = false;

    if (! quiet)
        std::printf ("Soak test: %.2f hours of simulated audio, seed %llu, %s\n", hours, (unsigned long long) seed, channelCount == 1 ? "mono" : "stereo");

    long long target = (long long) (hours * 3600.0 * 48000.0);
    long long played = 0;
    double lastWall = 0.0;
    while (played < target)
    {
        const long long before = s.h.totalSamples;
        s.step();
        played += s.h.totalSamples - before;

        // Report and check at every simulated hour (and for a quick run, once at the end).
        const bool hourDone = played >= nextReport + hourSamples;
        if (hourDone || played >= target)
        {
            if (hourDone) nextReport += hourSamples;
            audioHoursDone = (double) played / (double) hourSamples;
            rssNow = residentKB();
            ++reports;
            rssByHour.push_back (rssNow);
            const double wall = std::chrono::duration<double> (Clock::now() - wall0).count();
            if (! quiet)
                std::printf ("  %5.2f h  wall %7.1f s (+%6.1f s)  memory %5.0f MB  peak out %.2f  worst block %5.2f ms (%5.1f%% of its time)  reel %.1f s%s\n",
                             audioHoursDone, wall, wall - lastWall, (double) rssNow / 1024.0, s.h.maxAbs, s.h.worstBlockMs,
                             100.0 * s.h.worstBlockRatio, (double) s.h.e.reelLength() / s.h.sr,
                             s.h.badOutput ? "  BAD OUTPUT" : "");
            lastWall = wall;
            if (! s.h.reelHoldsOnlyNumbers()) { std::printf ("FAIL: the reel holds a not-a-number or infinity\n"); failed = true; }
            if (failed) break;
        }
        if (s.h.badOutput) { std::printf ("FAIL: the output was not a number or was infinite\n"); failed = true; break; }
        if (s.h.maxAbs > 200.0) { std::printf ("FAIL: the output became absurdly loud (%.1f)\n", s.h.maxAbs); failed = true; break; }
    }

    // Memory use moves up and down as the reel is made bigger or smaller by the sample-rate changes, so the test compares the
    // highest use in the first half of the run (after the first two hours) with the highest in the second half.
    if (! failed && rssByHour.size() >= 8)
    {
        const size_t half = rssByHour.size() / 2;
        long firstMax = 0, secondMax = 0;
        for (size_t i = 2; i < half; ++i) firstMax = std::max (firstMax, rssByHour[i]);
        for (size_t i = half; i < rssByHour.size(); ++i) secondMax = std::max (secondMax, rssByHour[i]);
        const double growth = (double) (secondMax - firstMax) / 1024.0;
        if (! quiet)
            std::printf ("Highest memory use: first half %.0f MB, second half %.0f MB (difference %.1f MB)\n",
                         (double) firstMax / 1024.0, (double) secondMax / 1024.0, growth);
        if (growth > 60.0) { std::printf ("FAIL: memory keeps growing (%.1f MB more in the second half)\n", growth); failed = true; }
    }

    if (! quiet)
    {
        static const char* names[] = { "knob moves", "Rec", "Rec New Splice", "Play", "Splice", "Shift", "Delete Splice Marker",
                                       "Delete All Markers", "Erase Splice", "Clear Reel", "Undo / Redo", "menu changes",
                                       "clock changes", "host changes", "input changes", "(unused)" };
        std::printf ("Actions: ");
        for (int i = 0; i < 15; ++i) std::printf ("%s %lld, ", names[i], s.actions[i]);
        std::printf ("saves %d, loads %d, sample-rate changes %d, bad-input blocks %d, refused hand-overs %d\n",
                     s.exportsDone, s.importsDone, s.rateChanges, s.nanBlocks, s.jobsRefused);
        std::printf ("Average CPU over the whole run: %.2f %% of one core; peak output %.2f\n", s.h.percentCpu(), s.h.maxAbs);
        std::printf ("Slow blocks (over 0.5 ms and over 30%% of their own time): %lld of %lld blocks\n", s.h.slowCount, s.h.blockCount);
        for (const auto& b : s.h.slowBlocks)
            std::printf ("   block #%lld: %.2f ms for %d samples (%.2f ms available), engine busy: %s, recording kind %d\n",
                         b.index, b.ms, b.n, 1000.0 * (double) b.n / s.h.sr, b.busy ? "yes" : "no", b.recKind);
    }
    std::printf ("%s\n", failed ? "SOAK FAILED" : "soak test: passed");
    return failed ? 1 : 0;
}

int main (int argc, char** argv)
{
    std::string mode = argc > 1 ? argv[1] : "quick";
    bool ftz = true;
    for (int i = 1; i < argc; ++i)
        if (std::strcmp (argv[i], "--noftz") == 0) ftz = false;

    setFlushToZero (ftz);
    if (mode == "bench") return runBench();
    if (mode == "soak")
    {
        const double hours = argc > 2 ? std::atof (argv[2]) : 1.0;
        const uint64_t seed = argc > 3 && argv[3][0] != '-' ? (uint64_t) std::atoll (argv[3]) : 1;
        bool mono = false;
        for (int i = 1; i < argc; ++i)
            if (std::strcmp (argv[i], "--mono") == 0) mono = true;
        return runSoak (hours, seed, false, mono ? 1 : 2);
    }
    // quick: a few minutes of audio for several seeds, run by the normal build every time
    int bad = 0;
    for (uint64_t seed = 1; seed <= 4; ++seed)
        bad += runSoak (0.03, seed, true, seed == 4 ? 1 : 2);
    return bad == 0 ? 0 : 1;
}
