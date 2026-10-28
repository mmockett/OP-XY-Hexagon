#pragma once
#include <opxy/page_header.hpp>

namespace bloom {
// Matches resources.display in definition.json: paths, points, nodes, texts, palette.
// No mutable outlines: every shape is authored and moved (tools/make_panel.py).
using Frame = opxy::draw::Frame<0, 0, 100, 16, 16>;

// Nodes, in drawing order: the header's bar, the sun and its stripes, each
// partial's stem, halo and blossom, the pollen, then the horizon.
const opxy::u32 header_first = 0, sun_node = 8, stripe_first = 9, stripe_count = 4,
                partial_first = 13, mote_first = 85, mote_count = 14, horizon_node = 99;

// The screen's own copy of the drift: one meadow for the track, its note
// time and weather clock advanced from elapsed time while notes are held (the
// display cannot read the engine's). Envelopes are Q16, tau Q10 seconds.
struct DisplayState {
    opxy::draw::PageHeader header;
    opxy::draw::NoteTracker notes;
    opxy::u32 time;       // note evolution time, spectrum.h's units
    opxy::u32 clock;      // the weather's clock, at half the partials' pace
    opxy::u32 tau;        // pollen time, wraps at 90 s
    opxy::i32 open;       // the blossoms opening while notes are held, 0..65536
    opxy::i32 pop;        // a note start's swell, 65536 decaying to 0
    opxy::u32 serial;     // the note-start serial last seen
    opxy::u32 track;
    opxy::u32 initialized;
};
}
