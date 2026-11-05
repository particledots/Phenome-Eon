// A coarse picture of a long recording (the loudest value in each of a fixed number of slices, per channel),
// for drawing a waveform. The audio side writes it, the window side reads it, with no locks: every value is a
// single number that is stored and read in one piece, so the window never sees half-written data.
//
// Plain C++, no JUCE. Reusable for any plugin that shows a recorded waveform.

#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace phe::core
{
    class PeakOverview
    {
    public:
        // One slice per pixel of the reel display in its fixed 1146-pixel-wide layout space.
        static constexpr int kBuckets = 1146;

        PeakOverview() { clear(); }

        void clear()
        {
            for (auto& v : data)
                v.store (0.0f, std::memory_order_relaxed);
        }

        void set (int bucket, float left, float right)
        {
            if (bucket < 0 || bucket >= kBuckets)
                return;
            data[(size_t) bucket * 2].store (left, std::memory_order_relaxed);
            data[(size_t) bucket * 2 + 1].store (right, std::memory_order_relaxed);
        }

        void get (int bucket, float& left, float& right) const
        {
            if (bucket < 0 || bucket >= kBuckets)
            {
                left = right = 0.0f;
                return;
            }
            left  = data[(size_t) bucket * 2].load (std::memory_order_relaxed);
            right = data[(size_t) bucket * 2 + 1].load (std::memory_order_relaxed);
        }

    private:
        std::array<std::atomic<float>, (size_t) kBuckets * 2> data;
    };
}
