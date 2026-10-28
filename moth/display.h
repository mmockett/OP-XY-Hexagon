#pragma once
#include <opxy/page_header.hpp>

namespace moth {
// Matches resources.display in definition.json: paths, points, nodes, texts, palette.
using Frame = opxy::draw::Frame<4, 64, 256, 16, 16>;

// Nodes (tools/make_panel.py authors the same numbers): the four edges (the
// paths: left and right dark, then left and right lit), the header's bar,
// the lamp's seven rings and the LED's glow, the dark membrane (a wedge
// under each of the dark edge's thirty chords a wing, left first, then a
// tooth on each), the lit membrane's twenty wedges a wing, nine veins a wing,
// eight eyespots of three nodes (ring, inside, pupil), the vactrol's fourteen
// parts, then a pool of scales a strike knocks loose.
const opxy::u32 kHeaderFirst = 4, kHaloFirst = 12, kHaloCount = 7, kLedGlow = 19, kDarkFirst = 20,
                kDarkTeeth = 80, kChords = 30, kLitFirst = 140, kVeinFirst = 180, kVeins = 9,
                kEyeFirst = 198, kBodyFirst = 222, kScaleFirst = 236, kScaleCount = 20;

// One voice's vactrol, as the screen keeps it: the cell's light (Q30), the
// time since its strike (Q16 seconds) and whether its note is held.
struct Lamp { opxy::i32 cell; opxy::u32 since, held; };
// A scale knocked loose: where it is and how it moves (Q10 pixels and pixels
// a second), its turn and spin (tenths of a degree, and a second), its age
// and life (Q16 seconds); life 0 when the slot is free.
struct Scale { opxy::i32 x, y, vx, vy, turn, spin; opxy::u32 age, life; };

struct DisplayState {
    opxy::draw::PageHeader header;
    opxy::draw::NoteTracker notes;
    opxy::i32 eased[8];   // the controls as drawn, easing to their values, 0..32767
    opxy::i32 stop;       // the ratio's stop as drawn, Q16, gliding between stops
    Lamp lamps[8];
    opxy::u32 tau;        // the moth's clock, Q16 seconds: it runs while the moth sounds
    opxy::u32 psi;        // the strobe's crawl, Q32 cycles
    opxy::u32 serial;     // the note-start serial last seen
    opxy::u32 started;    // 0 after display_reset: the next frame shows the end state
    opxy::u32 seed;       // scales' randomness
    opxy::u32 next;       // the next scale slot to fill
    opxy::u32 keys[4];    // the edges' geometry keys
    Scale scales[kScaleCount];
};
}
