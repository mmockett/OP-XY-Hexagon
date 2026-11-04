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
// Stock hands the screen the envelope page's control positions, not times, and
// its curve from position to time is not documented. This assumes one curve for
// attack, decay and release: a stage lasts 10 s times the cube of its position,
// so 0 is instant, a quarter 0.16 s, a half 1.25 s and the end of travel 10 s.
// Attack rises in a straight line; decay and release ease out, as a square of
// the time left. Sustain is a level, drawn as its square root (loudness, not
// amplitude), so a quiet sustain still moves. Integer arithmetic only.
#pragma once

namespace amp {
const opxy::i32 one = 32768;   // a level of 1, Q15

// A stage's length, Q10 seconds, at its control position (Q31): 10 s x position^3.
inline opxy::i32 seconds(opxy::i32 position) {
    const opxy::i32 x = position < 0 ? 0 : position >> 16;   // Q15
    return (((x * x) >> 15) * x >> 15) * 10240 >> 15;
}

// The square root of v, rounded down.
inline opxy::u32 root(opxy::u32 v) {
    opxy::u32 r = 0, bit = 1u << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else r >>= 1;
        bit >>= 2;
    }
    return r;
}

// The sustain's drawn level, Q15: the square root of its position.
inline opxy::i32 held_level(const opxy::draw::Envelope& e) {
    const opxy::u32 s = e.sustain < 0 ? 0u : static_cast<opxy::u32>(e.sustain) >> 16;   // Q15
    const opxy::i32 level = static_cast<opxy::i32>(root(s << 15));
    return s >= 32767u ? one : level;
}

const opxy::u32 quiet = 0, attack = 1, decay = 2, sustain = 3, release = 4;

// One voice's envelope as the screen follows it: its level (Q15, one at the
// attack's peak), its stage, the level its release began from and how far
// through its stage it is (Q15). 16 bytes; zero is quiet.
struct Follower {
    opxy::i32 level, from, progress;
    opxy::u32 stage;
};

// How far a stage of `length` (Q10 s) moves in dt (Q10 s), Q15 of the stage.
inline opxy::i32 advance(opxy::i32 length, opxy::i32 dt) { return length <= dt ? one : (dt << 15) / length; }
// What is left of an ease-out over a stage at progress p (Q15): (1 - p)^2.
inline opxy::i32 left(opxy::i32 p) { const opxy::i32 r = one - p; return r * r >> 15; }

// One frame of a voice: gate while its note is held; struck when it starts or
// starts again this frame; dt the frame (Q10 s, clamped by the caller); jump
// (a reset, a track change, reduced motion) settles at once where a held or
// released note rests. A note struck while it sounds rises again from where it
// is. Returns the level, Q15.
inline opxy::i32 follow(Follower& f, const opxy::draw::Envelope& e, bool gate, bool struck, opxy::i32 dt, bool jump) {
    const opxy::i32 top = held_level(e);
    if (jump) {
        f.stage = gate ? sustain : quiet;
        f.level = gate ? top : 0;
        f.from = f.progress = 0;
        return f.level;
    }
    if (dt < 0) dt = 0;
    if (gate && (struck || f.stage == quiet || f.stage == release)) { f.stage = attack; f.progress = 0; }
    if (!gate && struck) {
        // Played and let go of between frames: it rose for at most a frame, then
        // released from there.
        const opxy::i32 length = seconds(e.attack);
        const opxy::i32 rose = length <= dt ? one : f.level + (dt << 15) / length;
        f.level = rose < one ? rose : one;
        f.stage = attack;
    }
    if (!gate && f.stage != quiet && f.stage != release) { f.stage = release; f.from = f.level; f.progress = 0; }
    if (f.stage == attack) {
        const opxy::i32 length = seconds(e.attack);
        f.level = length <= dt ? one : f.level + (dt << 15) / length;
        if (f.level >= one) { f.level = one; f.stage = decay; f.progress = 0; }
        return f.level;
    }
    if (f.stage == decay) {
        f.progress += advance(seconds(e.decay), dt);
        if (f.progress >= one) f.stage = sustain;
        else f.level = top + ((one - top) * left(f.progress) >> 15);
    }
    if (f.stage == sustain) f.level = top;
    if (f.stage == release) {
        f.progress += advance(seconds(e.release), dt);
        if (f.progress >= one) { f.stage = quiet; f.level = 0; }
        else f.level = f.from * left(f.progress) >> 15;
    }
    return f.level;
}

// Whether the voice still sounds: struck and not yet through its release.
inline bool sounding(const Follower& f) { return f.stage != quiet; }
}
