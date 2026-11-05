// Reel files (milestone 9). Reusable: plain C++, no JUCE, no plugin code, so it is tested on its own with real bytes.
//
// Plain language: a "reel file" is an ordinary WAV file holding the whole reel, with the splice markers stored inside it as
// standard WAV markers (the "cue" list that Reaper, Audacity and other programs also read and write). That is the same idea the
// Morphagene module uses for its reel files, so a reel saved here can be opened in an audio editor with its markers showing,
// and a WAV made in an editor (with or without markers) can be loaded as a reel.
//
//  * Written: 32-bit float, stereo, at the reel's sample rate (at 48 kHz this is exactly what the module reads), markers as a
//    "cue " list after the audio. Not tested on the real module (none here); the layout follows the public WAV standard.
//  * Read: 8 / 16 / 24 / 32-bit whole-number PCM or 32 / 64-bit float, any sample rate, mono (copied to both sides), stereo or
//    more channels (the first two are kept). Markers are taken from the "cue " list when there is one.
//  * fitToReel() then makes what was read fit the reel: converts it to the project's sample rate (markers move with it), cuts
//    it at the reel's size (2.9 minutes) and tidies the markers (rising order, no splice shorter than the engine allows,
//    at most 299 markers).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace phe::core
{
    // One reel's worth of audio, as plain lists of numbers. 'markers' are splice markers in frames from the start (rising).
    struct ReelAudio
    {
        double sampleRate = 0.0;
        std::vector<float> ch[2];        // same length, -1..+1
        std::vector<int> markers;

        size_t frames() const { return ch[0].size(); }
    };

    // What was found in a file (for messages).
    struct ReelFileInfo
    {
        int sourceChannels = 0;
        int bits = 0;
        bool isFloat = false;
        int cuePoints = 0;               // marker entries found in the file (before tidying)
    };

    // The bytes of a reel file: 32-bit float stereo WAV with the markers as a cue list.
    std::vector<uint8_t> writeReelWav (const ReelAudio& audio);

    // Reads WAV bytes. On failure returns false and 'error' is a sentence for the user. The markers come out in rising order
    // without duplicates, only those strictly inside the audio; fitToReel() does the rest of the tidying.
    bool readReelWav (const uint8_t* data, size_t size, ReelAudio& out, std::string& error, ReelFileInfo* info = nullptr);

    // Converts audio from one sample rate to another (offline, whole signal at once). Same length in seconds.
    std::vector<float> resampleOffline (const std::vector<float>& in, double inRate, double outRate);

    struct FitResult
    {
        bool converted = false;          // the sample rate was changed
        double fromRate = 0.0;
        bool cut = false;                // the file was longer than the reel and was cut
        int markersDropped = 0;          // markers that were too close together, too close to an end, or too many
    };

    // Makes 'audio' fit a reel of 'capacityFrames' at 'reelRate'. False (with a sentence) if the audio is unusable.
    bool fitToReel (ReelAudio& audio, double reelRate, int capacityFrames, FitResult& result, std::string& error);

    // Puts markers in rising order and drops any that would make a splice shorter than the engine allows (or that are outside
    // the audio, or more than 299). Returns how many were dropped.
    int tidyMarkers (std::vector<int>& markers, int length);
}
