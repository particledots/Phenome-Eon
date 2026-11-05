// The PhenomeEon audio engine.
//
// Milestone 2: an empty engine. Audio passes straight through, untouched. The control values are
// accepted and stored but do not change the sound yet. The reel, record, playback head and genes
// arrive in milestones 4 to 8.
//
// Plain C++17. No JUCE, no plugin code.

#pragma once

#include "Params.h"

namespace phe
{
    class Engine
    {
    public:
        // Called when Logic starts or restarts playback or changes the sample rate or block size.
        void prepare (double sampleRate, int maxBlockSize, int numChannels);

        // Forget anything remembered about the audio so far.
        void reset();

        // The latest control values. Called by the wrapper at the start of every block.
        void setParams (const Params& p) { params = p; }
        const Params& getParams() const { return params; }

        // Process one block in place: 'channels' holds numChannels pointers to numSamples floats each.
        // The sound that comes in is replaced by the sound that goes out.
        void process (float* const* channels, int numChannels, int numSamples);

        double getSampleRate() const { return sampleRate; }

    private:
        Params params;
        double sampleRate = 48000.0;
        int maxBlock = 0;
        int channelCount = 2;
    };
}
