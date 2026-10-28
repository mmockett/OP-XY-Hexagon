#pragma once
// Bend's phase distortion as pure functions of the controls, shared by the
// engine and the screen, so the screen draws the maps and waves the audio plays.
//
// A line reads a cosine through a bent phase map. Its input phase u runs once
// a cycle (Q16 of a cycle: a 32-bit phase >> 16); the map gives the output
// phase. bend's share d (Q15) moves every map from the straight line (a pure
// sine) to the bent wave:
//
//   saw    two slopes meeting at (m, 1/2), m = (1 - d) / 2
//   sq     a rise, a hold at 1/2, a rise, a hold at 1 (each rise (1 - d) / 2 long)
//   pulse  a hold at 0, then one cycle in the last 1 - 0.9 d of the cycle
//   dsine  the straight line folded at 1/4, 1/2 and 3/4 by d / 4
//   sawpl  half a cycle in the first 4%, the rest by the half, then a hold
//   reso   the cosine runs at 1 + 5 d times the note, under a saw, triangle
//          or trapezoid window (reso1, reso2, reso3)
//
// warp, beyond the CZ, skews the input phase, slowing the start of the cycle
// and hurrying its end (u - k u (1 - u)), and overruns the output by up to
// 2.5 times, so the cosine wraps before the cycle ends, as hard sync does.
//
// Every map is evaluated as A + B x + the sum of c |x - knot| over at most
// three knots (or r x for the reso waves) in wrapping 32-bit arithmetic: the
// output phase is Q32, one cycle the whole word, so whole cycles fall away.
// Integer arithmetic only, and no division but the band limit's, once a block.
#include <opxy/types.hpp>
#include "tables.h"

