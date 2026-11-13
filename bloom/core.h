#pragma once
#include <opxy/audio.hpp>
#include "spectrum.h"

namespace bloom {
// The partials, field by field, so a block reads and writes only the fields
// it uses, each packed contiguously: every block, the phases and amplitudes
// (192 bytes, 6 cache lines, against 24 lines when each partial's fields sat
// together); haze's beat and spread, blur's noise and breath's clock and
// lattice values only while those controls are up. (Measured on the device,
// nothing a voice reads stays cached into its next block, so each line a
// block touches is fetched again.)
//   phase      each partial's phase
//   amplitude  its amplitude now, Q31 (level << 16)
//   beat       haze: the detuned pair's beat phase
//   spread     haze: pair_spread, Q8
//   wobble     blur's smoothed noise, Q15 (spread about 0.45)
//   offset     breath: the partial's clock offset (clock_offset)
//   rate       breath: and rate, Q8 (clock_rate)
//   point      breath: the lattice point whose values are held (0xffff: none yet)
//   from, to   breath: the drift's lattice values at point and point + 1
struct Voice {
    u32 phase[partial_count];
    i32 amplitude[partial_count];
    u32 beat[partial_count];
    u32 spread[partial_count];
    i32 wobble[partial_count];
    u32 offset[partial_count];
    u16 rate[partial_count];
    u16 point[partial_count];
    i16 from[partial_count], to[partial_count];
    u32 seed;       // this note's seed (note_seed)
    u32 time;       // this note's evolution time; stops while held
    u32 cycle;      // the shared block this voice last rendered in
    u32 sounding;   // partials that may still have amplitude
    u32 random;     // blur's noise generator
};
// The track's weather: one clock every voice reads, so a chord's voices lean
// brighter and darker together, and the note counter that seeds each note.
struct Shared {
    u32 clock;      // weather time, advanced once per block whatever the voice count
    u32 cycle;      // blocks counted so far
    u32 notes;      // notes started on the track
};
}
