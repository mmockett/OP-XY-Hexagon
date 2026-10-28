#pragma once
// Wane's controls: their slots and how each turns into the units the audio
// plays and the header shows. Shared by engine.cpp and display.cpp.
#include "tape.h"

namespace wane { namespace controls {
using opxy::i32;
using opxy::u32;

enum { SHAPE, DETUNE, DRIFT, WARMTH, WOW, FLUTTER, SAG, AGE };

// Squared depths, Q16 octaves (tape::depth): the oscillators up to 50 cents
// apart, each voice wandering up to +-20 cents.
const i32 detune_most = 50 * 65536 / 1200;
const i32 drift_most = 20 * 65536 / 1200;

// The shape control's regions, Q15: sine to triangle, triangle to saw, saw to
// square, then the square narrowing to a 15% pulse.
const i32 shape_triangle = 8192, shape_saw = 16384, shape_square = 26214;

// Q16 octaves in tenths of a cent, rounded, for captions.
inline u32 cent_tenths(i32 octaves) {
    return octaves <= 0 ? 0u : (static_cast<u32>(octaves) * 12000u + 32768u) >> 16;
}
}}
