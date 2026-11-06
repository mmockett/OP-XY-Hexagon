#pragma once
#include <opxy/page_header.hpp>

namespace bloom {
#include "amp.h"   // inside the namespace: each machine's own copy of the suite's envelope follower

// Matches resources.display in definition.json: paths, points, nodes, texts, palette.
// No mutable outlines: every shape is authored and moved (tools/make_panel.py).
using Frame = opxy::draw::Frame<0, 0, 111, 16, 16>;

// Nodes: the header's bar, the sun, the vowel's two hills, each partial's
// stem, halo, blossom and twin, then the four status dots. The artwork draws
// them sun (its stripes over it), hills, halos, stems, blossoms, the ground
// below the horizon, the horizon, the dots.
const opxy::u32 header_first = 0, sun_node = 8, hill_first = 9, hill_count = 2, partial_first = 11, partial_nodes = 4,
                dot_first = 107, dot_count = 4;

// The screen's own copy of the drift: one meadow for the track, its note
// time and weather clock advanced from elapsed time while notes are held (the
// display cannot read the engine's). Envelopes are Q16, tau Q10 seconds.
struct DisplayState {
    opxy::draw::PageHeader header;
    opxy::draw::NoteTracker notes;
    opxy::u32 time;       // note evolution time, spectrum.h's units
    opxy::u32 clock;      // the weather's clock, at a quarter of the partials' pace
    opxy::u32 tau;        // pollen, beat, shimmer and breeze time, wraps at 90 s
    opxy::u32 tick;       // frames, staggering the sun and pollen
    opxy::u32 turn;       // the partial whose turn it is to be redrawn
    opxy::i32 open;       // the blossoms opening while notes are held, 0..65536
    opxy::i32 pop;        // a note start's swell, 65536 decaying to 0
    opxy::u32 serial;     // the note-start serial last seen
    opxy::u32 track;
    opxy::u32 initialized;
    amp::Follower amp;    // the track's amp envelope as the blossoms follow it, when known (SDK 1.10)
};
}
