// The PhenomeEon audio engine.
//
// Milestone 4: the reel, recording, Sound On Sound and basic playback.
//
//   - The reel is one stereo strip of up to 2.9 minutes (see Reel.h).
//   - Rec on an empty reel makes the first recording: the reel is as long as the recording (or fills up to
//     2.9 minutes, when recording stops by itself).
//   - Rec on a reel that already has audio records over it, starting at the playhead and carrying on round the
//     loop until Rec is switched off (this is the Morphagene's "Time Lag Accumulation" / Sound On Sound).
//   - Sound On Sound blends the live input with the reel loop. What you hear is exactly what is recorded.
//   - Play loops the reel at normal speed. (Speed, direction, genes and splices come in milestones 5 to 7.)
//
// Plain C++17. No JUCE, no plugin code.

#pragma once

#include "Params.h"
#include "Reel.h"

#include <atomic>

namespace phe
{
    class Engine
    {
    public:
        // Called when Logic starts or restarts playback or changes the sample rate or block size.
        // The recording is kept unless the sample rate changed (then it is converted to the new rate).
        void prepare (double sampleRate, int maxBlockSize, int numChannels);

        // Stops any recording and returns the playhead to the start. The recording itself is kept.
        void reset();

        // The latest control values. Called by the wrapper at the start of every block.
        void setParams (const Params& p) { params = p; }
        const Params& getParams() const { return params; }

        // Process one block in place: 'channels' holds numChannels pointers to numSamples floats each.
        // The sound that comes in is replaced by the sound that goes out.
        void process (float* const* channels, int numChannels, int numSamples);

        double getSampleRate() const { return sampleRate; }

        // ---- for the window (safe to call from another thread while audio is running) ----
        struct ReelView
        {
            int lengthSamples = 0;       // recorded so far
            int capacitySamples = 0;     // the most the reel can hold (2.9 minutes)
            double sampleRate = 0.0;
            int playPos = 0;             // playhead, in samples from the start of the reel
            int recPos = 0;              // record head
            bool recording = false;
            bool playing = false;
        };
        ReelView getReelView() const;
        const Reel& getReel() const { return reel; }

        // The plugin wrapper calls these every so often (not from the audio thread) to find out whether
        // the engine switched something off by itself, so it can switch the matching button off too.
        bool takeRecordStopped() { return recordStopped.exchange (false); }   // reel full, or reel cleared while recording
        bool takeClearHandled()  { return clearHandled.exchange (false); }    // Clear Reel was done

    private:
        void startRecording();
        void stopRecording();
        void finishFirstRecording();
        void clearReel();
        void notePeak (int position, float left, float right);
        void flushPeak();

        Params params;
        Reel reel;

        double sampleRate = 48000.0;
        bool firstBlock = true;
        bool prevRec = false, prevPlay = false, prevClear = false;

        // Playback
        bool playing = false;
        double playPos = 0.0;

        // Recording
        bool recActive = false;
        bool recFirst = false;           // true while making the very first recording (the reel grows)
        bool recStopping = false;        // punching out: fading the recording off
        int recPos = 0;
        float recGate = 0.0f;            // 0..1: how much of the new signal is written (ramped to avoid clicks)

        // Smoothed control values
        float gainIn = 1.0f, gainOut = 1.0f, sos = 0.5f;

        // Waveform picture of the slice being written
        int peakBucket = -1;
        float peakL = 0.0f, peakR = 0.0f;

        // For the window and the wrapper
        std::atomic<int> viewPlayPos { 0 }, viewRecPos { 0 };
        std::atomic<bool> viewRecording { false }, viewPlaying { false };
        std::atomic<bool> recordStopped { false }, clearHandled { false };
    };
}
