// The reel: one long stereo strip of audio memory, up to 2.9 minutes, plus a coarse waveform picture of it.
//
// The audio side (Engine) reads and writes samples. The window only reads length() and the peak picture,
// which are safe to read at any time. Plain C++, no JUCE.

#pragma once

#include "core/PeakOverview.h"

#include <atomic>
#include <cstdlib>
#include <memory>

namespace phe
{
    class Reel
    {
    public:
        static constexpr double kMaxSeconds = 174.0;     // 2.9 minutes, shown as 2:54 on the panel

        // Sets up the memory for a sample rate. Call from prepare (not from the audio thread). If the rate is the
        // same as before nothing changes and the recording is kept. If the rate differs, what is recorded is
        // converted to the new rate so it keeps its pitch and length. Returns true if anything changed.
        bool allocate (double sampleRate);

        double sampleRate() const { return rate; }
        int capacity() const { return cap; }                                  // most samples the reel can hold
        int length() const { return len.load (std::memory_order_relaxed); }   // samples recorded so far
        void setLength (int samples) { len.store (samples, std::memory_order_relaxed); }

        // Forget the recording (the memory is reused, not wiped).
        void clear()
        {
            setLength (0);
            peaks.clear();
        }

        float get (int channel, int index) const { return data[channel & 1].get()[index]; }
        void  set (int channel, int index, float value) { data[channel & 1].get()[index] = value; }

        // The sample at a fractional position inside the recorded part (straight-line blend between neighbours;
        // the sample after the last one is the first, because the reel loops).
        float read (int channel, double position) const;

        // The same, but with a smoother four-point curve through the neighbouring samples. The tape head uses this
        // because it reads at all sorts of speeds. Exactly the sample itself when 'position' is a whole number.
        float readCubic (int channel, double position) const;

        // The waveform picture: each slice covers samplesPerBucket() samples of the whole reel capacity.
        int samplesPerBucket() const { return perBucket; }
        core::PeakOverview peaks;

        // Rebuild the whole waveform picture from the audio (used after a sample-rate conversion).
        void rebuildPeaks();

        // Brings the picture back in line after audio from 'fromSample' on was thrown away: redraws the slice that
        // contains it (from the audio that is still there) and blanks the slices after it. Cheap enough for the audio thread.
        void refreshPeaksFrom (int fromSample);

    private:
        struct FreeDeleter { void operator() (float* p) const { std::free (p); } };
        using Buffer = std::unique_ptr<float, FreeDeleter>;

        double rate = 0.0;
        int cap = 0;
        int perBucket = 1;
        std::atomic<int> len { 0 };
        Buffer data[2];
    };
}