namespace bend {
using opxy::i32;
using opxy::u32;

// Parameter slots, in definition.json's order.
enum Slot { WAVE, BEND, DETUNE, RING, INTERVAL, WARP, NOISE, LOFI };
enum Wave { SAW, SQUARE, PULSE, DSINE, SAWPULSE, RESO1, RESO2, RESO3 };

namespace pd {
const u32 waves = 8, intervals = 8;
const char* const wave_names[waves] = {"saw", "sq", "pulse", "dsine", "sawpl", "reso1", "reso2", "reso3"};
const char* const interval_names[intervals] = {"-oct", "uni", "4th", "5th", "m7", "oct", "oct+5", "2oct"};
// Line 2's pitch against line 1: semitones, and Q16 octaves.
const i32 interval_semitones[intervals] = {-12, 0, 5, 7, 10, 12, 19, 24};
const i32 interval_octaves[intervals] = {-65536, 0, 27307, 38229, 54613, 65536, 103765, 131072};

// A control word as Q15, 0..32767.
inline i32 q15(i32 control) { return control < 0 ? 0 : control >> 16; }
// An enum control's choice, as the SDK quantizes it (Q31 to Q15, then even shares).
inline u32 choice(i32 control, u32 count) { return static_cast<u32>(q15(control)) * count >> 15; }
inline i32 clamp(i32 x, i32 low, i32 high) { return x < low ? low : x > high ? high : x; }
inline i32 magnitude(i32 x) { return x < 0 ? -x : x; }

// -cos(2 pi p) for a Q32 phase, Q15, interpolated in the table.
inline i32 cosine(u32 p) {
    const u32 index = p >> 21;
    const i32 low = tables::cosine[index], high = tables::cosine[index + 1];
    return low + (((high - low) * static_cast<i32>((p >> 6) & 32767u)) >> 15);
}
// sin(2 pi p) for a Q32 phase, Q15.
inline i32 sine(u32 p) { return cosine(p + 0x40000000u); }

// 2^f for a Q16 fraction f in 0..65536, Q29 (1..2).
inline i32 exp2_fraction(u32 f) {
    const u32 index = f >> 11;
    if (index >= 32) return tables::power[32];
    const i32 fraction = static_cast<i32>((f & 2047u) << 4);   // Q15
    const i32 low = tables::power[index], high = tables::power[index + 1];
    return low + ((((high - low) >> 9) * fraction) >> 6);   // the step is under 2^25: the product stays in 32 bits
}
// 2^-octaves for Q16 octaves >= 0, Q15 (1.0 is 32768).
inline i32 decay(u32 octaves) {
    const u32 whole = octaves >> 16, f = octaves & 0xffffu;
    if (whole >= 15) return 0;
    if (!f) return 32768 >> whole;
    return (exp2_fraction(65536u - f) >> 15) >> whole;   // 2^(1 - f) / 2
}
// A phase increment moved by Q16 octaves (-12..+12), capped at `fastest`.
const u32 fastest = 0x70000000u;   // 19.3 kHz
inline u32 transpose(u32 increment, i32 octaves) {
    const u32 shifted = static_cast<u32>(clamp(octaves, -12 * 65536, 12 * 65536) + 16 * 65536);
    const u32 whole = shifted >> 16;   // 16 is no shift
    // (increment / 4) x 2^f, the product split so it stays in 32 bits.
    const u32 quarter = increment >> 2, mantissa = static_cast<u32>(exp2_fraction(shifted & 0xffffu)) >> 14;   // Q15
    const u32 base = (quarter >> 15) * mantissa + (((quarter & 0x7fffu) * mantissa) >> 15);
    u32 result;
    if (whole >= 14) {
        const u32 up = whole - 14;
        result = up >= 30 || base > (fastest >> up) ? fastest : base << up;
    } else {
        result = base >> (14 - whole);
    }
    return result > fastest ? fastest : result;
}

// The controls in the units they play.
// detune: line 2 up to 40 cents from line 1, 40 x^2: Q16 octaves, and tenths of a cent.
inline i32 detune_octaves(i32 control) { const i32 x = q15(control); return ((x * x) >> 15) * 2185 >> 15; }
inline u32 detune_tenths(i32 control) { const i32 x = q15(control); return static_cast<u32>((((x * x) >> 15) * 400 + 16384) >> 15); }
// lofi: the DAC's rate falls from 48 to 4 kHz (48 x 12^-x) as its depth falls
// from 16 to 4 bits (16 - 12 x, to the nearest bit). The rate in Q16 octaves
// below 48 kHz, and in tens of Hz.
inline u32 lofi_octaves(i32 control) { return static_cast<u32>(q15(control) * 7342 >> 10); }
inline u32 lofi_rate_tens(i32 control) { return static_cast<u32>((4800 * decay(lofi_octaves(control)) + 16384) >> 15); }
inline u32 lofi_bits(i32 control) { return 16u - static_cast<u32>((q15(control) * 12 + 16384) >> 15); }
// noise: the phase deviation's rms, Q16 cycles: 0.015, 0.045, 0.1 and 0.22
// of a cycle at 25, 50, 75 and 100%, straight between.
const i32 noise_anchors[5] = {0, 983, 2949, 6554, 14418};
inline i32 noise_depth(i32 control) {
    const i32* at = noise_anchors;
    const i32 x = q15(control), segment = x >> 13, f = x & 8191;
    return segment >= 4 ? at[4] : at[segment] + (((at[segment + 1] - at[segment]) * f) >> 13);
}
// warp's overrun, 1 to 2.5, Q12.
inline i32 overrun(i32 warp) { return 4096 + (warp * 6144 >> 15); }

// A line's map for a block.
struct Line {
    u32 a;        // the output phase at u = 0 less the knots' share, Q32
    u32 b;        // the mean of the first and last slopes, Q16
    i32 c[3];     // half the change of slope at each knot, Q16
    i32 k[3];     // the knots, Q16 of the cycle
    u32 knots;    // 3 for the bent waves (some may not bend), 0 for the reso waves
    u32 r;        // reso: the cosine's rate against the note, Q16, overrun included
    i32 skew;     // warp's input skew, Q15
};

// The map through its slopes (Q13, overrun included) and knots (Q16).
inline void slopes(Line& line, const i32 (&slope)[4], const i32 (&knot)[3]) {
    line.b = static_cast<u32>((slope[0] + slope[3]) * 4);   // (first + last) / 2, Q16
    u32 a = 0;
    for (u32 i = 0; i < 3; ++i) {
        line.c[i] = (slope[i + 1] - slope[i]) * 4;   // / 2, Q16
        line.k[i] = knot[i];
        a -= static_cast<u32>(line.c[i]) * static_cast<u32>(knot[i]);
    }
    line.a = a;
    line.knots = 3;
}

// The map of `wave` bent by d (Q15) with warp (Q15). `most` (pd::ceiling) is
// how far the line's pitch lets it bend: the whole turn is scaled into it, so
// high notes bend less but every part of the turn still bends them. The saw,
// square and pulse steepen as 1 / (1 - d), so they take d through
// 1 - (1 - d)^2 first, which spreads their brightening evenly over the turn,
// and stop at 0.985, where the steepest rise is 1.5% of the cycle.
inline i32 taper(u32 wave, i32 d) {
    if (wave > PULSE) return d;
    const i32 rest = 32768 - d;
    return 32768 - ((rest * rest) >> 15);
}
inline void shape(Line& line, u32 wave, i32 d, i32 warp, i32 most = 32767) {
    const i32 g = overrun(warp);                  // Q12
    if (wave <= PULSE && most > 32276) most = 32276;
    const i32 dd = (taper(wave, d) * most) >> 15;
    d = dd;
    const i32 g13 = g << 1;                       // the straight slope with the overrun, Q13
    line.skew = warp;
    line.r = 0;
    if (wave >= RESO1) {
        line.knots = 0;
        line.a = line.b = 0;
        for (u32 i = 0; i < 3; ++i) { line.c[i] = 0; line.k[i] = 0; }
        line.r = static_cast<u32>((8192 + (d * 5 >> 2)) * g >> 9);   // (1 + 5 d) g, Q16
        return;
    }
    if (wave == SAW) {
        // Half a cycle by m, the other half over the rest (two knots that do not bend).
        const i32 m = 32768 - dd;
        const i32 rest = static_cast<i32>((static_cast<u32>(g) << 16) / static_cast<u32>(65536 - m));
        const i32 sl[4] = {static_cast<i32>((static_cast<u32>(g) << 16) / static_cast<u32>(m)), rest, rest, rest};
        const i32 k[3] = {m, 49152, 57344};
        slopes(line, sl, k);
    } else if (wave == SQUARE) {
        const i32 q = 32768 - dd;   // each rise, Q16
        const i32 rise = static_cast<i32>((static_cast<u32>(g) << 16) / static_cast<u32>(q));
        const i32 sl[4] = {rise, 0, rise, 0};
        const i32 k[3] = {q, 32768, 32768 + q};
        slopes(line, sl, k);
    } else if (wave == PULSE) {
        const i32 hold = dd * 9 / 5;   // 0.9 d, Q16
        const i32 rise = static_cast<i32>((static_cast<u32>(g) << 17) / static_cast<u32>(65536 - hold));
        const i32 sl[4] = {0, rise, rise, rise};
        const i32 k[3] = {hold, 49152 + (hold >> 2), 57344 + (hold >> 3)};
        slopes(line, sl, k);
    } else if (wave == DSINE) {
        // Slopes 1 + d, 1 - 2 d, 1, 1 + d.
        const i32 sl[4] = {(g13 * (32768 + d)) >> 15, (g13 * (32768 - 2 * d)) >> 15, g13, (g13 * (32768 + d)) >> 15};
        const i32 k[3] = {16384, 32768, 49152};
        slopes(line, sl, k);
    } else {   // SAWPULSE: slopes 1 + 11.5 d, 1 + 0.087 d, 1 - d
        const i32 last = (g13 * (32768 - d)) >> 15;
        const i32 sl[4] = {g13 + ((((g13 * d) >> 15) * 23) >> 1), (g13 * (32768 + ((d * 2850) >> 15))) >> 15, last, last};
        const i32 k[3] = {2621, 32768, 49152};
        slopes(line, sl, k);
    }
}

// The input phase skewed by warp: x - k x (1 - x), Q16.
inline i32 skewed(i32 x, i32 skew) { return x - (((((x >> 1) * ((65536 - x) >> 1)) >> 14) * skew) >> 15); }
// The output phase at a (skewed) input phase, Q32.
inline u32 phase_at(const Line& line, i32 x) {
    if (!line.knots) return line.r * static_cast<u32>(x);
    u32 p = line.a + line.b * static_cast<u32>(x);
    for (u32 i = 0; i < 3; ++i) p += static_cast<u32>(line.c[i]) * static_cast<u32>(magnitude(x - line.k[i]));
    return p;
}
// A reso wave's window at u, Q16: saw, triangle, trapezoid.
inline i32 window(u32 wave, i32 x) {
    if (wave == RESO1) return 65535 - x;
    if (wave == RESO2) return 65536 - magnitude(2 * x - 65536);
    const i32 w = 2 * (65536 - x);
    return w < 65536 ? w : 65536;
}
// The line's output at u (Q16), its phase moved by `jitter` (Q32), Q15.
inline i32 output(const Line& line, u32 wave, i32 x, u32 jitter = 0) {
    const i32 v = cosine(phase_at(line, line.skew ? skewed(x, line.skew) : x) + jitter);
    return wave >= RESO1 ? (v * window(wave, x)) >> 16 : v;
}

// How far bend may go for a line of this increment: the steepest part of a
// bent map may run the cosine at most an eighth of the sample rate (5.5 kHz),
// so its corners' harmonics fade before they could fold back below 12 kHz;
// a reso wave's smooth cosine may run to a fifth (8.8 kHz). Returns d's
// ceiling (Q15) and, past even the straight map, lowers warp (whose overrun and
// skew also speed the cosine).
const u32 bent_limit = 536870912u, reso_limit = 858993459u;   // 0.125 and 0.2 x 2^32
struct Ceiling { i32 d, warp; };
inline Ceiling ceiling(u32 wave, u32 increment, i32 warp) {
    Ceiling c;
    c.warp = warp;
    // The fastest the cosine may run against the note, Q13.
    u32 room = (wave >= RESO1 ? reso_limit : bent_limit) / ((increment >> 13) | 1u);
    if (room > (1u << 22)) room = 1u << 22;
    // The fastest the straight map runs: overrun x (1 + skew), Q12.
    const i32 speed = (overrun(warp) * (32768 + warp)) >> 15;
    const i32 rest = static_cast<i32>((room << 8) / static_cast<u32>(speed >> 4));   // room / speed, Q13
    if (rest < 8192) {
        // Even straight the map runs too fast: take warp down in step.
        const i32 most = static_cast<i32>(room) - 8192;   // room for (speed - 1), Q13
        const i32 over = (speed - 4096) << 1;             // speed - 1, Q13
        c.warp = most <= 0 || over <= 0 ? 0 : static_cast<i32>(static_cast<u32>(warp) * static_cast<u32>(most) / static_cast<u32>(over));
        c.d = 0;
        return c;
    }
    const i32 spare = (rest - 8192) << 2;                                        // L - 1, Q15
    const i32 inverse = static_cast<i32>((1u << 28) / static_cast<u32>(rest));   // 1 / L, Q15
    switch (wave) {
    case SAW: case SQUARE: c.d = 32768 - inverse; break;   // 1 / (1 - d) <= L
    case PULSE: c.d = (32768 - inverse) * 10 / 9; break;    // 1 / (1 - 0.9 d) <= L
    case DSINE: c.d = spare; break;                        // 1 + d <= L
    case SAWPULSE: c.d = spare * 2 / 23; break;             // 1 + 11.5 d <= L
    default: c.d = spare / 5; break;                       // reso: 1 + 5 d <= L
    }
    c.d = clamp(c.d, 0, 32767);
    return c;
}

// Each note's DCW contour, the share of bend's remaining travel it adds, Q15:
// from `from` (where the last note left it) it rises within 35 ms to 0.52,
// decays over 0.8 s to 0.14 and breathes +-0.07 at `rate` (Q32 cycles a block)
// while held. `age` counts 64-sample blocks from the note's start.
const u32 rise_octaves = 3920, fall_octaves = 172;   // Q16 octaves a block: tau 35 ms and 0.8 s
const u32 breath_slowest = 1682911u, breath_spread = 373981u;   // 0.27 Hz, and up to 0.06 Hz more
inline i32 contour(u32 age, i32 from, u32 rate) {
    const u32 held = age < 0x10000u ? age : 0x10000u;
    const i32 rise = 32768 - decay(held * rise_octaves);
    const i32 level = 4588 + ((12452 * decay(held * fall_octaves)) >> 15);
    const i32 breath = (2294 * sine(age * rate)) >> 15;
    return from + (((level + breath - from) * rise) >> 15);
}
// bend's share d, Q15: the knob plus the contour's share of what is left,
// the contour fading in over bend's first fifth so that bend 0 is a still sine.
inline i32 bent(i32 bend, i32 envelope) {
    const i32 depth = bend >= 6554 ? 32768 : bend * 5;
    return clamp(bend + ((((32767 - bend) * envelope) >> 15) * depth >> 15), 0, 32767);
}
}
}
