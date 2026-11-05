#include <phe/GenePlayer.h>
#include <phe/core/Gain.h>

#include <algorithm>
#include <cmath>

namespace phe
{

namespace
{
    constexpr double kEdgeFadeSeconds  = 0.0015;   // fade at each end of a gene (same as the loop-edge fade of milestone 4)
    constexpr double kRateGlideSeconds = 0.004;    // the tape has a little inertia: speed changes glide over a few ms
    constexpr double kSlideGlideSeconds = 0.003;   // Slide glides a little too, so automation does not make zipper noise
    constexpr double kGateGlideSeconds = 0.005;    // fading out when the tape stops, and back in when it moves
    constexpr double kMovingFrom = 0.002;          // tape + scrub speed (reel samples per sample) below which we are silent
    constexpr double kMovingTo   = 0.02;           // ... and above which we are fully audible
    constexpr double kGeneClockMinRate = 0.002;    // slower than this the gene clock stands still (the tape is stopped)

    inline double smoothRange (double x, double lo, double hi)
    {
        if (x <= lo) return 0.0;
        if (x >= hi) return 1.0;
        const double t = (x - lo) / (hi - lo);
        return t * t * (3.0 - 2.0 * t);
    }

    inline double glideCoefficient (double seconds, double sampleRate)
    {
        return 1.0 - std::exp (-1.0 / (seconds * sampleRate));
    }
}

//==============================================================================
void GenePlayer::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    rateGlide  = (float) glideCoefficient (kRateGlideSeconds, sampleRate);
    slideGlide = (float) glideCoefficient (kSlideGlideSeconds, sampleRate);
    gateGlide  = (float) glideCoefficient (kGateGlideSeconds, sampleRate);
    fadeSamples = std::max (1, (int) std::llround (kEdgeFadeSeconds * sampleRate));
}

void GenePlayer::stop()
{
    playing = false;
    for (auto& v : voices)
        v = Voice();
    clock = 0.0;
    newest = 0;
}

void GenePlayer::start (const Reel& reel)
{
    for (auto& v : voices)
        v = Voice();
    clock = 0.0;
    spawnCount = 0;
    rng.seed (0x2545F491u);
    rateNow = (double) targets.rate;
    slideNow = (double) targets.slide;
    gate = smoothRange (std::fabs (rateNow), kMovingFrom, kMovingTo);
    playing = true;
    spawnVoice (0.0, targets.geneSeconds <= 0.0, reel);  // the first gene starts right away, from the Slide position
}

void GenePlayer::switchSplice (const Reel& reel)
{
    if (! playing)
    {
        start (reel);
        return;
    }
    for (auto& v : voices)
        if (v.active)
            v.releasing = true;
    clock = 0.0;
    spawnVoice (0.0, targets.geneSeconds <= 0.0, reel);
}

void GenePlayer::release()
{
    for (auto& v : voices)
        if (v.active)
            v.releasing = true;
}

int GenePlayer::activeVoices() const
{
    int n = 0;
    for (const auto& v : voices)
        if (v.active)
            ++n;
    return n;
}

// Starts one more gene. 'overshoot' is how far past the exact moment we already are (so the gene clock stays steady):
// time for short genes, signed reel distance for whole-reel genes.
// The splice the next gene will be in, kept inside the recorded part of the reel (no markers: the whole reel).
void GenePlayer::targetSplice (const Reel& reel, double& start, double& len) const
{
    const int L = reel.length();
    start = 0.0;
    len = (double) std::max (1, L);
    if (targets.spliceLength > 0 && L > 0)
    {
        const int s = std::min (L - 1, std::max (0, targets.spliceStart));
        const int n = std::min (targets.spliceLength, L - s);
        if (n >= 1)
        {
            start = (double) s;
            len = (double) n;
        }
    }
}

