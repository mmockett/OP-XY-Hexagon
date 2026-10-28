#pragma once
// Bloom's spectrum as pure functions of the controls, a note's seed and the
// note's evolution time. The engine renders exactly what these return; a
// display can include this header and call the same functions to draw what a
// note is doing, since it cannot read audio state. Integer only, no 64-bit
// arithmetic, bounded work: safe on the Blackfin and in display code.
//
// Units used throughout:
//   control  a Q31 parameter word, 0..2^31-1
//   unit     a control as 0..65535 (unit16)
//   log2     Q16 log2: 65536 is one octave (frequency) or 6.02 dB (amplitude)
//   time     Q20 lattice units: the drift picks a new random target every
//            1 << 20; wraps after 4096 targets
//   level    Q15 amplitude of full scale
#include "tables.h"

namespace bloom {

const u32 partial_count = 24;
// A partial is silent at or above 30.85 octaves of phase increment (19.9 kHz;
// 31 is Nyquist) and fades out over the quarter octave below (from 16.7 kHz).
const i32 log2_cutoff = 30 * 65536 + 55706;
const i32 log2_fade = 30 * 65536 + 39322;
// The levels of a note sum to at most peak_level (their sum stays under half
// scale whatever the phases), and their root-sum-square is at most loud_level
// (a pure sine's amplitude), which keeps loudness even as the tilt changes.
const i32 peak_level = 15000;
const i32 loud_level = 12000;
// `time` at or above this unit holds the spectrum (freeze).
const u32 hold_unit = 64553;   // 98.5% of the control's travel

// ---- Fixed-point helpers -------------------------------------------------

// A control word as 0..65535.
inline u32 unit16(i32 control) { return control < 0 ? 0u : static_cast<u32>(control) >> 15; }

// 2^(x / 65536) for 0 <= x < 31 << 16, to 32-bit precision; 0 below 0.
inline u32 pow2(i32 x) {
    if (x < 0) return 0;
    const u32 whole = static_cast<u32>(x) >> 16, i = (static_cast<u32>(x) >> 8) & 255, r = static_cast<u32>(x) & 255;
    const u32 m = exp2_table[i] + ((exp2_table[i + 1] - exp2_table[i]) * r >> 8);   // Q30
    return whole >= 30 ? m << (whole - 30) : m >> (30 - whole);
}

// The position of x's top bit (0 for 0 and 1).
inline u32 top_bit(u32 x) {
    if (!x) return 0;
    u32 e = 0;
    if (x >= 1u << 16) { e += 16; x >>= 16; }
    if (x >= 1u << 8) { e += 8; x >>= 8; }
    if (x >= 1u << 4) { e += 4; x >>= 4; }
    if (x >= 1u << 2) { e += 2; x >>= 2; }
    if (x >= 1u << 1) { e += 1; }
    return e;
}
// log2(x) in Q16 for x > 0 (0 for 0).
inline i32 log2_of(u32 x) {
    if (!x) return 0;
    const u32 whole = top_bit(x), m = x << (31 - whole);   // m: top bit set
    const u32 i = (m >> 23) & 255, r = (m >> 15) & 255;
    return static_cast<i32>((whole << 16) + log2_table[i] + ((log2_table[i + 1] - log2_table[i]) * r >> 8));
}

// Integer square root of a 32-bit value.
inline u32 root(u32 x) {
    u32 result = 0, bit = 1u << 30;
    while (bit > x) bit >>= 2;
    while (bit) {
        if (x >= result + bit) { x -= result + bit; result = (result >> 1) + bit; }
        else result >>= 1;
        bit >>= 2;
    }
    return result;
}

// ---- Controls --------------------------------------------------------------

// The eight controls, decoded once a block. Parameter order matches
// definition.json: tilt, odd, stretch, breath, partials, blur, haze, time.
struct Controls {
    i32 slope;      // tilt: log2 amplitude per octave, Q16 (-24..+6 dB/oct)
    u32 odd_keep;   // gain of the even partials, Q16: 65535 full .. 0 odd only
    i32 count;      // partials sounding, Q16: 1..24, the last one fractional
    u32 stretch;    // 0..65535: partial k sits at k^(1 + stretch / 131072)
    i32 breath;     // level drift depth, log2 Q16: 0..2 octaves (0..12 dB each way)
    u32 blur;       // 0..65535: how far each partial's drift spreads to its neighbours
    i32 haze;       // pitch drift depth, log2 Q16: 0..30 cents (squared law)
    u32 step;       // evolution per block, time units; 0 holds the spectrum
};

// Physical units, also used for the header captions.
inline i32 tilt_slope(i32 control) { return -261247 + static_cast<i32>((unit16(control) >> 4) * 326559u >> 12); }
inline i32 tilt_db(i32 control) {   // dB per octave, rounded: -24..6
    return (static_cast<i32>(unit16(control) * 30u + 32768u) >> 16) - 24;
}
inline i32 partials_q16(i32 control) { return 65536 + static_cast<i32>(unit16(control) * 23u); }
inline i32 partials_shown(i32 control) { return (partials_q16(control) + 32768) >> 16; }
// Partial 2's frequency ratio times 100: 200 (harmonic) .. 283.
inline u32 stretch_x100(i32 control) { return (pow2(566484 + static_cast<i32>(unit16(control) >> 1)) + 1) >> 1; }
inline i32 haze_log2(i32 control) {
    const u32 u = unit16(control), squared = u * u >> 16;
    return static_cast<i32>(1638u * squared >> 16);
}
inline u32 haze_tenths(i32 control) {   // tenths of a cent: 0..300
    const u32 u = unit16(control), squared = u * u >> 16;
    return (300u * squared + 32768u) >> 16;
}
inline bool held(i32 control) { return unit16(control) >= hold_unit; }
// The drift's period, hundredths of a second: 50 (0.5 s) .. 3000 (30 s); 0 when held.
inline u32 time_hundredths(i32 control) {
    if (held(control)) return 0;
    return pow2(369876 + static_cast<i32>(unit16(control) * (387114u >> 3) >> 13));
}
// Time units per 64-sample block: 2^20 * 64 / 44100 / period.
inline u32 time_step(i32 control) {
    if (held(control)) return 0;
    return pow2(758350 - static_cast<i32>(unit16(control) * (387114u >> 3) >> 13));
}

inline Controls decode(const i32* parameters) {
    Controls c;
    c.slope = tilt_slope(parameters[0]);
    const u32 odd = 65535u - unit16(parameters[1]);
    c.odd_keep = odd * odd >> 16;   // squared: the even partials thin out evenly in dB
    c.stretch = unit16(parameters[2]);
    c.breath = static_cast<i32>(unit16(parameters[3]) << 1);
    c.count = partials_q16(parameters[4]);
    c.blur = unit16(parameters[5]);
    c.haze = haze_log2(parameters[6]);
    c.step = time_step(parameters[7]);
    return c;
}

// ---- Drift -------------------------------------------------------------------
// Each partial wanders by smoothed random drift (value noise): every lattice
// point of time holds a seeded random value and the drift eases between them.
// Each partial runs its own clock, offset and 0.75..1.37 times as fast, so the
// partials never breathe in step.

inline u32 hash(u32 x) {
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
// A note's seed from a note counter.
inline u32 note_seed(u32 serial) { return hash(serial * 0x9e3779b9u + 0x632be5abu); }

// The random value at lattice point n of stream `salt`, Q15 -32768..32767.
inline i32 lattice(u32 seed, u32 salt, u32 n) {
    return static_cast<i32>(hash(seed ^ (salt * 0x9e3779b9u) ^ ((n & 4095u) * 0x85ebca6bu)) >> 16) - 32768;
}
// Smoothed random drift of stream `salt` at time t, Q15.
inline i32 noise(u32 seed, u32 salt, u32 t) {
    const u32 n = t >> 20, f = (t >> 5) & 32767u;            // f: Q15 position between points
    const u32 eased = (f * f >> 15) * (98304u - 2 * f) >> 15;  // smoothstep, Q15
    const i32 a = lattice(seed, salt, n), b = lattice(seed, salt, n + 1);
    return a + ((b - a) * static_cast<i32>(eased >> 1) >> 14);
}
// Partial k's (0-based) own clock at note time t.
inline u32 partial_time(u32 seed, u32 k, u32 t) {
    const u32 h = hash(seed + (k + 1) * 0x27d4eb2fu);
    const u32 rate = 192u + (h & 127u) + ((h >> 7) & 31u);   // Q8: 0.75..1.37
    return (t >> 8) * rate + (h ^ (h << 11));
}
// Partial k's level drift and pitch drift at note time t, Q15.
// (from_clock: the same, given partial_time already.)
inline i32 level_from_clock(u32 seed, u32 k, u32 clock) { return noise(seed, 2 * k, clock); }
inline i32 pitch_from_clock(u32 seed, u32 k, u32 clock) { return noise(seed, 2 * k + 1, clock + 0x5a5a5a5au); }
inline i32 level_drift(u32 seed, u32 k, u32 t) { return level_from_clock(seed, k, partial_time(seed, k, t)); }
inline i32 pitch_drift(u32 seed, u32 k, u32 t) { return pitch_from_clock(seed, k, partial_time(seed, k, t)); }
// The shared weather, Q15: one slow gust, the same for every voice on the
// track at the same shared clock, that brightens and darkens a chord together.
inline i32 weather(u32 clock) { return noise(0x5eedu, 99u, clock); }

// ---- The spectrum ------------------------------------------------------------

struct Spectrum {
    i32 pitch[partial_count];   // log2 of each partial's frequency ratio to the note, Q16
    i32 level[partial_count];   // amplitude, Q15 of full scale; the sum is at most peak_level
    u32 sounding;               // partials up to and including the last nonzero level
};

// Every partial's level drift after blur, given each partial's clock.
inline void drifts(const Controls& c, u32 seed, const u32 (&clocks)[partial_count], i32 (&out)[partial_count]) {
    if (!c.breath) {
        for (u32 k = 0; k < partial_count; ++k) out[k] = 0;
        return;
    }
    for (u32 k = 0; k < partial_count; ++k) out[k] = level_from_clock(seed, k, clocks[k]);
    if (!c.blur) return;
    // Blur: two one-pole passes across the partials, up and back down, so each
    // partial's drift spreads to its neighbours and the spectrum swells in broad
    // regions instead of single partials; then the spread is restored.
    const i32 b = static_cast<i32>(c.blur * 55705u >> 16) >> 1;   // Q15, 0..0.85
    for (u32 k = 1; k < partial_count; ++k) out[k] += (out[k - 1] - out[k]) * b >> 15;
    for (u32 k = partial_count - 1; k-- > 0;) out[k] += (out[k + 1] - out[k]) * b >> 15;
    const u32 i = c.blur >> 12, r = c.blur & 4095u;
    const i32 gain = blur_gain[i] + ((blur_gain[i + 1] - blur_gain[i]) * static_cast<i32>(r) >> 12);   // Q12
    for (u32 k = 0; k < partial_count; ++k) {
        const i32 v = out[k] * gain >> 12;
        out[k] = v > 49152 ? 49152 : v < -49152 ? -49152 : v;
    }
}

// The note's spectrum at note time t: each partial's pitch and level.
// gust: weather() at the shared clock (0 for none). note: log2 of the note's
// phase increment (log2_of), which decides what falls above Nyquist.
inline void shape(const Controls& c, u32 seed, u32 t, i32 gust, i32 note, Spectrum& s) {
    u32 clocks[partial_count];
    if (c.breath || c.haze)
        for (u32 k = 0; k < partial_count; ++k) clocks[k] = partial_time(seed, k, t);
    i32 drift[partial_count];
    drifts(c, seed, clocks, drift);
    // The weather leans the whole tilt by up to a quarter of breath's depth per octave.
    const i32 slope = c.slope + ((c.breath >> 2) * gust >> 15);
    i32 log_level[partial_count];
    u32 gain[partial_count];
    i32 loudest = -0x40000000;
    for (u32 k = 0; k < partial_count; ++k) {
        const i32 harmonic = log2_partial[k + 1];
        // Stretch: log2 k * (1 + stretch / 2); haze: the drift, less on partials 1 and 2.
        i32 pitch = harmonic + ((harmonic >> 2) * static_cast<i32>(c.stretch >> 2) >> 13);
        if (c.haze) pitch += (c.haze * pitch_from_clock(seed, k, clocks[k]) >> 15) >> (k == 0 ? 2 : k == 1 ? 1 : 0);
        s.pitch[k] = pitch;
        // Count: whole partials, then the fractional last one; odd: the even partials' gain.
        const i32 whole = c.count - static_cast<i32>(k << 16);
        u32 g = whole >= 65536 ? 65536u : whole <= 0 ? 0u : static_cast<u32>(whole);
        if (k & 1) g = g * c.odd_keep >> 16;
        i32 level = ((slope >> 4) * (harmonic >> 4) >> 8) + ((c.breath >> 2) * drift[k] >> 13);
        const i32 frequency = note + pitch;
        if (frequency >= log2_cutoff) g = 0;
        else if (frequency > log2_fade) level -= (frequency - log2_fade) * 40;
        gain[k] = g;
        log_level[k] = level;
        if (g && level > loudest) loudest = level;
    }
    // Levels relative to the loudest, then normalized for peak and loudness.
    u32 sum = 0, squares = 0;
    for (u32 k = 0; k < partial_count; ++k) {
        u32 r = gain[k] ? pow2(log_level[k] - loudest + (15 << 16)) : 0u;   // Q15, at most 32768
        r = r * gain[k] >> 16;
        s.level[k] = static_cast<i32>(r);
        sum += r;
        squares += r * r >> 6;
    }
    s.sounding = 0;
    if (!sum) {
        for (u32 k = 0; k < partial_count; ++k) s.level[k] = 0;
        return;
    }
    const u32 by_peak = (static_cast<u32>(peak_level) << 16) / sum;
    const u32 rms = root(squares) << 3;
    const u32 by_loudness = rms ? (static_cast<u32>(loud_level) << 16) / rms : by_peak;
    const u32 scale = by_peak < by_loudness ? by_peak : by_loudness;   // Q16
    for (u32 k = 0; k < partial_count; ++k) {
        s.level[k] = static_cast<i32>(static_cast<u32>(s.level[k]) * scale >> 16);
        if (s.level[k]) s.sounding = k + 1;
    }
}

}  // namespace bloom
