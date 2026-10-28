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
// The start sag, 0..32767: as a note starts the tape catches up. It eases
// from what was left of the last sag (`from`) to its full depth over 23 ms,
// then recovers, halving every 65536 / `rate` blocks (sag_rate: 116 ms with a
// little sag, 348 ms at full, so a deep sag takes over a second to settle).
// `since` counts blocks from the start.
inline i32 sag(u32 since, i32 from, u32 rate) {
    if (since < 16u) return from + (((one - from) * smoothstep(static_cast<i32>(since << 11))) >> 15);
    const u32 t = since - 16u;
    if (t >= 16384u || !rate) return 0;
    const u32 position = t * rate;                                  // Q16 halvings; rate < 2^18
    const u32 halvings = position >> 16;
    if (halvings >= 15u) return 0;
    const i32 top = one >> halvings;
    return top - static_cast<i32>((static_cast<u32>(top >> 1) * ((position & 0xffffu) >> 1)) >> 15);
}

// The controls' curves: Q16 octaves at 0, 25, 50, 75 and 100% of a turn,
// straight between (definition.json's anchors, in cents). Gentler than a
// square, so a quarter turn is already plainly heard and the top is drastic.
struct Curve { i32 at[5]; };
// Steps between anchors stay within +-2^19 (8 octaves) to keep the product in 32 bits.
inline i32 along(i32 control, const Curve& curve) {
    const i32 c = control < 0 ? 0 : control >> 16;                  // Q15
    const i32 segment = c >> 13, f = (c & 8191) >> 1;               // Q12 along the segment
    return curve.at[segment] + (((curve.at[segment + 1] - curve.at[segment]) * f) >> 12);
}
const Curve wow_curve = {{0, 874, 2294, 4260, 6554}};       // 0, 16, 42, 78, +-120 cents
const Curve flutter_curve = {{0, 437, 1092, 1857, 2731}};   // 0, 8, 20, 34, +-50 cents
const Curve sag_curve = {{0, 1638, 3823, 6554, 9830}};      // 0, 30, 70, 120, 180 cents flat
struct Depths {
    i32 wow, stretch, flutter, sag;
    u32 sag_rate;   // the sag's recovery, Q16 halvings a block
};
inline u32 sag_rate(i32 sag_control) {
    const i32 c = sag_control < 0 ? 0 : sag_control >> 16;
    return 65536u / static_cast<u32>(80 + ((c * 160) >> 15));       // halving in 80 .. 240 blocks
}
inline Depths depths(i32 wow_control, i32 flutter_control, i32 sag_control) {
    Depths d;
    d.wow = along(wow_control, wow_curve);
    // A drag sinks the tape by 1.25 times the wow's depth: 150 cents flat at full.
    d.stretch = d.wow + (d.wow >> 2);
    d.flutter = along(flutter_control, flutter_curve);
    d.sag = along(sag_control, sag_curve);
    d.sag_rate = sag_rate(sag_control);
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
inline i32 sag_bend(u32 since, i32 from, const Depths& d) { return -((sag(since, from, d.sag_rate) * d.sag) >> 15); }
}}
