// The track's amp envelope, as the screen follows it (SDK 1.10).
//
// The Hexagon Suite shares this file: each machine keeps an identical copy, since
// a machine builds from its own folder alone, and includes it inside its own
// namespace, so that no two machines in one firmware define the same symbol.
//
// The engines cannot see a note's release: a voice sounds until the OP-XY's amp
// envelope (M2) ends it. The screen reads that envelope (input.amp) and follows
// it here, so its motion swells with the attack, settles to the sustain while
// notes are held and fades over the release, lasting as long as the sound.
//
// The times and shapes are stock OS 1.2.1's, from amp_curve.h (also identical in
// every machine). A larger attack or decay is a longer stage, but a larger
// release is a shorter one. The attack rises concavely, fast at first and
// slower at the top; the decay falls exponentially and stops at the sustain
// level plus 1%; the sustain is a linear level; the release fades
// exponentially from wherever it starts to -66 dB, where the motion stops.
// Integer arithmetic only.
#pragma once

namespace amp {
#include "amp_curve.h"

const opxy::i32 one = 32768;   // a level of 1, Q15

const opxy::u32 quiet = 0, attack = 1, decay = 2, sustain = 3, release = 4;

// One voice's envelope as the screen follows it: its level (Q24, 1 << 24 at
// the attack's peak) and its stage. 16 bytes, the size each machine declares
// for it; zero is quiet.
struct Follower {
    opxy::i32 level;
    opxy::u32 stage;
    opxy::u32 spare[2];
};

const opxy::i32 full = 1 << 24;               // a level of 1, Q24
const opxy::i32 peak = attack_peak_q15 << 9;  // 0.99: the attack's end
const opxy::i32 cut = 8389;                   // 0.0005, -66 dB: the release's end

// A frame (Q10 s) in microseconds.
inline unsigned frame_us(opxy::i32 dt) { return static_cast<unsigned>(dt) * 15625u >> 4; }
// A Q24 level (0 .. 2 << 24) times a Q15 factor, in 32 bits.
inline opxy::i32 scaled(opxy::i32 level, unsigned factor) {
    const unsigned l = static_cast<unsigned>(level);
    return static_cast<opxy::i32>((l >> 15) * factor + (((l & 0x7fffu) * factor) >> 15));
}
// One frame of the attack's concave rise, L(u) = 2 (1 - 0.505^u): what is left
// below 2 shrinks by the attack's factor.
inline opxy::i32 rise(opxy::i32 level, const opxy::draw::Envelope& e, unsigned us) {
    return 2 * full - scaled(2 * full - level, attack_frame_q15(e.attack, us));
}

// One frame of a voice: gate while its note is held; struck when it starts or
// starts again this frame; dt the frame (Q10 s, clamped by the caller); jump
// (a reset, a track change, reduced motion) settles at once where a held or
// released note rests. A note struck while it sounds rises again from where it
// is. Returns the level, Q15.
inline opxy::i32 follow(Follower& f, const opxy::draw::Envelope& e, bool gate, bool struck, opxy::i32 dt, bool jump) {
    const opxy::i32 top = sustain_q15(e.sustain) << 9;
    if (jump) {
        f.stage = gate ? sustain : quiet;
        f.level = gate ? top : 0;
        return f.level >> 9;
    }
    const unsigned us = frame_us(dt < 0 ? 0 : dt);
    if (gate && (struck || f.stage == quiet || f.stage == release)) f.stage = attack;
    if (!gate && struck) {
        // Played and let go of between frames: it rose for at most a frame, then
        // released from there.
        f.level = rise(f.level, e, us);
        if (f.level > full) f.level = full;
        f.stage = attack;
    }
    if (!gate && f.stage != quiet && f.stage != release) f.stage = release;
    else if (f.stage == attack) {
        f.level = rise(f.level, e, us);
        if (f.level >= peak) { f.level = full; f.stage = decay; }
        return f.level >> 9;
    }
    if (f.stage == decay) {
        // Exponentially toward 0, ending at the sustain level plus 1%.
        f.level = scaled(f.level, decay_frame_q15(e.decay, us));
        if (f.level <= top + full / 100) f.stage = sustain;
    }
    if (f.stage == sustain) f.level = top;
    if (f.stage == release) {
        // Exponentially from where it starts, ending at -66 dB.
        f.level = scaled(f.level, release_frame_q15(e.release, us));
        if (f.level < cut) { f.stage = quiet; f.level = 0; }
    }
    return f.level >> 9;
}

// Whether the voice still sounds: struck and not yet through its release.
inline bool sounding(const Follower& f) { return f.stage != quiet; }
}
