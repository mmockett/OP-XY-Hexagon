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
// The levels of a note sum to at most peak_level (so a voice stays under
// declared peak whatever the phases), and their root-sum-square is at most
// loud_level, which keeps loudness even from a pure sine to a full, bright
// spectrum: only the brightest, most spread spectra reach peak_level first.
const i32 peak_level = 22000;
const i32 loud_level = 7600;
// `time` at or above this unit holds the drift (freeze).
const u32 hold_unit = 64553;   // 98.5% of the control's travel
// Shape's halfway point: below it the even partials fade, above it a pulse narrows.
const u32 square_unit = 32768;
// Vowel fades in over the first tenth of its travel, on oo.
const u32 vowel_onset = 6554;
// Haze's beats advance at most 0.4 radians a block (about 88 Hz), so the
// block-rate envelope stays smooth.
const u32 beat_cap = 273426110;

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

// |sin| of an angle in Q16 turns, Q15.
inline i32 abs_sine(u32 turns) {
    u32 y = turns & 32767u;            // |sin| repeats every half turn
    if (y > 16384u) y = 32768u - y;    // and mirrors about the quarter
    const u32 i = y >> 8, r = y & 255u;
    if (i >= 64) return 32767;
    return quarter_sine[i] + ((quarter_sine[i + 1] - quarter_sine[i]) * static_cast<i32>(r) >> 8);
}

// A smooth bump over log frequency: 1 at distance 0, 0 from 0.45 octave, Q15.
const i32 formant_reach = 29491;
inline i32 bump(i32 distance) {
    const i32 d = distance < 0 ? -distance : distance;
    if (d >= formant_reach) return 0;
    const i32 x = d * 36409 >> 15;                // d / reach, Q15
    const i32 rest = 32768 - (x * x >> 15);
    return rest * rest >> 15;
}

// ---- Controls --------------------------------------------------------------

// The eight controls, decoded once a block. Parameter order matches
// definition.json: tilt, shape, stretch, vowel, breath, blur, haze, time.
struct Controls {
    i32 slope;          // tilt: log2 amplitude per octave, Q16 (-24..+6 dB/oct)
    u32 even_keep;      // shape's first half: the even partials' gain, Q16 (65536 saw .. 0 square)
    u32 pulse;          // shape's second half, 0..65535 from square to the narrowest pulse; 0 below
    u32 stretch;        // partial k sits at k^(1 + stretch / 65536): 0..38336 (k^1.585)
    u32 vowel_depth;    // Q15: 0 off .. 32768
    u32 vowel_position; // Q16 vowels: 0 oo, 1 oh, 2 ah, 3 eh, 4 ee
    i32 breath;         // level drift depth, log2 Q16: 0..3 octaves (18 dB each way)
    u32 breath_unit;    // breath as 0..65535: how far pulse width and vowel wander
    i32 blur;           // each partial's random pitch spread, log2 Q16: 0..120 cents
    u32 blur_alpha;     // the spread's one-pole smoothing a block, Q12 (8..38 Hz)
    u32 blur_gain;      // its input gain, Q8, so the spread's deviation stays even
    u32 haze_ratio;     // the detuned pair's frequency difference, Q16 of the partial's
    u32 haze_depth;     // how fully the pair beats, Q15
    u32 haze_loud;      // root mean square of the beat envelope, Q15
    u32 step;           // drift per block, time units; 0 holds it
};

