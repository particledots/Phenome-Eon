// The PhenomeEon audio engine.
//
// Milestone 8: Erase Splice, Undo / Redo, Time Lag Accumulation polish and six optional behaviours (see Params.h), on top of
// milestone 7: splices, on top of milestone 6 (Morph and Pitch Scale), milestone 5 (the tape head and genes) and
// milestone 4 (the reel, recording, Sound On Sound, Clear Reel).
//
//   - The reel is one stereo strip of up to 2.9 minutes (see Reel.h), cut into splices by markers (core/SpliceMap.h).
//   - Rec on an empty reel makes the first recording: the reel is as long as the recording (or fills up to
//     2.9 minutes, when recording stops by itself).
//   - Rec on a reel that already has audio records over the splice the playhead is in, starting at the playhead and
//     carrying on round that splice until Rec is switched off (the Morphagene's "Time Lag Accumulation" / Sound On Sound).
//   - Rec New Splice records a brand-new splice onto the end of the reel (the reel grows) and, when it stops, the new
//     splice is selected and loops straight away.
//   - Sound On Sound blends the live input with the reel loop. What you hear is exactly what is recorded.
//   - Play makes the tape head read the reel (GenePlayer.h): Vari-Speed sets the speed and direction (and Pitch Snap
//     makes it land on notes), Gene-Size how long a gene is, Slide where it starts. The defaults give plain 1/1 looping.
//     Morph sets how genes follow each other (gaps, seamless 1/1, overlaps of two, three and four) and, far round the knob,
//     gives each new gene a random pitch-up and place in the stereo picture; Pitch Scale limits those pitch-ups to a scale.
//   - Splice drops a marker where the tape head is, cutting the splice it is in in two; Delete Splice Marker takes away the
//     marker at the end of the selected splice (joining it with the next); Delete All Markers makes the reel one splice.
//     Shift selects the next splice; Organize selects splices by knob position (equal zones). A newly selected splice starts
//     to play when the gene in progress ends.
//
//   - Erase Splice (milestone 8) removes the selected splice from the reel and closes the gap (on a reel of one splice it empties
//     the reel). Undo puts back what the last edit changed (a recording, a marker added or taken away, Erase Splice, Clear Reel);
//     pressed again it does that edit again. Only the newest edit can be undone.
//   - Big edits (Erase Splice on a long reel, Undo, Redo) are carried out a few thousand samples at a time over a number of audio
//     blocks, so Logic never stalls; while that goes on the reel is silent and the engine is "busy" and ignores the edit buttons.
//   - Recording over existing audio (Time Lag Accumulation) writes back through a gentle limiter, so many passes cannot run away.
//
// Plain C++17. No JUCE, no plugin code.

#pragma once

