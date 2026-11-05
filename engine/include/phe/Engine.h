// The PhenomeEon audio engine.
//
// Milestone 6: Morph and Pitch Scale, on top of milestone 5 (the tape head and genes) and milestone 4 (the reel,
// recording, Sound On Sound, Clear Reel).
//
//   - The reel is one stereo strip of up to 2.9 minutes (see Reel.h).
//   - Rec on an empty reel makes the first recording: the reel is as long as the recording (or fills up to
//     2.9 minutes, when recording stops by itself).
//   - Rec on a reel that already has audio records over it, starting at the playhead and carrying on round the
//     loop until Rec is switched off (this is the Morphagene's "Time Lag Accumulation" / Sound On Sound).
//   - Sound On Sound blends the live input with the reel loop. What you hear is exactly what is recorded.
//   - Play makes the tape head read the reel (GenePlayer.h): Vari-Speed sets the speed and direction (and Pitch Snap
//     makes it land on notes), Gene-Size how long a gene is, Slide where it starts. The defaults give plain 1/1 looping.
//     Morph sets how genes follow each other (gaps, seamless 1/1, overlaps of two, three and four) and, far round the knob,
//     gives each new gene a random pitch-up and place in the stereo picture; Pitch Scale limits those pitch-ups to a scale.
//     (Splices and Organize come in milestone 7.)
//
// Plain C++17. No JUCE, no plugin code.

#pragma once

#include "GenePlayer.h"
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
            int playPos = 0;             // where the tape head is reading, in samples from the start of the reel
            int geneStart = 0;           // the gene's window in the reel (samples); it may run past the end and wrap round
            int geneSpan = 0;
            float geneSeconds = 0.0f;    // length of a gene in seconds; 0 = the whole reel
            float rate = 0.0f;           // tape speed now (signed; negative = backwards)
            int voices = 0;              // how many genes are playing at this moment
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
        void setPlayerTargets();

        Params params;
        Reel reel;

        double sampleRate = 48000.0;
        bool firstBlock = true;
        bool prevRec = false, prevPlay = false, prevClear = false;

        // Playback: the tape head and the gene player
        GenePlayer player;

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
        std::atomic<int> viewPlayPos { 0 }, viewRecPos { 0 }, viewGeneStart { 0 }, viewGeneSpan { 0 }, viewVoices { 0 };
        std::atomic<float> viewGeneSeconds { 0.0f }, viewRate { 0.0f };
        std::atomic<bool> viewRecording { false }, viewPlaying { false };
        std::atomic<bool> recordStopped { false }, clearHandled { false };
    };
}
