#pragma once
#include <opxy/audio.hpp>

namespace moth {
// One path of the decimator: two first-order allpasses in series. x is the
// path's last input, a the first allpass's last output (the second's last
// input) and b the second's last output, all Q28.
struct Path { opxy::i32 x, a, b; };

struct Voice {
    opxy::u32 carrier, modulator;  // phases at twice the rate
    Path even, odd;                // the half-band decimator's two paths
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
    opxy::i32 share;               // the decimator's even path's share at this block's end, Q15: 0 at the device's rate
    opxy::i32 want;                // the share the bandwidth last asked for
};

// State shared by every voice; empty when voices share nothing.
struct Shared {};
}
