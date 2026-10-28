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
struct Mode { u32 phase; i32 a, b; };
// A sympathetic mode, ringing at another held note's pitch (owner 0: idle).
struct Echo { u32 owner, increment; Mode mode; };

struct Voice {
    Mode modes[kModes];
    Echo echoes[kEchoes];
    u32 seed;          // breath noise
    i32 hiss, slow;    // Air's breath: a fixed low-pass, and a slow one it subtracts
    i32 controls[8];   // smoothed controls, 0..32767
    u32 primed;        // controls hold real values; the first block snaps them
    u32 slot;          // this voice's registry slot plus one: a hint, checked before use
};

// Every held note's pitch, so each voice can ring at the others'.
struct Note { u32 owner, increment, stamp; };  // owner 0: free; increment 0: no pitch yet
struct Shared {
    Note notes[kNotes];
    u32 clock;  // counts renders by every voice; stamps say when a note was last refreshed
};
}
