#include <phe/Reel.h>

#include <algorithm>
#include <cmath>

namespace phe
{

bool Reel::allocate (double sampleRate)
{
    const double newRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    const int newCap = (int) std::llround (kMaxSeconds * newRate);

    if (cap > 0 && newRate == rate && newCap == cap)
        return false;                                  // nothing to do: keep the recording

    // calloc gives zeroed memory that the system only really provides when it is first written to,
    // so an empty reel costs almost nothing.
    Buffer newData[2] = { Buffer ((float*) std::calloc ((size_t) newCap, sizeof (float))),
                          Buffer ((float*) std::calloc ((size_t) newCap, sizeof (float))) };

    if (newData[0] == nullptr || newData[1] == nullptr)
    {
        // Out of memory: leave the reel unusable rather than crash.
        data[0].reset();
        data[1].reset();
        cap = 0;
        rate = newRate;
        perBucket = 1;
        clear();
        return true;
    }

    int newLen = 0;
    const int oldLen = length();
    if (cap > 0 && oldLen > 0 && rate > 0.0)
    {
        // Convert what is recorded to the new sample rate (straight-line blend; rare, not used while playing).
        const double ratio = rate / newRate;           // old samples per new sample
        newLen = std::min (newCap, (int) std::llround ((double) oldLen / ratio));
        for (int c = 0; c < 2; ++c)
        {
            const float* from = data[c].get();
            float* to = newData[c].get();
            for (int i = 0; i < newLen; ++i)
            {
                const double pos = (double) i * ratio;
                const int i0 = std::min (oldLen - 1, (int) pos);
                const int i1 = std::min (oldLen - 1, i0 + 1);
                const float frac = (float) (pos - (double) i0);
                to[i] = from[i0] + (from[i1] - from[i0]) * frac;
            }
        }
    }

    data[0] = std::move (newData[0]);
    data[1] = std::move (newData[1]);
    cap = newCap;
    rate = newRate;
    perBucket = std::max (1, (newCap + core::PeakOverview::kBuckets - 1) / core::PeakOverview::kBuckets);
    setLength (newLen);
    rebuildPeaks();
    return true;
}

float Reel::read (int channel, double position) const
{
    const int length = len.load (std::memory_order_relaxed);
    if (length <= 0)
        return 0.0f;

    const float* d = data[channel & 1].get();
    const int i0 = std::min (length - 1, std::max (0, (int) position));
    const int i1 = i0 + 1 < length ? i0 + 1 : 0;
    const float frac = (float) (position - (double) i0);
    return d[i0] + (d[i1] - d[i0]) * frac;
}

float Reel::readCubic (int channel, double position) const
{
    const int length = len.load (std::memory_order_relaxed);
    if (length <= 0)
        return 0.0f;

    const float* d = data[channel & 1].get();
    const int i1 = std::min (length - 1, std::max (0, (int) position));
    const float frac = (float) (position - (double) i1);
    if (frac <= 0.0f)
        return d[i1];

    // The reel loops, so the neighbours of the first and last samples wrap round.
    const int i0 = i1 > 0 ? i1 - 1 : length - 1;
    const int i2 = i1 + 1 < length ? i1 + 1 : 0;
    const int i3 = i2 + 1 < length ? i2 + 1 : 0;
    const float y0 = d[i0], y1 = d[i1], y2 = d[i2], y3 = d[i3];

    // Catmull-Rom curve: passes through y1 and y2, smooth in between.
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * frac + c2) * frac + c1) * frac + y1;
}

void Reel::rebuildPeaks()
{
    peaks.clear();
    const int length = len.load (std::memory_order_relaxed);
    if (length <= 0 || cap <= 0)
        return;

    for (int b = 0; b * perBucket < length && b < core::PeakOverview::kBuckets; ++b)
    {
        const int from = b * perBucket;
        const int to = std::min (length, from + perBucket);
        float l = 0.0f, r = 0.0f;
        for (int i = from; i < to; ++i)
        {
            l = std::max (l, std::fabs (data[0].get()[i]));
            r = std::max (r, std::fabs (data[1].get()[i]));
        }
        peaks.set (b, l, r);
    }
}

void Reel::refreshPeaksFrom (int fromSample)
{
    if (cap <= 0)
        return;

    const int length = len.load (std::memory_order_relaxed);
    const int first = std::max (0, fromSample) / perBucket;
    for (int b = first; b < core::PeakOverview::kBuckets; ++b)
    {
        float l = 0.0f, r = 0.0f;
        if (b == first)
        {
            const int from = b * perBucket;
            const int to = std::min (length, from + perBucket);
            for (int i = from; i < to; ++i)
            {
                l = std::max (l, std::fabs (data[0].get()[i]));
                r = std::max (r, std::fabs (data[1].get()[i]));
            }
        }
        peaks.set (b, l, r);
    }
}

} // namespace phe
