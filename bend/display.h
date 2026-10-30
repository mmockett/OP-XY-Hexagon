#pragma once
#include <opxy/page_header.hpp>
#include "pd.h"

namespace bend {
// Matches resources.display in definition.json: paths, points, nodes, texts, palette.
using Frame = opxy::draw::Frame<2, 64, 154, 16, 16>;

// Nodes, as tools/make_panel.py writes them into panel.svg.fragment.
namespace node {
const opxy::u32 map = 0, fading = 1,        // the bent map, and the last wave's fading out
                header = 2,                 // the two-page header's eight bar steps
                dot = 10, dots = 86,        // line 1's dots over two of its cycles
                twin = 96, twins = 41,      // line 2's rings
                needle = 137,               // each note's bend dial
                icon = 145,                 // the eight waves
                key = 153;                  // the chosen wave's key behind its icon
}
// Palette entries (tools/make_panel.py): the map's seven greys (1..7, dim to
// bright), then the dots, the needles, the icons and the key.
namespace shade {
const opxy::u32 map_dim = 1, map_bright = 7, dot = 8, twin = 9, twin_ring = 10, needle_lit = 11,
                needle_dim = 12, icon_lit = 13, icon_dim = 14;
}

// The screen keeps its own copy of what the sound is doing, from the controls
// and elapsed time (the display cannot read the audio): each held note's
// contour on its own dial, the controls eased, line 2's slide and the noise's
// roll. The OP-XY redraws whatever changes, so the dots sit on whole pixels and
// a beam redraws them, an eighth of the way across a frame, like a scope's sweep,
// whatever moved them; the dials take turns; and the dots follow the contour in
// coarse steps.
struct DisplayState {
    opxy::draw::PageHeader header;
    opxy::draw::NoteTracker notes;
    opxy::u32 initialized, track, serial;
    opxy::u32 blocks, rest;      // the screen's clock in audio blocks, while notes are held
    opxy::u32 start[8];          // each dial's note start on that clock
    opxy::i32 lit[8];            // each dial's light, Q15, easing on and off
    opxy::u32 held, newest;      // dials lit, and the latest note's
    opxy::i32 value[8];          // the controls, eased, Q15 (slots; the wave and interval unused)
    opxy::u32 wave, previous;    // the wave, and the one fading out
    opxy::i32 fade;              // the crossfade's progress, Q15 (32768 done)
    opxy::i32 key;               // the strip's key, Q12 waves, easing
    opxy::u32 beat;              // line 2's slide against line 1, Q32 cycles
    opxy::u32 roll;              // the noise's roll
    opxy::u32 map_key;           // what the map outlines were drawn for
    opxy::i32 dots_bend;         // the bend the dots show, Q15: it follows in steps of 1/32
    opxy::u32 turn;              // frames drawn: where the beam is, and whose turn it is among the dials
};
}
