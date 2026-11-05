#include <phe/Engine.h>
#include <phe/core/Gain.h>

#include <algorithm>
#include <cmath>

namespace phe
{

namespace
{
    constexpr int    kMinFirstRecording = 64;       // a first recording shorter than this many samples is thrown away
    constexpr double kEdgeFadeSeconds   = 0.0015;   // fade at each end of the loop, so the loop point does not click
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

    // Only if the sample rate changed (or this is the first time) does the reel's memory change;
    // otherwise whatever is recorded stays exactly as it is.
    if (reel.allocate (sampleRate))
    {
        recActive = recStopping = recFirst = false;
        peakBucket = -1;
        playPos = 0.0;
        playing = params.play && reel.length() > 0;
        viewPlayPos.store (0);
        viewRecPos.store (0);
        viewRecording.store (false);
        viewPlaying.store (playing);
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
    playPos = 0.0;
    playing = params.play && reel.length() > 0;
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
        playing = false;
        playPos = 0.0;
    }
    else
    {
        // Recording over what is there, starting where the playhead is.
        recFirst = false;
        recPos = std::min (length - 1, std::max (0, (int) playPos));
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
        playing = false;
        playPos = 0.0;
    }
    else
    {
        reel.setLength (length);
        playPos = 0.0;                              // the new recording starts looping straight away (if Play is on)
        playing = params.play;
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
    playing = false;
    playPos = 0.0;
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
        playing = params.play && reel.length() > 0 && ! (recActive && recFirst);
        firstBlock = false;
    }

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
        {
            playing = true;                         // turning Play on starts the reel from its beginning
            playPos = 0.0;
        }
    }
    prevPlay = params.play;

    // ---- constants for this block ----
    const float smoothK  = 1.0f - std::exp (-1.0f / (float) (kSmoothSeconds * sampleRate));
    const float gateStep = 1.0f / (float) (kGateRampSeconds * sampleRate);
    const int fadeLength = std::max (1, (int) std::llround (kEdgeFadeSeconds * sampleRate));
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

        // What the reel is playing right now (nothing while the first recording is being made)
        const int length = reel.length();
        float loopL = 0.0f, loopR = 0.0f, fade = 0.0f;
        if (playing && length > 0 && ! (recActive && recFirst))
        {
            loopL = reel.read (0, playPos);
            loopR = reel.read (1, playPos);
            fade  = dsp::edgeFade (playPos, length, fadeLength);
        }

        // The mix. What is recorded is the mix without the loop-edge fade (so the fade is never baked into the
        // reel); what is heard has the fade.
        const float liveL = inL * liveGain, liveR = inR * liveGain;
        const float recordL = liveL + loopL * loopGain;
        const float recordR = liveR + loopR * loopGain;
        channels[0][i] = (liveL + loopL * fade * loopGain) * gainOut;
        if (chs > 1)
            channels[1][i] = (liveR + loopR * fade * loopGain) * gainOut;

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

        // ---- playhead ----
        if (playing && ! (recActive && recFirst))
        {
            if (length > 0)
            {
                playPos += 1.0;
                if (playPos >= (double) length)
                {
                    playPos -= (double) length;
                    if (! params.play)              // Play was switched off: this pass was the last one
                    {
                        playing = false;
                        playPos = 0.0;
                    }
                }
            }
            else
            {
                playing = false;
                playPos = 0.0;
            }
        }
    }

    flushPeak();
    viewPlayPos.store ((int) playPos);
    viewRecPos.store (recPos);
    viewRecording.store (recActive);
    viewPlaying.store (playing && reel.length() > 0);
}

//==============================================================================
Engine::ReelView Engine::getReelView() const
{
    ReelView v;
    v.lengthSamples   = reel.length();
    v.capacitySamples = reel.capacity();
    v.sampleRate      = reel.sampleRate();
    v.playPos         = viewPlayPos.load();
    v.recPos          = viewRecPos.load();
    v.recording       = viewRecording.load();
    v.playing         = viewPlaying.load();
    return v;
}

} // namespace phe
