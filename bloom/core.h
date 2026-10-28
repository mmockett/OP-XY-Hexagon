#pragma once
#include <opxy/audio.hpp>
#include "spectrum.h"

namespace bloom {
// One sine partial: its phase and its amplitude now, Q31 (level << 16), with
// haze's beat phase and its share of the detune, blur's smoothed noise, and
// breath's clock and the lattice values its drift eases between.
struct Partial {
    u32 phase;
    i32 amplitude;
    u32 beat;       // the detuned pair's beat phase
    u32 spread;     // pair_spread, Q8
    i32 wobble;     // blur's smoothed noise, Q15 (spread about 0.45)
    u32 offset;     // breath: the partial's clock offset (clock_offset)
    u16 rate;       // and rate, Q8 (clock_rate)
    u16 point;      // the lattice point whose values are held (0xffff: none yet)
    i16 from, to;   // the drift's lattice values at point and point + 1
};
struct Voice {
    Partial partials[partial_count];
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
