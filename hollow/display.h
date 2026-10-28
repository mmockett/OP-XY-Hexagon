#pragma once
#include <opxy/page_header.hpp>

namespace hollow {
// Matches resources.display in definition.json: paths, points, nodes, texts, palette.
using Frame = opxy::draw::Frame<2, 64, 253, 16, 16>;

// Nodes (tools/make_panel.py authors the same numbers): the air line's two
// halves (the paths), the header's bar, eight vessels for each body family,
// the pipes' mouths, the sympathy arcs, breath motes (three a vessel), glass
// glints (two a vessel), then a pool of ripples for each vowel's aspect, oo
// round to ee flat.
const opxy::u32 header_first = 2, vessel_first = 10, mouth_first = 42, arc_first = 50, arc_count = 18,
                mote_first = 68, glint_first = 92, ripple_first = 108, ripple_pool = 29, aspect_count = 5;

struct DisplayState {
    opxy::draw::PageHeader header;
    opxy::draw::NoteTracker notes;
    opxy::i32 energy[8];      // how strongly each vessel rings, Q15
    opxy::u32 tau;            // the screen's clock, Q16 seconds: it runs while something rings
    opxy::u32 serial;         // the note-start serial last seen
    opxy::u32 held;           // the held-note count last seen
    opxy::u32 started;        // 0 after display_reset: the next frame shows the end state
    opxy::u32 air_key;        // the air line's geometry key,
    opxy::u32 air_tau;        // and the clock and amplitude it was drawn at
    opxy::i32 air_amp;
};
}
