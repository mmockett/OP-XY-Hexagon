#pragma once
// Wane's tape: the one worn cassette every voice plays through.
//
// Its motion is a pure function of a block clock (64-sample blocks at 44.1 kHz,
// 689.0625 a second) and a seed, so the screen can include this header and
// draw the same wobble the audio plays, without reading audio state. Only the
// start sag depends on events (when notes start); its shape is a pure function
// of the blocks since the last start and the sag left over from before it.
//
// Units: shapes are Q15 (32767 is one); pitch is Q16 octaves (65536 is an
// octave, 54.6 a cent). Everything is 32-bit integer arithmetic; the hash's two
// 32-bit multiplies run a few times a block, never per sample.
#include <opxy/types.hpp>

namespace wane { namespace tape {
using opxy::i32;
using opxy::u32;

// The tape's seed: the same cassette in the audio and on the screen.
const u32 seed = 0x3a5e1d07u;
const i32 one = 32767;          // Q15
const i32 cent = 55;            // Q16 octaves, rounded: 65536 / 1200 = 54.6

// A display's clock in audio blocks, advanced by its frame's elapsed time
// (Q10 seconds): 689.0625 / 1024 = 11025 / 16384 blocks per Q10 unit.
struct Clock {
    u32 blocks, rest;
    void reset() { blocks = rest = 0; }
    void advance(i32 elapsed_q10) {
        if (elapsed_q10 <= 0) return;
        rest += static_cast<u32>(elapsed_q10 > 4096 ? 4096 : elapsed_q10) * 11025u;
        blocks += rest >> 14;
        rest &= 16383u;
    }
};

// A well-mixed 32-bit hash (lowbias32).
inline u32 hash(u32 x) {
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
// A random Q15 value in -32768..32767 for each index of a stream.
inline i32 random(u32 stream, u32 index) {
    return static_cast<i32>(hash(stream ^ (index * 0x9e3779b9u)) >> 16) - 32768;
}
// Smoothstep of a Q15 fraction, 0..32767: 3f^2 - 2f^3.
inline i32 smoothstep(i32 f) {
    const i32 square = (f * f) >> 15;
    return 3 * square - 2 * ((square * f) >> 15);
}
// Smooth random motion in -32768..32767: a new random target every 2^shift
// blocks (shift 1..15), eased between them.
inline i32 smooth_noise(u32 stream, u32 clock, u32 shift) {
    const u32 knot = clock >> shift;
    const i32 f = static_cast<i32>((clock & ((1u << shift) - 1u)) << (15 - shift));
    const i32 a = random(stream, knot), b = random(stream, knot + 1);
    return a + (((b - a) * smoothstep(f)) >> 15);
}
// A parabolic sine of a 32-bit phase, Q15 (the tape's motions need no purer one).
inline i32 sine(u32 phase) {
    const i32 x = static_cast<i32>(phase >> 16) - 32768;          // -pi..pi as -32768..32767
    const i32 magnitude = x < 0 ? -x : x;
    const i32 y = (x * (32768 - magnitude)) >> 13;                // 4x(1-|x|)
    return -(y > one ? one : y < -one ? -one : y);                // sin, not -sin
}
// Phase increments per block of the tape's motions: Hz x 2^32 / 689.0625.
const u32 wow_rate = 3427295u;        // 0.55 Hz
const u32 wow_rate_2 = 5296729u;      // 0.85 Hz
const u32 flutter_rate = 45501853u;   // 7.3 Hz
const u32 flutter_rate_2 = 67940018u; // 10.9 Hz

// Wow, -32767..32767: the capstan's slow, irregular speed error, about 0.3 to
// 1.5 Hz. Two slow sines whose phases wander, and a smooth random drift, so it
// never settles into one period.
inline i32 wow(u32 clock, u32 stream = seed) {
    const i32 drift = smooth_noise(stream ^ 0x1u, clock, 9);       // a new target every 0.74 s
    const u32 bend = static_cast<u32>(smooth_noise(stream ^ 0x2u, clock, 10)) << 16;   // +-half a cycle
    const i32 slow = sine(clock * wow_rate + bend);
    const i32 slower = sine(clock * wow_rate_2 - (bend >> 1) + 0x40000000u);
    return (drift * 13 + slow * 12 + slower * 7) >> 5;
}
// Flutter, -32767..32767: faster, shallower, 6 to 12 Hz, its depth breathing.
inline i32 flutter(u32 clock, u32 stream = seed) {
    const i32 swell = 22938 + ((smooth_noise(stream ^ 0x3u, clock, 8) * 9830) >> 15);   // 0.4 .. 1
    const u32 jitter = static_cast<u32>(smooth_noise(stream ^ 0x4u, clock, 7)) << 13;
    const i32 mix = (sine(clock * flutter_rate) * 21 + sine(clock * flutter_rate_2 + jitter) * 11) >> 5;
    return (mix * swell) >> 15;
}
// Stretch, 0..32767: now and then the tape drags and the pitch sinks. Every
// 2^11 blocks (3 s) may hold one drag (two in five do): it eases in over
// 0.19 s, holds 0.1 to 0.8 s and eases out over 0.37 s, at half to full depth.
inline i32 stretch(u32 clock, u32 stream = seed) {
    const u32 r = hash(stream ^ 0x5u ^ ((clock >> 11) * 0x85ebca6bu));
    if ((r & 255u) >= 102u) return 0;
    const u32 offset = (r >> 8) & 1023u, hold = 64u + ((r >> 18) & 511u);
    const i32 depth = 16384 + static_cast<i32>((r >> 27) << 9);    // 0.5 .. 0.98
    const u32 at = clock & 2047u;
    if (at < offset) return 0;
    const u32 t = at - offset;
    i32 shape;
    if (t < 128u) shape = smoothstep(static_cast<i32>(t << 8));
    else if (t < 128u + hold) shape = one;
    else if (t < 384u + hold) shape = one - smoothstep(static_cast<i32>((t - 128u - hold) << 7));
    else return 0;
    return (shape * depth) >> 15;
}
// Dropout gain, 0..32767: the oxide is thin in places. Every 2^9 blocks
// (0.74 s) may hold one brief dip, more often with age (at full age, one
// stretch in four; none at zero): 17 to 108 ms deep, with 4 ms edges.
inline i32 dropout(u32 clock, i32 age_q15, u32 stream = seed) {
    if (age_q15 <= 0) return one;
    const u32 chance = static_cast<u32>((age_q15 * age_q15) >> 15) * 65536u / 4u;   // of 2^31
    const u32 r = hash(stream ^ 0x6u ^ ((clock >> 9) * 0xc2b2ae35u));
    if ((r >> 1) >= chance) return one;
    const u32 s = hash(r);
    const u32 offset = s & 255u, length = 12u + ((s >> 8) & 63u);
    const i32 floor = 2621 + static_cast<i32>(((s >> 14) & 255u) * 52u);   // 0.08 .. 0.48
    const u32 at = clock & 511u;
    if (at < offset || at >= offset + length + 6u) return one;
    const u32 t = at - offset;
    const u32 edge = t < 3u ? t : t >= length + 3u ? length + 6u - t : 3u;   // 0..3
    return one - static_cast<i32>(((one - floor) * static_cast<i32>(edge)) / 3);
}
// The start sag, 0..32767: as a note starts the tape catches up. It rises
// from what was left of the last sag (`from`) over 12 ms, then halves every
// 93 ms. `since` counts blocks from the start.
inline i32 sag(u32 since, i32 from) {
    if (since < 8u) return from + (((one - from) * static_cast<i32>(since)) >> 3);
    const u32 t = since - 8u, halvings = t >> 6;
    if (halvings >= 15u) return 0;
    const i32 top = one >> halvings;
    return top - (((top >> 1) * static_cast<i32>(t & 63u)) >> 6);
}

// Depths in Q16 octaves, from the controls' Q31 words; each control is
// squared, so a little is a little.
const i32 wow_most = 40 * 65536 / 1200;       // +-40 cents
const i32 stretch_most = 60 * 65536 / 1200;   // 60 cents flat
const i32 flutter_most = 12 * 65536 / 1200;   // +-12 cents
const i32 sag_most = 45 * 65536 / 1200;       // 45 cents flat
inline i32 squared(i32 control) {
    const i32 c = control < 0 ? 0 : control >> 16;
    return (c * c) >> 15;
}
inline i32 depth(i32 control, i32 most) { return (squared(control) * most) >> 15; }
struct Depths {
    i32 wow, stretch, flutter, sag;
};
inline Depths depths(i32 wow_control, i32 flutter_control, i32 sag_control) {
    Depths d;
    d.wow = depth(wow_control, wow_most);
    d.stretch = depth(wow_control, stretch_most);
    d.flutter = depth(flutter_control, flutter_most);
    d.sag = depth(sag_control, sag_most);
    return d;
}
// The transport's pitch, Q16 octaves, at a block: wow and flutter about the
// true speed, minus any drag. The start sag is subtracted separately.
inline i32 bend(u32 clock, const Depths& d, u32 stream = seed) {
    i32 total = 0;
    if (d.wow) total += ((wow(clock, stream) * d.wow) >> 15) - ((stretch(clock, stream) * d.stretch) >> 15);
    if (d.flutter) total += (flutter(clock, stream) * d.flutter) >> 15;
    return total;
}
inline i32 sag_bend(u32 since, i32 from, const Depths& d) { return -((sag(since, from) * d.sag) >> 15); }
}}
