#pragma once
#include <opxy/audio.hpp>

namespace moth {
struct Voice {
    opxy::u32 carrier, modulator;  // the oscillators' phases
    opxy::i32 dc;                  // the folder's offset the voice removes, Q28
    opxy::i32 estimate;            // the offset the folder's law predicts, Q28, last block
    opxy::i32 low1, low2;          // the gate's two low-pass poles, Q28
    opxy::i32 cell;                // the photocell, Q30 (1 << 30 fully lit)
    opxy::u32 since;               // Q16 seconds since the note's strike
    opxy::i32 cut, level;          // last block's low-pass coefficient and output level, Q31: each block ramps on
    opxy::i32 controls[8];         // smoothed controls, 0..32767
    opxy::u32 ratio;               // the modulator's gliding ratio, Q16
    opxy::u32 primed;              // controls hold real values; the first block snaps them
    opxy::u32 seed;                // this voice's drift rates and phases
    opxy::u32 wander[4];           // drift's phases: pitch slow and quick, fold slow and quick
};

// State shared by every voice; empty when voices share nothing.
struct Shared {};
}
