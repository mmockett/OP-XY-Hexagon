#pragma once
#include <opxy/page_header.hpp>

namespace bloom {
// Matches resources.display in definition.json: paths, points, nodes, texts, palette.
// Two mutable outlines, the vowel's hills, each 64 points; every other shape
// is authored and moved (tools/make_panel.py).
const opxy::u32 hill_points = 64;
using Frame = opxy::draw::Frame<2, hill_points, 122, 16, 16>;

// Nodes: the two hills (outlines must come first), the header's bar, the sun
// (one outline, stripes and all), each partial's stem, halo, blossom and twin,
// the pollen, then the horizon. The artwork draws them sun, hills, partials,
// pollen, horizon.
const opxy::u32 hill_first = 0, hill_count = 2, header_first = 2, sun_node = 10,
                partial_first = 11, partial_nodes = 4, mote_first = 107, mote_count = 14, horizon_node = 121;

// The screen's own copy of the drift: one meadow for the track, its note
// time and weather clock advanced from elapsed time while notes are held (the
// display cannot read the engine's). Envelopes are Q16, tau Q10 seconds.
struct DisplayState {
    opxy::draw::PageHeader header;
    opxy::draw::NoteTracker notes;
    opxy::u32 time;       // note evolution time, spectrum.h's units
    opxy::u32 clock;      // the weather's clock, at a quarter of the partials' pace
    opxy::u32 tau;        // pollen, beat, shimmer and breeze time, wraps at 90 s
    opxy::u32 tick;       // frames, staggering the pollen
    opxy::u32 turn;       // the partial whose turn it is to be redrawn
    opxy::u32 turned;     // frames left of a control counting as being turned
    opxy::u32 setting;    // the controls' sum last frame, to notice that
    opxy::i32 open;       // the blossoms opening while notes are held, 0..65536
    opxy::i32 pop;        // a note start's swell, 65536 decaying to 0
    opxy::u32 serial;     // the note-start serial last seen
    opxy::u32 track;
    opxy::u32 initialized;
};
}
