#pragma once
// Wane's controls: their slots and how each turns into the units the audio
// plays and the header shows. Shared by engine.cpp and display.cpp.
#include "tape.h"

namespace wane { namespace controls {
using opxy::i32;
using opxy::u32;

enum { SHAPE, DETUNE, DRIFT, WARMTH, WOW, FLUTTER, SAG, AGE };

// Curves in Q16 octaves (tape::along, definition.json's anchors): the
// oscillators up to 70 cents apart (8 cents at the default 40%), each voice
// wandering up to +-35 cents.
const tape::Curve detune_curve = {{0, 191, 601, 1638, 3823}};   // 0, 3.5, 11, 30, 70 cents apart
const tape::Curve drift_curve = {{0, 273, 655, 1201, 1911}};    // 0, 5, 12, 22, +-35 cents

// The shape control's regions, Q15: sine to triangle, triangle to saw, saw to
// square, then the square narrowing to a 15% pulse.
const i32 shape_triangle = 8192, shape_saw = 16384, shape_square = 26214;

// Q16 octaves in tenths of a cent, rounded, for captions.
inline u32 cent_tenths(i32 octaves) {
    return octaves <= 0 ? 0u : (static_cast<u32>(octaves) * 12000u + 32768u) >> 16;
}
}}