void GenePlayer::spawnVoice (double overshoot, bool whole, const Reel& reel)
{
    int slot = -1;
    for (int i = 0; i < kNumVoices; ++i)
        if (! voices[i].active)
        {
            slot = i;
            break;
        }

    if (slot < 0)
    {
        // Every voice is busy (only possible for a moment while the controls move): the oldest one makes way.
        double oldest = -1.0;
        for (int i = 0; i < kNumVoices; ++i)
        {
            const double progress = whole ? std::fabs (voices[i].qBase) : voices[i].age;
            if (progress > oldest)
            {
                oldest = progress;
                slot = i;
            }
        }
    }

    Voice& v = voices[slot];
    v = Voice();
    v.active = true;
    targetSplice (reel, v.start, v.len);
    if (whole)
        v.q = v.qBase = overshoot;
    else
        v.age = overshoot;

    // Random pitch-up (Morph past 2:30), kept to the Pitch Scale if one is chosen. Always built on the tape's own speed.
    // With a Morph Chord chosen the pitch-ups are fixed steps taken in turn (gene 1 at the tape's own pitch, then the chord's
    // three steps) whenever Morph is past 2:30; the random number is still drawn, so everything else stays the same.
    float up = dsp::randomPitchUp (rng, targets.morph.pitchRange, targets.scaleMask);
    if (targets.chordMode > 0 && targets.chordMode < dsp::kNumMorphChords && targets.morph.pitchRange > 0.0f)
        up = dsp::kMorphChords[targets.chordMode].semitones[spawnCount % 4];
    v.mult = up > 0.0f ? std::exp2 (up / 12.0f) : 1.0f;

    // Place in the stereo picture: a fixed rotation (left, right, centre ...) that turns into random places further
    // round the Morph knob.
    static constexpr float kPatterns[4][4] = { { 0.0f, 0.0f, 0.0f, 0.0f },
                                               { -1.0f, 1.0f, -1.0f, 1.0f },
                                               { -1.0f, 1.0f, 0.0f, -1.0f },
                                               { -1.0f, 1.0f, -0.33f, 0.33f } };
    const int slots = std::min (4, std::max (1, (int) std::ceil (targets.morph.genesAtOnce - 0.001f)));
    const float fixedPlace = kPatterns[slots - 1][spawnCount % 4];
    const float randomPlace = rng.nextSigned();
    const float place = (fixedPlace * (1.0f - targets.morph.panRandom) + randomPlace * targets.morph.panRandom)
                        * targets.morph.panAmount;
    v.gainL = std::min (1.0f, 1.0f - place) * targets.morph.voiceGain;
    v.gainR = std::min (1.0f, 1.0f + place) * targets.morph.voiceGain;

    ++spawnCount;
    newest = slot;
}

double GenePlayer::wrap (double position, double length) const
{
    position -= length * std::floor (position / length);
    return position >= length ? 0.0 : position;
}

double GenePlayer::headPosition (const Reel& reel) const
{
    const int length = reel.length();
    if (length <= 0)
        return 0.0;
    const Voice& v = voices[newest];
    if (v.active)
        return v.start + wrap (slideNow * v.len + v.q, v.len);

    double start, len;
    targetSplice (reel, start, len);
    return start + wrap (slideNow * len, len);
}

GenePlayer::View GenePlayer::view (const Reel& reel) const
{
    View v;
    const int length = reel.length();
    if (length <= 0)
        return v;

    // The window is drawn in the splice of the newest gene (or in the selected splice when nothing is playing).
    double spliceStart, L;
    if (voices[newest].active)
    {
        spliceStart = voices[newest].start;
        L = voices[newest].len;
    }
    else
        targetSplice (reel, spliceStart, L);

    const bool whole = targets.geneSeconds <= 0.0;
    const double start = std::min (L, std::max (0.0, slideNow * L));
    const double speed = std::max (0.05, std::fabs (rateNow));       // a stopped tape still shows its gene at normal size
    const double span = whole ? L : std::min (L, targets.geneSeconds * sampleRate * speed);

    // Going forwards a gene is the stretch of splice after the Slide position; going backwards, the stretch before it.
    // The window is given from the start of the splice, wrapping round inside the splice.
    v.windowSpan = span;
    v.windowStart = spliceStart + ((! whole && rateNow < 0.0) ? wrap (start - span, L) : wrap (start, L));
    v.spliceStart = spliceStart;
    v.spliceLength = L;
    v.geneSeconds = targets.geneSeconds;
    v.rate = (float) rateNow;
    v.voices = activeVoices();
    return v;
}

