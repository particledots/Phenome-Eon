// PhenomeEon engine (reusable core): the 28 scales behind Pitch Scale.
//
// The same list, in the same order, as the other particledots plugins (copied from the Beads plugin's
// src/Scales.h). Each scale is the set of semitones above its root note. The root is the note the Pitch knob
// plays (after Pitch Snap; in PhenomeEon that is the Vari-Speed note), so there is no Key setting.
//
// The engine works with a 12-bit mask: bit n is set when "n semitones above the root" is a note of the scale.
// Header only, no dependencies.

#pragma once
#include <array>
#include <cstddef>

namespace phe
{
namespace scales
{
    struct Scale { const char* name; std::array<int, 12> notes; int count; };

    // Menu entry 0 is "Off"; scale i here is menu entry i + 1.
    constexpr Scale kScales[] =
    {
        { "Major (Ionian)",            { 0, 2, 4, 5, 7, 9, 11 },                7 },
        { "Dorian",                    { 0, 2, 3, 5, 7, 9, 10 },                7 },
        { "Phrygian",                  { 0, 1, 3, 5, 7, 8, 10 },                7 },
        { "Lydian",                    { 0, 2, 4, 6, 7, 9, 11 },                7 },
        { "Mixolydian",                { 0, 2, 4, 5, 7, 9, 10 },                7 },
        { "Aeolian (Natural Minor)",   { 0, 2, 3, 5, 7, 8, 10 },                7 },
        { "Locrian",                   { 0, 1, 3, 5, 6, 8, 10 },                7 },
        { "Harmonic Minor",            { 0, 2, 3, 5, 7, 8, 11 },                7 },
        { "Melodic Minor",             { 0, 2, 3, 5, 7, 9, 11 },                7 },
        { "Harmonic Major",            { 0, 2, 4, 5, 7, 8, 11 },                7 },
        { "Hungarian Minor",           { 0, 2, 3, 6, 7, 8, 11 },                7 },
        { "Phrygian Dominant",         { 0, 1, 4, 5, 7, 8, 10 },                7 },
        { "Double Harmonic (Byzantine)", { 0, 1, 4, 5, 7, 8, 11 },              7 },
        { "Neapolitan Minor",          { 0, 1, 3, 5, 7, 8, 11 },                7 },
        { "Neapolitan Major",          { 0, 1, 3, 5, 7, 9, 11 },                7 },
        { "Enigmatic",                 { 0, 1, 4, 6, 8, 10, 11 },               7 },
        { "Major Pentatonic",          { 0, 2, 4, 7, 9 },                       5 },
        { "Minor Pentatonic",          { 0, 3, 5, 7, 10 },                      5 },
        { "Blues",                     { 0, 3, 5, 6, 7, 10 },                   6 },
        { "Hirajoshi",                 { 0, 2, 3, 7, 8 },                       5 },
        { "In Sen",                    { 0, 1, 5, 7, 10 },                      5 },
        { "Kumoi",                     { 0, 2, 3, 7, 9 },                       5 },
        { "Iwato",                     { 0, 1, 5, 6, 10 },                      5 },
        { "Whole Tone",                { 0, 2, 4, 6, 8, 10 },                   6 },
        { "Octatonic (Whole-Half)",    { 0, 2, 3, 5, 6, 8, 9, 11 },             8 },
        { "Octatonic (Half-Whole)",    { 0, 1, 3, 4, 6, 7, 9, 10 },             8 },
        { "Prometheus",                { 0, 2, 4, 6, 9, 10 },                   6 },
        { "Chromatic",                 { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }, 12 },
    };
    constexpr int kNumScales = (int) (sizeof (kScales) / sizeof (kScales[0]));

    // 0 = Off, 1..kNumScales = the scales above. Returns the 12-bit mask (0 for Off or out of range).
    constexpr int maskFor (int menuIndex)
    {
        if (menuIndex < 1 || menuIndex > kNumScales) return 0;
        int m = 0;
        for (int i = 0; i < kScales[menuIndex - 1].count; ++i) m |= 1 << kScales[menuIndex - 1].notes[(std::size_t) i];
        return m;
    }
} // namespace scales

} // namespace phe
