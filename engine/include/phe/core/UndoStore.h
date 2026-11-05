// One level of undo for an editable strip of audio (milestone 8). Reusable: it knows nothing about knobs, plugins or sound.
//
// Plain language: before an edit changes any audio, the old audio is copied, a piece at a time ("chunks" of about a third of
// a second), into a second hidden strip the same size as the reel. Only pieces that are really about to change are copied,
// and each is copied once, so recording costs next to nothing extra and the second strip only fills as far as the edit reaches.
// Undo then swaps the saved pieces with the live ones. Because it is a swap, pressing Undo again puts everything back (Redo).
//
// The "state" (how long the reel is, where the splice markers are, which splice is selected) is remembered too.
//
// Only the newest edit can be undone. Starting a new edit forgets the one before it.
//
// Plain C++17. No JUCE.

#pragma once

#include "SpliceMap.h"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

namespace phe::core
{
    // Everything about a reel except its audio.
    struct EditState
    {
        int length = 0;                           // samples recorded
        int selected = 0;                         // the selected splice (0-based)
        int numMarkers = 0;
        int markers[kMaxSpliceMarkers] = {};      // splice markers (samples)
    };

    class UndoStore
    {
    public:
        static constexpr int kChunkShift  = 14;
        static constexpr int kChunkFrames = 1 << kChunkShift;      // 16384 samples

        // Gets the memory ready for a reel of 'capacityFrames' samples (not from the audio thread). Forgets all history.
        // The memory is only really provided by the system as it is first written to. Returns false if memory ran out.
        bool prepare (int capacityFrames)
        {
            forget();
            data[0].reset();
            data[1].reset();
            chunks = 0;
            capacity = 0;
            if (capacityFrames <= 0)
                return false;

            Buffer a ((float*) std::calloc ((size_t) capacityFrames, sizeof (float)));
            Buffer b ((float*) std::calloc ((size_t) capacityFrames, sizeof (float)));
            if (a == nullptr || b == nullptr)
                return false;

            data[0] = std::move (a);
            data[1] = std::move (b);
            capacity = capacityFrames;
            chunks = (capacityFrames + kChunkFrames - 1) / kChunkFrames;
            saved.assign ((size_t) chunks, 0);
            return true;
        }

        bool ready() const { return chunks > 0; }
        int numChunks() const { return chunks; }

        // ---- the history ----
        bool hasStep() const { return stepOpen; }        // there is something to undo (or redo)
        bool undone() const { return isUndone; }         // the step has been undone: the next press redoes it

        // A new edit is about to change things: forget the old step; 'before' is how things are now.
        void begin (const EditState& before)
        {
            if (! ready())
                return;
            std::fill (saved.begin(), saved.end(), (uint8_t) 0);
            lo = INT_MAX;
            hi = -1;
            otherSide = before;
            isUndone = false;
            stepOpen = true;
        }

        // The edit turned out to change nothing: there is nothing to undo.
        void cancel() { stepOpen = false; isUndone = false; }

        void forget()
        {
            stepOpen = false;
            isUndone = false;
            std::fill (saved.begin(), saved.end(), (uint8_t) 0);
            lo = INT_MAX;
            hi = -1;
        }

        // The state on the other side of the step: what Undo will bring back (or, after an Undo, what Redo will).
        const EditState& other() const { return otherSide; }

        // After an Undo or Redo: 'current' is how things were just before it; it becomes the new other side.
        void flip (const EditState& current)
        {
            otherSide = current;
            isUndone = ! isUndone;
        }

        // ---- the audio ----
        // Copies chunk 'c' of the reel into the hidden strip, if the step has not copied it yet. 'reel' = left, right.
        void save (float* const* reel, int c)
        {
            if (! stepOpen || c < 0 || c >= chunks || saved[(size_t) c] != 0)
                return;
            const size_t from = (size_t) c * kChunkFrames;
            const size_t n = framesIn (c);
            std::memcpy (data[0].get() + from, reel[0] + from, n * sizeof (float));
            std::memcpy (data[1].get() + from, reel[1] + from, n * sizeof (float));
            saved[(size_t) c] = 1;
            lo = std::min (lo, c);
            hi = std::max (hi, c);
        }

        // Saves every chunk touched by the samples [from, to).
        void saveRange (float* const* reel, int from, int to)
        {
            if (to <= from)
                return;
            const int first = std::max (0, from) >> kChunkShift;
            const int last = std::min (capacity - 1, to - 1) >> kChunkShift;
            for (int c = first; c <= last; ++c)
                save (reel, c);
        }

        bool isSaved (int c) const { return c >= 0 && c < chunks && saved[(size_t) c] != 0; }
        int firstSaved() const { return hi < 0 ? -1 : lo; }
        int lastSaved() const { return hi; }

        // Swaps chunk 'c' of the reel with the hidden strip (does nothing if the step never saved it).
        void swap (float* const* reel, int c)
        {
            if (! isSaved (c))
                return;
            const size_t from = (size_t) c * kChunkFrames;
            const size_t n = framesIn (c);
            for (int ch = 0; ch < 2; ++ch)
            {
                float* a = reel[ch] + from;
                float* b = data[ch].get() + from;
                for (size_t i = 0; i < n; ++i)
                    std::swap (a[i], b[i]);
            }
        }

    private:
        size_t framesIn (int c) const { return (size_t) std::min (kChunkFrames, capacity - c * kChunkFrames); }

        struct FreeDeleter { void operator() (float* p) const { std::free (p); } };
        using Buffer = std::unique_ptr<float, FreeDeleter>;

        Buffer data[2];
        std::vector<uint8_t> saved;
        int capacity = 0;
        int chunks = 0;
        int lo = INT_MAX, hi = -1;
        bool stepOpen = false;
        bool isUndone = false;
        EditState otherSide;
    };
}
