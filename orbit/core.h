#pragma once
#include <opxy/audio.hpp>
#include "vector.h"

namespace orbit {
// One note: its orbit's phase, the two sets' oscillator phases, the block's
// mixed wave (mixed again only when the mix moves) and what the last block
// played, which the next one fades from.
const u32 table_points = 130;   // the block's wave: 128 points, a guard point (the first again) and a spare
struct Voice {
    i16 table[table_points];   // the block's mixed wave, Q14
    u32 orbit;       // the vector's phase along its path, Q32 cycles: 0, the top, at each note start
    u32 phase[2];     // the note's set and its detuned twin, Q32 cycles; free running
    u32 mixed;        // the table's bank and band (bank * 8 + level), or ~0 before the first
    i32 fade;         // and the next band's share, Q14,
    i32 share[4];     // and each corner's weight, Q14
    i32 lift;         // its loudness: Q31 output per Q14 unit, so its loudest point plays at 0.144
    i32 gain[2];      // each set's multiplier in the last block (0: silent)
    u32 clock;        // grit's DAC clock, Q32 of a sample: a new DAC sample each time it wraps
};
// Nothing is shared: each note's orbit starts at the top of its path as the
// note starts, so notes struck together move together.
struct Shared {};
}
