#pragma once
// Orbit's laws as pure functions, shared by the voice and the screen: the
// controls and their units, the orbit's paths, the vector's corner weights,
// and the header's captions. Integer arithmetic only, 32-bit.
#include "waves.h"
#include <opxy/q31.hpp>

namespace orbit {
using opxy::i32;
using opxy::u32;
using opxy::i16;

// Parameter slots (definition.json's order).
enum { BANK, X, Y, ORBIT, RATE, PATH, GRIT, DETUNE };

// The field, Q14: the diamond's vertices lie `one` from its centre. x runs
// right and y down, as on the screen, so corner A is (0, -one), B (one, 0),
// C (0, one) and D (-one, 0).
const i32 one = 16384;
struct Point { i32 x, y; };

inline i32 q15(i32 control) { return control < 0 ? 0 : control >> 16; }
inline i32 clamp(i32 x, i32 low, i32 high) { return x < low ? low : x > high ? high : x; }

// sin of a phase in Q32 cycles, Q15.
inline i32 sine(u32 phase) {
    const u32 i = phase >> 22;
    const i32 f = static_cast<i32>((phase >> 7) & 0x7fffu);
    const i32 a = sine_table[i], b = sine_table[i + 1];
    return a + (((b - a) * f) >> 15);
}

// 2^(k/32), k = 0..32, Q29.
const i32 exp2_table[33] = {
    536870912, 548626854, 560640218, 572916640, 585461881, 598281827, 611382493, 624770026, 638450709, 652430959,
    666717336, 681316545, 696235435, 711481005, 727060411, 742980961, 759250125, 775875538, 792865000, 810226483,
    827968133, 846098274, 864625414, 883558245, 902905651, 922676710, 942880699, 963527098, 984625594, 1006186087,
    1028218694, 1050733751, 1073741824,
};
// 2^(octaves / 65536) for Q16 octaves 0 .. 32 (exclusive): a u32.
inline u32 exp2_q16(i32 octaves) {
    const u32 o = static_cast<u32>(clamp(octaves, 0, 32 * 65536 - 1));
    const u32 index = (o >> 11) & 31u;
    const i32 fraction = static_cast<i32>((o & 2047u) << 20);
    const u32 mantissa = static_cast<u32>(exp2_table[index] +
                                          opxy::q31::multiply(exp2_table[index + 1] - exp2_table[index], fraction));
    const u32 whole = o >> 16;   // mantissa is Q29: 1..2
    return whole >= 29 ? mantissa << (whole - 29) : mantissa >> (29 - whole);
}
// log2 of x, Q16, straight between octaves (within 0.09 of an octave).
inline i32 log2_q16(u32 x) {
    if (!x) return 0;
    u32 m = x;
    i32 msb = 0;
    if (m >> 16) { m >>= 16; msb += 16; }
    if (m >> 8) { m >>= 8; msb += 8; }
    if (m >> 4) { m >>= 4; msb += 4; }
    if (m >> 2) { m >>= 2; msb += 2; }
    if (m >> 1) msb += 1;
    const u32 normal = msb >= 16 ? x >> (msb - 16) : x << (16 - msb);   // 1..2, Q16
    return msb * 65536 + static_cast<i32>(normal - 65536u);
}

// The integer square root.
inline u32 root(u32 value) {
    u32 result = 0, bit = 1u << 30;
    while (bit > value) bit >>= 2;
    while (bit) {
        if (value >= result + bit) { value -= result + bit; result = (result >> 1) + bit; }
        else result >>= 1;
        bit >>= 2;
    }
    return result;
}
// A mix's loudness: its root mean square, Q14, from its weights (Q14) and
// the band's products of corners (waves.h).
inline i32 loudness(u32 bank, u32 level, const i32 (&w)[4]) {
    const i32* g = gram[bank][level];
    const i32 aa = w[0] * w[0] >> 14, ab = w[0] * w[1] >> 13, ac = w[0] * w[2] >> 13, ad = w[0] * w[3] >> 13;
    const i32 bb = w[1] * w[1] >> 14, bc = w[1] * w[2] >> 13, bd = w[1] * w[3] >> 13;
    const i32 cc = w[2] * w[2] >> 14, cd = w[2] * w[3] >> 13, dd = w[3] * w[3] >> 14;
    const i32 square = aa * g[0] + ab * g[1] + ac * g[2] + ad * g[3] + bb * g[4] + bc * g[5] + bd * g[6] +
                       cc * g[7] + cd * g[8] + dd * g[9];   // Q28
    return static_cast<i32>(root(static_cast<u32>(square > 0 ? square : 0)));
}

// Bank: eight banks, an enum (the SDK's quantisation of a control).
inline u32 bank_of(i32 control) {
    const u32 b = static_cast<u32>(q15(control)) * 8u >> 15;
    return b > 7 ? 7 : b;
}
// x and y: the orbit's centre, -100..100 (y up, toward A).
inline Point centre_of(i32 x, i32 y) {
    const Point p = {q15(x) - one, one - q15(y)};
    return p;
}
// Orbit: the path's radius, up to 0.85 of the way to a corner.
const i32 widest = 13926;   // 0.85, Q14
inline i32 radius_of(i32 orbit) { return q15(orbit) * widest >> 15; }

// Rate: 0.02 to 8 Hz, exponential. Its log2 in Q16 octaves of the orbit's
// phase step per 64-sample block (Q32 cycles): 0.02 Hz is 2^16.928 a block,
// and the control spans log2(400) = 8.644 octaves.
inline i32 rate_octaves(i32 rate) { return 1109371 + (q15(rate) * 17703 >> 10); }
inline u32 rate_step(i32 rate) { return exp2_q16(rate_octaves(rate)); }
// The rate in hundredths of a hertz (2 .. 800), for captions and the screen.
inline u32 rate_hundredths(i32 rate) {
    // 0.02 Hz, 2 hundredths, is a step of 124661 a block.
    return (rate_step(rate) + 31165u) / 62331u;
}

// Path: circle, ellipse, figure 8, steps, morphing evenly between neighbours.
// The orbit's offset from its centre at `phase` (Q32 cycles), for a radius
// (Q14) and the path control (Q15). Phase 0 is the top of every path, where
// each note starts. jumpy: steps hold at each corner and jump in the last
// sixth of each step, as a wave sequence does (the sound and the points);
// otherwise they glide straight (the dotted path the screen draws).
inline Point shape(i32 which, u32 phase, i32 r, bool jumpy) {
    Point p;
    if (which == 3) {
        const u32 segment = phase >> 30;
        const i32 u = static_cast<i32>((phase >> 15) & 0x7fffu);
        i32 t = u;
        if (jumpy) {
            t = clamp((u - 27197) * 6024 >> 10, 0, 32768);    // (u - 0.83) / 0.17
            t = (t * t >> 15) * (98304 - 2 * t) >> 15;      // smoothstep
        }
        const i32 cx[4] = {0, r, 0, -r}, cy[4] = {-r, 0, r, 0};
        const u32 next = (segment + 1) & 3u;
        p.x = cx[segment] + ((cx[next] - cx[segment]) * t >> 15);
        p.y = cy[segment] + ((cy[next] - cy[segment]) * t >> 15);
        return p;
    }
    const i32 s = sine(phase), c = sine(phase + 0x40000000u);
    if (which == 0) {             // circle, clockwise from the top
        p.x = r * s >> 15;
        p.y = -(r * c >> 15);
    } else if (which == 1) {      // an ellipse 0.42 wide, tilted 0.6 rad anticlockwise
        const i32 u = (r * s >> 15) * 13763 >> 15, v = -(r * c >> 15);
        p.x = (u * 27045 + v * 18502) >> 15;
        p.y = (v * 27045 - u * 18502) >> 15;
    } else {                      // a figure 8 through the centre
        p.x = r * s >> 15;
        p.y = -(r * sine(phase * 2) >> 16);
    }
    return p;
}
inline Point path_offset(u32 phase, i32 r, i32 path, bool jumpy) {
    const i32 at = clamp(path, 0, 32767) * 3;
    const i32 which = at >= 65536 ? 2 : at >> 15, w = at - (which << 15);
    const Point a = shape(which, phase, r, jumpy);
    if (!w) return a;
    const Point b = shape(which + 1, phase, r, jumpy);
    const Point p = {a.x + ((b.x - a.x) * w >> 15), a.y + ((b.y - a.y) * w >> 15)};
    return p;
}
// The joystick's gate: a point beyond the diamond rides along its edge.
inline Point gate(Point p) {
    const i32 m = (p.x < 0 ? -p.x : p.x) + (p.y < 0 ? -p.y : p.y);
    if (m <= one) return p;
    const i32 k = (one << 14) / m;   // Q14, one division
    const Point q = {p.x * k >> 14, p.y * k >> 14};
    return q;
}
// The vector's point: the centre, plus the path, within the gate.
inline Point point_at(Point centre, u32 phase, i32 r, i32 path, bool jumpy) {
    const Point o = path_offset(phase, r, path, jumpy);
    const Point p = {centre.x + o.x, centre.y + o.y};
    return gate(p);
}
// Each corner's share of the mix at a point, Q14 (summing to one): bilinear
// across the diamond, as on the Prophet VS.
inline void weights(Point p, i32 (&w)[4]) {
    const i32 u = clamp((one + p.x + p.y) >> 1, 0, one), t = clamp((one - p.x + p.y) >> 1, 0, one);
    w[0] = (one - u) * (one - t) >> 14;
    w[1] = u * (one - t) >> 14;
    w[2] = u * t >> 14;
    w[3] = (one - u) * t >> 14;
}

// Grit: the DAC's rate falls from 48 to 10 kHz (exponentially) as its bits
// fall from 16 to 8. The rate in Q16 octaves relative to 44.1 kHz:
// log2(48 / 44.1) = 0.1223 (8012), less log2(4.8) = 2.263 octaves over the turn.
inline i32 grit_octaves(i32 grit) { return 8012 - (q15(grit) * 4635 >> 10); }
// The rate in tenths of a kHz (480 .. 100), for captions and the screen.
inline u32 grit_tenths(i32 grit) { return (exp2_q16(grit_octaves(grit) + 20 * 65536) * 441u + 0x80000u) >> 20; }
// Its bits, Q16: 16 down to 8.
inline i32 grit_bits(i32 grit) { return 16 * 65536 - q15(grit) * 16; }

// Detune: the second set's spread, 0 to 30 cents (by the square of the
// control), the two sets either side of the note. Cents in tenths.
inline i32 detune_tenths(i32 detune) {
    const i32 d = q15(detune);
    return ((d * d >> 15) * 300 + 16384) >> 15;
}
// Each set's offset from the note, Q16 octaves: half the spread.
inline i32 detune_octaves(i32 detune) {
    const i32 d = q15(detune);
    return (d * d >> 15) * 1638 >> 16;   // 15 cents is 819
}

// Captions, as the board writes them.
inline const char* bank_name(u32 bank) {
    static const char* const names[8] = {"glass", "vox", "organ", "reed", "bell", "pluck", "wire", "dusk"};
    return names[bank & 7u];
}
inline u32 put_number(char* out, u32 at, u32 n) {
    char digits[10];
    u32 count = 0;
    do { digits[count++] = static_cast<char>('0' + n % 10); n /= 10; } while (n && count < 10);
    while (count) out[at++] = digits[--count];
    return at;
}
// -100..100 for x and y.
inline void caption_axis(char* out, i32 control) {
    const i32 v = ((q15(control) * 200 + 16384) >> 15) - 100;
    u32 at = 0;
    if (v < 0) out[at++] = '-';
    at = put_number(out, at, static_cast<u32>(v < 0 ? -v : v));
    out[at] = 0;
}
// ".40Hz" under 1 Hz, "1.8Hz" from 1 Hz.
inline void caption_rate(char* out, i32 rate) {
    const u32 h = rate_hundredths(rate);
    u32 at = 0;
    if (h < 100) {
        out[at++] = '.';
        out[at++] = static_cast<char>('0' + h / 10);
        out[at++] = static_cast<char>('0' + h % 10);
    } else {
        const u32 tenths = (h + 5) / 10;
        at = put_number(out, at, tenths / 10);
        out[at++] = '.';
        out[at++] = static_cast<char>('0' + tenths % 10);
    }
    out[at++] = 'H';
    out[at++] = 'z';
    out[at] = 0;
}
// A path's name near it, or the two it lies between.
inline const char* path_name(i32 path) {
    static const char* const names[4] = {"circ", "ellip", "fig8", "steps"};
    static const char* const between[3] = {"c/el", "el/f8", "f8/st"};
    const i32 at = q15(path) * 3, near = (at + 16384) >> 15, off = at - (near << 15);
    if (off > -5898 && off < 5898) return names[near > 3 ? 3 : near];   // within 0.18
    const i32 lower = at >> 15;
    return between[lower > 2 ? 2 : lower];
}
// "clean", then the DAC's rate: "33k" from 20 kHz, "18.2k" below.
inline void caption_grit(char* out, i32 grit) {
    if (q15(grit) < 164) {   // under 0.005
        const char clean[6] = "clean";
        for (u32 i = 0; i < 6; ++i) out[i] = clean[i];
        return;
    }
    const u32 tenths = grit_tenths(grit);
    u32 at = 0;
    if (tenths >= 200) {
        at = put_number(out, at, (tenths + 5) / 10);
    } else {
        at = put_number(out, at, tenths / 10);
        out[at++] = '.';
        out[at++] = static_cast<char>('0' + tenths % 10);
    }
    out[at++] = 'k';
    out[at] = 0;
}
// "9.1c": the spread in cents.
inline void caption_detune(char* out, i32 detune) {
    const u32 tenths = static_cast<u32>(detune_tenths(detune));
    u32 at = put_number(out, 0, tenths / 10);
    out[at++] = '.';
    out[at++] = static_cast<char>('0' + tenths % 10);
    out[at++] = 'c';
    out[at] = 0;
}
}
