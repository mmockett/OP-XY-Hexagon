#pragma once
#include <opxy/audio.hpp>
#include "spectrum.h"

namespace bloom {
// One sine partial: its phase and its amplitude now, Q31 (level << 16).
struct Partial {
    u32 phase;
    i32 amplitude;
};
struct Voice {
    Partial partials[partial_count];
    u32 seed;       // this note's seed (note_seed)
    u32 time;       // this note's evolution time; stops while held
    u32 cycle;      // the shared block this voice last rendered in
    u32 sounding;   // partials that may still have amplitude
};
// The track's weather: one clock every voice reads, so a chord's voices lean
// brighter and darker together, and the note counter that seeds each note.
struct Shared {
    u32 clock;      // weather time, advanced once per block whatever the voice count
    u32 cycle;      // blocks counted so far
    u32 notes;      // notes started on the track
};
}
