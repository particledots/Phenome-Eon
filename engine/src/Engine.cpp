#include <phe/Engine.h>
#include <phe/core/Gain.h>
#include <phe/core/Morph.h>
#include <phe/core/Scales.h>
#include <phe/core/VariSpeed.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace phe
{

namespace
{
    constexpr int    kMinFirstRecording = 64;       // a first recording (or a new splice) shorter than this many samples is thrown away
    constexpr double kGateRampSeconds   = 0.004;    // punching in and out of a recording over existing audio
    constexpr double kSmoothSeconds     = 0.010;    // level knobs glide instead of jumping
    constexpr int    kJobChunksPerBlock = 8;        // a big edit (Erase Splice, Undo) moves this many chunks (about 2.7 s of audio) per block
    constexpr int    kJobBucketsPerBlock = 48;      // ... and redraws this many waveform slices per block

    // Anything that is not a normal number is treated as silence, so a bad sample from the host can never
    // get into the reel and stay there forever.
    inline float clean (float x) { return std::isfinite (x) ? x : 0.0f; }

    inline float clamp01 (float x) { return std::min (1.0f, std::max (0.0f, x)); }
}

//==============================================================================
void Engine::prepare (double newSampleRate, int /*maxBlockSize*/, int /*numChannels*/)
{
    const double oldRate = reel.sampleRate();
    const int oldLength = reel.length();

    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    player.prepare (sampleRate);

    // Only if the sample rate changed (or this is the first time) does the reel's memory change;
    // otherwise whatever is recorded stays exactly as it is.
    if (reel.allocate (sampleRate))
    {
        abandonJob();
        undo.prepare (reel.capacity());                  // a new reel size: the undo history starts again
        recActive = recStopping = recFirst = recNew = false;
        peakBucket = -1;

        // The splice markers move with the audio when it is converted to the new sample rate.
        if (oldRate > 0.0 && oldLength > 0 && reel.length() > 0)
            map.rescale (sampleRate / oldRate, reel.length());
        else
            map.clear();
        selector.sync (params.organize, map.numSplices());
        selectorSynced = true;

        if (playOn() && reel.length() > 0)
        {
            setPlayerTargets();
            player.start (reel);
        }
        else
            player.stop();
        viewPlayPos.store (0);
        viewRecPos.store (0);
        viewRecEnd.store (0);
        viewRecording.store (false);
        viewPlaying.store (player.isPlaying());
        viewBusy.store (false);
        viewUndoState.store (0);
    }
    else
    {
        finishJobNow();                                  // never leave a big edit half done
        if (! undo.ready() && reel.capacity() > 0)
            undo.prepare (reel.capacity());
    }
}

void Engine::reset()
{
    finishJobNow();
    if (recActive)
    {
        if (recFirst)
            finishFirstRecording();
        else if (recNew)
            finishNewSplice();
        recActive = recStopping = recFirst = recNew = false;
        flushPeak();
        peakBucket = -1;
        signalRecordEnded();
    }
    if (playOn() && reel.length() > 0)
    {
        setPlayerTargets();
        player.start (reel);
    }
    else
        player.stop();
    firstBlock = true;      // the controls are picked up afresh at the next block
}

//==============================================================================
// Tells the wrapper to switch off the button that started the recording that has just ended by itself.
void Engine::signalRecordEnded()
{
    if (recByNew)
        newSpliceStopped.store (true);
    else
        recordStopped.store (true);
}

void Engine::startRecording()
{
    if (reel.capacity() <= 0)
    {
        signalRecordEnded();                        // no memory: tell the wrapper to switch the button off
        return;
    }

    const int length = reel.length();
    recStopping = false;
    recNew = false;
    peakBucket = -1;
    recSavedChunk = -1;
    bumpEdit();                                     // the reel is about to change

    if (length <= 0)
    {
        // First recording: the reel is empty, so it grows as we record (and it becomes splice 1).
        commitStep (captureState());                // the recording is one undoable step
        recActive = true;
        recFirst = true;
        recPos = 0;
        recGate = 1.0f;
        player.stop();
    }
    else if (recByNew)
    {
        // A new splice onto the end of the reel. Needs room for at least a few samples and a free marker.
        if (reel.capacity() - length < kMinFirstRecording || map.numMarkers() >= core::kMaxSpliceMarkers)
        {
            signalRecordEnded();
            return;
        }
        commitStep (captureState());
        recActive = true;
        recFirst = false;
        recNew = true;
        newSpliceBase = length;
        recPos = length;
        recGate = 1.0f;
    }
    else
    {
        // Recording over the splice the playhead is in, starting where the playhead is.
        commitStep (captureState());
        recActive = true;
        recFirst = false;
        const int pos = std::min (length - 1, std::max (0, (int) player.headPosition (reel)));
        const int j = map.indexAt (pos, length);
        recLo = map.start (j);
        recHi = map.end (j, length);
        recPos = pos;
        recGate = 0.0f;
    }
}

void Engine::stopRecording()
{
    if (! recActive)
        return;

    if (recFirst)
        finishFirstRecording();
    else if (recNew)
        finishNewSplice();
    else
        recStopping = true;         // the recording fades off over a few milliseconds, then stops
}

void Engine::finishFirstRecording()
{
    flushPeak();
    peakBucket = -1;
    bumpEdit();

    const int length = recPos;
    recActive = false;
    recStopping = false;
    recFirst = false;

    if (length < kMinFirstRecording)
    {
        reel.clear();                               // too short to be a recording
        player.stop();
        undo.cancel();                              // nothing changed: nothing to undo
    }
    else
    {
        reel.setLength (length);
        map.clear();                                // a new reel is one splice
        selector.sync (params.organize, 1);
        lastSelStart = 0;
        if (playOn())
            player.start (reel);                    // the new recording starts looping straight away (if Play is on)
        else
            player.stop();
    }
}

// The recording of a new splice is finished: it becomes the last splice of the reel, is selected, and starts looping.
void Engine::finishNewSplice()
{
    flushPeak();
    peakBucket = -1;
    bumpEdit();

    const int base = newSpliceBase;
    const int end = recPos;
    recActive = false;
    recStopping = false;
    recNew = false;

    if (end - base < kMinFirstRecording)
    {
        reel.refreshPeaksFrom (base);               // too short to be a splice: the reel stays as it was
        undo.cancel();
        return;
    }

    reel.setLength (end);
    const int j = map.addMarker (base, end);        // the old end of the reel becomes the marker in front of the new splice
    if (j >= 0)
    {
        selector.markerInserted (j);
        selector.select (map.numSplices() - 1);
        restartPlayback();
    }
}

// Moves playback to the selected splice straight away (used after a new splice is recorded). With Play off nothing plays.
void Engine::restartPlayback()
{
    setPlayerTargets();
    const int length = reel.length();
    lastSelStart = length > 0 ? map.start (std::min (map.numSplices() - 1, std::max (0, selector.selected()))) : 0;
    if (playOn() && length > 0)
        player.switchSplice (reel);
    else
        player.stop();
}

void Engine::clearReel()
{
    if (recActive)
    {
        recActive = recStopping = recFirst = recNew = false;
        signalRecordEnded();
    }
    const EditState before = captureState();
    peakBucket = -1;
    reel.clear();
    map.clear();
    selector.sync (params.organize, 1);
    player.stop();
    recPos = 0;
    if (before.length > 0)
    {
        commitStep (before);                        // the audio itself is untouched, so Undo only has to bring back the length and markers
        bumpEdit();
    }
    clearHandled.store (true);
}

//==============================================================================
// SPLICE: a marker where the tape head is now. The splice it cuts keeps its number (it is now the part before the marker),
// so the loop you were hearing becomes the stretch up to the point where you pressed.
void Engine::dropMarker()
{
    const int length = reel.length();
    if (length <= 0 || (recActive && recFirst))
        return;

    const EditState before = captureState();
    const int position = (int) player.headPosition (reel);
    const int j = map.addMarker (position, length);
    if (j >= 0)
    {
        selector.markerInserted (j);
        commitStep (before);
        bumpEdit();
    }
}

// DELETE SPLICE MARKER: takes away the marker at the end of the selected splice, so it joins the next one.
void Engine::deleteMarker()
{
    const int k = selector.selected();
    const EditState before = captureState();
    if (map.removeMarker (k))
    {
        selector.markerRemoved (k);
        commitStep (before);
        bumpEdit();
    }
}

// DELETE ALL MARKERS: the whole reel becomes one splice.
void Engine::deleteAllMarkers()
{
    if (map.numMarkers() == 0)
        return;
    const EditState before = captureState();
    map.clear();
    selector.sync (params.organize, 1);
    commitStep (before);
    bumpEdit();
}

//==============================================================================
// Edits and undo (milestone 8)

Engine::EditState Engine::captureState() const
{
    EditState state;
    state.length = reel.length();
    state.selected = selector.selected();
    state.numMarkers = map.snapshot (state.markers, core::kMaxSpliceMarkers);
    return state;
}

// An edit that is about to change (or has just changed) the reel becomes the one step Undo can undo. A recording in progress
// keeps its own step open until it ends, so marker edits made while recording are undone with the recording.
void Engine::commitStep (const EditState& before)
{
    if (recActive)
        return;
    undo.begin (before);
}

void Engine::saveChunkForUndo (int position)
{
    const int chunk = position >> core::UndoStore::kChunkShift;
    if (chunk != recSavedChunk)
    {
        float* channels[2] = { reel.channel (0), reel.channel (1) };
        undo.save (channels, chunk);
        recSavedChunk = chunk;
    }
}

// ERASE SPLICE: takes the selected splice out of the reel and closes the gap. On a reel that is one splice, that is the
// whole reel, so it is emptied (the same as Clear Reel). The audio after the splice is moved up over a number of blocks.
void Engine::eraseSelectedSplice()
{
    const int length = reel.length();
    if (length <= 0 || recActive || busy)
        return;

    if (map.numSplices() <= 1)
    {
        clearReel();
        return;
    }

    const int j = std::min (map.numSplices() - 1, std::max (0, selector.selected()));
    job = Job();
    job.kind = Job::Kind::eraseMove;
    job.spliceIndex = j;
    job.eraseStart = map.start (j);
    job.eraseEnd = map.end (j, length);
    job.oldLength = length;
    commitStep (captureState());
    player.stop();
    busy = true;
}

// UNDO: puts back what the last edit changed. The state it brings back is kept the other way round, so pressing again redoes it.
void Engine::undoOrRedo()
{
    if (recActive || busy || ! undo.ready() || ! undo.hasStep())
        return;

    const EditState current = captureState();
    job = Job();
    job.kind = Job::Kind::swapBack;
    job.target = undo.other();
    job.cursor = std::max (0, undo.firstSaved());
    undo.flip (current);
    player.stop();
    busy = true;
}

// Carries a big edit on a little. Returns true when it is completely done.
bool Engine::runJobSlice (int budgetChunks)
{
    budgetChunks = std::max (1, std::min (budgetChunks, 4096));
    float* channels[2] = { reel.channel (0), reel.channel (1) };

    // Saving: the reel is copied out a piece at a time (nothing is drawn or restarted afterwards).
    if (job.kind == Job::Kind::exportCopy)
    {
        const int total = job.oldLength;
        const int count = std::min (total - job.cursor, budgetChunks * core::UndoStore::kChunkFrames);
        if (count > 0)
        {
            for (int c = 0; c < 2; ++c)
                std::memcpy (exportAudio.ch[c].data() + job.cursor, channels[c] + job.cursor, (size_t) count * sizeof (float));
            job.cursor += count;
        }
        return job.cursor >= total;
    }

    if (job.phase == Job::Phase::audio)
    {
        if (job.kind == Job::Kind::eraseMove)
        {
            const int removed = job.eraseEnd - job.eraseStart;
            const int total = job.oldLength - job.eraseEnd;                       // samples after the splice, to be moved up
            const int count = std::min (total - job.cursor, budgetChunks * core::UndoStore::kChunkFrames);
            if (count > 0)
            {
                const int to = job.eraseStart + job.cursor, from = job.eraseEnd + job.cursor;
                undo.saveRange (channels, to, to + count);                         // the old audio is kept for Undo first
                for (int c = 0; c < 2; ++c)
                    std::memmove (channels[c] + to, channels[c] + from, (size_t) count * sizeof (float));
                job.cursor += count;
            }
            if (job.cursor < total)
                return false;

            map.eraseSplice (job.spliceIndex, job.oldLength);
            reel.setLength (job.oldLength - removed);
            selector.select (std::min (job.spliceIndex, map.numSplices() - 1));
            job.peakFrom = job.eraseStart;
            job.peakTo = job.oldLength;
            bumpEdit();
        }
        else if (job.kind == Job::Kind::importWrite)
        {
            // Loading a reel file: the old audio is kept for Undo a piece at a time, just before it is written over.
            const int total = (int) importAudio.frames();
            const int count = std::min (total - job.cursor, budgetChunks * core::UndoStore::kChunkFrames);
            if (count > 0)
            {
                undo.saveRange (channels, job.cursor, job.cursor + count);
                for (int c = 0; c < 2; ++c)
                    std::memcpy (channels[c] + job.cursor, importAudio.ch[c].data() + job.cursor, (size_t) count * sizeof (float));
                job.cursor += count;
            }
            if (job.cursor < total)
                return false;

            const int oldLength = reel.length();
            reel.setLength (total);
            map.setMarkers (importAudio.markers.data(), (int) importAudio.markers.size());
            selector.sync (params.organize, map.numSplices());
            job.peakFrom = 0;
            job.peakTo = std::max (oldLength, total);
            bumpEdit();
        }
        else if (job.kind == Job::Kind::swapBack)
        {
            const int last = undo.lastSaved();
            int c = job.cursor;
            for (int n = 0; n < budgetChunks && c <= last; ++n, ++c)
                undo.swap (channels, c);
            job.cursor = c;
            if (c <= last)
                return false;

            const int currentLength = reel.length();
            reel.setLength (job.target.length);
            map.setMarkers (job.target.markers, job.target.numMarkers);
            selector.select (std::min (map.numSplices() - 1, std::max (0, job.target.selected)));

            const int firstChanged = undo.firstSaved() >= 0 ? undo.firstSaved() * core::UndoStore::kChunkFrames : currentLength;
            job.peakFrom = std::min (firstChanged, std::min (currentLength, job.target.length));
            job.peakTo = std::max (currentLength, job.target.length);
            bumpEdit();
        }
        else
            return true;

        job.phase = Job::Phase::peaks;
        job.peakCursor = job.peakFrom / std::max (1, reel.samplesPerBucket());
    }

    // The waveform picture catches up, a few slices at a time.
    const int perBucket = std::max (1, reel.samplesPerBucket());
    const int endBucket = std::min (core::PeakOverview::kBuckets, (job.peakTo + perBucket - 1) / perBucket);
    if (job.peakCursor < endBucket)
    {
        const int upTo = std::min (endBucket, job.peakCursor + kJobBucketsPerBlock * budgetChunks / kJobChunksPerBlock);
        reel.rebuildPeakBuckets (job.peakCursor, upTo);
        job.peakCursor = upTo;
        if (job.peakCursor < endBucket)
            return false;
    }
    return true;
}

void Engine::finishJob()
{
    const Job::Kind kind = job.kind;
    job = Job();
    busy = false;
    viewBusy.store (false);

    if (kind == Job::Kind::exportCopy)
    {
        exportState.store (3, std::memory_order_release);       // the copy is complete; playback was never interrupted
        return;
    }
    if (kind == Job::Kind::importWrite)
    {
        importStamp = editStamp();
        importState.store (3, std::memory_order_release);
    }
    restartPlayback();              // plays from the start of the (now different) selected splice, if Play is on
}

void Engine::finishJobNow()
{
    while (busy)
        if (runJobSlice (4096))
            finishJob();
}

void Engine::abandonJob()
{
    const Job::Kind kind = job.kind;
    job = Job();
    busy = false;
    viewBusy.store (false);
    if (kind == Job::Kind::exportCopy)
        exportState.store (6, std::memory_order_release);
    else if (kind == Job::Kind::importWrite)
        importState.store (4, std::memory_order_release);
}

//==============================================================================
void Engine::notePeak (int position, float left, float right)
{
    const int perBucket = reel.samplesPerBucket();
    const int bucket = position / perBucket;

    if (bucket != peakBucket)
    {
        flushPeak();
        peakBucket = bucket;
        if (position % perBucket == 0)
            peakL = peakR = 0.0f;                              // starting a slice afresh
        else
            reel.peaks.get (bucket, peakL, peakR);             // joining a slice part-way: keep what is already there
    }

    peakL = std::max (peakL, std::fabs (left));
    peakR = std::max (peakR, std::fabs (right));
}

void Engine::flushPeak()
{
    if (peakBucket >= 0)
        reel.peaks.set (peakBucket, peakL, peakR);
}

//==============================================================================
void Engine::process (float* const* channels, int numChannels, int numSamples)
{
    if (numSamples <= 0 || numChannels <= 0)
        return;

    const int chs = std::min (numChannels, 2);

    // ---- the controls, once per block ----
    const int stepIndex = std::min (kNumInputGainSteps - 1, std::max (0, params.inputGainStep));
    const float targetIn  = dsp::dbToGain (kInputGainStepDb[stepIndex]) * dsp::levelKnobToGain (params.inputLevel);
    const float targetOut = dsp::levelKnobToGain (params.outputLevel);
    const float targetSos = clamp01 (params.soundOnSound);

    if (firstBlock)
    {
        // Start from the controls as they are: no gliding in, and no button counts as "just pressed".
        gainIn = targetIn;
        gainOut = targetOut;
        sos = targetSos;
        prevRec = params.rec;
        prevPlay = params.play;
        prevClear = params.clearReel;
        prevRecNew = params.recNewSplice;
        prevSplice = params.splice;
        prevShift = params.shift;
        prevDeleteMarker = params.deleteMarker;
        prevDeleteAll = params.deleteAllMarkers;
        prevErase = params.eraseSplice;
        prevUndo = params.undo;
        if (! selectorSynced)
        {
            selector.sync (params.organize, map.numSplices());
            selectorSynced = true;
        }
        setPlayerTargets();
        lastSelStart = reel.length() > 0 ? map.start (std::min (map.numSplices() - 1, selector.selected())) : 0;
        if (playOn() && reel.length() > 0 && ! (recActive && recFirst) && ! busy)
            player.start (reel);
        else
            player.stop();
        firstBlock = false;
    }

    // A big edit in progress (Erase Splice, Undo, Redo) carries on a little each block; until it is done the reel is silent.
    if (busy && runJobSlice (kJobChunksPerBlock))
        finishJob();

    // Saving or loading a reel file starts here, when nothing else is going on (never while recording or in the middle of an edit).
    if (! busy && ! recActive)
    {
        if (exportState.load (std::memory_order_acquire) == 1)
            startExportJob();
        else if (importState.load (std::memory_order_acquire) == 1)
            startImportJob();
    }

    // ---- buttons: act when they go on ----
    const bool clearEdge     = params.clearReel && ! prevClear;
    const bool spliceEdge    = params.splice && ! prevSplice;
    const bool deleteEdge    = params.deleteMarker && ! prevDeleteMarker;
    const bool deleteAllEdge = params.deleteAllMarkers && ! prevDeleteAll;
    const bool eraseEdge     = params.eraseSplice && ! prevErase;
    const bool undoEdge      = params.undo && ! prevUndo;
    prevClear = params.clearReel;
    prevSplice = params.splice;
    prevDeleteMarker = params.deleteMarker;
    prevDeleteAll = params.deleteAllMarkers;
    prevErase = params.eraseSplice;
    prevUndo = params.undo;

    if (busy)
    {
        // A big edit is still being carried out: the edit buttons are ignored, but they still go back off.
        if (clearEdge)     clearHandled.store (true);
        if (spliceEdge)    handledButtons.fetch_or (kButtonSplice);
        if (deleteEdge)    handledButtons.fetch_or (kButtonDeleteMarker);
        if (deleteAllEdge) handledButtons.fetch_or (kButtonDeleteAllMarkers);
        if (eraseEdge)     handledButtons.fetch_or (kButtonErase);
        if (undoEdge)      handledButtons.fetch_or (kButtonUndo);
    }
    else
    {
        if (clearEdge)
            clearReel();

        // Splice markers
        if (spliceEdge)
        {
            dropMarker();
            handledButtons.fetch_or (kButtonSplice);
        }
        if (deleteEdge)
        {
            deleteMarker();
            handledButtons.fetch_or (kButtonDeleteMarker);
        }
        if (deleteAllEdge)
        {
            deleteAllMarkers();
            handledButtons.fetch_or (kButtonDeleteAllMarkers);
        }

        // Erase Splice and Undo (both are refused while recording)
        if (eraseEdge)
        {
            eraseSelectedSplice();
            handledButtons.fetch_or (kButtonErase);
        }
        if (undoEdge)
        {
            undoOrRedo();
            handledButtons.fetch_or (kButtonUndo);
        }
    }

    // Which splice is selected: Shift steps on, Organize (when it moves into another zone) wins over Shift.
    const bool shiftPressed = params.shift && ! prevShift;
    if (shiftPressed)
        handledButtons.fetch_or (kButtonShift);
    prevShift = params.shift;
    selector.update (params.organize, map.numSplices(), shiftPressed);

    // ---- the tape head and genes: what Vari-Speed, Pitch Snap, Gene-Size, Slide, Play and the selected splice ask for ----
    setPlayerTargets();

    // Organize Timing "immediately": a newly selected splice takes over at once instead of when the gene ends.
    {
        const int length = reel.length();
        const int selStart = length > 0 ? map.start (std::min (map.numSplices() - 1, std::max (0, selector.selected()))) : 0;
        if (params.organizeMode == 1 && ! busy && playOn() && player.isPlaying() && length > 0 && ! (recActive && recFirst)
            && selStart != lastSelStart)
            player.switchSplice (reel);
        lastSelStart = selStart;
    }

    // Play Mode "trigger loop" plays at all times (even when Play was off when this mode was chosen).
    if (params.playMode == 2 && ! busy && ! player.isPlaying() && reel.length() > 0 && ! (recActive && recFirst))
        player.start (reel);

    // Rec (a switch): on = recording over the splice the playhead is in; Rec New Splice (a switch): on = recording a new splice.
    if (params.rec && ! prevRec)
    {
        if (busy)
            recordStopped.store (true);             // a big edit is being carried out: refused, the button goes back off
        else if (recActive && recByNew && ! recStopping)
        {
            // Pressing Rec while a new splice is being recorded stops it, as on the module.
            stopRecording();
            recordStopped.store (true);
            newSpliceStopped.store (true);
        }
        else if (recActive && recStopping)
            recStopping = false;                    // pressed again while still fading out: just carry on
        else if (! recActive)
        {
            recByNew = false;
            startRecording();
        }
    }
    else if (! params.rec && prevRec)
    {
        if (recActive && ! recByNew)
            stopRecording();
    }
    prevRec = params.rec;

    if (params.recNewSplice && ! prevRecNew)
    {
        if (busy)
            newSpliceStopped.store (true);
        else if (! recActive)
        {
            recByNew = true;
            startRecording();
        }
        else
            newSpliceStopped.store (true);          // something is already recording: this press is refused, the button goes back off
    }
    else if (! params.recNewSplice && prevRecNew)
    {
        if (recActive && recByNew)
            stopRecording();
    }
    prevRecNew = params.recNewSplice;

    if (params.play && ! prevPlay)
    {
        if (! busy && reel.length() > 0 && ! (recActive && recFirst))
        {
            if (params.playMode == 2)
                player.switchSplice (reel);         // trigger loop: jumps back to the start (the old genes fade out quickly)
            else
                player.start (reel);                // turning Play on starts a fresh gene, at the Slide position
        }
    }
    else if (! params.play && prevPlay && params.playMode == 1 && ! busy)
        player.release();                           // momentary: stops straight away (a fade of a millisecond or two)
    prevPlay = params.play;

    // ---- constants for this block ----
    const float smoothK  = 1.0f - std::exp (-1.0f / (float) (kSmoothSeconds * sampleRate));
    const float gateStep = 1.0f / (float) (kGateRampSeconds * sampleRate);
    const int capacity = reel.capacity();

    for (int i = 0; i < numSamples; ++i)
    {
        gainIn  += (targetIn  - gainIn)  * smoothK;
        gainOut += (targetOut - gainOut) * smoothK;
        sos     += (targetSos - sos)     * smoothK;

        const float inL = clean (channels[0][i]) * gainIn;
        const float inR = chs > 1 ? clean (channels[1][i]) * gainIn : inL;

        float liveGain, loopGain;
        dsp::sosGains (sos, liveGain, loopGain);

        // What the tape head is playing right now (nothing while the first recording is being made)
        const int length = reel.length();
        GenePlayer::Out loop;
        if (player.isPlaying() && length > 0 && ! (recActive && recFirst))
            loop = player.step (reel);

        // The mix. What is heard is the live input plus the gene player's sound. What is recorded is the same mix,
        // except that the fade at the end of a whole-splice lap is never baked into the reel (see GenePlayer.h).
        const float liveL = inL * liveGain, liveR = inR * liveGain;
        // Record Source "input only": what is written is the live input alone, whatever Sound On Sound says (Sound On Sound then only
        // changes what you hear). Laying one pass over another is then impossible; recording over a splice replaces it.
        const bool inputOnly = params.recordSource == 1;
        const float recordL = inputOnly ? inL : liveL + loop.recordL * loopGain;
        const float recordR = inputOnly ? inR : liveR + loop.recordR * loopGain;
        channels[0][i] = (liveL + loop.heardL * loopGain) * gainOut;
        if (chs > 1)
            channels[1][i] = (liveR + loop.heardR * loopGain) * gainOut;

        // ---- recording ----
        if (recActive)
        {
            const bool writesFresh = recFirst || recNew;       // writing into new tape: nothing underneath to blend with

            if (recStopping)
                recGate = std::max (0.0f, recGate - gateStep);
            else if (! writesFresh)
                recGate = std::min (1.0f, recGate + gateStep);

            // Over audio that is already there (Time Lag Accumulation) what is written goes through a gentle limiter, so that
            // pass after pass can pile up without the reel running away.
            float writeL = recordL, writeR = recordR;
            float oldL = 0.0f, oldR = 0.0f;
            if (! writesFresh)
            {
                oldL = reel.get (0, recPos);
                oldR = reel.get (1, recPos);
                writeL = dsp::softLimit (writeL);
                writeR = dsp::softLimit (writeR);
            }
            const float newL = recGate >= 1.0f ? writeL : oldL + (writeL - oldL) * recGate;
            const float newR = recGate >= 1.0f ? writeR : oldR + (writeR - oldR) * recGate;
            saveChunkForUndo (recPos);              // the audio about to be overwritten is kept for Undo
            reel.set (0, recPos, newL);
            reel.set (1, recPos, newR);
            notePeak (recPos, newL, newR);
            ++recPos;

            if (recFirst)
            {
                reel.setLength (recPos);
                if (recPos >= capacity)             // the reel is full: stop by itself
                {
                    finishFirstRecording();
                    signalRecordEnded();
                }
            }
            else if (recNew)
            {
                if (recPos >= capacity)             // the reel is full: the new splice ends here
                {
                    finishNewSplice();
                    signalRecordEnded();
                }
            }
            else
            {
                if (recPos >= recHi)
                    recPos = recLo;                 // carry on round the splice
                if (recStopping && recGate <= 0.0f)
                {
                    recActive = false;
                    recStopping = false;
                    flushPeak();
                    peakBucket = -1;
                    bumpEdit();
                }
            }
        }
    }

    flushPeak();
    const int length = reel.length();
    const auto gene = player.view (reel);
    const int numSplices = map.numSplices();
    const int selected = std::min (numSplices - 1, std::max (0, selector.selected()));

    int playingSplice = selected;
    if (player.isPlaying() && length > 0 && gene.spliceLength > 0.0)
        playingSplice = map.indexAt ((int) (gene.spliceStart + 0.5), length);

    viewPlayPos.store ((int) player.headPosition (reel));
    viewGeneStart.store ((int) gene.windowStart);
    viewGeneSpan.store ((int) gene.windowSpan);
    viewGeneSpliceStart.store ((int) gene.spliceStart);
    viewGeneSpliceLength.store ((int) gene.spliceLength);
    viewGeneSeconds.store ((float) gene.geneSeconds);
    viewRate.store (gene.rate);
    viewVoices.store (gene.voices);
    viewSelected.store (selected);
    viewPlayingSplice.store (playingSplice);
    viewSpliceLength.store (length > 0 ? map.length (selected, length) : 0);
    viewNewBase.store (recNew ? newSpliceBase : 0);
    viewRecEnd.store (recNew ? recPos : 0);
    viewRecPos.store (recPos);
    viewRecording.store (recActive);
    viewPlaying.store (player.isPlaying() && reel.length() > 0);
    viewBusy.store (busy);
    viewRecKind.store (recActive ? (recFirst ? 1 : (recNew ? 3 : 2)) : 0);
    viewUndoState.store (undo.hasStep() ? (undo.undone() ? 2 : 1) : 0);
}

void Engine::setPlayerTargets()
{
    const int length = reel.length();
    const int selected = std::min (map.numSplices() - 1, std::max (0, selector.selected()));
    const int spliceLength = length > 0 ? map.length (selected, length) : 0;

    GenePlayer::Targets t;
    t.rate = dsp::variSpeedRate (params.variSpeed, std::min (2, std::max (0, params.pitchSnap)),
                                 std::min (2, std::max (0, params.variSpeedMode)));
    t.geneSeconds = geneSeconds (params.geneSize, (double) (spliceLength > 0 ? spliceLength : length) / sampleRate);
    t.slide = clamp01 (params.slide);
    t.playWanted = params.playMode == 1 ? params.play : playOn();
    t.morph = dsp::morphFromKnob (params.morph);
    t.scaleMask = scales::maskFor (params.pitchScale);
    t.spliceStart = length > 0 ? map.start (selected) : 0;
    t.spliceLength = spliceLength;
    t.smoothWindow = params.geneWindow == 1;
    t.chordMode = std::min (dsp::kNumMorphChords - 1, std::max (0, params.morphChord));
    player.setTargets (t);
}

//==============================================================================
// Reel files (milestone 9)
//
// The window thread and the audio thread hand a whole reel to each other through two slots (exportAudio / importAudio). Each slot
// has a state number; whoever the state says owns the slot is the only one that touches its data, and a change of owner is a
// release / acquire pair, so the data is always complete when the other side sees it. The audio thread never allocates: the window
// thread makes the lists the right size first, and the audio thread only copies into or out of them a piece at a time.

bool Engine::requestExport()
{
    int expected = 0;
    if (! exportState.compare_exchange_strong (expected, 5))
        return false;                                   // a copy is already in progress (or waiting to be collected)

    const int length = reel.length();
    exportAudio = ReelAudio();
    exportMarkerCount = 0;
    exportLength = 0;
    if (length > 0)
    {
        exportAudio.ch[0].assign ((size_t) length, 0.0f);
        exportAudio.ch[1].assign ((size_t) length, 0.0f);
        exportAudio.markers.assign ((size_t) core::kMaxSpliceMarkers, 0);
    }
    exportState.store (length > 0 ? 1 : 4, std::memory_order_release);
    return true;
}

void Engine::startExportJob()
{
    const int length = reel.length();
    if (length <= 0)
    {
        exportState.store (4, std::memory_order_release);
        return;
    }
    if (exportAudio.ch[0].size() < (size_t) length || exportAudio.ch[1].size() < (size_t) length)
    {
        exportState.store (6, std::memory_order_release);      // the reel grew since it was asked for: ask again
        return;
    }

    exportAudio.sampleRate = reel.sampleRate();
    exportMarkerCount = map.snapshot (exportAudio.markers.data(), core::kMaxSpliceMarkers);
    exportLength = length;
    exportStamp = editStamp();

    job = Job();
    job.kind = Job::Kind::exportCopy;
    job.oldLength = length;
    busy = true;                                               // edit buttons are ignored while the copy is made; playback carries on
    exportState.store (2, std::memory_order_relaxed);
}

Engine::IoResult Engine::pollExport (ReelAudio& out, uint32_t& stamp)
{
    const int state = exportState.load (std::memory_order_acquire);
    if (state == 3)
    {
        exportAudio.ch[0].resize ((size_t) exportLength);
        exportAudio.ch[1].resize ((size_t) exportLength);
        exportAudio.markers.resize ((size_t) exportMarkerCount);
        out = std::move (exportAudio);
        exportAudio = ReelAudio();
        stamp = exportStamp;
        exportState.store (0, std::memory_order_release);
        return IoResult::done;
    }
    if (state == 4 || state == 6)
    {
        exportAudio = ReelAudio();
        exportState.store (0, std::memory_order_release);
        return state == 4 ? IoResult::empty : IoResult::rejected;
    }
    return IoResult::none;
}

bool Engine::submitImport (ReelAudio&& audio, std::string& whyNot)
{
    if (reel.capacity() <= 0 || reel.sampleRate() <= 0.0)
    {
        whyNot = "Not ready: the reel has no memory yet.";
        return false;
    }
    if (viewRecording.load())
    {
        whyNot = "A recording is running - stop it first.";
        return false;
    }
    const size_t n = audio.frames();
    if (n < (size_t) kMinFirstRecording || n > (size_t) reel.capacity() || audio.ch[1].size() != n
        || std::fabs (audio.sampleRate - reel.sampleRate()) > 0.01)
    {
        whyNot = "Does not fit: the sample rate or the reel size changed.";
        return false;
    }

    int expected = 0;
    if (! importState.compare_exchange_strong (expected, 5))
    {
        whyNot = "Still busy with the last file - try again in a moment.";
        return false;
    }
    importAudio = std::move (audio);
    importState.store (1, std::memory_order_release);
    return true;
}

void Engine::startImportJob()
{
    const size_t n = importAudio.frames();
    if (n < (size_t) kMinFirstRecording || n > (size_t) reel.capacity() || importAudio.ch[1].size() != n
        || std::fabs (importAudio.sampleRate - reel.sampleRate()) > 0.01)
    {
        importState.store (4, std::memory_order_release);      // the sample rate changed since the file was prepared
        return;
    }

    job = Job();
    job.kind = Job::Kind::importWrite;
    commitStep (captureState());                               // the whole load is one undoable step
    player.stop();
    busy = true;
    importState.store (2, std::memory_order_relaxed);
}

Engine::IoResult Engine::pollImport (uint32_t& stamp)
{
    const int state = importState.load (std::memory_order_acquire);
    if (state == 3)
    {
        stamp = importStamp;
        importAudio = ReelAudio();
        importState.store (0, std::memory_order_release);
        return IoResult::done;
    }
    if (state == 4)
    {
        importAudio = ReelAudio();
        importState.store (0, std::memory_order_release);
        return IoResult::rejected;
    }
    return IoResult::none;
}

//==============================================================================
Engine::ReelView Engine::getReelView() const
{
    ReelView v;
    const int recEnd = viewRecEnd.load();
    v.lengthSamples   = std::max (reel.length(), recEnd);      // while a new splice is being recorded the reel shows as growing
    v.capacitySamples = reel.capacity();
    v.sampleRate      = reel.sampleRate();
    v.playPos         = viewPlayPos.load();
    v.geneStart       = viewGeneStart.load();
    v.geneSpan        = viewGeneSpan.load();
    v.geneSpliceStart = viewGeneSpliceStart.load();
    v.geneSpliceLength = viewGeneSpliceLength.load();
    v.geneSeconds     = viewGeneSeconds.load();
    v.rate            = viewRate.load();
    v.voices          = viewVoices.load();
    v.numSplices      = map.numSplices();
    v.selectedSplice  = viewSelected.load();
    v.playingSplice   = viewPlayingSplice.load();
    v.newSpliceBase   = viewNewBase.load();
    v.spliceLengthSamples = viewSpliceLength.load();
    v.recPos          = viewRecPos.load();
    v.recording       = viewRecording.load();
    v.playing         = viewPlaying.load();
    v.recKind         = viewRecKind.load();
    v.busy            = viewBusy.load();
    v.undoState       = viewUndoState.load();
    return v;
}

} // namespace phe
