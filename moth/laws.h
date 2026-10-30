#pragma once
// Moth's laws: how each control maps to the sound, and how the vactrol moves,
// as pure functions of the controls. Audio and the screen read the same
// numbers here, so the screen can draw the folded wave and each voice's
// photocell without any audio state. Nothing in this header changes at run
// time, and it uses integers only.
#include <opxy/types.hpp>
#include "sine.h"

namespace moth {
using opxy::i32;
using opxy::u32;

// Controls, in slot order: page 1 fold, fm, ratio, strike; page 2 glow, bias, mode, drift.
enum { kFold, kFm, kRatio, kStrike, kGlow, kBias, kMode, kDrift };

// A Q31 control word as 0..32767.
inline i32 control(i32 word) { return word < 0 ? 0 : word >> 16; }

// (a * b) >> 16 without a 64-bit product: exact while the result fits 32 bits.
inline u32 times(u32 a, u32 b) {
    const u32 ah = a >> 16, al = a & 0xffffu, bh = b >> 16, bl = b & 0xffffu;
    return (ah * bh << 16) + ah * bl + al * bh + (al * bl >> 16);
}

// Index of the highest set bit; value must not be 0.
inline u32 top_bit(u32 value) {
    u32 n = 0;
    if (value >= 1u << 16) { n += 16; value >>= 16; }
    if (value >= 1u << 8) { n += 8; value >>= 8; }
    if (value >= 1u << 4) { n += 4; value >>= 4; }
    if (value >= 1u << 2) { n += 2; value >>= 2; }
    if (value >= 1u << 1) n += 1;
    return n;
}

// Sixteenth-octave tables for log2_q16 and exp2_q16, at file scope: the
// Blackfin GCC would otherwise give an inline function's static table to
// every object that uses it.
static const i32 log2_table[17] = {0, 5732, 11136, 16248, 21098, 25711, 30109, 34312, 38336,
                                   42196, 45904, 49472, 52911, 56229, 59434, 62534, 65536};
static const u32 exp2_table[17] = {65536, 68438, 71468, 74632, 77936, 81386, 84990, 88752, 92682,
                                   96785, 101070, 105545, 110218, 115098, 120194, 125515, 131072};

// log2(value) in Q16, within 0.0004 octave; 0 for 0.
inline i32 log2_q16(u32 value) {
    const i32* const table = log2_table;
    if (!value) return 0;
    const u32 top = top_bit(value);
    const u32 normal = top >= 16 ? value >> (top - 16) : value << (16 - top);  // 65536..131071
    const u32 fraction = normal & 0xffffu, at = fraction >> 12, rest = fraction & 0xfffu;
    return static_cast<i32>(top << 16) + table[at] + ((table[at + 1] - table[at]) * static_cast<i32>(rest) >> 12);
}

// 2^(x / 65536) in Q16 (65536 is 1.0), for x up to +15 octaves; 0 far below.
inline u32 exp2_q16(i32 x) {
    const u32* const table = exp2_table;
    const i32 whole = x >> 16;
    const u32 fraction = static_cast<u32>(x) & 0xffffu, at = fraction >> 12, rest = fraction & 0xfffu;
    const u32 m = table[at] + ((table[at + 1] - table[at]) * rest >> 12);
    if (whole >= 15) return m << 15;
    if (whole >= 0) return m << whole;
    return whole <= -31 ? 0 : m >> -whole;
}

// e^-(2^l), Q16: the share of a decay with time constant tau left after a
// time t, where l = log2(t / tau) in Q16.
inline u32 decay(i32 log2_ratio) {
    if (log2_ratio > 4 * 65536) return 0;  // more than 16 time constants
    const u32 octaves = times(exp2_q16(log2_ratio), 94548u);  // times log2 e
    return octaves >= (30u << 16) ? 0 : exp2_q16(-static_cast<i32>(octaves));
}

// ---------------------------------------------------------------------------
// The oscillators.

// ratio: sixteen stops of the modulator over the carrier, Q16: harmonic,
// near-harmonic (1.01 and 2.02 beat slowly) and inharmonic (bells). The
// control is an enum: each stop takes a sixteenth of its travel.
const u32 kStops = 16;
static const u32 kRatioQ16[kStops] = {32768, 65536, 66191, 92406, 98304, 131072, 132383, 163840,
                                      180879, 196608, 229376, 262144, 308019, 327680, 393216, 458752};
static const char kRatioLabel[kStops][5] = {"0.50", "1.00", "1.01", "1.41", "1.50", "2.00", "2.02", "2.50",
                                            "2.76", "3.00", "3.50", "4.00", "4.70", "5.00", "6.00", "7.00"};
inline u32 ratio_stop(i32 ratio) { return static_cast<u32>(ratio < 0 ? 0 : ratio > 32767 ? 32767 : ratio) >> 11; }

// fm: the modulation index, 4 x^2 radians, Q16.
inline i32 index_q16(i32 fm) { return (fm * fm >> 15) * 8; }

// fold: the folder's drive, 0.02 + 6.8 x radians, Q16: a sine passes nearly
// untouched at 0; past a quarter turn every further half turn adds a fold,
// so 100 folds it about four times.
inline i32 drive_q16(i32 fold) { return 1311 + (fold * 13926 >> 10); }

// Radians (Q16) to a Q32 phase: 2^32 / 2 pi, Q16.
const u32 kRadian = 683565276u;

// The folder, sin(A (x + b/2)) for bias b: its multiplier of a Q15 input
// (A / 2 pi, times 2^14, so the phase is (x turns) << 3), the phase of its
// offset A b/2, and the gain (times 64) that brings its peak to 1. The peak
// is 1 once A (1 + b/2) reaches a quarter turn; below, it is sin(A (1 + b/2)).
struct Fold { i32 turns; u32 offset; i32 gain; };
inline Fold fold_plan(i32 drive, i32 bias) {
    Fold f;
    f.turns = static_cast<i32>(times(static_cast<u32>(drive), 2608u));
    f.offset = static_cast<u32>(f.turns) * static_cast<u32>(bias) * 4u;
    const u32 reach = static_cast<u32>(drive) + times(static_cast<u32>(drive), static_cast<u32>(bias));
    f.gain = 64;
    if (reach < 102944u)
        f.gain = static_cast<i32>(((0xffffffffu / static_cast<u32>(sine22(kSine22, times(reach, kRadian)) >> 6)) + 512u) >> 10);
    return f;
}
// The folder at x (Q15), its peak scaled to 1, Q28.
inline i32 folded(const Fold& f, i32 x) { return sine22(kSine22, (static_cast<u32>(x * f.turns) << 3) + f.offset) * f.gain; }

// drift: the pitch wander's reach, 12.5 (x + x^2) cents, Q16 (3.9 cents at
// 25, 25 at 100), and the drive's, 25 x percent either way, Q15.
inline i32 drift_cents_q16(i32 drift) { return (drift + (drift * drift >> 15)) * 25; }
inline i32 drift_fold_q15(i32 drift) { return drift >> 2; }

// Slow seeded wander, -32767..32767: 0.6 of a slow sine (0.11 to 0.16 Hz)
// and 0.4 of a quicker one (0.29 to 0.39 Hz), as the board draws it. Rates
// are Q32 cycles a second; a voice picks its own from its seed.
inline u32 wander_rate(u32 seed, u32 quick) {
    const u32 h = (seed >> (quick ? 8 : 0)) & 0xffu;  // 0..255
    return quick ? 1245540000u + h * 1684000u : 472446000u + h * 842000u;
}

// ---------------------------------------------------------------------------
// The lowpass gate: a vactrol. Its LED follows the note, held at 1 - strike,
// with a strike's pulse at note start; the photocell opens fast (the pulse
// drives it open in about a millisecond, so it pings over the held level)
// and closes at glow's pace, faster while it is still bright, as a vactrol
// does. An unstruck note's cell opens at a twentieth of glow's pace, so a
// long glow also swells in.

const i32 kOpen = 1 << 30;       // the photocell fully lit, Q30
const u32 kPulse = 262;          // the strike's pulse: 4 ms, Q16 seconds
const u32 kBlockSeconds = 95;    // a 64-sample block, Q16 seconds (1.451 ms)
const i32 kBlockLog2 = -617905;  // log2 of a block's seconds, Q16
const i32 kFall = 172628;        // log2 6.21: glow is the time to fall from open to -60 dB

// glow: 40 ms to 6 s, exponentially; log2 seconds, Q16.
inline i32 glow_log2(i32 glow) { return -304340 + (glow * 14804 >> 10); }

struct Vactrol {
    i32 strike;      // 0..32767
    i32 sustain;     // the LED's held level, Q30
    i32 log2_close;  // log2 of the cell's slowest closing time constant, Q16 seconds
    i32 log2_swell;  // log2 of an unstruck cell's opening time constant
};

inline Vactrol vactrol(i32 strike, i32 glow) {
    Vactrol v;
    v.strike = strike < 0 ? 0 : strike > 32767 ? 32767 : strike;
    v.sustain = (32767 - v.strike) << 15;
    const i32 lg = glow_log2(glow);
    v.log2_close = lg - kFall;
    // The swell: 2 ms, plus 5% of glow for the share of the note not struck.
    const u32 swell = 131u + (times(exp2_q16(lg), 3277u) * static_cast<u32>(32767 - v.strike) >> 15);
    v.log2_swell = log2_q16(swell) - (16 << 16);
    return v;
}

// One step of the cell, g (Q30), over dt (Q16 seconds, log2 log2_dt), its note
// struck `since` ago (Q16 seconds) and still held.
inline i32 gate_step(i32 g, u32 since, u32 dt, i32 log2_dt, const Vactrol& v) {
    if (since < kPulse && v.strike) {
        // The pulse drives the cell open with a time constant of 1 ms, or
        // more slowly for a light strike (under 12.5, down to 0 at 0).
        const u32 overlap = kPulse - since < dt ? kPulse - since : dt;
        const u32 force = v.strike >= 4096 ? 32768u : static_cast<u32>(v.strike) * 8u;
        const u32 x = force * overlap >> 5;  // Q16: overlap over the time constant
        const u32 octaves = times(x, 94548u);
        const u32 keep = octaves >= (30u << 16) ? 0 : exp2_q16(-static_cast<i32>(octaves));
        // While the pulse lights the LED the cell cannot close.
        return g + static_cast<i32>(times(static_cast<u32>(kOpen - g), 65536u - keep));
    }
    // After it, the cell settles toward the held level.
    if (g < v.sustain) {
        const u32 keep = decay(log2_dt - v.log2_swell);
        g += static_cast<i32>(times(static_cast<u32>(v.sustain - g), 65536u - keep));
    } else if (g > v.sustain) {
        // Closing: the time constant is 0.3 of the slowest while fully lit,
        // rising to all of it as the cell darkens.
        const u32 shape = 19661u + (45875u * (static_cast<u32>(kOpen - g) >> 14) >> 16);
        const i32 log2_tau = v.log2_close + log2_q16(shape) - (16 << 16);
        const u32 keep = decay(log2_dt - log2_tau);
        g -= static_cast<i32>(times(static_cast<u32>(g - v.sustain), 65536u - keep));
    }
    return g;
}

// mode: how much the gate filters (lp and combo) and how much it fades
// (combo and vca), Q15. lp at 0, combo at the middle, vca at 100.
// Each follows the cell's light to that power (lit below), so the morph is
// even in decibels and octaves.
struct Mix { i32 filter, fade; };
inline Mix mode_mix(i32 mode) {
    Mix m;
    m.filter = mode < 16384 ? 32767 : 32767 - (mode - 16384) * 2;
    m.fade = mode >= 16384 ? 32767 : mode * 2;
    if (m.filter < 0) m.filter = 0;
    return m;
}

// The cell's light g (Q30) to the power `amount` (Q15), Q30: g at 1, open at 0.
inline i32 lit(i32 g, i32 amount) {
    if (g >= kOpen || amount <= 0) return kOpen;
    if (g <= 0) return 0;
    const i32 octaves = log2_q16(static_cast<u32>(g)) - (30 << 16);  // at most 0
    return static_cast<i32>(exp2_q16((octaves >> 6) * amount >> 9) << 14);
}

// ---------------------------------------------------------------------------
// Captions for the header, at most five characters.

inline void put(char* out, u32 capacity, const char* text) {
    u32 i = 0;
    for (; i + 1 < capacity && text[i]; ++i) out[i] = text[i];
    for (; i < capacity; ++i) out[i] = 0;
}
// A whole number, then optional text.
inline u32 put_number(char* out, u32 at, u32 n) {
    char digits[10];
    u32 count = 0;
    do { digits[count++] = static_cast<char>('0' + n % 10); n /= 10; } while (n && count < 10);
    while (count) out[at++] = digits[--count];
    return at;
}
// n / 10^places with a point, e.g. 36 at two places: "0.36".
inline void fixed_caption(char* out, u32 capacity, u32 n, u32 places, const char* unit) {
    char text[16];
    u32 scale = places == 2 ? 100 : 10;
    u32 at = put_number(text, 0, n / scale);
    text[at++] = '.';
    if (places == 2) text[at++] = static_cast<char>('0' + n / 10 % 10);
    text[at++] = static_cast<char>('0' + n % 10);
    for (u32 i = 0; unit[i] && at < 15; ++i) text[at++] = unit[i];
    text[at] = 0;
    put(out, capacity, text);
}

// fm: the index, e.g. "0.36".
inline void index_caption(char* out, u32 capacity, i32 fm) {
    fixed_caption(out, capacity, static_cast<u32>(((fm * fm >> 15) * 400 + 16384) >> 15), 2, "");
}
inline void ratio_caption(char* out, u32 capacity, i32 ratio) { put(out, capacity, kRatioLabel[ratio_stop(ratio)]); }
// glow: "40ms" to "99ms", then "0.10s" to "0.99s" and "1.0s" to "6.0s": three
// figures of ms overflow the header's value column ("100ms" is 61 px of 53).
inline void glow_caption(char* out, u32 capacity, i32 glow) {
    const u32 ms = (exp2_q16(glow_log2(glow) + 653083) + 32768u) >> 16;  // seconds times 1000
    if (ms < 100) {
        char text[8];
        const u32 at = put_number(text, 0, ms);
        text[at] = 'm';
        text[at + 1] = 's';
        text[at + 2] = 0;
        put(out, capacity, text);
    } else if (ms < 995) {
        fixed_caption(out, capacity, (ms + 5) / 10, 2, "s");
    } else {
        fixed_caption(out, capacity, (ms + 50) / 100, 1, "s");
    }
}
// mode: the nearest of lp, both (combo) and vca; four characters at most,
// so it fits the header's value column.
inline void mode_caption(char* out, u32 capacity, i32 mode) {
    put(out, capacity, mode < 8192 ? "lp" : mode < 24576 ? "both" : "vca");
}
// drift: cents, e.g. "3.9c".
inline void drift_caption(char* out, u32 capacity, i32 drift) {
    fixed_caption(out, capacity, static_cast<u32>(((drift + (drift * drift >> 15)) * 125 + 16384) >> 15), 1, "c");
}
}
