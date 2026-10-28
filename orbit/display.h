#pragma once
#include <opxy/page_header.hpp>
#include "vector.h"

namespace orbit {
// Matches resources.display in definition.json: paths, points, nodes, texts, palette.
using Frame = opxy::draw::Frame<3, 64, 252, 16, 16>;

// Nodes, as tools/make_panel.py writes them into panel.svg.fragment.
namespace node {
const opxy::u32 comb = 0, twin = 1, wave = 2,   // the scope's outlines: the body, the detuned twin, the wave
                header = 3,                     // the two-page header's eight bar steps
                glyph = 11,                     // the corner waves: [stepped][bank][corner]
                tick = 75,                      // the gate's corner ticks
                dot = 79, dots = 48,            // the orbit, dotted
                station = 127,                  // steps' four stations
                cross = 131,                    // the orbit's centre
                spoke = 132, spokes = 6,        // the vector, centre to the newest point
                trail = 138, trails = 8,        // each note's trail
                point = 202,                    // each note's point
                ring = 210,                     // the newest point's ring
                ping = 211,                     // each note's ping as it starts
                home = 219,                     // where the next note will start
                pip = 220;                      // each corner's share, eight pips a corner
}

// A note on the screen: its point's phase along the path, how long ago it
// started and how lit it is. The screen cannot hear the voices: it starts a
// point for each held note and moves it as the audio does.
struct Note {
    opxy::u32 phase;   // Q32 cycles, 0 (the top of the path) as the note starts
    opxy::i32 age;     // Q10 seconds since it started
    opxy::i32 level;   // Q10: 1 while held, easing to 0 when released
    opxy::u32 on;
};
struct DisplayState {
    opxy::draw::PageHeader header;
    opxy::draw::NoteTracker notes;
    opxy::u32 initialized, track, serial, held;
    opxy::i32 eased[8];        // each control as drawn, Q15, easing to where it is set
    opxy::u32 bank, from;      // the bank drawn, and the one it replaces
    opxy::i32 change;          // Q15: how far the new bank has grown in
    opxy::u32 beat;            // the detuned twin's drift, Q32 cycles
    opxy::u32 newest;          // the note started last
    Note voices[8];
};
}
