// Splice markers and splice selection (milestone 7). Reusable: it knows nothing about knobs, plugins or sound.
//
// Plain language: a reel is one long strip of tape. A "splice marker" is a cut mark on it. Markers cut the reel into
// splices: no markers = one splice (the whole reel); one marker = two splices; and so on, up to 299 markers (300 splices).
// Splice 0 starts at the start of the reel and the last splice ends where the recorded part of the reel ends.
//
// "Selecting" a splice is a separate job: Organize (a knob that cuts its travel into equal zones, one per splice, no
// matter how long each splice is) and Shift (steps on to the next splice, back to the first after the last) say which splice
// should play next. Organize wins over Shift: Shift moves on from wherever Organize last put you, and the moment the Organize
// knob moves into a different zone it takes over again.
//
// Plain C++17. No JUCE.

#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>

namespace phe::core
{
    inline constexpr int kMaxSpliceMarkers = 299;
    inline constexpr int kMaxSplices       = kMaxSpliceMarkers + 1;
    inline constexpr int kMinSpliceSamples = 64;       // a splice shorter than this is refused (it would be a click)

    //==========================================================================
    // The list of markers. The audio side changes it; the window reads a consistent copy with snapshot().
    class SpliceMap
    {
    public:
        SpliceMap() { clear(); }

        void clear()
        {
            beginWrite();
            count.store (0, std::memory_order_relaxed);
            endWrite();
        }

        int numMarkers() const { return count.load (std::memory_order_relaxed); }
        int numSplices() const { return numMarkers() + 1; }

        // Marker i sits between splice i and splice i+1 (a position in samples from the start of the reel).
        int marker (int i) const { return markers[i].load (std::memory_order_relaxed); }

        // Where splice i starts and ends. 'totalLength' is the length of the recorded part of the reel.
        int start (int splice) const { return splice <= 0 ? 0 : marker (std::min (splice, numMarkers()) - 1); }
        int end (int splice, int totalLength) const
        {
            return splice >= numMarkers() ? totalLength : marker (std::max (0, splice));
        }
        int length (int splice, int totalLength) const { return end (splice, totalLength) - start (splice); }

        // The splice that contains a position (clamped into the reel).
        int indexAt (int position, int totalLength) const
        {
            if (position < 0 || totalLength <= 0)
                return 0;
            int i = 0;
            const int n = numMarkers();
            while (i < n && marker (i) <= position)
                ++i;
            return i;
        }

        // Cuts the splice that contains 'position' in two. Returns the index of the new marker (so the old splice j is now
        // splice j on the left and splice j+1 on the right), or -1 if refused: no room for 299+ markers, or either piece
        // would be shorter than kMinSpliceSamples.
        int addMarker (int position, int totalLength)
        {
            const int n = numMarkers();
            if (n >= kMaxSpliceMarkers || totalLength <= 0 || position < 0 || position >= totalLength)
                return -1;

            const int j = indexAt (position, totalLength);
            if (position - start (j) < kMinSpliceSamples || end (j, totalLength) - position < kMinSpliceSamples)
                return -1;

            beginWrite();
            for (int i = n; i > j; --i)
                markers[i].store (markers[i - 1].load (std::memory_order_relaxed), std::memory_order_relaxed);
            markers[j].store (position, std::memory_order_relaxed);
            count.store (n + 1, std::memory_order_relaxed);
            endWrite();
            return j;
        }

        // Takes marker i away: splices i and i+1 become one. Returns false if there is no such marker.
        bool removeMarker (int i)
        {
            const int n = numMarkers();
            if (i < 0 || i >= n)
                return false;
            beginWrite();
            for (int k = i; k < n - 1; ++k)
                markers[k].store (markers[k + 1].load (std::memory_order_relaxed), std::memory_order_relaxed);
            count.store (n - 1, std::memory_order_relaxed);
            endWrite();
            return true;
        }

        // Takes splice j out of the reel altogether (its audio is removed and everything after it moves up by its length).
        // Splice j's closing marker goes (or, for the last splice, the marker in front of it) and every later marker moves
        // earlier by the splice's length. 'totalLength' is the length of the reel before the removal. Returns false if
        // there is only one splice (nothing to join) or no such splice.
        bool eraseSplice (int j, int totalLength)
        {
            const int n = numMarkers();
            if (n <= 0 || j < 0 || j > n)
                return false;

            const int len = length (j, totalLength);
            beginWrite();
            if (j < n)
            {
                for (int k = j; k < n - 1; ++k)
                    markers[k].store (markers[k + 1].load (std::memory_order_relaxed) - len, std::memory_order_relaxed);
                count.store (n - 1, std::memory_order_relaxed);
            }
            else
                count.store (n - 1, std::memory_order_relaxed);       // the last splice: the marker in front of it goes
            endWrite();
            return true;
        }

