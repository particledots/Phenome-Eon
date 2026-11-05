#include <phe/core/ReelFile.h>
#include <phe/core/Resampler.h>
#include <phe/core/SpliceMap.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace phe::core
{

namespace
{
    uint32_t rd32 (const uint8_t* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
    uint16_t rd16 (const uint8_t* p) { return (uint16_t) (p[0] | (p[1] << 8)); }
    void wr32 (std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back ((uint8_t) (x >> (8 * i))); }
    void wr16 (std::vector<uint8_t>& v, uint16_t x) { v.push_back ((uint8_t) x); v.push_back ((uint8_t) (x >> 8)); }
    void tag (std::vector<uint8_t>& v, const char* t) { for (int i = 0; i < 4; ++i) v.push_back ((uint8_t) t[i]); }
    void wrFloat (std::vector<uint8_t>& v, float x)
    {
        uint32_t bits;
        std::memcpy (&bits, &x, 4);
        wr32 (v, bits);
    }
}

//==============================================================================
int tidyMarkers (std::vector<int>& markers, int length)
{
    const size_t before = markers.size();
    std::sort (markers.begin(), markers.end());

    std::vector<int> kept;
    kept.reserve (std::min<size_t> (markers.size(), (size_t) kMaxSpliceMarkers));
    int previous = 0;
    for (int m : markers)
    {
        if ((int) kept.size() >= kMaxSpliceMarkers)
            break;
        if (m - previous >= kMinSpliceSamples && length - m >= kMinSpliceSamples)
        {
            kept.push_back (m);
            previous = m;
        }
    }
    markers = std::move (kept);
    return (int) (before - markers.size());
}

//==============================================================================
std::vector<uint8_t> writeReelWav (const ReelAudio& a)
{
    const size_t n = std::min (a.ch[0].size(), a.ch[1].empty() ? a.ch[0].size() : a.ch[1].size());
    const bool haveRight = a.ch[1].size() >= n && ! a.ch[1].empty();

    std::vector<int> marks;
    for (int m : a.markers)
        if (m > 0 && (size_t) m < n)
            marks.push_back (m);
    std::sort (marks.begin(), marks.end());
    marks.erase (std::unique (marks.begin(), marks.end()), marks.end());

    const uint32_t rate = (uint32_t) std::llround (a.sampleRate);
    const size_t dataBytes = n * 8;
    const size_t cueBytes = marks.empty() ? 0 : 8 + 4 + 24 * marks.size();
    const size_t riffBytes = 4 + (8 + 18) + (8 + 4) + (8 + dataBytes) + cueBytes;

    std::vector<uint8_t> v;
    v.reserve (8 + riffBytes);
    tag (v, "RIFF"); wr32 (v, (uint32_t) riffBytes); tag (v, "WAVE");

    tag (v, "fmt "); wr32 (v, 18);
    wr16 (v, 3);                        // 32-bit float
    wr16 (v, 2);                        // stereo
    wr32 (v, rate);
    wr32 (v, rate * 8);                 // bytes per second
    wr16 (v, 8);                        // bytes per frame
    wr16 (v, 32);
    wr16 (v, 0);                        // no extra format bytes

    tag (v, "fact"); wr32 (v, 4); wr32 (v, (uint32_t) n);

    tag (v, "data"); wr32 (v, (uint32_t) dataBytes);
    for (size_t i = 0; i < n; ++i)
    {
        const float l = a.ch[0][i];
        const float r = haveRight ? a.ch[1][i] : l;
        wrFloat (v, std::isfinite (l) ? l : 0.0f);
        wrFloat (v, std::isfinite (r) ? r : 0.0f);
    }

    if (! marks.empty())
    {
        tag (v, "cue "); wr32 (v, (uint32_t) (4 + 24 * marks.size()));
        wr32 (v, (uint32_t) marks.size());
        for (size_t k = 0; k < marks.size(); ++k)
        {
            wr32 (v, (uint32_t) (k + 1));          // the marker's number
            wr32 (v, (uint32_t) marks[k]);         // position
            tag (v, "data");
            wr32 (v, 0);                           // chunk start
            wr32 (v, 0);                           // block start
            wr32 (v, (uint32_t) marks[k]);         // sample offset
        }
    }
    return v;
}

//==============================================================================
bool readReelWav (const uint8_t* d, size_t size, ReelAudio& out, std::string& err, ReelFileInfo* info)
{
    out = ReelAudio();
    if (info != nullptr)
        *info = ReelFileInfo();
    if (size < 12 || std::memcmp (d, "RIFF", 4) != 0 || std::memcmp (d + 8, "WAVE", 4) != 0)
    {
        err = "That is not a WAV file.";
        return false;
    }

    int format = 0, nch = 0, bits = 0;
    uint32_t rate = 0;
    bool haveFmt = false;
    const uint8_t* pcm = nullptr;
    size_t pcmBytes = 0;
    std::vector<uint32_t> cueFrames;
    int cueCount = 0;

    size_t pos = 12;
    while (pos + 8 <= size)
    {
        const uint8_t* ck = d + pos;
        const uint32_t len = rd32 (ck + 4);
        const size_t body = pos + 8;
        const size_t avail = size - body;
        const size_t usable = std::min<size_t> (len, avail);

        if (std::memcmp (ck, "fmt ", 4) == 0 && usable >= 16)
        {
            format = rd16 (d + body);
            nch = rd16 (d + body + 2);
            rate = rd32 (d + body + 4);
            bits = rd16 (d + body + 14);
            if (format == 0xFFFE && usable >= 26)
                format = rd16 (d + body + 24);     // "extensible": the real format tag is in the sub-format
            haveFmt = true;
        }
        else if (std::memcmp (ck, "data", 4) == 0 && pcm == nullptr)
        {
            pcm = d + body;
            pcmBytes = (len == 0xFFFFFFFFu || len > avail) ? avail : (size_t) len;   // a streamed file may claim more than it holds
        }
        else if (std::memcmp (ck, "cue ", 4) == 0 && usable >= 4)
        {
            const uint32_t claimed = rd32 (d + body);
            const size_t room = (usable - 4) / 24;
            const size_t n = std::min<size_t> (claimed, room);
            for (size_t k = 0; k < n; ++k)
            {
                const uint8_t* p = d + body + 4 + 24 * k;
                if (std::memcmp (p + 8, "data", 4) != 0)
                    continue;                      // a marker that points into some other kind of chunk
                const uint32_t offset = rd32 (p + 20);
                const uint32_t position = rd32 (p + 4);
                cueFrames.push_back (offset != 0 ? offset : position);
                ++cueCount;
            }
        }

        if (len > avail)
            break;
        pos = body + (size_t) len + (len & 1);
    }

    if (! haveFmt || pcm == nullptr)
    {
        err = "That WAV file has no audio data I can read.";
        return false;
    }
    if (nch < 1 || rate < 1000 || rate > 400000)
    {
        err = "That WAV file has an unusual format I can't read.";
        return false;
    }
    const bool isFloat = (format == 3);
    if (! ((format == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) || (isFloat && (bits == 32 || bits == 64))))
    {
        err = "Only PCM or float WAV files are supported (8, 16, 24 or 32-bit).";
        return false;
    }

    const size_t bytesPer = (size_t) bits / 8, frameBytes = bytesPer * (size_t) nch;
    const size_t frames = pcmBytes / frameBytes;
    if (frames == 0)
    {
        err = "That WAV file is empty.";
        return false;
    }

    out.sampleRate = (double) rate;
    out.ch[0].resize (frames);
    out.ch[1].resize (frames);
    const int stored = nch >= 2 ? 2 : 1;
    for (size_t i = 0; i < frames; ++i)
    {
        for (int c = 0; c < stored; ++c)
        {
            const uint8_t* s = pcm + i * frameBytes + (size_t) c * bytesPer;
            float x = 0.0f;
            if (isFloat)
            {
                if (bits == 32) { float f; std::memcpy (&f, s, 4); x = f; }
                else            { double f; std::memcpy (&f, s, 8); x = (float) f; }
            }
            else if (bits == 8)  x = ((int) s[0] - 128) / 128.0f;
            else if (bits == 16) x = (int16_t) rd16 (s) / 32768.0f;
            else if (bits == 24) { const int32_t v = (int32_t) ((uint32_t) s[0] << 8 | (uint32_t) s[1] << 16 | (uint32_t) s[2] << 24); x = (float) (v / 2147483648.0); }
            else                 x = (float) ((int32_t) rd32 (s) / 2147483648.0);
            if (! std::isfinite (x))
                x = 0.0f;
            out.ch[c][i] = x;
        }
        if (stored == 1)
            out.ch[1][i] = out.ch[0][i];           // mono: the same on both sides
    }

    std::sort (cueFrames.begin(), cueFrames.end());
    for (size_t k = 0; k < cueFrames.size(); ++k)
    {
        const uint32_t f = cueFrames[k];
        if (f > 0 && (size_t) f < frames && (out.markers.empty() || out.markers.back() != (int) f))
            out.markers.push_back ((int) f);
    }

    if (info != nullptr)
    {
        info->sourceChannels = nch;
        info->bits = bits;
        info->isFloat = isFloat;
        info->cuePoints = cueCount;
    }
    return true;
}

//==============================================================================
// Offline version of the streaming resampler, with its own delay (17 input samples) taken out.
std::vector<float> resampleOffline (const std::vector<float>& in, double inRate, double outRate)
{
    if (std::abs (inRate - outRate) < 1e-6 || in.empty())
        return in;
    SincResampler rs;
    rs.init (inRate, outRate);
    std::vector<float> padded (in);
    padded.insert (padded.end(), 64, 0.0f);
    std::vector<float> raw;
    raw.reserve ((size_t) ((double) padded.size() * outRate / inRate) + 8);
    rs.process (padded.data(), padded.size(), raw);
    const size_t drop = (size_t) std::lround (17.0 * outRate / inRate);
    const size_t want = (size_t) std::lround ((double) in.size() * outRate / inRate);
    std::vector<float> out;
    if (raw.size() > drop)
        out.assign (raw.begin() + (long) drop, raw.begin() + (long) std::min (raw.size(), drop + want));
    out.resize (want, 0.0f);
    return out;
}

bool fitToReel (ReelAudio& a, double reelRate, int capacityFrames, FitResult& r, std::string& err)
{
    r = FitResult();
    if (a.frames() == 0 || a.ch[1].size() != a.ch[0].size() || a.sampleRate <= 0.0)
    {
        err = "That file has no usable audio.";
        return false;
    }
    if (reelRate <= 0.0 || capacityFrames <= 0)
    {
        err = "The reel is not ready yet.";
        return false;
    }

    if (std::abs (a.sampleRate - reelRate) > 0.01)
    {
        const double ratio = reelRate / a.sampleRate;
        for (int c = 0; c < 2; ++c)
            a.ch[c] = resampleOffline (a.ch[c], a.sampleRate, reelRate);
        for (int& m : a.markers)
            m = (int) std::llround ((double) m * ratio);
        r.converted = true;
        r.fromRate = a.sampleRate;
        a.sampleRate = reelRate;
    }

    if (a.frames() > (size_t) capacityFrames)
    {
        a.ch[0].resize ((size_t) capacityFrames);
        a.ch[1].resize ((size_t) capacityFrames);
        r.cut = true;
    }

    if (a.frames() < (size_t) kMinSpliceSamples)
    {
        err = "That file is too short to be a reel.";
        return false;
    }

    r.markersDropped = tidyMarkers (a.markers, (int) a.frames());
    return true;
}

} // namespace phe::core
