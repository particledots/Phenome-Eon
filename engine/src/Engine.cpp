#include <phe/Engine.h>

namespace phe
{

void Engine::prepare (double newSampleRate, int maxBlockSize, int numChannels)
{
    sampleRate   = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    maxBlock     = maxBlockSize;
    channelCount = numChannels;
    reset();
}

void Engine::reset()
{
    // Nothing is remembered yet (the reel comes in milestone 4).
}

void Engine::process (float* const* channels, int numChannels, int numSamples)
{
    // Milestone 2: straight through. The audio is already in the buffers, so there is nothing to do.
    // (The arguments are kept so the real engine can use them later without changing the wrapper.)
    (void) channels;
    (void) numChannels;
    (void) numSamples;
}

} // namespace phe
