// A musical clock taken from the host's tempo (Logic's tempo and playhead). Plain C++17, no JUCE. Reusable by later plugins.
//
// Plain language: the hardware module is told when to do things by a clock cable. A plugin has no cable, but Logic knows the
// tempo and where the song is, so we make the clock ourselves: a "tick" every so often, always on the beat grid.
//
//   - While Logic plays, the clock follows the song position, so ticks land exactly on the beats (and start again at the
//     right place if Logic jumps, loops or is moved).
//   - While Logic is stopped (or gives no position) the clock keeps running at Logic's tempo by itself, so the clock features
//     still work when you are just playing live through the plugin.
//   - Tempo not known at all: 120 beats per minute.
//
// The clock counts in beats. A "division" is how many beats there are between ticks (a quarter note = 1 beat, a bar = the
// number of beats in the host's bar). Tick number n happens when the song position reaches n x (beats per tick).

#pragma once

#include <algorithm>
#include <cmath>

namespace phe::core
{
    // What the host told us for this block (the plugin wrapper fills this in; every field may be missing).
    struct HostInfo
    {
        bool tempoValid = false;
        double bpm = 120.0;            // beats (quarter notes) per minute
        bool playing = false;
        bool ppqValid = false;
        double ppq = 0.0;              // song position in beats at the start of the block
        double beatsPerBar = 4.0;      // from the time signature (3/4 = 3, 6/8 = 3, 4/4 = 4)
    };

    // How far apart the ticks are. A bar is counted in the host's own bars, the others in beats.
    struct ClockDivision { const char* name; double beats; double bars; };    // beats between ticks = beats + bars x beatsPerBar
    inline constexpr int kNumClockDivisions = 10;
    inline constexpr int kDefaultClockDivision = 3;                           // 1/4
    inline constexpr ClockDivision kClockDivisions[kNumClockDivisions] = {
        { "2 bars",  0.0,       2.0 },
        { "1 bar",   0.0,       1.0 },
        { "1/2",     2.0,       0.0 },
        { "1/4",     1.0,       0.0 },
        { "1/4T",    2.0 / 3.0, 0.0 },
        { "1/8",     0.5,       0.0 },
        { "1/8T",    1.0 / 3.0, 0.0 },
        { "1/16",    0.25,      0.0 },
        { "1/16T",   1.0 / 6.0, 0.0 },
        { "1/32",    0.125,     0.0 },
    };

    inline double beatsPerTick (int divisionIndex, double beatsPerBar)
    {
        const auto& d = kClockDivisions[std::min (kNumClockDivisions - 1, std::max (0, divisionIndex))];
        const double bar = std::isfinite (beatsPerBar) && beatsPerBar >= 0.25 && beatsPerBar <= 64.0 ? beatsPerBar : 4.0;
        return d.beats + d.bars * bar;
    }

    // "Clocks per splice" (Time Stretch): how many clock ticks one trip through the splice lasts.
    inline constexpr int kNumStretchClocks = 10;
    inline constexpr int kDefaultStretchClocks = 3;                           // 4 clocks (a bar of quarter notes)
    inline constexpr int kStretchClocks[kNumStretchClocks] = { 1, 2, 3, 4, 6, 8, 12, 16, 32, 64 };

    // "Play Retrigger": every how many ticks the loop is pulled back to the Slide position (0 = never).
    inline constexpr int kNumRetriggers = 8;
    inline constexpr int kRetriggerClocks[kNumRetriggers] = { 0, 1, 2, 4, 8, 16, 32, 64 };

    inline int stretchClocksFor (int index) { return kStretchClocks[std::min (kNumStretchClocks - 1, std::max (0, index))]; }
    inline int retriggerEveryFor (int index) { return kRetriggerClocks[std::min (kNumRetriggers - 1, std::max (0, index))]; }

    class HostClock
    {
    public:
        void prepare (double newSampleRate)
        {
            sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
            reset();
        }

        void reset()
        {
            beats = 0.0;
            lastTick = -1;
            beatsEvery = 0.0;                                      // set by the first beginBlock
            locked = false;
            tickCounter = 0;
            updateStep();
        }

        // Once per block, before the first sample: the host's news and the tick spacing wanted (in beats).
        void beginBlock (const HostInfo& host, double ticksEveryBeats)
        {
            if (host.tempoValid && std::isfinite (host.bpm) && host.bpm > 0.0)
                bpmNow = std::min (999.0, std::max (10.0, host.bpm));
            updateStep();

            const double spacing = std::isfinite (ticksEveryBeats) && ticksEveryBeats > 1.0e-6 ? ticksEveryBeats : 1.0;
            if (spacing != beatsEvery)
            {
                beatsEvery = spacing;
                lastTick = indexOf (beats);                       // a new division: no tick for it, the next one is the next boundary
            }

            const bool follow = host.playing && host.ppqValid && std::isfinite (host.ppq);
            if (follow)
            {
                const double drift = std::fabs (host.ppq - beats);
                if (! locked || drift > kJumpBeats)
                {
                    // Playback started, or the song position jumped (loop, locate): carry on from there. A tick exactly at the new
                    // position still happens (on the very first sample).
                    beats = host.ppq;
                    lastTick = (long long) std::ceil (beats / beatsEvery - kTickEpsilon) - 1;
                }
                else
                    beats = host.ppq;                              // just a tiny correction
            }
            locked = follow;
        }

        // Is there a tick at the position of the sample about to be processed? (Call once per sample, then next().)
        bool tickNow()
        {
            if (! (beatsEvery > 0.0))
                return false;
            const long long index = indexOf (beats);
            if (index > lastTick)
            {
                lastTick = index;
                ++tickCounter;
                return true;
            }
            return false;
        }

        void next() { beats += beatsPerSample; }

        // How far through a stretch of 'lengthBeats' we are now (0 up to just under 1), counted from song position 0.
        double phaseOver (double lengthBeats) const
        {
            if (! (lengthBeats > 1.0e-9))
                return 0.0;
            const double x = beats / lengthBeats;
            return x - std::floor (x);
        }

        long long tickIndex() const { return lastTick; }          // number of the latest tick (negative before song position 0)
        double beatsNow() const { return beats; }
        double bpm() const { return bpmNow; }
        bool followingHost() const { return locked; }
        int ticksSoFar() const { return tickCounter; }            // goes up by one at every tick (for a blinking light)
        double beatsPerTickNow() const { return beatsEvery; }
        double secondsPerBeat() const { return 60.0 / bpmNow; }

    private:
        static constexpr double kJumpBeats = 0.02;
        static constexpr double kTickEpsilon = 1.0e-9;            // a position a hair short of a boundary still counts as on it

        long long indexOf (double position) const
        {
            return beatsEvery > 0.0 ? (long long) std::floor (position / beatsEvery + kTickEpsilon) : 0;
        }
        void updateStep() { beatsPerSample = bpmNow / 60.0 / sampleRate; }

        double sampleRate = 48000.0;
        double bpmNow = 120.0;
        double beatsPerSample = 120.0 / 60.0 / 48000.0;
        double beatsEvery = 0.0;
        double beats = 0.0;
        long long lastTick = -1;
        bool locked = false;
        int tickCounter = 0;
    };

    // Positive remainder (tick numbers can be negative before the song starts).
    inline long long floorMod (long long a, long long m)
    {
        const long long r = a % m;
        return r < 0 ? r + m : r;
    }
}
