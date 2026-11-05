// Self-test for milestone 9: reel files with splice markers inside, the file link library, and the engine's safe
// hand-over for saving and loading a reel. Run automatically by the build; the build fails if any check fails.
//
// Uses real files in a temporary folder (made fresh, removed at the end).

#include <phe/Engine.h>
#include <phe/ReelLibrary.h>
#include <phe/core/ReelFile.h>
#include <phe/core/SpliceMap.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using phe::core::ReelAudio;

static int failures = 0;
static int checks = 0;

#define CHECK(cond, ...)                                                   \
    do {                                                                   \
        ++checks;                                                          \
        if (! (cond)) {                                                    \
            std::printf ("FAIL line %d: %s   ", __LINE__, #cond);          \
            std::printf (__VA_ARGS__);                                     \
            std::printf ("\n");                                            \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

using Channels = std::vector<std::vector<float>>;
using Gen = std::function<float (int, long long)>;

static float tone (int channel, long long t)
{
    uint32_t seed = (uint32_t) (t * 2654435761ull) + (uint32_t) channel * 777u;
    seed = seed * 1664525u + 1013904223u;
    const float noise = ((float) (seed >> 8) / 16777216.0f - 0.5f) * 0.1f;
    return 0.4f * (float) std::sin (0.05 * (double) t * (double) (channel + 1)) + noise;
}
static float silence (int, long long) { return 0.0f; }

//==============================================================================
// A small rig: the engine, fed block by block.
struct Rig
{
    phe::Engine e;
    phe::Params p;
    long long t = 0;
    int channels = 2;
    double rate = 48000.0;

    explicit Rig (double sampleRate = 48000.0, bool prepare = true) : rate (sampleRate)
    {
        if (prepare)
        {
            e.prepare (sampleRate, 4096, 2);
            run (16, 16, silence);
            t = 0;
        }
    }

    Channels run (int n, int block, const Gen& in = silence)
    {
        Channels out ((size_t) channels, std::vector<float> ((size_t) n));
        for (int pos = 0; pos < n; pos += block)
        {
            const int m = std::min (block, n - pos);
            Channels buf ((size_t) channels, std::vector<float> ((size_t) m));
            float* ptrs[2] = { nullptr, nullptr };
            for (int c = 0; c < channels; ++c)
            {
                for (int k = 0; k < m; ++k)
                    buf[(size_t) c][(size_t) k] = in (c, t + pos + k);
                ptrs[c] = buf[(size_t) c].data();
            }
            e.setParams (p);
            e.process (ptrs, channels, m);
            for (int c = 0; c < channels; ++c)
                std::copy (buf[(size_t) c].begin(), buf[(size_t) c].end(), out[(size_t) c].begin() + pos);
        }
        t += n;
        return out;
    }

    void recordFirstTake (int n, int block, const Gen& in = tone)
    {
        p.rec = true;
        run (n, block, in);
        p.rec = false;
        run (block, block, silence);
    }

    void press (bool phe::Params::*button, int block = 64)
    {
        p.*button = true;
        run (block, block);
        p.*button = false;
        run (block, block);
    }

    // Runs blocks until 'done' says so (or gives up). Returns whether it finished.
    bool runUntil (const std::function<bool()>& done, int maxBlocks = 4000, int block = 256)
    {
        for (int k = 0; k < maxBlocks; ++k)
        {
            run (block, block);
            if (done())
                return true;
        }
        return false;
    }

    std::vector<int> markers() const
    {
        int m[phe::core::kMaxSpliceMarkers];
        const int n = e.getSpliceMap().snapshot (m, phe::core::kMaxSpliceMarkers);
        return std::vector<int> (m, m + n);
    }

    bool sameAs (const ReelAudio& a) const
    {
        if ((int) a.frames() != e.getReel().length())
            return false;
        for (size_t i = 0; i < a.frames(); ++i)
            if (a.ch[0][i] != e.getReel().get (0, (int) i) || a.ch[1][i] != e.getReel().get (1, (int) i))
                return false;
        return a.markers == markers();
    }

    // A fingerprint of the reel's audio and markers, to see that nothing changed.
    uint64_t fingerprint() const
    {
        uint64_t h = 14695981039346656037ull;
        auto mix = [&h] (uint32_t v) { h ^= v; h *= 1099511628211ull; };
        const int n = e.getReel().length();
        mix ((uint32_t) n);
        for (int i = 0; i < n; ++i)
        {
            float l = e.getReel().get (0, i), r = e.getReel().get (1, i);
            uint32_t a, b;
            std::memcpy (&a, &l, 4);
            std::memcpy (&b, &r, 4);
            mix (a);
            mix (b);
        }
        for (int m : markers())
            mix ((uint32_t) m);
        return h;
    }
};

static ReelAudio makeAudio (double rate, int frames, std::vector<int> markers = {}, int seed = 0)
{
    ReelAudio a;
    a.sampleRate = rate;
    for (int c = 0; c < 2; ++c)
    {
        a.ch[c].resize ((size_t) frames);
        for (int i = 0; i < frames; ++i)
            a.ch[c][(size_t) i] = tone (c, (long long) i + seed * 100003LL);
    }
    a.markers = std::move (markers);
    return a;
}

static bool exportNow (Rig& r, ReelAudio& out, uint32_t& stamp, int maxBlocks = 4000, int block = 256)
{
    if (! r.e.requestExport())
        return false;
    for (int k = 0; k < maxBlocks; ++k)
    {
        r.run (block, block);
        const auto res = r.e.pollExport (out, stamp);
        if (res == phe::Engine::IoResult::done)
            return true;
        if (res != phe::Engine::IoResult::none)
            return false;
    }
    return false;
}

static bool importNow (Rig& r, ReelAudio audio, std::string& why, int maxBlocks = 4000, int block = 256)
{
    if (! r.e.submitImport (std::move (audio), why))
        return false;
    for (int k = 0; k < maxBlocks; ++k)
    {
        r.run (block, block);
        uint32_t stamp = 0;
        const auto res = r.e.pollImport (stamp);
        if (res == phe::Engine::IoResult::done)
            return true;
        if (res != phe::Engine::IoResult::none)
            return false;
    }
    return false;
}

//==============================================================================
// Building WAV files by hand, in every shape the reader has to cope with.
struct WavOptions
{
    int format = 1;                       // 1 whole-number PCM, 3 float
    int channels = 2;
    int rate = 48000;
    int bits = 16;
    bool extensible = false;
    bool oddChunkBeforeData = false;      // a LIST chunk with an odd length (so it is padded) in front of the audio
    std::vector<std::pair<int, int>> cues;   // (id, frame)
    bool cueBeforeData = false;
    const char* cueFcc = "data";
    uint32_t dataLenOverride = 0;         // 0 = the real length
    bool useOverride = false;
    int cueCountClaim = -1;               // -1 = the real count
    bool dropFmt = false;
};

static void w32 (std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back ((uint8_t) (x >> (8 * i))); }
static void w16 (std::vector<uint8_t>& v, uint16_t x) { v.push_back ((uint8_t) x); v.push_back ((uint8_t) (x >> 8)); }
static void wtag (std::vector<uint8_t>& v, const char* t) { for (int i = 0; i < 4; ++i) v.push_back ((uint8_t) t[i]); }

static std::vector<uint8_t> makeWav (const WavOptions& o, int frames, const std::function<double (int, int)>& sample)
{
    std::vector<uint8_t> pcm;
    for (int i = 0; i < frames; ++i)
        for (int c = 0; c < o.channels; ++c)
        {
            const double x = sample (c, i);
            if (o.format == 3 && o.bits == 32) { const float f = (float) x; uint32_t b; std::memcpy (&b, &f, 4); w32 (pcm, b); }
            else if (o.format == 3 && o.bits == 64) { uint64_t b; std::memcpy (&b, &x, 8); w32 (pcm, (uint32_t) b); w32 (pcm, (uint32_t) (b >> 32)); }
            else if (o.bits == 8)  pcm.push_back ((uint8_t) std::lround (x * 127.0 + 128.0));
            else if (o.bits == 16) w16 (pcm, (uint16_t) (int16_t) std::lround (x * 32767.0));
            else if (o.bits == 24) { const int32_t s = (int32_t) std::lround (x * 8388607.0); pcm.push_back ((uint8_t) s); pcm.push_back ((uint8_t) (s >> 8)); pcm.push_back ((uint8_t) (s >> 16)); }
            else                   w32 (pcm, (uint32_t) (int32_t) std::lround (x * 2147483647.0));
        }

    std::vector<uint8_t> cue;
    if (! o.cues.empty())
    {
        wtag (cue, "cue ");
        w32 (cue, (uint32_t) (4 + 24 * o.cues.size()));
        w32 (cue, (uint32_t) (o.cueCountClaim >= 0 ? o.cueCountClaim : (int) o.cues.size()));
        for (auto& [id, frame] : o.cues)
        {
            w32 (cue, (uint32_t) id); w32 (cue, (uint32_t) frame); wtag (cue, o.cueFcc); w32 (cue, 0); w32 (cue, 0); w32 (cue, (uint32_t) frame);
        }
    }

    std::vector<uint8_t> body;
    wtag (body, "WAVE");
    if (! o.dropFmt)
    {
        wtag (body, "fmt ");
        if (o.extensible)
        {
            w32 (body, 40);
            w16 (body, 0xFFFE); w16 (body, (uint16_t) o.channels); w32 (body, (uint32_t) o.rate);
            w32 (body, (uint32_t) (o.rate * o.channels * o.bits / 8)); w16 (body, (uint16_t) (o.channels * o.bits / 8)); w16 (body, (uint16_t) o.bits);
            w16 (body, 22); w16 (body, (uint16_t) o.bits); w32 (body, 3);
            w16 (body, (uint16_t) o.format);                      // the sub-format starts with the real format tag
            for (int k = 0; k < 14; ++k) body.push_back (0);
        }
        else
        {
            w32 (body, 16);
            w16 (body, (uint16_t) o.format); w16 (body, (uint16_t) o.channels); w32 (body, (uint32_t) o.rate);
            w32 (body, (uint32_t) (o.rate * o.channels * o.bits / 8)); w16 (body, (uint16_t) (o.channels * o.bits / 8)); w16 (body, (uint16_t) o.bits);
        }
    }
    if (o.oddChunkBeforeData)
    {
        wtag (body, "LIST"); w32 (body, 5);
        for (int k = 0; k < 5; ++k) body.push_back ((uint8_t) 'x');
        body.push_back (0);                                       // the pad byte
    }
    if (o.cueBeforeData)
        body.insert (body.end(), cue.begin(), cue.end());
    wtag (body, "data");
    w32 (body, o.useOverride ? o.dataLenOverride : (uint32_t) pcm.size());
    body.insert (body.end(), pcm.begin(), pcm.end());
    if (pcm.size() & 1)
        body.push_back (0);
    if (! o.cueBeforeData)
        body.insert (body.end(), cue.begin(), cue.end());

    std::vector<uint8_t> v;
    wtag (v, "RIFF");
    w32 (v, (uint32_t) body.size());
    v.insert (v.end(), body.begin(), body.end());
    return v;
}

static bool writeFile (const fs::path& p, const std::vector<uint8_t>& bytes)
{
    std::error_code ec;
    if (p.has_parent_path())
        fs::create_directories (p.parent_path(), ec);
    std::ofstream f (p, std::ios::binary | std::ios::trunc);
    f.write (reinterpret_cast<const char*> (bytes.data()), (std::streamsize) bytes.size());
    return (bool) f;
}

static std::vector<uint8_t> slurp (const fs::path& p)
{
    std::ifstream f (p, std::ios::binary);
    return std::vector<uint8_t> ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
}

static bool contains (const std::string& text, const char* needle) { return text.find (needle) != std::string::npos; }

//==============================================================================
static void testWavRoundTrip()
{
    ReelAudio a = makeAudio (48000.0, 5000, { 100, 2500, 4900 });
    const auto bytes = phe::core::writeReelWav (a);

    CHECK (std::memcmp (bytes.data(), "RIFF", 4) == 0 && std::memcmp (bytes.data() + 8, "WAVE", 4) == 0, "starts as a WAV file");
    // The RIFF size field says how many bytes follow it.
    uint32_t riff = (uint32_t) bytes[4] | ((uint32_t) bytes[5] << 8) | ((uint32_t) bytes[6] << 16) | ((uint32_t) bytes[7] << 24);
    CHECK (riff + 8 == bytes.size(), "RIFF size %u vs file %zu", riff, bytes.size());
    const std::string raw (bytes.begin(), bytes.end());
    CHECK (raw.find ("fmt ") != std::string::npos && raw.find ("cue ") != std::string::npos && raw.find ("data") != std::string::npos, "has fmt, data and cue chunks");

    ReelAudio b;
    std::string err;
    phe::core::ReelFileInfo info;
    CHECK (phe::core::readReelWav (bytes.data(), bytes.size(), b, err, &info), "reads its own file: %s", err.c_str());
    CHECK (b.frames() == a.frames() && b.sampleRate == 48000.0, "same length and rate");
    CHECK (info.bits == 32 && info.isFloat && info.sourceChannels == 2 && info.cuePoints == 3, "info: %d bits float=%d channels=%d cues=%d", info.bits, (int) info.isFloat, info.sourceChannels, info.cuePoints);
    bool exact = true;
    for (size_t i = 0; i < a.frames(); ++i)
        if (a.ch[0][i] != b.ch[0][i] || a.ch[1][i] != b.ch[1][i])
            exact = false;
    CHECK (exact, "the audio comes back bit for bit");
    CHECK (b.markers == a.markers, "the markers come back exactly");

    // No markers: no cue chunk, and a plain reel comes back with no markers.
    ReelAudio plain = makeAudio (44100.0, 300);
    const auto pb = phe::core::writeReelWav (plain);
    const std::string praw (pb.begin(), pb.end());
    CHECK (praw.find ("cue ") == std::string::npos, "no cue chunk without markers");
    ReelAudio pr;
    CHECK (phe::core::readReelWav (pb.data(), pb.size(), pr, err) && pr.markers.empty() && pr.sampleRate == 44100.0, "plain reel reads back");

    // Markers that cannot be written (at the very start, at or past the end, repeated, out of order) are left out or put in order.
    ReelAudio odd = makeAudio (48000.0, 1000, { 900, 0, 300, 300, 1000, 5000, -4 });
    ReelAudio back;
    const auto ob = phe::core::writeReelWav (odd);
    CHECK (phe::core::readReelWav (ob.data(), ob.size(), back, err), "odd marker list written");
    CHECK ((back.markers == std::vector<int> ({ 300, 900 })), "odd markers tidied to 300, 900 (got %zu)", back.markers.size());

    // A mono list (right side empty) is written as the same on both sides.
    ReelAudio mono;
    mono.sampleRate = 48000.0;
    mono.ch[0] = { 0.5f, -0.25f, 0.125f };
    const auto mb = phe::core::writeReelWav (mono);
    ReelAudio mr;
    CHECK (phe::core::readReelWav (mb.data(), mb.size(), mr, err) && mr.frames() == 3 && mr.ch[1][1] == -0.25f, "missing right side copied from the left");

    // Not-a-number samples are written as silence.
    ReelAudio nan = makeAudio (48000.0, 10);
    nan.ch[0][3] = std::numeric_limits<float>::quiet_NaN();
    nan.ch[1][4] = std::numeric_limits<float>::infinity();
    const auto nb = phe::core::writeReelWav (nan);
    ReelAudio nr;
    CHECK (phe::core::readReelWav (nb.data(), nb.size(), nr, err) && nr.ch[0][3] == 0.0f && nr.ch[1][4] == 0.0f, "bad samples written as silence");
}

static void testReadFormats()
{
    const int n = 2000;
    auto wave = [] (int c, int i) { return 0.8 * std::sin (0.03 * i * (c + 1)); };
    std::string err;

    struct Case { const char* name; int format, channels, bits; double tol; bool extensible; };
    const Case cases[] = {
        { "8-bit stereo", 1, 2, 8, 0.02, false },
        { "16-bit stereo", 1, 2, 16, 6e-5, false },
        { "24-bit stereo", 1, 2, 24, 1e-6, false },
        { "32-bit PCM stereo", 1, 2, 32, 1e-6, false },
        { "32-bit float stereo", 3, 2, 32, 1e-7, false },
        { "64-bit float stereo", 3, 2, 64, 1e-7, false },
        { "16-bit mono", 1, 1, 16, 6e-5, false },
        { "24-bit extensible stereo", 1, 2, 24, 1e-6, true },
        { "float extensible stereo", 3, 2, 32, 1e-7, true },
        { "16-bit three channels", 1, 3, 16, 6e-5, false },
    };
    for (const Case& c : cases)
    {
        WavOptions o;
        o.format = c.format; o.channels = c.channels; o.bits = c.bits; o.extensible = c.extensible; o.rate = 44100;
        const auto bytes = makeWav (o, n, wave);
        ReelAudio a;
        phe::core::ReelFileInfo info;
        const bool ok = phe::core::readReelWav (bytes.data(), bytes.size(), a, err, &info);
        CHECK (ok, "%s: %s", c.name, err.c_str());
        if (! ok)
            continue;
        CHECK (a.frames() == (size_t) n && a.sampleRate == 44100.0 && info.sourceChannels == c.channels, "%s: shape", c.name);
        double worst = 0.0;
        for (int i = 0; i < n; ++i)
        {
            worst = std::max (worst, std::fabs ((double) a.ch[0][(size_t) i] - wave (0, i)));
            worst = std::max (worst, std::fabs ((double) a.ch[1][(size_t) i] - wave (c.channels == 1 ? 0 : 1, i)));
        }
        CHECK (worst <= c.tol, "%s: worst error %g (allowed %g)", c.name, worst, c.tol);
        if (c.channels == 1)
        {
            bool same = true;
            for (int i = 0; i < n; ++i)
                if (a.ch[0][(size_t) i] != a.ch[1][(size_t) i])
                    same = false;
            CHECK (same, "%s: mono copied to both sides", c.name);
        }
    }

    // Markers: before or after the audio, with an odd-sized chunk in front (padded), unordered, doubled, outside, in other chunks.
    {
        WavOptions o;
        o.cues = { { 3, 1500 }, { 1, 500 }, { 2, 500 }, { 4, 0 }, { 5, n }, { 6, n + 50 }, { 7, 1000 } };
        for (bool before : { false, true })
            for (bool odd : { false, true })
            {
                o.cueBeforeData = before;
                o.oddChunkBeforeData = odd;
                const auto bytes = makeWav (o, n, wave);
                ReelAudio a;
                phe::core::ReelFileInfo info;
                CHECK (phe::core::readReelWav (bytes.data(), bytes.size(), a, err, &info), "cue read (before=%d odd=%d): %s", (int) before, (int) odd, err.c_str());
                CHECK ((a.markers == std::vector<int> ({ 500, 1000, 1500 })), "markers 500 1000 1500 (before=%d odd=%d, got %zu)", (int) before, (int) odd, a.markers.size());
                CHECK (info.cuePoints == 7, "all seven entries counted (%d)", info.cuePoints);
            }

        WavOptions other = o;
        other.cueFcc = "slnt";
        const auto bytes = makeWav (other, n, wave);
        ReelAudio a;
        CHECK (phe::core::readReelWav (bytes.data(), bytes.size(), a, err) && a.markers.empty(), "markers that point into other chunks are ignored");

        WavOptions lying = o;
        lying.cueCountClaim = 1000;           // claims far more entries than the chunk holds
        const auto lb = makeWav (lying, n, wave);
        ReelAudio la;
        CHECK (phe::core::readReelWav (lb.data(), lb.size(), la, err) && la.markers.size() == 3, "a cue count that is too large is limited to what is there");
    }

    // Damaged and streamed files.
    {
        WavOptions o;
        auto good = makeWav (o, n, wave);

        std::vector<uint8_t> cut (good.begin(), good.begin() + (long) good.size() - 301);   // the file stops in the middle of the audio
        ReelAudio a;
        CHECK (phe::core::readReelWav (cut.data(), cut.size(), a, err) && a.frames() > 0 && a.frames() < (size_t) n, "a file cut short still gives the audio that is there");

        WavOptions streamed;
        streamed.useOverride = true;
        streamed.dataLenOverride = 0xFFFFFFFFu;           // length unknown (streamed)
        const auto sb = makeWav (streamed, n, wave);
        ReelAudio s;
        CHECK (phe::core::readReelWav (sb.data(), sb.size(), s, err) && s.frames() == (size_t) n, "unknown data length: the rest of the file is the audio");

        WavOptions huge;
        huge.useOverride = true;
        huge.dataLenOverride = 0x7FFFFFFFu;
        const auto hb = makeWav (huge, n, wave);
        ReelAudio h;
        CHECK (phe::core::readReelWav (hb.data(), hb.size(), h, err) && h.frames() == (size_t) n, "data length bigger than the file: what is there is used");

        // Refusals, each with a sentence.
        const std::vector<uint8_t> text = { 'h', 'e', 'l', 'l', 'o' };
        CHECK (! phe::core::readReelWav (text.data(), text.size(), a, err) && ! err.empty(), "text is not a WAV");
        std::vector<uint8_t> noise (5000);
        uint32_t seed = 1;
        for (auto& b : noise) { seed = seed * 1664525u + 1013904223u; b = (uint8_t) (seed >> 24); }
        CHECK (! phe::core::readReelWav (noise.data(), noise.size(), a, err), "noise is not a WAV");
        CHECK (! phe::core::readReelWav (nullptr, 0, a, err), "nothing is not a WAV");
        WavOptions nofmt; nofmt.dropFmt = true;
        const auto nb = makeWav (nofmt, 100, wave);
        CHECK (! phe::core::readReelWav (nb.data(), nb.size(), a, err) && contains (err, "no audio data"), "no fmt chunk: %s", err.c_str());
        const auto empty = makeWav (o, 0, wave);
        CHECK (! phe::core::readReelWav (empty.data(), empty.size(), a, err) && contains (err, "empty"), "empty audio: %s", err.c_str());
        WavOptions adpcm; adpcm.format = 2;
        const auto ab = makeWav (adpcm, 100, wave);
        CHECK (! phe::core::readReelWav (ab.data(), ab.size(), a, err) && contains (err, "PCM or float"), "ADPCM: %s", err.c_str());
        WavOptions weird; weird.bits = 12;
        const auto wb = makeWav (weird, 100, wave);
        CHECK (! phe::core::readReelWav (wb.data(), wb.size(), a, err), "12-bit refused");
        WavOptions slow; slow.rate = 500;
        const auto lo = makeWav (slow, 100, wave);
        CHECK (! phe::core::readReelWav (lo.data(), lo.size(), a, err) && contains (err, "unusual"), "rate 500 Hz: %s", err.c_str());

        // Every possible cut of a file and every single flipped byte must be survived (an answer, never a crash).
        WavOptions rich;
        rich.cues = { { 1, 300 }, { 2, 900 } };
        const auto base = makeWav (rich, 400, wave);
        int answered = 0;
        for (size_t len = 0; len <= base.size(); ++len)
        {
            ReelAudio x;
            std::string e2;
            (void) phe::core::readReelWav (base.data(), len, x, e2);
            ++answered;
        }
        for (size_t at = 0; at < base.size(); ++at)
            for (int bit : { 0, 3, 7 })
            {
                auto mutated = base;
                mutated[at] ^= (uint8_t) (1 << bit);
                ReelAudio x;
                std::string e2;
                if (phe::core::readReelWav (mutated.data(), mutated.size(), x, e2))
                {
                    CHECK (x.ch[0].size() == x.ch[1].size(), "mutated file: equal channel lengths");
                    for (int m : x.markers)
                        CHECK (m > 0 && (size_t) m < x.frames(), "mutated file: marker inside the audio");
                }
                ++answered;
            }
        CHECK (answered > 1000, "damaged-file sweep ran (%d)", answered);
    }
}

static void testTidyMarkers()
{
    using phe::core::tidyMarkers;
    std::vector<int> m = { 5000, 100, 100, 2000, 2030, 2064, 9990, 9950, 0, -7, 10000, 20000 };
    const int dropped = tidyMarkers (m, 10000);
    // 100 stays; the second 100 is a repeat; 2000 stays; 2030 is too close; 2064 is exactly 64 away from 2000 and stays;
    // 5000 stays; 9950 is too close to the end (50 left); 9990, 0, -7, 10000 and 20000 are outside or too close.
    CHECK ((m == std::vector<int> ({ 100, 2000, 2064, 5000 })), "tidy result has %zu markers", m.size());
    CHECK (dropped == 12 - 4, "dropped count %d", dropped);

    std::vector<int> many;
    for (int i = 1; i <= 500; ++i)
        many.push_back (i * 100);
    tidyMarkers (many, 100000);
    CHECK ((int) many.size() == phe::core::kMaxSpliceMarkers && many.front() == 100, "at most 299 markers (%zu)", many.size());

    std::vector<int> none;
    CHECK (tidyMarkers (none, 1000) == 0 && none.empty(), "an empty list stays empty");
}

static void testFitToReel()
{
    using namespace phe::core;
    std::string err;
    FitResult fit;

    // Same rate: nothing converted; a long file is cut at the reel size and the markers beyond it go.
    {
        ReelAudio a = makeAudio (48000.0, 10000, { 2000, 8000 });
        CHECK (fitToReel (a, 48000.0, 5000, fit, err), "fit same rate: %s", err.c_str());
        CHECK (! fit.converted && fit.cut && a.frames() == 5000 && a.markers == std::vector<int> ({ 2000 }) && fit.markersDropped == 1, "cut to 5000, one marker left");
        CHECK (a.ch[0].size() == a.ch[1].size(), "equal lengths");
    }
    // Nothing to do.
    {
        ReelAudio a = makeAudio (48000.0, 3000, { 1000 });
        const ReelAudio copy = a;
        CHECK (fitToReel (a, 48000.0, 5000, fit, err) && ! fit.converted && ! fit.cut && fit.markersDropped == 0 && a.ch[0] == copy.ch[0] && a.markers == copy.markers, "unchanged when it already fits");
    }
    // 44.1 kHz to 48 kHz: a pure tone keeps its pitch and its place, the length and the markers scale.
    {
        const double f = 1000.0;
        const int n = 44100;
        ReelAudio a;
        a.sampleRate = 44100.0;
        for (int c = 0; c < 2; ++c)
        {
            a.ch[c].resize ((size_t) n);
            for (int i = 0; i < n; ++i)
                a.ch[c][(size_t) i] = 0.5f * (float) std::sin (2.0 * 3.14159265358979323846 * f * i / 44100.0);
        }
        a.markers = { 11025, 22050, 33075 };
        CHECK (fitToReel (a, 48000.0, 200000, fit, err), "fit 44.1k to 48k: %s", err.c_str());
        CHECK (fit.converted && fit.fromRate == 44100.0 && a.sampleRate == 48000.0 && a.frames() == 48000, "converted, 48000 frames (%zu)", a.frames());
        CHECK ((a.markers == std::vector<int> ({ 12000, 24000, 36000 })), "markers moved with the audio (%zu)", a.markers.size());
        double worst = 0.0;
        for (int i = 2000; i < 46000; ++i)
            worst = std::max (worst, std::fabs ((double) a.ch[0][(size_t) i] - 0.5 * std::sin (2.0 * 3.14159265358979323846 * f * i / 48000.0)));
        CHECK (worst < 0.04, "the tone is still the same tone, within half a sample of timing (worst difference %g)", worst);
    }
    // 96 kHz down to 48 kHz and 8 kHz up.
    {
        ReelAudio a = makeAudio (96000.0, 9600, { 4800 });
        CHECK (fitToReel (a, 48000.0, 200000, fit, err) && a.frames() == 4800 && a.markers == std::vector<int> ({ 2400 }), "96k to 48k");
        ReelAudio b = makeAudio (8000.0, 800, { 400 });
        CHECK (fitToReel (b, 48000.0, 200000, fit, err) && b.frames() == 4800 && b.markers == std::vector<int> ({ 2400 }), "8k to 48k");
    }
    // Refusals.
    {
        ReelAudio a = makeAudio (48000.0, 63);
        CHECK (! fitToReel (a, 48000.0, 5000, fit, err) && contains (err, "too short"), "too short: %s", err.c_str());
        ReelAudio e;
        e.sampleRate = 48000.0;
        CHECK (! fitToReel (e, 48000.0, 5000, fit, err), "empty refused");
        ReelAudio ok = makeAudio (48000.0, 1000);
        CHECK (! fitToReel (ok, 0.0, 5000, fit, err) && contains (err, "not ready"), "no reel rate: %s", err.c_str());
        ReelAudio uneven = makeAudio (48000.0, 1000);
        uneven.ch[1].resize (900);
        CHECK (! fitToReel (uneven, 48000.0, 5000, fit, err), "uneven channels refused");
    }
}

//==============================================================================
static void testExportBasic()
{
    for (int block : { 1, 7, 64, 480, 4096 })
    {
        Rig r;
        r.recordFirstTake (60000, 480);
        r.press (&phe::Params::splice);
        r.run (20000, 480);
        r.press (&phe::Params::splice);
        const auto marks = r.markers();
        CHECK (marks.size() >= 1, "block %d: set up markers", block);

        ReelAudio out;
        uint32_t stamp = 0;
        CHECK (exportNow (r, out, stamp, 100000, block), "block %d: export finished", block);
        CHECK (r.sameAs (out), "block %d: the copy equals the reel and its markers", block);
        CHECK (out.sampleRate == 48000.0, "block %d: sample rate", block);
        CHECK (stamp == r.e.editStamp(), "block %d: the stamp is the reel's stamp", block);
        CHECK (! r.e.getReelView().busy, "block %d: not busy afterwards", block);

        // Written out and read back it is the same again.
        const auto bytes = phe::core::writeReelWav (out);
        ReelAudio back;
        std::string err;
        CHECK (phe::core::readReelWav (bytes.data(), bytes.size(), back, err) && r.sameAs (back), "block %d: file round trip equals the reel", block);
    }

    // An empty reel says so.
    {
        Rig r;
        ReelAudio out;
        uint32_t stamp = 0;
        CHECK (r.e.requestExport(), "empty: request taken");
        r.run (512, 256);
        CHECK (r.e.pollExport (out, stamp) == phe::Engine::IoResult::empty, "empty reel reported as empty");
        CHECK (r.e.requestExport(), "empty: a new request is possible afterwards");
        r.run (512, 256);
        (void) r.e.pollExport (out, stamp);
    }

    // A second request while one is waiting is refused; the slot is free again once the copy was collected.
    {
        Rig r;
        r.recordFirstTake (20000, 480);
        CHECK (r.e.requestExport(), "first request");
        CHECK (! r.e.requestExport(), "second request refused while the first is pending");
        ReelAudio out;
        uint32_t stamp = 0;
        for (int k = 0; k < 100 && r.e.pollExport (out, stamp) == phe::Engine::IoResult::none; ++k)
            r.run (256, 256);
        CHECK (r.e.requestExport(), "slot free again after collecting");
        r.run (2048, 256);
        (void) r.e.pollExport (out, stamp);
    }
}

static void testExportDoesNotDisturbSound()
{
    // Playing carries on exactly as it would have: the copy is made on the side.
    for (int block : { 64, 480 })
    {
        Rig a, b;
        for (Rig* r : { &a, &b })
        {
            r->recordFirstTake (90000, 480);
            r->p.geneSize = 0.6f;
            r->p.slide = 0.3f;
            r->p.morph = 0.5f;
            r->run (5000, 480);
        }
        const auto before = a.run (3000, block, tone);
        const auto beforeB = b.run (3000, block, tone);
        CHECK (before == beforeB, "block %d: identical before the request", block);

        CHECK (a.e.requestExport(), "request taken");
        const auto outA = a.run (60000, block, tone);
        const auto outB = b.run (60000, block, tone);
        CHECK (outA == outB, "block %d: the sound is identical while a copy is made", block);
        ReelAudio copy;
        uint32_t stamp = 0;
        CHECK (a.e.pollExport (copy, stamp) == phe::Engine::IoResult::done, "block %d: copy is ready", block);
    }
}

static void testExportWaitsForRecording()
{
    Rig r;
    r.recordFirstTake (50000, 480);
    r.p.rec = true;                                   // recording over the reel (Time Lag Accumulation)
    r.run (2000, 480, tone);
    CHECK (r.e.getReelView().recording, "recording over the reel");
    CHECK (r.e.requestExport(), "export requested during the recording");
    r.run (20000, 480, tone);
    ReelAudio out;
    uint32_t stamp = 0;
    CHECK (r.e.pollExport (out, stamp) == phe::Engine::IoResult::none, "no copy while recording");
    r.p.rec = false;
    bool done = false;
    for (int k = 0; k < 200 && ! done; ++k)
    {
        r.run (480, 480, tone);
        done = r.e.pollExport (out, stamp) == phe::Engine::IoResult::done;
    }
    CHECK (done, "the copy is made once the recording has stopped");
    CHECK (done && r.sameAs (out), "and it holds the finished recording");
    CHECK (stamp == r.e.editStamp(), "stamp taken after the recording");

    // Requested while a first recording is still growing: the reel grew meanwhile, so the answer is "ask again".
    Rig g;
    g.recordFirstTake (10000, 480);
    g.press (&phe::Params::clearReel);
    g.p.rec = true;
    g.run (4800, 480, tone);
    CHECK (g.e.requestExport(), "export requested during a first recording");
    g.run (9600, 480, tone);
    g.p.rec = false;
    bool answered = false;
    phe::Engine::IoResult res = phe::Engine::IoResult::none;
    for (int k = 0; k < 100 && ! answered; ++k)
    {
        g.run (480, 480, tone);
        res = g.e.pollExport (out, stamp);
        answered = res != phe::Engine::IoResult::none;
    }
    CHECK (answered && res == phe::Engine::IoResult::rejected, "a reel that grew is reported as rejected (so the caller asks again)");
    ReelAudio again;
    CHECK (exportNow (g, again, stamp) && g.sameAs (again), "and asking again works");
}

static void testExportRefusesEditsMeanwhile()
{
    // While the copy is being made the edit buttons are ignored (so the copy is one moment of the reel); they still go back off.
    Rig r;
    r.recordFirstTake (400000, 4800);                 // long enough that the copy takes several blocks
    const auto before = r.markers();
    CHECK (r.e.requestExport(), "request");
    r.p.splice = true;
    r.run (256, 256);
    CHECK (r.e.getReelView().busy, "busy while copying");
    r.p.splice = false;
    CHECK ((r.e.takeHandledButtons() & phe::Engine::kButtonSplice) != 0, "the Splice press was handled (so it goes back off)");
    CHECK (r.markers() == before, "but no marker was added");
    ReelAudio out;
    uint32_t stamp = 0;
    for (int k = 0; k < 500 && r.e.pollExport (out, stamp) == phe::Engine::IoResult::none; ++k)
        r.run (256, 256);
    CHECK (r.sameAs (out), "the copy is the reel");
}

//==============================================================================
static void testImportBasic()
{
    for (int block : { 1, 7, 64, 333, 4096 })
    {
        Rig r;
        const uint32_t stampBefore = r.e.editStamp();
        ReelAudio a = makeAudio (48000.0, 96000, { 20000, 50000 }, 1);
        const ReelAudio expected = a;
        std::string why;
        CHECK (importNow (r, std::move (a), why, 100000, block), "block %d: import finished (%s)", block, why.c_str());
        CHECK (r.sameAs (expected), "block %d: reel and markers equal the file", block);
        CHECK (r.e.getReelView().numSplices == 3, "block %d: three splices", block);
        CHECK (r.e.editStamp() != stampBefore, "block %d: the edit stamp moved", block);
        CHECK (r.e.getReelView().undoState == 1, "block %d: one undo step", block);
        CHECK (! r.e.getReelView().busy, "block %d: not busy afterwards", block);

        // The waveform picture was drawn (the first slice holds the first samples' peak).
        float l = 0, rr = 0;
        r.e.getReel().peaks.get (0, l, rr);
        CHECK (l > 0.05f && rr > 0.05f, "block %d: waveform picture drawn (%g %g)", block, (double) l, (double) rr);

        // And it plays (Play is on): the sound is the reel's.
        r.run (4800, 480);
        const auto out = r.run (9600, 480);
        double energy = 0.0;
        for (float v : out[0])
            energy += (double) v * v;
        CHECK (energy / 9600.0 > 0.01, "block %d: it plays (mean square %g)", block, energy / 9600.0);
        CHECK (r.e.getReelView().playing, "block %d: playing", block);
    }
}

static void testImportUndoRedo()
{
    for (int variant = 0; variant < 3; ++variant)
    {
        Rig r;
        const int oldLength = variant == 0 ? 144000 : (variant == 1 ? 40000 : 96000);
        const int newLength = variant == 0 ? 48000 : (variant == 1 ? 130000 : 96000);
        r.recordFirstTake (oldLength, 480);
        r.press (&phe::Params::splice);
        r.run (3000, 480);
        r.press (&phe::Params::splice);
        const uint64_t original = r.fingerprint();
        const auto originalMarkers = r.markers();

        ReelAudio b = makeAudio (48000.0, newLength, { 10000, 30000 }, 5);
        const ReelAudio imported = b;
        std::string why;
        CHECK (importNow (r, std::move (b), why), "variant %d: import (%s)", variant, why.c_str());
        CHECK (r.sameAs (imported), "variant %d: imported", variant);
        const uint64_t afterImport = r.fingerprint();

        r.press (&phe::Params::undo);
        r.runUntil ([&] { return ! r.e.getReelView().busy; });
        CHECK (r.fingerprint() == original, "variant %d: Undo gives back the old reel exactly", variant);
        CHECK (r.markers() == originalMarkers, "variant %d: and its markers", variant);
        CHECK (r.e.getReelView().undoState == 2, "variant %d: the button now offers Redo", variant);

        r.press (&phe::Params::undo);
        r.runUntil ([&] { return ! r.e.getReelView().busy; });
        CHECK (r.fingerprint() == afterImport && r.sameAs (imported), "variant %d: Redo gives back the loaded reel exactly", variant);

        // Waveform picture matches after the swaps: a slice inside the old reel is no longer quiet.
        float l = 0, rr = 0;
        r.e.getReel().peaks.get (0, l, rr);
        CHECK (l > 0.05f, "variant %d: waveform drawn after redo", variant);
    }

    // A load after a load: only the newest can be undone.
    Rig r;
    r.recordFirstTake (30000, 480);
    const uint64_t original = r.fingerprint();
    std::string why;
    ReelAudio first = makeAudio (48000.0, 20000, {}, 2);
    ReelAudio second = makeAudio (48000.0, 25000, { 5000 }, 3);
    const ReelAudio secondCopy = second;
    CHECK (importNow (r, std::move (first), why) && importNow (r, std::move (second), why), "two loads");
    r.press (&phe::Params::undo);
    r.runUntil ([&] { return ! r.e.getReelView().busy; });
    CHECK (r.fingerprint() != original, "undoing the second load gives the first load, not the original");
    CHECK (r.e.getReelView().numSplices == 1 && r.e.getReel().length() == 20000, "first load is back (length %d)", r.e.getReel().length());
    (void) secondCopy;
}

static void testImportRefusals()
{
    // Engine not prepared yet.
    {
        phe::Engine e;
        std::string why;
        CHECK (! e.submitImport (makeAudio (48000.0, 1000), why) && why.rfind ("Not ready", 0) == 0, "unprepared: %s", why.c_str());
        CHECK (e.reelSampleRate() == 0.0 && e.reelCapacity() == 0, "unprepared engine has no reel memory");
    }
    Rig r;
    std::string why;
    CHECK (! r.e.submitImport (makeAudio (44100.0, 1000), why) && why.rfind ("Does not fit", 0) == 0, "wrong rate: %s", why.c_str());
    CHECK (! r.e.submitImport (makeAudio (48000.0, 63), why) && why.rfind ("Does not fit", 0) == 0, "too short: %s", why.c_str());
    ReelAudio tooLong;
    tooLong.sampleRate = 48000.0;
    tooLong.ch[0].assign ((size_t) r.e.reelCapacity() + 1, 0.0f);
    tooLong.ch[1].assign ((size_t) r.e.reelCapacity() + 1, 0.0f);
    CHECK (! r.e.submitImport (std::move (tooLong), why) && why.rfind ("Does not fit", 0) == 0, "longer than the reel: %s", why.c_str());
    ReelAudio uneven = makeAudio (48000.0, 1000);
    uneven.ch[1].resize (500);
    CHECK (! r.e.submitImport (std::move (uneven), why), "uneven channels refused");

    // Exactly the reel's size is fine.
    {
        ReelAudio full;
        full.sampleRate = 48000.0;
        full.ch[0].assign ((size_t) r.e.reelCapacity(), 0.25f);
        full.ch[1].assign ((size_t) r.e.reelCapacity(), -0.25f);
        CHECK (importNow (r, std::move (full), why, 20000, 4096), "a file exactly as long as the reel loads");
        CHECK (r.e.getReel().length() == r.e.reelCapacity() && r.e.getReel().get (0, r.e.reelCapacity() - 1) == 0.25f, "all of it is there");
        r.press (&phe::Params::clearReel);
    }

    // Second file while the first is pending.
    {
        CHECK (r.e.submitImport (makeAudio (48000.0, 5000), why), "first accepted");
        CHECK (! r.e.submitImport (makeAudio (48000.0, 6000), why) && why.rfind ("Still busy", 0) == 0, "second while pending: %s", why.c_str());
        r.run (4096, 256);
        uint32_t stamp = 0;
        CHECK (r.e.pollImport (stamp) == phe::Engine::IoResult::done, "first completed");
        CHECK (r.e.reelLength() == 5000, "and it is the one that was loaded");
    }

    // While recording.
    {
        Rig g;
        g.p.rec = true;
        g.run (2048, 256, tone);
        CHECK (! g.e.submitImport (makeAudio (48000.0, 5000), why) && why.rfind ("A recording", 0) == 0, "while recording: %s", why.c_str());
        g.p.rec = false;
        g.run (1024, 256);
    }

    // A file waiting for a recording to stop is written right after it.
    {
        Rig g;
        g.recordFirstTake (20000, 480);
        CHECK (g.e.submitImport (makeAudio (48000.0, 8000, {}, 9), why), "accepted while not recording");
        g.p.rec = true;                                // a recording starts before the audio thread picks the file up
        g.run (480, 480, tone);
        uint32_t stamp = 0;
        for (int k = 0; k < 20; ++k)
            g.run (480, 480, tone);
        // Whichever came first, the engine ends in a consistent state once the recording is stopped.
        g.p.rec = false;
        for (int k = 0; k < 400 && g.e.pollImport (stamp) == phe::Engine::IoResult::none; ++k)
            g.run (480, 480, tone);
        CHECK (! g.e.getReelView().busy && ! g.e.getReelView().recording, "settled");
    }
}

static void testEditStamp()
{
    Rig r;
    auto stamp = [&] { return r.e.editStamp(); };
    uint32_t s = stamp();

    r.p.rec = true;
    r.run (20000, 480, tone);
    CHECK (stamp() != s, "recording moves the stamp");
    r.p.rec = false;
    r.run (480, 480);
    s = stamp();

    r.run (5000, 480);
    r.press (&phe::Params::shift);
    r.p.organize = 0.9f;
    r.run (1000, 480);
    r.p.play = false;
    r.run (1000, 480);
    r.p.play = true;
    r.run (1000, 480);
    r.p.variSpeed = 0.3f;
    r.p.geneSize = 0.5f;
    r.p.morph = 0.9f;
    r.run (4000, 480);
    CHECK (stamp() == s, "Shift, Organize, Play and the knobs do not move the stamp");

    r.press (&phe::Params::splice);
    CHECK (stamp() != s, "Splice moves it");
    s = stamp();
    r.press (&phe::Params::deleteMarker);
    CHECK (stamp() != s, "Delete Splice Marker moves it");
    s = stamp();
    r.press (&phe::Params::splice);
    s = stamp();
    r.press (&phe::Params::deleteAllMarkers);
    CHECK (stamp() != s, "Delete All Markers moves it");
    s = stamp();
    r.press (&phe::Params::undo);
    r.runUntil ([&] { return ! r.e.getReelView().busy; });
    CHECK (stamp() != s, "Undo moves it");
    s = stamp();
    r.press (&phe::Params::undo);
    r.runUntil ([&] { return ! r.e.getReelView().busy; });
    CHECK (stamp() != s, "Redo moves it");
    s = stamp();
    r.press (&phe::Params::splice);
    r.press (&phe::Params::eraseSplice);
    r.runUntil ([&] { return ! r.e.getReelView().busy; });
    CHECK (stamp() != s, "Erase Splice moves it");
    if (r.e.getReel().length() == 0)
        r.recordFirstTake (20000, 480);
    s = stamp();
    r.press (&phe::Params::clearReel);
    CHECK (r.e.getReel().length() == 0 && stamp() != s, "Clear Reel moves it");
    s = stamp();
    r.press (&phe::Params::clearReel);
    CHECK (stamp() == s, "Clear Reel on an empty reel does not");

    r.p.recNewSplice = true;
    r.run (9600, 480, tone);
    r.p.recNewSplice = false;
    r.run (480, 480);
    s = stamp();
    r.p.recNewSplice = true;
    r.run (9600, 480, tone);
    r.p.recNewSplice = false;
    r.run (480, 480);
    CHECK (stamp() != s, "Rec New Splice moves it");
    s = stamp();
    ReelAudio out;
    uint32_t st = 0;
    CHECK (exportNow (r, out, st) && stamp() == s, "saving does not move it");
    std::string why;
    CHECK (importNow (r, makeAudio (48000.0, 4000), why) && stamp() != s, "loading moves it");
}

static void testLongReelRoundTrip()
{
    Rig r;
    const int frames = 48000 * 100;
    ReelAudio a = makeAudio (48000.0, frames, { 100000, 1000000, 2500000, 4700000 }, 4);
    const ReelAudio expected = a;
    std::string why;
    CHECK (importNow (r, std::move (a), why, 20000, 512), "100-second load (%s)", why.c_str());
    CHECK (r.sameAs (expected), "all 100 seconds and four markers are there");
    ReelAudio out;
    uint32_t stamp = 0;
    CHECK (exportNow (r, out, stamp, 20000, 512), "100-second save");
    CHECK (out.ch[0] == expected.ch[0] && out.ch[1] == expected.ch[1] && out.markers == expected.markers, "saved equals loaded");
}

static void testRateChangeMidJob()
{
    // Loading
    {
        Rig r;
        std::string why;
        CHECK (r.e.submitImport (makeAudio (48000.0, 48000 * 30, {}, 1), why), "accepted");
        r.run (256, 256);                              // the job has started
        CHECK (r.e.getReelView().busy, "busy while writing");
        r.e.prepare (44100.0, 4096, 2);                // the host changes the sample rate
        uint32_t stamp = 0;
        r.run (1024, 256);
        CHECK (r.e.pollImport (stamp) == phe::Engine::IoResult::rejected, "a load interrupted by a rate change is rejected");
        CHECK (! r.e.getReelView().busy, "not busy afterwards");
        CHECK (r.e.reelSampleRate() == 44100.0, "new rate");
        // The caller fits the file to the new rate and tries again.
        CHECK (importNow (r, makeAudio (44100.0, 20000), why), "loading again at the new rate works");
    }
    // Saving
    {
        Rig r;
        r.recordFirstTake (48000 * 30, 4800);
        CHECK (r.e.requestExport(), "request");
        r.run (256, 256);
        r.e.prepare (44100.0, 4096, 2);
        r.run (1024, 256);
        ReelAudio out;
        uint32_t stamp = 0;
        const auto res = r.e.pollExport (out, stamp);
        CHECK (res == phe::Engine::IoResult::rejected || res == phe::Engine::IoResult::done, "a copy interrupted by a rate change is rejected (or had finished)");
        if (res == phe::Engine::IoResult::done)
            CHECK (out.sampleRate == 48000.0 && out.frames() == (size_t) 48000 * 30, "a finished copy is still whole");
        CHECK (r.e.requestExport(), "slot free again");
        r.run (4096, 256);
        (void) r.e.pollExport (out, stamp);
    }
}

static void testRandomStress()
{
    // Many random sequences of button presses, loads and saves at random block sizes: the reel and markers always stay sane,
    // the sound stays finite, and every request is answered one way or the other.
    bool markersOk = true, finite = true, lengthOk = true;
    int exportsDone = 0, importsDone = 0;
    for (int seed = 1; seed <= 24; ++seed)
    {
        uint32_t rng = (uint32_t) seed * 7919u;
        auto next = [&rng] (int range) { rng = rng * 1664525u + 1013904223u; return (int) ((rng >> 8) % (uint32_t) range); };

        Rig r;
        bool exportOut = false, importOut = false;
        for (int step = 0; step < 220; ++step)
        {
            switch (next (14))
            {
                case 0: r.p.rec = ! r.p.rec; break;
                case 1: r.p.recNewSplice = ! r.p.recNewSplice; break;
                case 2: r.p.splice = true; break;
                case 3: r.p.shift = true; break;
                case 4: r.p.deleteMarker = true; break;
                case 5: r.p.deleteAllMarkers = true; break;
                case 6: r.p.eraseSplice = true; break;
                case 7: r.p.clearReel = (next (5) == 0); break;
                case 8: r.p.undo = true; break;
                case 9:
                    if (! importOut)
                    {
                        std::vector<int> marks;
                        const int len = 1000 + next (150000);
                        for (int k = 0, n = next (5); k < n; ++k)
                            marks.push_back (next (len));
                        std::sort (marks.begin(), marks.end());
                        marks.erase (std::unique (marks.begin(), marks.end()), marks.end());
                        phe::core::tidyMarkers (marks, len);
                        std::string why;
                        importOut = r.e.submitImport (makeAudio (48000.0, len, marks, seed), why);
                    }
                    break;
                case 10:
                    if (! exportOut)
                        exportOut = r.e.requestExport();
                    break;
                case 11: r.p.play = ! r.p.play; break;
                case 12: r.p.geneSize = (float) next (100) / 100.0f; r.p.morph = (float) next (100) / 100.0f; r.p.variSpeed = (float) next (100) / 100.0f; break;
                default: break;
            }
            const int block = 1 + next (3000);
            const auto out = r.run (block + 64, block + 64, tone);
            for (auto& ch : out)
                for (float v : ch)
                    if (! std::isfinite (v))
                        finite = false;
            // The wrapper switches the one-shot buttons off after they act.
            const int handled = r.e.takeHandledButtons();
            if (handled & phe::Engine::kButtonSplice) r.p.splice = false;
            if (handled & phe::Engine::kButtonShift) r.p.shift = false;
            if (handled & phe::Engine::kButtonDeleteMarker) r.p.deleteMarker = false;
            if (handled & phe::Engine::kButtonDeleteAllMarkers) r.p.deleteAllMarkers = false;
            if (handled & phe::Engine::kButtonErase) r.p.eraseSplice = false;
            if (handled & phe::Engine::kButtonUndo) r.p.undo = false;
            if (r.e.takeClearHandled()) r.p.clearReel = false;
            if (r.e.takeRecordStopped()) r.p.rec = false;
            if (r.e.takeNewSpliceStopped()) r.p.recNewSplice = false;

            ReelAudio tmp;
            uint32_t stamp = 0;
            if (exportOut)
            {
                const auto res = r.e.pollExport (tmp, stamp);
                if (res != phe::Engine::IoResult::none)
                {
                    exportOut = false;
                    ++exportsDone;
                    if (res == phe::Engine::IoResult::done)
                    {
                        for (int m : tmp.markers)
                            if (m <= 0 || (size_t) m >= tmp.frames())
                                markersOk = false;
                    }
                }
            }
            if (importOut)
            {
                const auto res = r.e.pollImport (stamp);
                if (res != phe::Engine::IoResult::none)
                {
                    importOut = false;
                    ++importsDone;
                }
            }

            const auto m = r.markers();
            int prev = 0;
            for (int v : m)
            {
                if (v - prev < phe::core::kMinSpliceSamples || v >= r.e.getReel().length())
                    markersOk = false;
                prev = v;
            }
            if (! m.empty() && r.e.getReel().length() - m.back() < phe::core::kMinSpliceSamples)
                markersOk = false;
            if (r.e.getReel().length() > r.e.reelCapacity() || r.e.getReel().length() < 0)
                lengthOk = false;
        }
    }
    CHECK (markersOk, "markers always rising, apart by at least 64, inside the reel");
    CHECK (finite, "the sound is always finite");
    CHECK (lengthOk, "the reel is never longer than its memory");
    CHECK (exportsDone > 20 && importsDone > 20, "enough saves and loads were answered (%d, %d)", exportsDone, importsDone);
}

//==============================================================================
// The file link library, with real files.
struct TempDir
{
    fs::path path;
    TempDir()
    {
        std::error_code ec;
        path = fs::temp_directory_path (ec) / ("phe_reelfile_test_" + std::to_string ((long long) std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories (path, ec);
    }
    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all (path, ec);
    }
};

struct Session
{
    Rig rig;
    phe::ReelLibrary lib;

    explicit Session (double rate = 48000.0, bool prepare = true) : rig (rate, prepare), lib (rig.e) {}

    // Feeds audio and ticks the library until it has nothing to do.
    bool pump (int maxTicks = 4000, int block = 256)
    {
        for (int k = 0; k < maxTicks; ++k)
        {
            rig.run (block, block);
            lib.tick();
            if (! lib.busy())
                return true;
        }
        return false;
    }

    void takeWithMarkers (int frames = 60000)
    {
        rig.recordFirstTake (frames, 480);
        rig.press (&phe::Params::splice);
        rig.run (20000, 480);
        rig.press (&phe::Params::splice);
    }
};

static void testLibrarySave()
{
    TempDir dir;
    const fs::path reels = dir.path / "Reels";
    Session s;
    s.lib.setHomeFolder (reels.string());
    s.takeWithMarkers();
    CHECK (s.rig.markers().size() >= 1, "markers set up");

    std::string why;
    const fs::path file = reels / "first.wav";
    CHECK (s.lib.saveReel (file.string(), why), "save accepted: %s", why.c_str());
    CHECK (s.lib.info().status == phe::ReelLibrary::Working && s.lib.busy(), "working");
    CHECK (! s.lib.saveReel ((reels / "other.wav").string(), why) && contains (why, "busy"), "a second save while busy is refused: %s", why.c_str());
    CHECK (s.pump(), "save finished");

    const auto info = s.lib.info();
    CHECK (info.bound && info.status == phe::ReelLibrary::Ok && ! info.edited && ! info.unsaved && ! info.busy, "bound, Ok, not edited");
    CHECK (info.fileName == "first.wav" && contains (info.message, "saved first.wav"), "message: %s", info.message.c_str());
    CHECK (fs::equivalent (info.fullPath, file), "full path points at the file");
    CHECK (s.lib.binding().path == "first.wav", "stored relative to the reel folder: %s", s.lib.binding().path.c_str());
    CHECK (fs::exists (file) && ! fs::exists (reels / "first.wav.phe-tmp"), "file written, temporary file gone");

    const auto bytes = slurp (file);
    CHECK (s.lib.binding().size == bytes.size() && s.lib.binding().hash == phe::ReelLibrary::hashBytes (bytes.data(), bytes.size()), "size and fingerprint match the file");
    ReelAudio back;
    std::string err;
    CHECK (phe::core::readReelWav (bytes.data(), bytes.size(), back, err) && s.rig.sameAs (back), "the file holds the reel and its markers");
    CHECK (s.lib.hasBinding(), "has a binding");

    // Edited since saved: the link stays, and says so.
    s.rig.press (&phe::Params::splice);
    s.lib.tick();
    CHECK (s.lib.info().bound && s.lib.info().edited, "a new marker: edited since saved");
    CHECK (s.lib.hasBinding(), "the link is kept");

    // Saving again puts it right, over the same file (no leftovers).
    CHECK (s.lib.saveReel (file.string(), why) && s.pump(), "save again");
    CHECK (! s.lib.info().edited && s.lib.info().status == phe::ReelLibrary::Ok, "no longer edited");
    ReelAudio back2;
    const auto bytes2 = slurp (file);
    CHECK (phe::core::readReelWav (bytes2.data(), bytes2.size(), back2, err) && s.rig.sameAs (back2), "the file was updated");
    int wavCount = 0, other = 0;
    for (auto& e : fs::directory_iterator (reels))
        (e.path().extension() == ".wav" ? wavCount : other)++;
    CHECK (wavCount == 1 && other == 0, "only the one file is in the folder (%d, %d)", wavCount, other);

    // Undoing an edit does not make the file match again (the stamp only goes up), which is the safe way round.
    s.rig.press (&phe::Params::undo);
    s.rig.runUntil ([&] { return ! s.rig.e.getReelView().busy; });
    s.lib.tick();
    CHECK (s.lib.info().edited, "after Undo the reel is still reported as edited");

    // Saving elsewhere moves the link; outside the reel folder the path is stored in full.
    const fs::path elsewhere = dir.path / "Elsewhere" / "copy.wav";
    CHECK (s.lib.saveReel (elsewhere.string(), why) && s.pump(), "save outside the reel folder");
    CHECK (s.lib.binding().path == phe::ReelLibrary::normalise (elsewhere.string()), "stored in full: %s", s.lib.binding().path.c_str());

    // Clear Reel drops the link.
    s.rig.press (&phe::Params::clearReel);
    s.lib.tick();
    CHECK (! s.lib.hasBinding() && ! s.lib.info().bound && s.lib.info().status == phe::ReelLibrary::NotBound, "an emptied reel has no file");
    CHECK (! s.lib.saveReel (file.string(), why) && contains (why, "empty"), "nothing to save: %s", why.c_str());
    CHECK (! s.lib.saveReel ("", why), "no name refused");
}

static void testLibrarySaveWhileRecordingAndStopped()
{
    TempDir dir;
    Session s;
    s.lib.setHomeFolder ((dir.path / "r").string());
    s.rig.recordFirstTake (30000, 480);
    std::string why;

    // Refused while recording.
    s.rig.p.rec = true;
    s.rig.run (2000, 480, tone);
    CHECK (! s.lib.saveReel ((dir.path / "r" / "x.wav").string(), why) && contains (why, "recording"), "save while recording: %s", why.c_str());
    CHECK (! s.lib.loadReel ((dir.path / "nothing.wav").string(), why), "load while recording refused");
    s.rig.p.rec = false;
    s.rig.run (1000, 480);

    // Logic stopped: nothing runs, the request waits and says so; it completes once audio runs.
    CHECK (s.lib.saveReel ((dir.path / "r" / "waiting.wav").string(), why), "accepted");
    for (int k = 0; k < 100; ++k)
        s.lib.tick();
    CHECK (s.lib.busy() && contains (s.lib.info().message, "waiting for audio"), "waits and says why: %s", s.lib.info().message.c_str());
    CHECK (! fs::exists (dir.path / "r" / "waiting.wav"), "nothing written yet");
    CHECK (s.pump(), "completes when audio runs");
    CHECK (fs::exists (dir.path / "r" / "waiting.wav") && s.lib.info().status == phe::ReelLibrary::Ok, "written");
}

static void testLibraryLoad()
{
    TempDir dir;
    const fs::path reels = dir.path / "Reels";

    // A 44.1 kHz 16-bit stereo file with two markers into a 48 kHz project.
    {
        Session s;
        s.lib.setHomeFolder (reels.string());
        WavOptions o;
        o.rate = 44100; o.bits = 16; o.channels = 2;
        o.cues = { { 1, 30000 }, { 2, 60000 } };
        const auto wav = makeWav (o, 88200, [] (int c, int i) { return 0.5 * std::sin (0.02 * i * (c + 1)); });
        const fs::path file = reels / "cd.wav";
        CHECK (writeFile (file, wav), "wrote the test file");

        std::string why;
        CHECK (s.lib.loadReel (file.string(), why), "load accepted: %s", why.c_str());
        CHECK (s.lib.busy() && s.lib.info().status == phe::ReelLibrary::Working, "working");
        CHECK (s.pump(), "load finished");
        const auto info = s.lib.info();
        CHECK (info.status == phe::ReelLibrary::Ok && info.bound && ! info.edited, "Ok and bound: %s", info.message.c_str());
        CHECK (s.rig.e.getReel().length() == 96000, "two seconds at 48 kHz (%d)", s.rig.e.getReel().length());
        const auto marks = s.rig.markers();
        CHECK (marks.size() == 2 && std::abs (marks[0] - 32653) <= 1 && std::abs (marks[1] - 65306) <= 1, "markers moved with the audio");
        CHECK (contains (info.message, "converted from 44100 Hz") && contains (info.message, "3 splices") && contains (info.message, "cd.wav"), "message: %s", info.message.c_str());
        CHECK (s.rig.e.getReelView().undoState == 1, "the load is one undoable step");
        CHECK (s.lib.binding().path == "cd.wav" && s.lib.binding().size == wav.size(), "linked to the file");
        double peak = 0.0;
        for (int i = 10000; i < 80000; ++i)
            peak = std::max (peak, (double) std::fabs (s.rig.e.getReel().get (0, i)));
        CHECK (peak > 0.45 && peak < 0.55, "the audio is there (peak %g)", peak);

        // Undo puts back an empty reel (and then the link goes, because the reel is empty).
        s.rig.press (&phe::Params::undo);
        s.rig.runUntil ([&] { return ! s.rig.e.getReelView().busy; });
        s.lib.tick();
        CHECK (s.rig.e.getReel().length() == 0 && ! s.lib.hasBinding(), "Undo of the load empties the reel and drops the link");
    }

    // A load replaces an existing reel and Undo gives it back.
    {
        Session s;
        s.lib.setHomeFolder (reels.string());
        s.takeWithMarkers();
        const uint64_t before = s.rig.fingerprint();
        WavOptions o;
        o.format = 3; o.bits = 32; o.channels = 1; o.rate = 48000;
        const auto wav = makeWav (o, 30000, [] (int, int i) { return 0.25 * std::sin (0.01 * i); });
        writeFile (reels / "mono.wav", wav);
        std::string why;
        CHECK (s.lib.loadReel ((reels / "mono.wav").string(), why) && s.pump(), "mono float load");
        CHECK (s.rig.e.getReel().length() == 30000 && s.rig.markers().empty(), "30000 frames, no markers");
        CHECK (s.rig.e.getReel().get (0, 5000) == s.rig.e.getReel().get (1, 5000) && std::fabs (s.rig.e.getReel().get (0, 5000) - 0.25f * std::sin (50.0f)) < 1e-5f, "mono on both sides, values right");
        CHECK (! contains (s.lib.info().message, "splices") && ! contains (s.lib.info().message, "converted"), "plain message: %s", s.lib.info().message.c_str());
        s.rig.press (&phe::Params::undo);
        s.rig.runUntil ([&] { return ! s.rig.e.getReelView().busy; });
        CHECK (s.rig.fingerprint() == before, "Undo gives back the old reel exactly");
    }

    // Too long for the reel: cut, with a note.
    {
        Session s;
        s.lib.setHomeFolder (reels.string());
        WavOptions o;
        o.rate = 8000; o.bits = 16; o.channels = 1;
        o.cues = { { 1, 100000 }, { 2, 500000 } };
        const auto wav = makeWav (o, 8000 * 200, [] (int, int i) { return 0.3 * std::sin (0.001 * i); });
        writeFile (reels / "long.wav", wav);
        std::string why;
        CHECK (s.lib.loadReel ((reels / "long.wav").string(), why) && s.pump (6000, 4096), "long load");
        CHECK (s.rig.e.getReel().length() == s.rig.e.reelCapacity(), "cut at the reel's size (%d)", s.rig.e.getReel().length());
        const auto info = s.lib.info();
        CHECK (contains (info.message, "cut to 2:54") && contains (info.message, "converted from 8000 Hz"), "message: %s", info.message.c_str());
        CHECK (s.rig.markers().size() == 2, "markers inside the reel kept");
    }

    // Markers too close together are skipped, with a note.
    {
        Session s;
        s.lib.setHomeFolder (reels.string());
        WavOptions o;
        o.cues = { { 1, 1000 }, { 2, 1020 }, { 3, 5000 } };
        writeFile (reels / "close.wav", makeWav (o, 20000, [] (int, int i) { return 0.1 * std::sin (0.02 * i); }));
        std::string why;
        CHECK (s.lib.loadReel ((reels / "close.wav").string(), why) && s.pump(), "load");
        CHECK (s.rig.markers() == std::vector<int> ({ 1000, 5000 }) && contains (s.lib.info().message, "1 marker skipped"), "message: %s", s.lib.info().message.c_str());
    }

    // Files that cannot be loaded: a sentence, the reel and the link untouched.
    {
        Session s;
        s.lib.setHomeFolder (reels.string());
        s.takeWithMarkers();
        std::string why;
        CHECK (s.lib.saveReel ((reels / "keep.wav").string(), why) && s.pump(), "saved a reel to keep");
        const uint64_t before = s.rig.fingerprint();
        const auto binding = s.lib.binding();

        writeFile (reels / "junk.wav", std::vector<uint8_t> (5000, 0x55));
        CHECK (s.lib.loadReel ((reels / "junk.wav").string(), why) && s.pump(), "junk accepted for trying");
        CHECK (s.lib.info().status == phe::ReelLibrary::Failed && contains (s.lib.info().message, "junk.wav") && contains (s.lib.info().message, "not a WAV"), "message: %s", s.lib.info().message.c_str());
        CHECK (s.rig.fingerprint() == before && s.lib.binding().hash == binding.hash && s.lib.hasBinding(), "reel and link untouched");

        WavOptions o;
        writeFile (reels / "empty.wav", makeWav (o, 0, [] (int, int) { return 0.0; }));
        CHECK (s.lib.loadReel ((reels / "empty.wav").string(), why) && s.pump() && s.lib.info().status == phe::ReelLibrary::Failed && contains (s.lib.info().message, "empty"), "empty file: %s", s.lib.info().message.c_str());
        writeFile (reels / "tiny.wav", makeWav (o, 30, [] (int, int) { return 0.1; }));
        CHECK (s.lib.loadReel ((reels / "tiny.wav").string(), why) && s.pump() && s.lib.info().status == phe::ReelLibrary::Failed && contains (s.lib.info().message, "too short"), "tiny file: %s", s.lib.info().message.c_str());

        CHECK (! s.lib.loadReel ((reels / "nothing.wav").string(), why) && contains (why, "find"), "missing file: %s", why.c_str());
        CHECK (! s.lib.loadReel ("", why), "no name refused");
        CHECK (! s.lib.loadReel (reels.string(), why), "a folder is not a file");
        CHECK (s.rig.fingerprint() == before, "still untouched");
    }
}

static void testLibraryRestore()
{
    TempDir dir;
    const fs::path reels = dir.path / "Reels";
    ReelAudio saved;
    phe::ReelLibrary::Binding binding;
    uint64_t savedPrint = 0;
    {
        Session a;
        a.lib.setHomeFolder (reels.string());
        a.takeWithMarkers (70000);
        std::string why;
        CHECK (a.lib.saveReel ((reels / "proj.wav").string(), why) && a.pump(), "saved the project's reel");
        binding = a.lib.binding();
        savedPrint = a.rig.fingerprint();
    }

    // A fresh instance (a project opening) brings the reel back.
    {
        Session b;
        b.lib.setHomeFolder (reels.string());
        b.lib.restore (true, binding);
        CHECK (b.lib.info().status == phe::ReelLibrary::Waiting && b.lib.info().bound && b.lib.busy(), "waiting to load");
        CHECK (b.pump(), "restored");
        CHECK (b.lib.info().status == phe::ReelLibrary::Ok && contains (b.lib.info().message, "loaded proj.wav"), "message: %s", b.lib.info().message.c_str());
        CHECK (b.rig.fingerprint() == savedPrint, "the reel and markers are exactly as saved");
        CHECK (! b.lib.info().edited, "not edited");
        CHECK (b.lib.binding().hash == binding.hash, "same link");

        // The host sets the same state again: the audio (with any edits) is kept, not reloaded.
        b.rig.press (&phe::Params::splice);
        const uint64_t edited = b.rig.fingerprint();
        CHECK (edited != savedPrint, "edited");
        b.lib.restore (true, binding);
        CHECK (! b.lib.busy(), "no reload");
        CHECK (b.pump() && b.rig.fingerprint() == edited, "edits kept when the same link is set again");
        b.lib.tick();
        CHECK (b.lib.info().edited, "still reported as edited");

        // A project with no file: the link goes, the audio stays.
        b.lib.restore (false, {});
        CHECK (! b.lib.hasBinding() && b.rig.fingerprint() == edited, "no file in the project: link dropped, audio kept");
    }

    // The file was moved into a sub-folder: found by its size and fingerprint.
    {
        fs::create_directories (reels / "sub" / "deeper");
        fs::rename (reels / "proj.wav", reels / "sub" / "deeper" / "moved.wav");
        Session c;
        c.lib.setHomeFolder (reels.string());
        c.lib.restore (true, binding);
        CHECK (c.pump(), "restored from a moved file");
        CHECK (c.lib.info().status == phe::ReelLibrary::Ok && c.rig.fingerprint() == savedPrint, "the moved file was found and loaded");
        CHECK (c.lib.binding().path == "sub/deeper/moved.wav", "link updated: %s", c.lib.binding().path.c_str());
        binding = c.lib.binding();
    }

    // The file has changed (saved again with different audio at the same place): loaded anyway, and it says so.
    {
        Session d;
        d.lib.setHomeFolder (reels.string());
        d.rig.recordFirstTake (50000, 480, [] (int c, long long t) { return 0.3f * (float) std::sin (0.11 * (double) t * (c + 1)); });
        std::string why;
        CHECK (d.lib.saveReel ((reels / "sub" / "deeper" / "moved.wav").string(), why) && d.pump(), "overwrote the file with other audio");
        const auto newBinding = d.lib.binding();
        CHECK (newBinding.hash != binding.hash, "different fingerprint");

        Session e;
        e.lib.setHomeFolder (reels.string());
        e.lib.restore (true, binding);                      // the old link
        CHECK (e.pump(), "restore finished");
        CHECK (e.lib.info().status == phe::ReelLibrary::Changed && contains (e.lib.info().message, "changed"), "message: %s", e.lib.info().message.c_str());
        CHECK (e.rig.e.getReel().length() == 50000, "the file as it is now was loaded");
        CHECK (e.lib.binding().hash == newBinding.hash, "link updated to the file as it is now");
        CHECK (! e.lib.info().edited, "not reported as edited");
    }

    // The file is gone: the link is kept (so the project does not lose it), the reel stays empty.
    {
        std::error_code ec;
        fs::remove (reels / "sub" / "deeper" / "moved.wav", ec);
        Session f;
        f.lib.setHomeFolder (reels.string());
        f.lib.restore (true, binding);
        CHECK (f.pump(), "restore finished");
        CHECK (f.lib.info().status == phe::ReelLibrary::Missing && contains (f.lib.info().message, "missing: moved.wav"), "message: %s", f.lib.info().message.c_str());
        CHECK (f.lib.hasBinding() && f.lib.binding().path == binding.path && f.lib.binding().hash == binding.hash, "the link is still there for the next project save");
        for (int k = 0; k < 50; ++k)
        {
            f.rig.run (256, 256);
            f.lib.tick();
        }
        CHECK (f.lib.hasBinding(), "and it survives ticking with an empty reel");
        CHECK (f.rig.e.getReel().length() == 0, "reel empty");

        // The file comes back (say, a drive is mounted): restoring again finds it.
        writeFile (reels / "moved.wav", slurp (reels / "nothing"));      // not there: reading an absent file gives an empty list
        std::error_code ec2;
        fs::remove (reels / "moved.wav", ec2);
    }
}

static void testLibraryRestoreBeforeReady()
{
    TempDir dir;
    const fs::path reels = dir.path / "Reels";
    phe::ReelLibrary::Binding binding;
    uint64_t print = 0;
    {
        Session a;
        a.lib.setHomeFolder (reels.string());
        a.takeWithMarkers (40000);
        std::string why;
        CHECK (a.lib.saveReel ((reels / "early.wav").string(), why) && a.pump(), "saved");
        binding = a.lib.binding();
        print = a.rig.fingerprint();
    }

    // The host sets the project state before it has prepared the plugin.
    Session s (48000.0, false);
    s.lib.setHomeFolder (reels.string());
    s.lib.restore (true, binding);
    for (int k = 0; k < 20; ++k)
        s.lib.tick();
    CHECK (s.lib.info().status == phe::ReelLibrary::Waiting && contains (s.lib.info().message, "early.wav"), "waits for the engine: %s", s.lib.info().message.c_str());
    s.rig.e.prepare (48000.0, 4096, 2);
    CHECK (s.pump(), "loads once the engine is ready and audio runs");
    CHECK (s.lib.info().status == phe::ReelLibrary::Ok && s.rig.fingerprint() == print, "restored exactly");

    // A restore that has not reached the audio thread can be replaced by another one (the host sets state twice).
    Session t (48000.0, true);
    t.lib.setHomeFolder (reels.string());
    t.lib.restore (true, binding);
    phe::ReelLibrary::Binding other = binding;
    other.path = "nowhere.wav"; other.size = 1; other.hash = 2;
    t.lib.restore (true, other);
    CHECK (t.pump(), "second restore ran");
    CHECK (t.lib.info().status == phe::ReelLibrary::Missing && contains (t.lib.info().message, "nowhere.wav"), "the newer link won: %s", t.lib.info().message.c_str());
}

static void testLibraryOddities()
{
    TempDir dir;
    const fs::path reels = dir.path / "Reels";

    // A folder that cannot be made (a file is in the way): a clear failure, nothing broken.
    {
        Session s;
        s.lib.setHomeFolder (reels.string());
        s.takeWithMarkers (30000);
        writeFile (dir.path / "iamafile", std::vector<uint8_t> { 1, 2, 3 });
        std::string why;
        CHECK (s.lib.saveReel ((dir.path / "iamafile" / "inside.wav").string(), why), "accepted for trying");
        CHECK (s.pump(), "finished");
        CHECK (s.lib.info().status == phe::ReelLibrary::Failed && contains (s.lib.info().message, "couldn't write"), "message: %s", s.lib.info().message.c_str());
        CHECK (! s.lib.hasBinding(), "no link made");
        CHECK (fs::exists (dir.path / "iamafile") && slurp (dir.path / "iamafile").size() == 3, "the file in the way is untouched");

        // The next save works as usual.
        CHECK (s.lib.saveReel ((reels / "fine.wav").string(), why) && s.pump() && s.lib.info().status == phe::ReelLibrary::Ok, "a later save works");
    }

    // Helpers.
    CHECK (phe::ReelLibrary::hashBytes (nullptr, 0) == 14695981039346656037ull, "hash of nothing");
    const uint8_t abc[] = { 'a', 'b', 'c' };
    CHECK (phe::ReelLibrary::hashBytes (abc, 3) == 0xe71fa2190541574bull, "known fingerprint of abc");
    CHECK (phe::ReelLibrary::normalise ("a\\b\\c") == "a/b/c", "backslashes");
    {
        Session s;
        const std::string name = s.lib.suggestedFileName();
        CHECK (name.rfind ("PhenomeEon reel ", 0) == 0 && name.size() == std::string ("PhenomeEon reel 2026-11-05 1627.wav").size() && name.substr (name.size() - 4) == ".wav", "suggested name: %s", name.c_str());
    }

    // 44.1 kHz session saves; a 48 kHz session loads it and converts.
    {
        Session a (44100.0);
        a.lib.setHomeFolder (reels.string());
        a.takeWithMarkers (44100);
        std::string why;
        CHECK (a.lib.saveReel ((reels / "cd.wav").string(), why) && a.pump(), "saved at 44.1k");
        const auto bytes = slurp (reels / "cd.wav");
        ReelAudio back;
        std::string err;
        CHECK (phe::core::readReelWav (bytes.data(), bytes.size(), back, err) && back.sampleRate == 44100.0, "the file says 44100 Hz");

        Session b (48000.0);
        b.lib.setHomeFolder (reels.string());
        CHECK (b.lib.loadReel ((reels / "cd.wav").string(), why) && b.pump(), "loaded into 48k");
        CHECK (contains (b.lib.info().message, "converted from 44100 Hz"), "message: %s", b.lib.info().message.c_str());
        CHECK (std::abs (b.rig.e.getReel().length() - (int) std::lround ((double) back.frames() * 48000.0 / 44100.0)) <= 1, "length scaled");
        CHECK (b.rig.markers().size() == back.markers.size(), "markers carried over");
    }

    // A sample-rate change after loading: the engine converts the reel itself; the link still holds.
    {
        Session a;
        a.lib.setHomeFolder (reels.string());
        a.takeWithMarkers (48000);
        std::string why;
        CHECK (a.lib.saveReel ((reels / "rate.wav").string(), why) && a.pump(), "saved");
        a.rig.e.prepare (44100.0, 4096, 2);
        a.rig.run (2048, 256);
        a.lib.tick();
        CHECK (a.lib.hasBinding() && a.rig.e.getReel().length() > 0, "reel converted, link kept");
    }
}

//==============================================================================
int main()
{
    testWavRoundTrip();
    testReadFormats();
    testTidyMarkers();
    testFitToReel();
    testExportBasic();
    testExportDoesNotDisturbSound();
    testExportWaitsForRecording();
    testExportRefusesEditsMeanwhile();
    testImportBasic();
    testImportUndoRedo();
    testImportRefusals();
    testEditStamp();
    testLongReelRoundTrip();
    testRateChangeMidJob();
    testRandomStress();
    testLibrarySave();
    testLibrarySaveWhileRecordingAndStopped();
    testLibraryLoad();
    testLibraryRestore();
    testLibraryRestoreBeforeReady();
    testLibraryOddities();

    std::printf ("reelfile tests: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
