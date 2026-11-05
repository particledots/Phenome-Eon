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
    constexpr int    kMinFirstRecording = 64;       // a first recording shorter than this many samples is thrown away
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
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    player.prepare (sampleRate);

    // Only if the sample rate changed (or this is the first time) does the reel's memory change;
    // otherwise whatever is recorded stays exactly as it is.
    if (reel.allocate (sampleRate))
    {
        recActive = recStopping = recFirst = false;
        peakBucket = -1;
        if (params.play && reel.length() > 0)
            player.start();
        else
            player.stop();
        viewPlayPos.store (0);
        viewRecPos.store (0);
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
        recActive = recStopping = recFirst = false;
        flushPeak();
        peakBucket = -1;
        recordStopped.store (true);
    }
    if (params.play && reel.length() > 0)
        player.start();
    else
        player.stop();
    firstBlock = true;      // the controls are picked up afresh at the next block
}

//==============================================================================
void Engine::startRecording()
{
    if (reel.capacity() <= 0)
    {
        recordStopped.store (true);                 // no memory: tell the wrapper to switch the button off
        return;
    }

    const int length = reel.length();
    recActive = true;
    recStopping = false;
    peakBucket = -1;

    if (length <= 0)
    {
        // First recording: the reel is empty, so it grows as we record.
        recFirst = true;
        recPos = 0;
        recGate = 1.0f;
        player.stop();
    }
    else
    {
        // Recording over what is there, starting where the playhead is.
        recFirst = false;
        recPos = std::min (length - 1, std::max (0, (int) player.headPosition (reel)));
        recGate = 0.0f;
    }
}

void Engine::stopRecording()
{
    if (! recActive)
        return;

    if (recFirst)
        finishFirstRecording();
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
        if (params.play)
            player.start();                         // the new recording starts looping straight away (if Play is on)
        else
            player.stop();
    }
}

void Engine::clearReel()
{
    if (recActive)
    {
        recActive = recStopping = recFirst = false;
        recordStopped.store (true);
    }
    peakBucket = -1;
    reel.clear();
    player.stop();
    recPos = 0;
    clearHandled.store (true);
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
        setPlayerTargets();
        if (params.play && reel.length() > 0 && ! (recActive && recFirst))
            player.start();
        else
            player.stop();
        firstBlock = false;
    }

    // ---- the tape head and genes: what Vari-Speed, Pitch Snap, Gene-Size, Slide and Play ask for ----
    setPlayerTargets();

    // ---- buttons: act when they change ----
    if (params.clearReel && ! prevClear)
        clearReel();
    prevClear = params.clearReel;

    if (params.rec && ! prevRec)
    {
        if (recActive && recStopping)
            recStopping = false;                    // pressed again while still fading out: just carry on
        else if (! recActive)
            startRecording();
    }
    else if (! params.rec && prevRec)
    {
        stopRecording();
    }
    prevRec = params.rec;

    if (params.play && ! prevPlay)
    {
        if (reel.length() > 0 && ! (recActive && recFirst))
            player.start();                         // turning Play on starts a fresh gene, at the Slide position
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
        // except that the fade at the end of a whole-reel lap is never baked into the reel (see GenePlayer.h).
        const float liveL = inL * liveGain, liveR = inR * liveGain;
        const float recordL = liveL + loop.recordL * loopGain;
        const float recordR = liveR + loop.recordR * loopGain;
        channels[0][i] = (liveL + loop.heardL * loopGain) * gainOut;
        if (chs > 1)
            channels[1][i] = (liveR + loop.heardR * loopGain) * gainOut;

        // ---- recording ----
        if (recActive)
        {
            if (recStopping)
                recGate = std::max (0.0f, recGate - gateStep);
            else if (! recFirst)
                recGate = std::min (1.0f, recGate + gateStep);

            float oldL = 0.0f, oldR = 0.0f;
            if (! recFirst)
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
                    recordStopped.store (true);
                }
            }
            else
            {
                if (recPos >= length)
                    recPos = 0;                     // carry on round the loop
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
    const auto gene = player.view (reel);
    viewPlayPos.store ((int) player.headPosition (reel));
    viewGeneStart.store ((int) gene.windowStart);
    viewGeneSpan.store ((int) gene.windowSpan);
    viewGeneSeconds.store ((float) gene.geneSeconds);
    viewRate.store (gene.rate);
    viewVoices.store (gene.voices);
    viewRecPos.store (recPos);
    viewRecording.store (recActive);
    viewPlaying.store (player.isPlaying() && reel.length() > 0);
}

void Engine::setPlayerTargets()
{
    GenePlayer::Targets t;
    t.rate = dsp::variSpeedRate (params.variSpeed, std::min (2, std::max (0, params.pitchSnap)));
    t.geneSeconds = geneSeconds (params.geneSize, (double) reel.length() / sampleRate);
    t.slide = clamp01 (params.slide);
    t.playWanted = params.play;
    t.morph = dsp::morphFromKnob (params.morph);
    t.scaleMask = scales::maskFor (params.pitchScale);
    player.setTargets (t);
}

//==============================================================================
Engine::ReelView Engine::getReelView() const
{
    ReelView v;
    v.lengthSamples   = reel.length();
    v.capacitySamples = reel.capacity();
    v.sampleRate      = reel.sampleRate();
    v.playPos         = viewPlayPos.load();
    v.geneStart       = viewGeneStart.load();
    v.geneSpan        = viewGeneSpan.load();
    v.geneSeconds     = viewGeneSeconds.load();
    v.rate            = viewRate.load();
    v.voices          = viewVoices.load();
    v.recPos          = viewRecPos.load();
    v.recording       = viewRecording.load();
    v.playing         = viewPlaying.load();
    return v;
}

} // namespace phe
