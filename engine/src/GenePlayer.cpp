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

void GenePlayer::start()
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
    spawnVoice (0.0, targets.geneSeconds <= 0.0);        // the first gene starts right away, from the Slide position
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
void GenePlayer::spawnVoice (double overshoot, bool whole)
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
    if (whole)
        v.q = v.qBase = overshoot;
    else
        v.age = overshoot;

    // Random pitch-up (Morph past 2:30), kept to the Pitch Scale if one is chosen. Always built on the tape's own speed.
    const float up = dsp::randomPitchUp (rng, targets.morph.pitchRange, targets.scaleMask);
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
    return wrap (slideNow * (double) length + (v.active ? v.q : 0.0), (double) length);
}

GenePlayer::View GenePlayer::view (const Reel& reel) const
{
    View v;
    const int length = reel.length();
    if (length <= 0)
        return v;

    const double L = (double) length;
    const bool whole = targets.geneSeconds <= 0.0;
    const double start = std::min (L, std::max (0.0, slideNow * L));
    const double speed = std::max (0.05, std::fabs (rateNow));       // a stopped tape still shows its gene at normal size
    const double span = whole ? L : std::min (L, targets.geneSeconds * sampleRate * speed);

    // Going forwards a gene is the stretch of reel after the Slide position; going backwards, the stretch before it.
    v.windowSpan = span;
    v.windowStart = (! whole && rateNow < 0.0) ? wrap (start - span, L) : wrap (start, L);
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

    const double L = (double) length;

    // The tape's speed and the Slide position glide towards what the controls ask for.
    rateNow += ((double) targets.rate - rateNow) * (double) rateGlide;
    if (targets.rate == 0.0f && std::fabs (rateNow) < 1.0e-9)
        rateNow = 0.0;                                 // settled at a standstill (and never creeping into tiny numbers)
    const double slideBefore = slideNow;
    slideNow += ((double) targets.slide - slideNow) * (double) slideGlide;
    const double start = slideNow * L;

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
    const double spacing = std::max (1.0, geneLength * (double) std::max (0.2f, targets.morph.spacing));
    const int fadeLength = (int) std::min (geneLength, 2.0e9);

    // Read the reel under every gene that is playing, and mix them.
    const float g = (float) gate;
    for (const auto& v : voices)
    {
        if (! v.active)
            continue;

        const double position = wrap (start + v.q, L);
        const float sampleL = reel.readCubic (0, position);
        const float sampleR = reel.readCubic (1, position);

        const double progress = whole ? std::fabs (v.qBase) : v.age;
        const float fade = dsp::edgeFade (progress, fadeLength, fadeSamples);

        const float heardL = sampleL * fade * g * v.gainL;
        const float heardR = sampleR * fade * g * v.gainR;
        out.heardL += heardL;
        out.heardR += heardR;
        // What is recorded back onto the reel: a short gene's fades are part of its sound, so they are kept; the fade at
        // the end of a whole-reel lap only hides the loop point when listening, so it is not baked into the recording.
        out.recordL += whole ? sampleL * g * v.gainL : heardL;
        out.recordR += whole ? sampleR * g * v.gainR : heardR;
    }

    // Move every gene on, and retire the ones that are finished.
    const bool moving = std::fabs (rateNow) >= kGeneClockMinRate;
    for (auto& v : voices)
    {
        if (! v.active)
            continue;
        v.q += rateNow * (double) v.mult;
        v.qBase += rateNow;
        if (moving)
            v.age += 1.0;
        if ((whole ? std::fabs (v.qBase) : v.age) >= geneLength)
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
            spawnVoice (overshoot, whole);
    }

    // Play was switched off: the genes in progress are the last ones, then stop.
    if (! targets.playWanted && activeVoices() == 0)
        stop();

    return out;
}

} // namespace phe