        // Replaces every marker (used by Undo). Markers must be in rising order.
        void setMarkers (const int* in, int n)
        {
            n = std::max (0, std::min (n, kMaxSpliceMarkers));
            beginWrite();
            for (int i = 0; i < n; ++i)
                markers[i].store (in[i], std::memory_order_relaxed);
            count.store (n, std::memory_order_relaxed);
            endWrite();
        }

        // The reel was converted to another sample rate: moves every marker by the same ratio, and drops any that no
        // longer fit (too close to a neighbour or to the end).
        void rescale (double ratio, int totalLength)
        {
            int kept[kMaxSpliceMarkers];
            int n = 0, previous = 0;
            for (int i = 0; i < numMarkers(); ++i)
            {
                const int m = (int) std::llround ((double) marker (i) * ratio);
                if (m - previous >= kMinSpliceSamples && totalLength - m >= kMinSpliceSamples)
                {
                    kept[n++] = m;
                    previous = m;
                }
            }
            beginWrite();
            for (int i = 0; i < n; ++i)
                markers[i].store (kept[i], std::memory_order_relaxed);
            count.store (n, std::memory_order_relaxed);
            endWrite();
        }

        // For the window (any thread): copies up to maxCount markers into 'out' and returns how many, retrying if the
        // audio side was in the middle of a change, so the copy is always a real list that existed at one moment.
        int snapshot (int* out, int maxCount) const
        {
            for (int attempt = 0; attempt < 8; ++attempt)
            {
                const unsigned before = version.load (std::memory_order_acquire);
                if ((before & 1u) != 0u)
                    continue;
                const int n = std::min (maxCount, std::min (kMaxSpliceMarkers, count.load (std::memory_order_relaxed)));
                for (int i = 0; i < n; ++i)
                    out[i] = markers[i].load (std::memory_order_relaxed);
                std::atomic_thread_fence (std::memory_order_acquire);
                if (version.load (std::memory_order_relaxed) == before)
                    return n;
            }
            return 0;
        }

    private:
        void beginWrite() { version.fetch_add (1u, std::memory_order_acq_rel); }
        void endWrite()   { version.fetch_add (1u, std::memory_order_release); }

        std::atomic<int> markers[kMaxSpliceMarkers];
        std::atomic<int> count { 0 };
        std::atomic<unsigned> version { 0 };
    };

    //==========================================================================
    // Organize: the knob (0..1) cut into equal zones, one per splice.
    inline int organizeZone (float knob, int numSplices)
    {
        const int n = std::max (1, numSplices);
        const float k = std::isfinite (knob) ? std::min (1.0f, std::max (0.0f, knob)) : 0.0f;
        return std::min (n - 1, (int) std::floor (k * (float) n));
    }

    // Which splice is selected. Organize and Shift both move it; the splice actually playing changes only when the
    // splice or gene in progress ends (the engine does that, by spawning its next gene in the selected splice).
    class SpliceSelector
    {
    public:
        int selected() const { return current; }

        // Start from the knob as it is (first block, after Clear Reel): the zone it points at is selected.
        void sync (float organize, int numSplices)
        {
            lastCount = std::max (1, numSplices);
            lastZone = organizeZone (organize, lastCount);
            current = lastZone;
        }

        // Once per block. 'shiftPressed' is true for the one block in which Shift was pressed.
        void update (float organize, int numSplices, bool shiftPressed)
        {
            const int n = std::max (1, numSplices);
            current = std::min (n - 1, std::max (0, current));

            if (shiftPressed && n > 1)
                current = (current + 1) % n;

            const int zone = organizeZone (organize, n);
            if (n != lastCount)
            {
                // The number of splices changed (a marker was added or taken away): the zones moved under the knob, but the
                // knob did not, so nothing is selected by it.
                lastCount = n;
                lastZone = zone;
            }
            else if (zone != lastZone)
            {
                lastZone = zone;
                current = zone;                 // Organize takes priority over Shift
            }
        }

        // Called when the engine changes the markers, so the selected number keeps pointing at the same piece of tape.
        void markerInserted (int markerIndex)  { if (current > markerIndex) ++current; }
        void markerRemoved (int markerIndex)   { if (current > markerIndex) --current; }
        void select (int splice)               { current = std::max (0, splice); }

    private:
        int current = 0;
        int lastZone = 0;
        int lastCount = 1;
    };
}
