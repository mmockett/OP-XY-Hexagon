#pragma once
#include <opxy/page_header.hpp>
#include "vector.h"

namespace orbit {
// Matches resources.display in definition.json: paths, points, nodes, texts, palette.
using Frame = opxy::draw::Frame<3, 64, 253, 16, 16>;

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
                pip = 220,                      // each corner's share, eight pips a corner
                beam = 252;                     // the bead sweeping the scope's wave while a note sounds
}

// A note on the screen: its point's phase along the path, how long ago it
// started, how lit it is, and the dots it has dropped behind it (a ring of
// node::trails). The screen cannot hear the voices: it starts a point for each
// held note and moves it as the audio does.
struct Note {
    opxy::u32 phase;   // Q32 cycles, 0 (the top of the path) as the note starts
    opxy::i32 age;     // Q10 seconds since it started
    opxy::i32 level;   // Q10: 1 while held, easing to 0 when released
    opxy::u32 on;
    opxy::u32 laid;    // Q32 cycles: the phase its next dot is counted from
    opxy::u32 head, filled, count;   // the newest dot in the ring, how many are down, the ring's length
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
    opxy::u32 sweep;           // the beam's place along the wave, Q32 of its width
    opxy::u32 frames;          // counts frames: whose turn it is to be redrawn
    Note voices[8];
};
}