#include "GenePlayer.h"
#include "Params.h"
#include "Reel.h"
#include "core/SpliceMap.h"
#include "core/UndoStore.h"

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
            int geneSpliceStart = 0;     // the splice the gene window is in (samples), the window wraps round inside it
            int geneSpliceLength = 0;
            int numSplices = 1;
            int selectedSplice = 0;      // the splice chosen by Organize / Shift (0-based)
            int playingSplice = 0;       // the splice being played right now (differs from the selected one until the gene ends)
            int newSpliceBase = 0;       // while recording a new splice: where it begins (otherwise 0)
            int spliceLengthSamples = 0; // length of the selected splice
            int recPos = 0;              // record head
            bool recording = false;
            bool playing = false;
            int recKind = 0;             // while recording: 1 first recording, 2 over the splice (Time Lag Accumulation), 3 new splice
            bool busy = false;           // a big edit (Erase Splice, Undo, Redo) is being carried out
            int undoState = 0;           // 0 nothing to undo, 1 Undo will act, 2 Redo will act
        };
        ReelView getReelView() const;
        const Reel& getReel() const { return reel; }
        const core::SpliceMap& getSpliceMap() const { return map; }

        // The plugin wrapper calls these every so often (not from the audio thread) to find out whether
        // the engine switched something off by itself, so it can switch the matching button off too.
        bool takeRecordStopped() { return recordStopped.exchange (false); }   // reel full, or reel cleared while recording
        bool takeClearHandled()  { return clearHandled.exchange (false); }    // Clear Reel was done
        bool takeNewSpliceStopped() { return newSpliceStopped.exchange (false); }   // Rec New Splice ended (or was refused)

        // The momentary buttons (Splice, Shift, Delete Splice Marker, Delete All Markers) act the moment they go on, then
        // the wrapper switches them back off. This returns a mask of the ones that acted since the last call.
        // Erase Splice and Undo (milestone 8) act the same way.
        static constexpr int kButtonSplice = 1, kButtonShift = 2, kButtonDeleteMarker = 4, kButtonDeleteAllMarkers = 8,
                             kButtonErase = 16, kButtonUndo = 32;
        int takeHandledButtons() { return handledButtons.exchange (0); }

    private:
        void startRecording();
        void stopRecording();
        void finishFirstRecording();
        void finishNewSplice();
        void signalRecordEnded();
        void clearReel();
        void restartPlayback();
        void dropMarker();
        void deleteMarker();
        void deleteAllMarkers();
        void notePeak (int position, float left, float right);
        void flushPeak();
        void setPlayerTargets();

        // Edits and undo (milestone 8)
        using EditState = core::EditState;
        EditState captureState() const;
        void commitStep (const EditState& before);       // an edit has just changed something: it becomes the step Undo undoes
        void saveChunkForUndo (int position);
        void eraseSelectedSplice();
        void undoOrRedo();
        bool runJobSlice (int budgetChunks);             // true when the job is finished
        void finishJob();
        void finishJobNow();
        void abandonJob();
        bool playOn() const { return params.play || params.playMode == 2; }     // trigger loop always plays

        Params params;
        Reel reel;

        double sampleRate = 48000.0;
        bool firstBlock = true;
        bool prevRec = false, prevPlay = false, prevClear = false;
        bool prevRecNew = false, prevSplice = false, prevShift = false, prevDeleteMarker = false, prevDeleteAll = false;
        bool prevErase = false, prevUndo = false;

        // Splices
        core::SpliceMap map;
        core::SpliceSelector selector;
        bool selectorSynced = false;      // the very first block starts the selection from the Organize knob; later blocks never do
        int lastSelStart = 0;             // where the selected splice began last block (Organize Timing "immediately" watches it)

        // Playback: the tape head and the gene player
        GenePlayer player;

        // Recording
        bool recActive = false;
        bool recFirst = false;           // true while making the very first recording (the reel grows)
        bool recStopping = false;        // punching out: fading the recording off
        bool recNew = false;             // recording a new splice onto the end of the reel (the reel grows when it stops)
        bool recByNew = false;           // this recording was started by Rec New Splice (so its switch is Rec New Splice)
        int newSpliceBase = 0;           // where the new splice begins (the old end of the reel)
        int recLo = 0, recHi = 0;        // recording over: the splice it goes round
        int recPos = 0;
        float recGate = 0.0f;            // 0..1: how much of the new signal is written (ramped to avoid clicks)

        // The undo store, and the big edit that is in progress (if any)
        core::UndoStore undo;
        int recSavedChunk = -1;          // the chunk the record head last saved for undo (so most samples need no check)
        struct Job
        {
            enum class Kind { none, eraseMove, swapBack };
            enum class Phase { audio, peaks };
            Kind kind = Kind::none;
            Phase phase = Phase::audio;
            int cursor = 0;              // erase: samples moved so far; undo: the next chunk to swap
            int eraseStart = 0, eraseEnd = 0, oldLength = 0, spliceIndex = 0;     // erase: the removed stretch and the old reel length
            EditState target;            // undo: how things will be when the swap is done
            int peakFrom = 0, peakTo = 0, peakCursor = 0;                         // waveform slices to redraw (in samples)
        } job;
        bool busy = false;

        // Smoothed control values
        float gainIn = 1.0f, gainOut = 1.0f, sos = 0.5f;

        // Waveform picture of the slice being written
        int peakBucket = -1;
        float peakL = 0.0f, peakR = 0.0f;

        // For the window and the wrapper
        std::atomic<int> viewPlayPos { 0 }, viewRecPos { 0 }, viewGeneStart { 0 }, viewGeneSpan { 0 }, viewVoices { 0 };
        std::atomic<int> viewGeneSpliceStart { 0 }, viewGeneSpliceLength { 0 }, viewSelected { 0 }, viewPlayingSplice { 0 };
        std::atomic<int> viewNewBase { 0 }, viewSpliceLength { 0 };
        std::atomic<int> viewRecEnd { 0 };       // while recording a new splice: how far the recording has got (else 0)
        std::atomic<float> viewGeneSeconds { 0.0f }, viewRate { 0.0f };
        std::atomic<bool> viewRecording { false }, viewPlaying { false }, viewBusy { false };
        std::atomic<int> viewRecKind { 0 }, viewUndoState { 0 };
        std::atomic<bool> recordStopped { false }, clearHandled { false }, newSpliceStopped { false };
        std::atomic<int> handledButtons { 0 };
    };
}
