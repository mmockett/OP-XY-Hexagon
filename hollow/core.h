#pragma once
#include <opxy/audio.hpp>
#include "modes.h"

namespace hollow {
// Notes the shared registry holds: one per voice (definition.json voiceCapacity).
const u32 kNotes = 8;
// Sympathetic resonators per voice: the first other held notes to arrive keep
// one each, so a big chord costs a bounded amount.
const u32 kEchoes = 4;
// A registry entry not refreshed for this many renders, by any voice, is stale:
// a sounding voice refreshes its own every block, so only a voice that stopped
// rendering without destroy can leave one, and it is ignored after this.
const u32 kFresh = 3 * kNotes;

// One resonant mode in phasor form: its oscillator phase and its complex
// amplitude, a (in phase with the bow) and b (quadrature), each an amplitude
// times 65536, at most 4095 * 65536. Each block the amplitude moves like a
// damped resonator's driven at its own frequency: it decays by the mode's
// ring and is pushed by the bow (steadily) and the breath (noise).
// Spread's ensemble sounds that amplitude as three copies: the mode itself and
// a sharp and a flat copy, whose phases (up, down) run ahead of and behind the
// mode's by their detunes. (sa, sb) is the amplitude last sounded, all three
// copies summed, so the next block ramps on from it.
struct Mode { u32 phase; i32 a, b; i32 sa, sb; u32 up, down; };
// A sympathetic mode, ringing at another held note's pitch (owner 0: idle).
// swell follows how far it has bloomed toward its steady power (Q31), so the
// output gain makes room for it as it grows.
struct Echo { u32 owner, increment; i32 swell; Mode mode; };

// A mode's plan for a block, a pure function of the smoothed controls and the
// note's increment: kept while they hold still, so a held note recomputes nothing.
struct Planned { u32 at; i32 level, decay, bow, noise; };
struct Voice {
    Mode modes[kModes];
    Echo echoes[kEchoes];
    u32 seed;          // breath noise
    i32 hiss, slow;    // Air's breath: a fixed low-pass, and a slow one it subtracts
    i32 controls[8];   // smoothed controls, 0..32767
    u32 primed;        // controls hold real values; the first block snaps them
    u32 slot;          // this voice's registry slot plus one: a hint, checked before use
    u32 gain;          // the output gain last block (Q10): each block ramps on from it
    u32 age;           // blocks rendered: the clock of Spread's sway
    u32 sway;          // this voice's sway: its phases and how much faster it runs
    Planned planned[kModes];
    i32 planned_for[6];  // breath, body, bright, ring, vowel, increment the plans were made for
    u32 planned_power;   // the modes' steady power they sum to
    i32 planned_fundamental;
    u32 planned_valid;
};

// Every held note's pitch, so each voice can ring at the others'.
struct Note { u32 owner, increment, stamp; };  // owner 0: free; increment 0: no pitch yet
struct Shared {
    Note notes[kNotes];
    u32 clock;  // counts renders by every voice; stamps say when a note was last refreshed
};
}
