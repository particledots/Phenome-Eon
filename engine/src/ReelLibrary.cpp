#include <phe/ReelLibrary.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace phe
{

namespace
{
    constexpr long long kMaxFileBytes = 300ll * 1024 * 1024;
    constexpr int kWaitMessageTicks = 40;          // about two seconds
    constexpr int kMaxAttempts = 60;

    std::string lowerExt (const fs::path& p)
    {
        std::string e = p.extension().string();
        std::transform (e.begin(), e.end(), e.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
        return e;
    }

    std::string nameOf (const std::string& p) { return fs::path (p).filename().string(); }

    std::string minutesSeconds (double seconds)
    {
        const int total = (int) std::lround (seconds);
        char buf[32];
        std::snprintf (buf, sizeof buf, "%d:%02d", total / 60, total % 60);
        return buf;
    }

    std::string join (const std::string& a, const std::string& b) { return a.empty() ? b : a + ", " + b; }
}

ReelLibrary::ReelLibrary (Engine& e) : engine (e) {}

uint64_t ReelLibrary::hashBytes (const uint8_t* d, size_t n)
{
    uint64_t h = 14695981039346656037ull;
    for (size_t i = 0; i < n; ++i)
    {
        h ^= d[i];
        h *= 1099511628211ull;
    }
    return h;
}

std::string ReelLibrary::normalise (const std::string& p)
{
    std::string s = p;
    std::replace (s.begin(), s.end(), '\\', '/');
    return s;
}

void ReelLibrary::setHomeFolder (const std::string& p) { std::lock_guard<std::mutex> g (m); home = p; }
std::string ReelLibrary::homeFolder() const { std::lock_guard<std::mutex> g (m); return home; }

std::string ReelLibrary::toStored (const std::string& absolute) const
{
    if (! home.empty())
    {
        const fs::path rel = fs::path (absolute).lexically_relative (fs::path (home));
        const std::string r = rel.generic_string();
        if (! r.empty() && r.rfind ("..", 0) != 0 && ! rel.is_absolute())
            return r;
    }
    return normalise (absolute);
}

std::string ReelLibrary::toAbsolute (const std::string& stored) const
{
    fs::path p (stored);
    if (p.is_absolute())
        return p.string();
    if (home.empty())
        return {};
    return (fs::path (home) / p).string();
}

bool ReelLibrary::readFile (const std::string& path, std::vector<uint8_t>& out)
{
    std::ifstream f (path, std::ios::binary);
    if (! f)
        return false;
    f.seekg (0, std::ios::end);
    const std::streamoff n = f.tellg();
    if (n < 0 || n > kMaxFileBytes)
        return false;
    f.seekg (0);
    out.resize ((size_t) n);
    if (n > 0)
        f.read (reinterpret_cast<char*> (out.data()), n);
    return (bool) f || f.eof();
}

// 1. the stored path, if it holds the same file; 2. any .wav under the reel folder with the same size and fingerprint;
// 3. the stored path, if there is a file there that has changed; otherwise not found. 'bytes' is left holding the file chosen.
bool ReelLibrary::findFile (const Binding& want, std::vector<uint8_t>& bytes, std::string& chosen, uint64_t& hash, uint64_t& size, bool& changed) const
{
    changed = false;
    std::error_code ec;
    const std::string stored = toAbsolute (want.path);
    const bool storedExists = ! stored.empty() && fs::is_regular_file (stored, ec);
    if (storedExists && readFile (stored, bytes))
    {
        if (bytes.size() == want.size && hashBytes (bytes.data(), bytes.size()) == want.hash)
        {
            chosen = stored; hash = want.hash; size = want.size;
            return true;
        }
    }

    if (! home.empty() && fs::is_directory (home, ec))
    {
        size_t visited = 0;
        for (fs::recursive_directory_iterator it (home, fs::directory_options::skip_permission_denied, ec), end; ! ec && it != end; it.increment (ec))
        {
            if (++visited > 50000)
                break;
            std::error_code e2;
            if (! it->is_regular_file (e2) || lowerExt (it->path()) != ".wav")
                continue;
            if (it->file_size (e2) != want.size || e2)
                continue;
            if (readFile (it->path().string(), bytes) && hashBytes (bytes.data(), bytes.size()) == want.hash)
            {
                chosen = it->path().string(); hash = want.hash; size = want.size;
                return true;
            }
        }
    }

    if (storedExists && readFile (stored, bytes))
    {
        chosen = stored; hash = hashBytes (bytes.data(), bytes.size()); size = bytes.size(); changed = true;
        return true;
    }
    return false;
}

void ReelLibrary::bindTo (const std::string& absoluteFile, uint64_t hash, uint64_t size, uint32_t stamp, Status st, const std::string& msg)
{
    bound = true;
    b.path = toStored (absoluteFile);
    b.hash = hash;
    b.size = size;
    savedStamp = stamp;
    status = st;
    message = msg;
}

//==============================================================================
bool ReelLibrary::saveReel (const std::string& file, std::string& whyNot)
{
    std::lock_guard<std::mutex> g (m);
    if (file.empty()) { whyNot = "Choose a file name first."; return false; }
    if (job) { whyNot = "Still busy with the last file - try again in a moment."; return false; }
    if (engine.getReelView().recording) { whyNot = "A recording is running - stop it first (Rec), then save."; return false; }
    if (engine.reelLength() <= 0) { whyNot = "The reel is empty - there is nothing to save."; return false; }

    Job j;
    j.type = Job::Save;
    j.path = file;
    status = Working;
    message = "saving " + nameOf (file) + "...";
    job = std::move (j);
    return true;
}

bool ReelLibrary::loadReel (const std::string& file, std::string& whyNot)
{
    std::lock_guard<std::mutex> g (m);
    if (job) { whyNot = "Still busy with the last file - try again in a moment."; return false; }
    if (engine.getReelView().recording) { whyNot = "A recording is running - stop it first (Rec), then load."; return false; }

    std::error_code ec;
    if (file.empty() || ! fs::is_regular_file (file, ec)) { whyNot = "Couldn't find that file."; return false; }
    const auto bytes = fs::file_size (file, ec);
    if (ec || (long long) bytes > kMaxFileBytes) { whyNot = "Couldn't read that file (or it is over 300 MB)."; return false; }

    Job j;
    j.type = Job::Load;
    j.path = file;
    j.chosen = file;
    status = Working;
    message = "loading " + nameOf (file) + "...";
    job = std::move (j);
    return true;
}

void ReelLibrary::unbind()
{
    std::lock_guard<std::mutex> g (m);
    bound = false;
    status = NotBound;
    message.clear();
    b = Binding();
    if (job && job->restore && job->stage <= 3)
        job.reset();
}

bool ReelLibrary::hasBinding() const { std::lock_guard<std::mutex> g (m); return bound; }
ReelLibrary::Binding ReelLibrary::binding() const { std::lock_guard<std::mutex> g (m); return b; }

void ReelLibrary::restore (bool has, const Binding& want)
{
    std::lock_guard<std::mutex> g (m);

    // A restore that has not handed anything to the audio thread yet can be replaced; a user's own save or load is left alone.
    auto cancelWaitingRestore = [this]
    {
        if (job && job->restore && job->stage <= 3)
            job.reset();
    };

    if (! has || want.path.empty())
    {
        cancelWaitingRestore();
        if (! job)
        {
            bound = false;
            status = NotBound;
            message.clear();
            b = Binding();
        }
        return;
    }

    if (bound && b.size == want.size && b.hash == want.hash && engine.reelLength() > 0 && ! job)
    {
        b.path = want.path;                 // already there (the host set the same state again): keep the audio, edits and all
        return;
    }

    cancelWaitingRestore();
    if (job)
        return;

    bound = true;
    b = want;
    status = Waiting;
    message = "waiting to load " + nameOf (want.path);
    Job j;
    j.type = Job::Load;
    j.restore = true;
    j.want = want;
    job = std::move (j);
}

ReelLibrary::Info ReelLibrary::info() const
{
    std::lock_guard<std::mutex> g (m);
    Info r;
    r.bound = bound;
    r.status = status;
    r.message = message;
    r.busy = job.has_value();
    r.fileName = bound ? nameOf (b.path) : std::string();
    r.fullPath = bound ? toAbsolute (b.path) : std::string();
    r.edited = bound && ! job && (status == Ok || status == Changed) && engine.editStamp() != savedStamp;
    r.unsaved = ! bound && engine.reelLength() > 0;
    return r;
}

bool ReelLibrary::busy() const { std::lock_guard<std::mutex> g (m); return job.has_value(); }

std::string ReelLibrary::suggestedFileName() const
{
    const std::time_t t = std::time (nullptr);
    std::tm tmv {};
    localtime_r (&t, &tmv);
    char buf[96];
    std::snprintf (buf, sizeof buf, "PhenomeEon reel %04d-%02d-%02d %02d%02d.wav", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min);
    return buf;
}

//==============================================================================
// One step of the job. Returns true when it is finished (done, failed or given up).
bool ReelLibrary::stepJob (Job& j)
{
    using IoResult = Engine::IoResult;

    //---------------------------------------------------------------- saving
    if (j.type == Job::Save)
    {
        if (j.stage == 0)
        {
            if (engine.getReelView().recording)
            {
                status = Failed;
                message = "a recording started - nothing saved";
                return true;
            }
            if (! engine.requestExport())
                return false;                       // try again next tick
            j.stage = 1;
            j.waitTicks = 0;
            return false;
        }

        core::ReelAudio audio;
        uint32_t stamp = 0;
        const IoResult r = engine.pollExport (audio, stamp);
        if (r == IoResult::none)
        {
            if (++j.waitTicks == kWaitMessageTicks)
                message = "waiting for audio to run (press Play in Logic)...";
            return false;
        }
        if (r == IoResult::empty)
        {
            status = bound ? status : NotBound;
            message = "the reel is empty - nothing saved";
            return true;
        }
        if (r == IoResult::rejected)
        {
            if (++j.attempts > kMaxAttempts) { status = Failed; message = "couldn't copy the reel - nothing saved"; return true; }
            j.stage = 0;
            return false;
        }

        const std::vector<uint8_t> bytes = core::writeReelWav (audio);
        std::error_code ec;
        const fs::path dest (j.path);
        if (dest.has_parent_path())
            fs::create_directories (dest.parent_path(), ec);
        fs::path tmp = dest;
        tmp += ".phe-tmp";                           // written under another name first, so a failure never damages an older file
        bool ok = false;
        {
            std::ofstream f (tmp, std::ios::binary | std::ios::trunc);
            if (f)
                f.write (reinterpret_cast<const char*> (bytes.data()), (std::streamsize) bytes.size());
            ok = f && f.good();
        }
        if (ok)
        {
            fs::rename (tmp, dest, ec);
            ok = ! ec;
        }
        if (! ok)
        {
            std::error_code e2;
            fs::remove (tmp, e2);
            status = Failed;
            message = "couldn't write " + nameOf (j.path) + " (is the folder writable?)";
            return true;
        }
        bindTo (j.path, hashBytes (bytes.data(), bytes.size()), bytes.size(), stamp, Ok, "saved " + nameOf (j.path));
        return true;
    }

    //---------------------------------------------------------------- loading
    if (j.stage == 0)
    {
        j.stage = 1;                                 // one tick to let the window show "loading..." before the heavy part
        return false;
    }

    if (j.stage == 1)
    {
        if (engine.reelSampleRate() <= 0.0 || engine.reelCapacity() <= 0)
        {
            status = Waiting;
            message = "waiting to load " + nameOf (j.restore ? j.want.path : j.chosen);
            return false;                            // the engine has not been prepared yet
        }

        if (j.restore)
        {
            bool changed = false;
            if (! findFile (j.want, j.bytes, j.chosen, j.hash, j.size, changed))
            {
                status = Missing;
                message = "missing: " + nameOf (j.want.path) + (home.empty() ? "" : " (not found in the reel folder either)");
                return true;                         // the link is kept, so the project does not lose it
            }
            j.changed = changed;
        }
        else
        {
            if (! readFile (j.chosen, j.bytes))
            {
                status = Failed;
                message = "couldn't read " + nameOf (j.chosen);
                return true;
            }
            j.hash = hashBytes (j.bytes.data(), j.bytes.size());
            j.size = j.bytes.size();
        }
        j.stage = 2;
        return false;
    }

    if (j.stage == 2)
    {
        core::ReelAudio audio;
        core::ReelFileInfo fileInfo;
        std::string err;
        if (! core::readReelWav (j.bytes.data(), j.bytes.size(), audio, err, &fileInfo))
        {
            status = Failed;
            message = nameOf (j.chosen) + ": " + err;
            return true;
        }
        core::FitResult fit;
        if (! core::fitToReel (audio, engine.reelSampleRate(), engine.reelCapacity(), fit, err))
        {
            status = Failed;
            message = nameOf (j.chosen) + ": " + err;
            return true;
        }

        j.note.clear();
        if (fit.converted)
            j.note = join (j.note, "converted from " + std::to_string ((int) std::lround (fit.fromRate)) + " Hz");
        if (fit.cut)
            j.note = join (j.note, "cut to " + minutesSeconds ((double) engine.reelCapacity() / engine.reelSampleRate()));
        if (fit.markersDropped > 0)
            j.note = join (j.note, std::to_string (fit.markersDropped) + (fit.markersDropped == 1 ? " marker" : " markers") + " skipped (too close together or too many)");
        j.splices = (int) audio.markers.size() + 1;
        j.audio = std::move (audio);
        j.stage = 3;
        return false;
    }

    if (j.stage == 3)
    {
        std::string why;
        core::ReelAudio copy = std::move (j.audio);
        if (engine.submitImport (std::move (copy), why))
        {
            j.stage = 4;
            j.waitTicks = 0;
            return false;
        }
        // submitImport only moves the audio when it accepts it, so a refusal leaves 'copy' as it was.
        j.audio = std::move (copy);
        if (why.rfind ("Does not fit", 0) == 0)
        {
            if (++j.attempts > kMaxAttempts) { status = Failed; message = "couldn't load " + nameOf (j.chosen); return true; }
            j.audio = core::ReelAudio();
            j.stage = 2;                             // the sample rate changed: fit the file to the new rate
            return false;
        }
        if (why.rfind ("A recording", 0) == 0)
        {
            status = Waiting;
            message = "waiting for the recording to stop, then loading " + nameOf (j.chosen);
            return false;
        }
        if (why.rfind ("Still busy", 0) == 0 || why.rfind ("Not ready", 0) == 0)
        {
            status = Waiting;
            message = "waiting to load " + nameOf (j.chosen);
            return false;
        }
        status = Failed;
        message = why;
        return true;
    }

    // stage 4: the audio thread is writing the reel
    uint32_t stamp = 0;
    const IoResult r = engine.pollImport (stamp);
    if (r == IoResult::none)
    {
        if (++j.waitTicks == kWaitMessageTicks)
            message = "waiting for audio to run (press Play in Logic)...";
        return false;
    }
    if (r == IoResult::rejected)
    {
        if (++j.attempts > kMaxAttempts) { status = Failed; message = "couldn't load " + nameOf (j.chosen); return true; }
        j.stage = 2;
        return false;
    }

    std::string text = "loaded " + nameOf (j.chosen);
    if (j.splices > 1)
        text += ": " + std::to_string (j.splices) + " splices";
    if (j.changed)
        text += " (the file has changed since the project saved it)";
    if (! j.note.empty())
        text += " (" + j.note + ")";
    bindTo (j.chosen, j.hash, j.size, stamp, j.changed ? Changed : Ok, text);
    return true;
}

void ReelLibrary::tick()
{
    std::lock_guard<std::mutex> g (m);

    if (job)
    {
        if (stepJob (*job))
            job.reset();
        return;
    }

    // A reel that has been emptied no longer has a file: drop the link (a missing or failed link is kept, so a project that
    // could not find its file yet does not lose it).
    if (bound && (status == Ok || status == Changed) && engine.reelLength() == 0 && ! engine.getReelView().busy)
    {
        bound = false;
        status = NotBound;
        message.clear();
        b = Binding();
    }
}

} // namespace phe
