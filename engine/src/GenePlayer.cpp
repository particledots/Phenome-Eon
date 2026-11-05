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
    q = 0.0;
    age = 0.0;
}

void GenePlayer::start()
{
    q = 0.0;
    age = 0.0;
    rateNow = (double) targets.rate;
    slideNow = (double) targets.slide;
    gate = smoothRange (std::fabs (rateNow), kMovingFrom, kMovingTo);
    playing = true;
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
    return wrap (slideNow * (double) length + q, (double) length);
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

    // Read the reel under the head.
    const double position = wrap (start + q, L);
    const float sampleL = reel.readCubic (0, position);
    const float sampleR = reel.readCubic (1, position);

    // The fades at the two ends of the gene. A whole-reel lap measures itself by how far the head has travelled; a
    // shorter gene measures itself by time.
    const bool whole = targets.geneSeconds <= 0.0;
    const double geneSamples = whole ? L : std::max (2.0, targets.geneSeconds * sampleRate);
    const double progress = whole ? std::fabs (q) : age;
    const float fade = dsp::edgeFade (progress, (int) std::min (geneSamples, 2.0e9), fadeSamples);

    const float g = (float) gate;
    out.heardL = sampleL * fade * g;
    out.heardR = sampleR * fade * g;
    // What is recorded back onto the reel: a short gene's fades are part of its sound, so they are kept; the fade at the
    // end of a whole-reel lap only hides the loop point when listening, so it is not baked into the recording.
    out.recordL = whole ? sampleL * g : out.heardL;
    out.recordR = whole ? sampleR * g : out.heardR;

    // Move the head on.
    q += rateNow;
    if (std::fabs (rateNow) >= kGeneClockMinRate)
        age += 1.0;

    const bool ended = whole ? std::fabs (q) >= L : age >= geneSamples;
    if (ended)
    {
        if (whole)
        {
            q -= std::copysign (L, q);
            age = 0.0;
        }
        else
        {
            q = 0.0;                                   // the next gene starts again at the Slide position
            age = std::fmod (age, geneSamples);        // keep the fraction, so the gene clock stays steady
        }

        if (! targets.playWanted)                      // Play was switched off: that gene was the last one
            stop();
    }

    return out;
}

} // namespace phe