//==============================================================================
GenePlayer::Out GenePlayer::step (const Reel& reel)
{
    Out out;
    const int length = reel.length();
    if (! playing)
        return out;
    if (length <= 0)
    {
        stop();
        return out;
    }

    // The splice the next gene will be in. (Genes already playing keep the splice they started in.)
    double targetStart, L;
    targetSplice (reel, targetStart, L);

    // The tape's speed and the Slide position glide towards what the controls ask for.
    rateNow += ((double) targets.rate - rateNow) * (double) rateGlide;
    if (targets.rate == 0.0f && std::fabs (rateNow) < 1.0e-9)
        rateNow = 0.0;                                 // settled at a standstill (and never creeping into tiny numbers)
    const double slideBefore = slideNow;
    slideNow += ((double) targets.slide - slideNow) * (double) slideGlide;

    // Silent when nothing moves (stopped tape, still Slide): otherwise a stopped head would hold one sample for ever.
    // Moving Slide counts as movement, so it scrubs even when the tape is stopped.
    const double motion = std::fabs (rateNow) + std::fabs (slideNow - slideBefore) * L;
    const double gateTarget = smoothRange (motion, kMovingFrom, kMovingTo);
    gate += (gateTarget - gate) * (double) gateGlide;
    if (gateTarget == 0.0 && gate < 1.0e-9)
        gate = 0.0;

    // The gene clock. A whole-reel gene measures itself by how far the tape has travelled; a shorter gene by time.
    // A new gene is started every 'spacing' (Morph): later than the last one ends (gap), exactly when it ends (1/1),
    // or earlier (overlap).
    const bool whole = targets.geneSeconds <= 0.0;
    const double geneLength = whole ? L : std::max (2.0, targets.geneSeconds * sampleRate);
    // A whole-splice gene is as long as the splice it started in, so the next one is due in terms of that splice, not of
    // whichever splice is selected now (otherwise cutting a splice short would start the next gene early).
    const double spacingBase = (whole && voices[newest].active) ? voices[newest].len : geneLength;
    const double spacing = std::max (1.0, spacingBase * (double) std::max (0.2f, targets.morph.spacing));

    // Read the reel under every gene that is playing, and mix them.
    const float g = (float) gate;
    for (const auto& v : voices)
    {
        if (! v.active)
            continue;

        const double position = v.start + wrap (slideNow * v.len + v.q, v.len);
        const float sampleL = reel.readCubic (0, position);
        const float sampleR = reel.readCubic (1, position);

        const double progress = whole ? std::fabs (v.qBase) : v.age;
        const int fadeLength = (int) std::min (whole ? v.len : geneLength, 2.0e9);
        // Gene Window option: a short gene fades in and out over its whole length (the fade is as long as the gene allows)
        const float fade = dsp::edgeFade (progress, fadeLength, (targets.smoothWindow && ! whole) ? fadeLength : fadeSamples);

        const float vg = g * v.releaseGain;
        const float heardL = sampleL * fade * vg * v.gainL;
        const float heardR = sampleR * fade * vg * v.gainR;
        out.heardL += heardL;
        out.heardR += heardR;
        // What is recorded back onto the reel: a short gene's fades are part of its sound, so they are kept; the fade at
        // the end of a whole-reel lap only hides the loop point when listening, so it is not baked into the recording.
        out.recordL += whole ? sampleL * vg * v.gainL : heardL;
        out.recordR += whole ? sampleR * vg * v.gainR : heardR;
    }

    // Move every gene on, and retire the ones that are finished.
    const bool moving = std::fabs (rateNow) >= kGeneClockMinRate;
    for (auto& v : voices)
    {
        if (! v.active)
            continue;
        if (v.releasing)
        {
            v.releaseGain -= 1.0f / (float) fadeSamples;
            if (v.releaseGain <= 0.0f)
            {
                v.active = false;
                continue;
            }
        }
        v.q += rateNow * (double) v.mult;
        v.qBase += rateNow;
        if (moving)
            v.age += 1.0;
        if ((whole ? std::fabs (v.qBase) : v.age) >= (whole ? v.len : geneLength))
            v.active = false;
    }

    // Start the next gene when it is due.
    if (whole)
        clock += rateNow;
    else if (moving)
        clock += 1.0;

    if (std::fabs (clock) >= spacing)
    {
        double overshoot = whole ? clock - std::copysign (spacing, clock) : clock - spacing;
        if (std::fabs (overshoot) >= spacing)
            overshoot = std::fmod (overshoot, spacing);                 // the controls jumped: stay on the beat
        clock = overshoot;
        if (targets.playWanted)
            spawnVoice (overshoot, whole, reel);
    }

    // Play was switched off: the genes in progress are the last ones, then stop.
    if (! targets.playWanted && activeVoices() == 0)
        stop();

    return out;
}

} // namespace phe
