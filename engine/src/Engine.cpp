#include <phe/Engine.h>
#include <phe/core/Gain.h>
#include <phe/core/Morph.h>
#include <phe/core/Scales.h>
#include <phe/core/VariSpeed.h>

#include <algorithm>
#include <cmath>

namespace phe
{

namespace
{
    constexpr int    kMinFirstRecording = 64;       // a first recording (or a new splice) shorter than this many samples is thrown away
    constexpr double kGateRampSeconds   = 0.004;    // punching in and out of a recording over existing audio
    constexpr double kSmoothSeconds     = 0.010;    // level knobs glide instead of jumping

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
        recActive = recStopping = recFirst = recNew = false;
        peakBucket = -1;

        // The splice markers move with the audio when it is converted to the new sample rate.
        if (oldRate > 0.0 && oldLength > 0 && reel.length() > 0)
            map.rescale (sampleRate / oldRate, reel.length());
        else
            map.clear();
        selector.sync (params.organize, map.numSplices());
        selectorSynced = true;

        if (params.play && reel.length() > 0)
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
    }
}

void Engine::reset()
{
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
    if (params.play && reel.length() > 0)
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

    if (length <= 0)
    {
        // First recording: the reel is empty, so it grows as we record (and it becomes splice 1).
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

    const int length = recPos;
    recActive = false;
    recStopping = false;
    recFirst = false;

    if (length < kMinFirstRecording)
    {
        reel.clear();                               // too short to be a recording
        player.stop();
    }
    else
    {
        reel.setLength (length);
        map.clear();                                // a new reel is one splice
        selector.sync (params.organize, 1);
        if (params.play)
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

    const int base = newSpliceBase;
    const int end = recPos;
    recActive = false;
    recStopping = false;
    recNew = false;

    if (end - base < kMinFirstRecording)
    {
        reel.refreshPeaksFrom (base);               // too short to be a splice: the reel stays as it was
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
    if (params.play && reel.length() > 0)
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
    peakBucket = -1;
    reel.clear();
    map.clear();
    selector.sync (params.organize, 1);
    player.stop();
    recPos = 0;
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

    const int position = (int) player.headPosition (reel);
    const int j = map.addMarker (position, length);
    if (j >= 0)
        selector.markerInserted (j);
}

// DELETE SPLICE MARKER: takes away the marker at the end of the selected splice, so it joins the next one.
void Engine::deleteMarker()
{
    const int k = selector.selected();
    if (map.removeMarker (k))
        selector.markerRemoved (k);
}

// DELETE ALL MARKERS: the whole reel becomes one splice.
void Engine::deleteAllMarkers()
{
    map.clear();
    selector.sync (params.organize, 1);
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
        if (! selectorSynced)
        {
            selector.sync (params.organize, map.numSplices());
            selectorSynced = true;
        }
        setPlayerTargets();
        if (params.play && reel.length() > 0 && ! (recActive && recFirst))
            player.start (reel);
        else
            player.stop();
        firstBlock = false;
    }

    // ---- buttons: act when they go on ----
    if (params.clearReel && ! prevClear)
        clearReel();
    prevClear = params.clearReel;

    // Splice markers
    if (params.splice && ! prevSplice)
    {
        dropMarker();
        handledButtons.fetch_or (kButtonSplice);
    }
    prevSplice = params.splice;

    if (params.deleteMarker && ! prevDeleteMarker)
    {
        deleteMarker();
        handledButtons.fetch_or (kButtonDeleteMarker);
    }
    prevDeleteMarker = params.deleteMarker;

    if (params.deleteAllMarkers && ! prevDeleteAll)
    {
        deleteAllMarkers();
        handledButtons.fetch_or (kButtonDeleteAllMarkers);
    }
    prevDeleteAll = params.deleteAllMarkers;

    // Which splice is selected: Shift steps on, Organize (when it moves into another zone) wins over Shift.
    const bool shiftPressed = params.shift && ! prevShift;
    if (shiftPressed)
        handledButtons.fetch_or (kButtonShift);
    prevShift = params.shift;
    selector.update (params.organize, map.numSplices(), shiftPressed);

    // ---- the tape head and genes: what Vari-Speed, Pitch Snap, Gene-Size, Slide, Play and the selected splice ask for ----
    setPlayerTargets();

    // Rec (a switch): on = recording over the splice the playhead is in; Rec New Splice (a switch): on = recording a new splice.
    if (params.rec && ! prevRec)
    {
        if (recActive && recByNew && ! recStopping)
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
        if (! recActive)
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
        if (reel.length() > 0 && ! (recActive && recFirst))
            player.start (reel);                    // turning Play on starts a fresh gene, at the Slide position
    }
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
        const float recordL = liveL + loop.recordL * loopGain;
        const float recordR = liveR + loop.recordR * loopGain;
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

            float oldL = 0.0f, oldR = 0.0f;
            if (! writesFresh)
            {
                oldL = reel.get (0, recPos);
                oldR = reel.get (1, recPos);
            }
            const float newL = recGate >= 1.0f ? recordL : oldL + (recordL - oldL) * recGate;
            const float newR = recGate >= 1.0f ? recordR : oldR + (recordR - oldR) * recGate;
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
}

void Engine::setPlayerTargets()
{
    const int length = reel.length();
    const int selected = std::min (map.numSplices() - 1, std::max (0, selector.selected()));
    const int spliceLength = length > 0 ? map.length (selected, length) : 0;

    GenePlayer::Targets t;
    t.rate = dsp::variSpeedRate (params.variSpeed, std::min (2, std::max (0, params.pitchSnap)));
    t.geneSeconds = geneSeconds (params.geneSize, (double) (spliceLength > 0 ? spliceLength : length) / sampleRate);
    t.slide = clamp01 (params.slide);
    t.playWanted = params.play;
    t.morph = dsp::morphFromKnob (params.morph);
    t.scaleMask = scales::maskFor (params.pitchScale);
    t.spliceStart = length > 0 ? map.start (selected) : 0;
    t.spliceLength = spliceLength;
    player.setTargets (t);
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
    return v;
}

} // namespace phe
