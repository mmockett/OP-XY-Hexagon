#pragma once
#include <opxy/audio.hpp>
#include "pd.h"

namespace bend {
// How far a line may bend at its pitch, kept until the pitch, wave or warp change.
struct Band {
    u32 increment, wave;
    i32 warp;
    pd::Ceiling top;
    u32 inverse;        // 2^32 over the increment, for the BLEP
};
// One note: two phase-distortion lines (line 2 at the interval, detuned),
// the note's DCW contour, the noise that modulates their phases, the DC
// blocker the bent waves need, and lofi's held sample.
struct Voice {
    u32 phase[2];       // each line's input phase, Q32 cycles
    u32 rate[2];        // reso: the cosine's rate at the end of the last block, Q16
    i32 last[2];        // each line's last sample, still to be played (it runs a sample late)
    Band band[2];
    u32 age;            // blocks since the note started
    i32 from;           // the contour where the last note left it, Q15
    i32 envelope;       // the contour in the last block, Q15
    u32 breath;         // this note's breath rate, Q32 cycles a block
    u32 notes;          // notes started on this voice: each breathes at its own rate
    u32 seed;           // the noise's generator
    i32 noise;          // the noise, low-passed, Q15
    u32 noise_note;     // the pitch and depth the noise's pole and scale were found for
    i32 noise_level;
    i32 noise_pole;     // Q15
    u32 noise_scale;    // Q15 noise to Q32 cycles
    i32 dc_in, dc_out;  // the DC blocker's last input and output, Q24
    i32 level;          // the output gain at the end of the last block
    u32 clock;          // lofi's sample clock
    i32 held;           // lofi's held sample, Q24
    u32 fresh;          // a new owner: prime the DC blocker and take the reso rate at once
};
// Voices share nothing.
struct Shared {};
}
