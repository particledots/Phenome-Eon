// Ties the reel to a WAV file ("file link"), so a Logic project can bring its reel back without storing any audio.
// The same idea as the file library in Ceo (which links six layers); here there is one reel, so one link.
// No JUCE (std::filesystem), so it is tested on Linux with real files. Every call is made from the window / main thread (or, under
// the mutex, from the host's state calls); the audio thread never touches it or any file: the audio goes through the engine's
// export / import hand-over (see Engine.h).
//
//  * A reel that has been saved to / loaded from a file is "linked" to it: the library remembers the path (relative to the reel
//    folder when the file is inside it), the file size and a fingerprint (hash of the file's bytes).
//  * The link is what a project stores (binding() / restore()). On restore the reel is loaded from the file. If the file is not
//    where it was, the reel folder and its sub-folders are searched for a file with the same size and fingerprint. If a file is
//    still at the stored path but has changed, it is loaded anyway and the status says so.
//  * Editing the reel (recording, markers, Erase Splice ...) keeps the link, and info().edited says the reel no longer matches
//    its file ("edited since saved"). Opening the project again brings back the file as it was last saved. Emptying the reel
//    (Clear Reel, Erase Splice on the only splice) drops the link.
//  * Save and Load work while Logic is processing audio; a request waits until then.
#pragma once

#include "Engine.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace phe
{
    class ReelLibrary
    {
    public:
        enum Status { NotBound = 0, Waiting, Working, Ok, Changed, Missing, Failed };

        struct Binding
        {
            std::string path;            // relative to the reel folder if inside it, else absolute
            uint64_t size = 0;
            uint64_t hash = 0;
        };

        struct Info
        {
            bool bound = false;
            bool unsaved = false;        // the reel holds audio that no file has
            bool edited = false;         // linked, but the reel was changed since the file was saved or loaded
            bool busy = false;           // a save or load is in progress
            Status status = NotBound;
            std::string fileName;        // just the name of the linked file
            std::string fullPath;        // where that file is now (absolute)
            std::string message;         // a short sentence for the status line ("saved reel.wav", "missing: ...")
        };

        explicit ReelLibrary (Engine& e);

        // The reel folder: where Save starts and where moved files are looked for.
        void setHomeFolder (const std::string& absolutePath);
        std::string homeFolder() const;

        // Save the reel to a WAV file (absolute path; the folder is made if needed). Finished by tick().
        bool saveReel (const std::string& file, std::string& whyNot);
        // Load a WAV file as the reel (one undoable edit) and link the reel to it. Finished by tick().
        bool loadReel (const std::string& file, std::string& whyNot);

        // Forget the link (the audio stays).
        void unbind();

        // What a project stores, and bringing it back. restore (false, ...) means "this project has no file": the link is dropped
        // and the audio is left alone. restore (true, b) loads the reel from the file (unless this reel is already linked to
        // that very file and holds audio, which is kept as it is, edits and all).
        bool hasBinding() const;
        Binding binding() const;
        void restore (bool has, const Binding& b);

        void tick();                     // about 20 times a second
        Info info() const;
        bool busy() const;
        std::string suggestedFileName() const;     // "PhenomeEon reel 2026-11-05 1627.wav"

        // helpers (public for tests)
        static uint64_t hashBytes (const uint8_t* data, size_t n);
        static std::string normalise (const std::string& p);

    private:
        struct Job
        {
            enum Type { Save, Load } type = Load;
            std::string path;            // Save: destination.  Load: the file chosen by the user (absolute); unused for a restore
            bool restore = false;
            Binding want;                // restore: what to find
            int stage = 0;
            int attempts = 0;
            int waitTicks = 0;
            std::vector<uint8_t> bytes;  // Load: the file as read (kept until the load is done, in case it has to be fitted again)
            core::ReelAudio audio;       // Load: the fitted audio waiting to be handed over
            std::string chosen;          // the file actually used
            uint64_t hash = 0, size = 0;
            bool changed = false;
            std::string note;            // "converted from 44100 Hz, cut to 2:54"
            int splices = 1;
        };

        bool stepJob (Job& j);           // true when finished (done, failed or given up)
        std::string toStored (const std::string& absolute) const;
        std::string toAbsolute (const std::string& stored) const;
        bool findFile (const Binding& want, std::vector<uint8_t>& bytes, std::string& chosen, uint64_t& hash, uint64_t& size, bool& changed) const;
        static bool readFile (const std::string& path, std::vector<uint8_t>& out);
        void bindTo (const std::string& absoluteFile, uint64_t hash, uint64_t size, uint32_t stamp, Status st, const std::string& msg);

        Engine& engine;
        mutable std::mutex m;
        std::string home;

        bool bound = false;
        Binding b;
        uint32_t savedStamp = 0;         // the engine's editStamp when the file was saved or loaded
        Status status = NotBound;
        std::string message;
        std::optional<Job> job;
    };
}
