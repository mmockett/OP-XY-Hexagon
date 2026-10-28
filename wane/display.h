#pragma once
#include <opxy/page_header.hpp>
#include "tape.h"

namespace wane {
// Matches resources.display in definition.json: paths, points, nodes, texts, palette.
using Frame = opxy::draw::Frame<4, 64, 172, 16, 16>;

// Nodes, as tools/make_panel.py writes them into panel.svg.fragment.
namespace node {
const opxy::u32 twin = 0, wave = 1, tape = 2,   // the outlines: the oscillator's twin, the oscillator, the tape's halves
                header = 4,                     // the two-page header's eight bar steps
                pack = 12,                      // the tape packs, one a hub
                ring = 14, rings = 8,           // winding rings within each pack
                tooth = 30, teeth = 7,          // per hub: six spline teeth, then the tape's clamp
                stripe = 44,                    // the label's three stripes
                needle = 47, pivot = 55,        // eight needles and their pivots
                wear = 63, wears = 109;         // age's wear: the label's, the window's, the shell's, then dust
}

struct DisplayState {
    opxy::draw::PageHeader header;
    opxy::draw::NoteTracker notes;
    tape::Clock clock;          // the screen's own tape, in audio blocks; it runs while notes are held
    opxy::u32 initialized, track, serial;
    opxy::i32 hold;             // Q10: the tape's speed, easing to 1 while notes are held and to 0 after
    opxy::u32 turn;             // the reels' angle, Q10 tenths of a degree
    opxy::u32 beat;             // the detuned twin's phase, Q32 cycles
    opxy::u32 wound;            // how far the tape has played on from the board's start, Q26
    opxy::u32 lit;              // needles lit, one a held note (at most eight)
    opxy::u32 starts[8];        // when each needle's note started, on the tape's clock
    opxy::u32 sag_last, sag_before;   // the last two note starts, for the tape's sag
};
}
