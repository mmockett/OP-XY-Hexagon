#pragma once
#include <opxy/audio.hpp>
#include "tape.h"

namespace wane {
using opxy::i32;
using opxy::u32;

// One note: two morphing oscillators, a sub an octave down, a breath of
// noise, its own drift, then the warm low-pass, saturation and head loss.
struct Voice {
    u32 cycle;        // the tape's block cycle this voice last rendered in
    u32 seed;         // this voice's own random stream (drift, phases)
    u32 noise;        // xorshift state for breath and hiss
    u32 notes;        // notes started on this voice: each one's slop is new
    u32 clock;        // blocks rendered since the voice was given out: its drift's time
    u32 fresh;        // a note has started: take its pitch at once
    i32 slop;         // this note's random start offset, Q15 (scaled by drift)
    u32 phase[3];     // oscillator 1, oscillator 2, sub
    u32 increment[2]; // the oscillators' increments at the end of the last block
    i32 low[2];       // the warm two-pole low-pass
    i32 head;         // the tape head's high-frequency loss
    i32 hiss;         // the last noise sample, for hiss's first difference
    i32 level;        // output gain at the end of the last block, Q31
    i32 offset[2];    // each oscillator's pitch offset this block, Q16 octaves
};

// The tape every voice plays through. It advances once per audio block: the
// first voice to render a second time in a cycle starts the next one.
struct Shared {
    u32 cycle;        // the block cycle being rendered
    u32 clock;        // blocks the tape has run: tape.h's clock
    u32 sagging;      // a note has started since the tape was threaded
    u32 sag_start;    // the clock at the last note start
    i32 sag_from;     // the sag left at that moment, Q15
    i32 bend;         // the transport's pitch this block, Q16 octaves (sag included)
    i32 gain;         // dropout gain this block, Q15
};
}