// Physical units, also used for the header captions.
inline i32 tilt_slope(i32 control) { return -261247 + static_cast<i32>((unit16(control) >> 4) * 326559u >> 12); }
inline i32 tilt_db(i32 control) {   // dB per octave, rounded: -24..6
    return (static_cast<i32>(unit16(control) * 30u + 32768u) >> 16) - 24;
}
// Shape: below halfway the even partials fade (saw to square); above, the
// pulse narrows from 50% to 6.25%. width: the pulse's duty, Q16 of a period.
inline u32 shape_pulse(i32 control) {
    const u32 u = unit16(control);
    return u <= square_unit ? 0u : (u - square_unit) << 1;
}
inline u32 pulse_width(u32 pulse) { return pow2((15 << 16) - 3 * static_cast<i32>(pulse)); }
inline u32 shape_square_percent(i32 control) {   // how far from saw to square, 0..100
    const u32 u = unit16(control);
    return u >= square_unit ? 100u : (u * 100u + 16384u) >> 15;
}
inline u32 shape_width_percent(i32 control) {    // pulse width, 6..50
    return (pulse_width(shape_pulse(control)) * 100u + 32768u) >> 16;
}
// Partial 2's frequency ratio times 100: 200 (harmonic) .. 300.
inline u32 stretch_excess(i32 control) { return unit16(control) * 38336u >> 16; }
inline u32 stretch_x100(i32 control) { return (pow2(566484 + static_cast<i32>(stretch_excess(control))) + 1) >> 1; }
// Vowel: 0 off; the nearest vowel, 0 oo .. 4 ee.
inline u32 vowel_position(i32 control) {
    const u32 u = unit16(control);
    return u <= vowel_onset ? 0u : (u - vowel_onset) * 4551u >> 10;   // 65535 - 6554 maps to 4 << 16
}
inline u32 vowel_shown(i32 control) {
    const u32 p = (vowel_position(control) + 32768u) >> 16;
    return p > 4 ? 4u : p;
}
// Blur: cents of random spread, 120 * u^1.5.
inline u32 blur_power(i32 control) {   // u^1.5, Q16
    const u32 u = unit16(control);
    return u * root(u << 16) >> 16;
}
inline i32 blur_log2(i32 control) { return static_cast<i32>(6554u * blur_power(control) >> 16); }
inline u32 blur_cents(i32 control) { return (120u * blur_power(control) + 32768u) >> 16; }
// Haze: cents between each partial's detuned pair, 100 * u^2.
inline u32 haze_square(i32 control) { const u32 u = unit16(control); return u * u >> 16; }
inline u32 haze_cents(i32 control) { return (100u * haze_square(control) + 32768u) >> 16; }
inline bool held(i32 control) { return unit16(control) >= hold_unit; }
// Time: the drift's period in milliseconds, 30 * 1000^u: 30 ms .. 30 s; 0 when held.
inline i32 time_log2(i32 control) { return 321578 + static_cast<i32>((unit16(control) >> 1) * (653118u >> 4) >> 11); }
inline u32 time_ms(i32 control) {
    if (held(control)) return 0;
    return pow2(time_log2(control));
}
// Time units per 64-sample block: 2^20 * 64 / 44100 / period.
inline u32 time_step(i32 control) {
    if (held(control)) return 0;
    return pow2(1345932 - time_log2(control));
}

inline Controls decode(const i32* parameters) {
    Controls c;
    c.slope = tilt_slope(parameters[0]);
    const u32 u = unit16(parameters[1]);
    const u32 fade = u >= square_unit ? 0u : 65535u - (u << 1);
    c.even_keep = fade * fade >> 16;   // squared: the even partials thin out evenly in dB
    c.pulse = shape_pulse(parameters[1]);
    c.stretch = stretch_excess(parameters[2]);
    const u32 v = unit16(parameters[3]);
    c.vowel_depth = v >= vowel_onset ? 32768u : v * 5u;   // 32768 / 6554 = 5
    c.vowel_position = vowel_position(parameters[3]);
    c.breath_unit = unit16(parameters[4]);
    c.breath = static_cast<i32>(c.breath_unit * 3u);
    // Blur: deviation, then its rate from 8 to 38 Hz, then the gain keeping the
    // smoothed noise's spread near 0.45 of the deviation whatever the rate.
    c.blur = blur_log2(parameters[5]);
    c.blur_alpha = 299u + (unit16(parameters[5]) * 1000u >> 16);
    c.blur_gain = 200u * root(((8192u - c.blur_alpha) << 16) / c.blur_alpha) >> 8;
    // Haze: the pair's frequency ratio minus one, and how fully it beats
    // (in over the first quarter of the travel).
    const i32 cents_log = static_cast<i32>(5461u * haze_square(parameters[6]) >> 16);
    c.haze_ratio = pow2((16 << 16) + cents_log) - 65536u;
    const u32 h = unit16(parameters[6]);
    c.haze_depth = h >= 16384u ? 32768u : h << 1;
    // The beat envelope 1 - m + m cos: mean square (1 - m)^2 + m^2 / 2.
    const u32 m = c.haze_depth, rest = 32768u - m;
    c.haze_loud = root(((rest * rest >> 15) + (m * m >> 16)) << 15);
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
// Eases from lattice value a to b at time t's position between points, Q15.
inline i32 between(i32 a, i32 b, u32 t) {
    const u32 f = (t >> 5) & 32767u;                            // Q15 position between points
    const u32 eased = (f * f >> 15) * (98304u - 2 * f) >> 15;  // smoothstep, Q15
    return a + ((b - a) * static_cast<i32>(eased >> 1) >> 14);
}
// Smoothed random drift of stream `salt` at time t, Q15.
inline i32 noise(u32 seed, u32 salt, u32 t) {
    const u32 n = t >> 20;
    return between(lattice(seed, salt, n), lattice(seed, salt, n + 1), t);
}
// Partial k's (0-based) own clock: its rate (Q8, 0.75..1.37) and offset.
inline u32 clock_hash(u32 seed, u32 k) { return hash(seed + (k + 1) * 0x27d4eb2fu); }
inline u32 clock_rate(u32 seed, u32 k) {
    const u32 h = clock_hash(seed, k);
    return 192u + (h & 127u) + ((h >> 7) & 31u);
}
inline u32 clock_offset(u32 seed, u32 k) {
    const u32 h = hash(seed + (k + 1) * 0x27d4eb2fu);
    return h ^ (h << 11);
}
inline u32 partial_time(u32 seed, u32 k, u32 t) { return (t >> 8) * clock_rate(seed, k) + clock_offset(seed, k); }
// Partial k's level drift at note time t, Q15 (stream 2k at its own clock).
inline i32 level_drift(u32 seed, u32 k, u32 t) { return noise(seed, 2 * k, partial_time(seed, k, t)); }
// Every partial's level drift at note time t, Q15; all 0 without breath.
// (The engine keeps each partial's clock and lattice values and gets the same.)
inline void drifts(const Controls& c, u32 seed, u32 t, i32 (&out)[partial_count]) {
    for (u32 k = 0; k < partial_count; ++k) out[k] = c.breath ? level_drift(seed, k, t) : 0;
}
// The shared weather, Q15: one slow gust, the same for every voice on the
// track at the same shared clock, that brightens and darkens a chord together.
inline i32 weather(u32 clock) { return noise(0x5eedu, 99u, clock); }

// Breath's slower wanders of a note, after drift: the pulse width (0..65535,
// as Controls::pulse) and the vowel position (Q16 vowels). Each note wanders
// on its own clock, a little slower than its partials.
struct Wander {
    u32 pulse;
    u32 vowel;
};
inline Wander wander(const Controls& c, u32 seed, u32 t) {
    Wander w;
    w.pulse = c.pulse;
    w.vowel = c.vowel_position;
    if (!c.breath_unit) return w;
    const u32 slow = (t >> 2) * 3;   // three quarters of the note's pace
    if (c.pulse) {
        // Up to 0.3 of the pulse half either way: almost an octave of width.
        const i32 p = static_cast<i32>(c.pulse) + ((static_cast<i32>(c.breath_unit * 19661u >> 16) * noise(seed, 60u, slow)) >> 15);
        w.pulse = p < 1 ? 1u : p > 65535 ? 65535u : static_cast<u32>(p);
    }
    if (c.vowel_depth) {
        // Up to 0.6 of a vowel either way.
        const i32 v = static_cast<i32>(c.vowel_position) + ((static_cast<i32>(c.breath_unit * 39321u >> 16) * noise(seed, 61u, slow + 0x3c3c3c3cu)) >> 15);
        w.vowel = v < 0 ? 0u : v > (4 << 16) ? 4u << 16 : static_cast<u32>(v);
    }
    return w;
}

// The first two formants at a vowel position, log2 phase increments, Q16.
inline void formants(u32 position, i32& f1, i32& f2) {
    u32 i = position >> 16, r = position & 65535u;
    if (i >= 4) { i = 3; r = 65536; }
    const i32 fraction = static_cast<i32>(r >> 4);   // Q12
    f1 = vowel_f1[i] + ((vowel_f1[i + 1] - vowel_f1[i]) * fraction >> 12);
    f2 = vowel_f2[i] + ((vowel_f2[i + 1] - vowel_f2[i]) * fraction >> 12);
}
// One formant's lift at a distance from it (log2 Q16): up to 30 dB (five
// octaves of level, log2 Q16) times depth (Q15).
inline i32 formant_lift(i32 distance, u32 depth) {
    return (bump(distance) * 10 >> 4) * static_cast<i32>(depth) >> 11;
}
// The vowel's boost at a frequency (log2 phase increment), log2 Q16: up to
// 30 dB at each of the first two formants (the second sits where the tilt has
// already darkened the partials).
inline i32 vowel_boost(i32 f1, i32 f2, u32 depth, i32 frequency) {
    return formant_lift(frequency - f1, depth) + formant_lift(frequency - f2, depth);
}

// ---- The spectrum ------------------------------------------------------------

struct Spectrum {
    i32 pitch[partial_count];   // log2 of each partial's frequency ratio to the note, Q16
    i32 level[partial_count];   // amplitude, Q15 of full scale; the sum is at most peak_level
    u32 sounding;               // partials up to and including the last nonzero level
};

// Shape's gain on partial k (0-based), Q16: the even partials fade toward a
// square, then a pulse of the (wandered) width notches every 1/width partials.
inline u32 shape_gain(const Controls& c, u32 pulse, u32 k) {
    if (!c.pulse) return k & 1 ? c.even_keep : 65536u;
    const u32 width = pulse_width(pulse);   // Q16 of a period: |sin(pi (k + 1) width)|
    return static_cast<u32>(abs_sine((k + 1) * width >> 1)) << 1;
}

// The note's spectrum at note time t: each partial's pitch and level.
// gust: weather() at the shared clock (0 for none). note: log2 of the note's
// phase increment (log2_of), which places the vowel and decides what falls
// above Nyquist. drift: drifts() at t.
inline void shape(const Controls& c, u32 seed, u32 t, i32 gust, i32 note, const i32 (&drift)[partial_count], Spectrum& s) {
    const Wander w = wander(c, seed, t);
    i32 f1 = 0, f2 = 0;
    if (c.vowel_depth) formants(w.vowel, f1, f2);
    // The weather leans the whole tilt by up to a quarter of breath's depth per octave.
    const i32 slope = c.slope + ((c.breath >> 2) * gust >> 15);
    i32 log_level[partial_count];
    u32 gain[partial_count];
    i32 loudest = -0x40000000;
    for (u32 k = 0; k < partial_count; ++k) {
        const i32 harmonic = log2_partial[k + 1];
        // Stretch: log2 k * (1 + stretch).
        const i32 pitch = harmonic + ((harmonic >> 4) * static_cast<i32>(c.stretch >> 1) >> 11);
        s.pitch[k] = pitch;
        i32 level = (slope >> 4) * (harmonic >> 4) >> 8;
        level += (c.breath >> 2) * drift[k] >> 13;
        const i32 frequency = note + pitch;
        if (c.vowel_depth) level += vowel_boost(f1, f2, c.vowel_depth, frequency);
        u32 g = shape_gain(c, w.pulse, k);
        if (frequency >= log2_cutoff) g = 0;
        else if (frequency > log2_fade) level -= (frequency - log2_fade) * 40;
        gain[k] = g;
        log_level[k] = level;
        if (g && level > loudest) loudest = level;
    }
    // Levels relative to the loudest, then normalized for peak and loudness
    // (the loudness as haze's beating will leave it).
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
    const u32 rms = (root(squares) << 3) * c.haze_loud >> 15;
    const u32 by_loudness = rms ? (static_cast<u32>(loud_level) << 16) / rms : by_peak;
    const u32 scale = by_peak < by_loudness ? by_peak : by_loudness;   // Q16
    for (u32 k = 0; k < partial_count; ++k) {
        s.level[k] = static_cast<i32>(static_cast<u32>(s.level[k]) * scale >> 16);
        if (s.level[k]) s.sounding = k + 1;
    }
}

// The same, computing the drift.
inline void shape(const Controls& c, u32 seed, u32 t, i32 gust, i32 note, Spectrum& s) {
    i32 drift[partial_count];
    drifts(c, seed, t, drift);
    shape(c, seed, t, gust, note, drift, s);
}

// ---- Haze and blur, per partial ---------------------------------------------

// How strongly haze and blur act on partial k, Q8: the fundamental least, so
// the note stays centred.
static const u16 reach_table[partial_count] = {96, 160, 208, 256, 256, 256, 256, 256, 256, 256, 256, 256,
                                               256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256};
inline u32 partial_reach(u32 k) { return reach_table[k]; }
// Partial k's own share of haze's detune, Q8 0.6..1.4 of the control, from the
// note's seed (times its reach).
inline u32 pair_spread(u32 seed, u32 k) {
    const u32 h = hash(seed ^ ((k + 7) * 0x2c1b3c6du));
    return (154u + (h >> 24) * 205u / 256u) * partial_reach(k) >> 8;
}
// The beat phase's advance a block for a partial at phase increment inc.
inline u32 beat_step(const Controls& c, u32 spread, u32 inc) {
    const u32 ratio = c.haze_ratio * spread >> 8;            // Q16, at most about 0.08
    const u32 half = (inc >> 12) * ratio;                     // inc * ratio * 64 / 2 per block, halved
    return half >= beat_cap >> 1 ? beat_cap : half << 1;
}

}  // namespace bloom
