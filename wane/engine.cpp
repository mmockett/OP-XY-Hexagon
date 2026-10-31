#include "core.h"
#include "controls.h"
#include <opxy/q31.hpp>

// Wane: an analogue-style polysynth heard through a worn, shared tape.
//
// Each voice runs two oscillators that morph sine -> triangle -> saw -> pulse
// (PolyBLEP saws; the pulse is the difference of two), a triangle sub an
// octave down and a breath of noise, detuned apart, through a gentle two-pole
// low-pass into soft saturation, then the tape head's high-frequency loss and
// hiss. Every voice bends with the one tape in Shared (tape.h): wow, flutter,
// drags and the sag as notes start, advanced once per audio block.
//
// Integer arithmetic only, 32-bit: oscillators in Q15 with 16 x 16 multiplies,
// filters in Q31. Divisions and transposes run once per block.
namespace wane {
namespace engine {
using opxy::q31::multiply;
using namespace controls;
const i32 one = opxy::q31::maximum;
// The output's ceiling, 0.1875 of full scale: a voice sits low enough that a
// chord of eight peaks near 0.6.
const i32 ceiling = 0x18000000;
const u32 fastest = 0x40000000u;                 // increments capped at a quarter of the sample rate

// A control word as Q15, 0..32767.
inline i32 q15(i32 control) { return control < 0 ? 0 : control >> 16; }
// a x b >> 15 for 16-bit operands: one 16 x 16 multiply on the Blackfin.
inline i32 m15(i32 a, i32 b) { return (static_cast<i32>(static_cast<opxy::i16>(a)) * static_cast<opxy::i16>(b)) >> 15; }
inline i32 clamp(i32 x, i32 low, i32 high) { return x < low ? low : x > high ? high : x; }

// 2^(k/32), k = 0..32, Q29.
const i32 exp2_table[33] = {
    536870912, 548626854, 560640218, 572916640, 585461881, 598281827, 611382493, 624770026, 638450709, 652430959,
    666717336, 681316545, 696235435, 711481005, 727060411, 742980961, 759250125, 775875538, 792865000, 810226483,
    827968133, 846098274, 864625414, 883558245, 902905651, 922676710, 942880699, 963527098, 984625594, 1006186087,
    1028218694, 1050733751, 1073741824,
};
// 2^f for a Q16 fraction f in 0..65535, Q29 (1..2).
inline i32 exp2_fraction(u32 f) {
    const u32 index = f >> 11;
    const i32 fraction = static_cast<i32>((f & 2047u) << 20);
    return exp2_table[index] + multiply(exp2_table[index + 1] - exp2_table[index], fraction);
}
// 2^octaves for Q16 octaves in -4..2.9, Q28.
inline i32 power2_q28(i32 octaves) {
    const u32 shifted = static_cast<u32>(clamp(octaves, -4 * 65536, 190000) + 4 * 65536);
    const i32 mantissa = exp2_fraction(shifted & 0xffffu) >> 1;   // Q28, 1..2
    const u32 whole = shifted >> 16;                                // 4 is 2^0
    return whole >= 4 ? mantissa * (1 << (whole - 4)) : mantissa >> (4 - whole);
}
// A phase increment moved by Q16 octaves (-12..+12), capped at `fastest`.
inline u32 transpose(u32 increment, i32 octaves) {
    const u32 shifted = static_cast<u32>(clamp(octaves, -12 * 65536, 12 * 65536) + 16 * 65536);
    const u32 whole = shifted >> 16;                                // 16 is no shift
    // (increment / 2) x ratio(Q29) / 2^31 = increment x ratio / 8.
    const u32 base = static_cast<u32>(multiply(static_cast<i32>(increment >> 1), exp2_fraction(shifted & 0xffffu)));
    u32 result;
    if (whole >= 13) {
        const u32 up = whole - 13;
        result = up >= 8 || base > (fastest >> up) ? fastest : base << up;
    } else {
        result = base >> (13 - whole);
    }
    return result > fastest ? fastest : result;
}
// One-pole low-pass coefficients 1 - e^(-2 pi f / fs) for f = 20 Hz x 2^(k/2),
// k = 0..22, Q31; from 20.5 kHz up the filter is open.
const i32 pole_table[23] = {
    6110581, 8636571, 12203775, 17238409, 24338198, 34338441, 48400563, 68127807, 95710261, 134094294,
    187154853, 259815402, 357999016, 488196785, 656317348, 865409674, 1112049950, 1382069822, 1648237498,
    1874672106, 2031419100, 2147483647, 2147483647,
};
// The coefficient for a cutoff Q16 octaves above 20 Hz (0..11).
inline i32 pole(i32 octaves) {
    const u32 position = static_cast<u32>(clamp(octaves, 0, 11 * 65536 - 1)) * 2;
    const u32 index = position >> 16;
    const i32 fraction = static_cast<i32>((position & 0xffffu) << 15);
    return pole_table[index] + multiply(pole_table[index + 1] - pole_table[index], fraction);
}

// The PolyBLEP correction (1 - x)^2, Q15, where x is the distance to the
// saw's reset in steps of the increment: `distance` x `reciprocal` / 2^32.
inline i32 blep(u32 distance, u32 reciprocal) {
    const i32 x = static_cast<i32>((distance * reciprocal) >> 17);
    const i32 e = 32768 - x;
    return (e * e) >> 15;
}
// A band-limited saw of a phase, Q15, given the step that reached it.
inline i32 saw(u32 phase, u32 step, u32 reciprocal) {
    i32 s = static_cast<i32>(phase >> 16) - 32768;
    // Rare (two samples a cycle): told so, so the Blackfin predicts the branch not taken.
    if (__builtin_expect(phase < step, 0)) s += blep(phase, reciprocal);                 // just past the reset
    else if (__builtin_expect(phase > 0u - step, 0)) s -= blep(0u - phase, reciprocal);  // just before it
    return s;
}

// A sine in phase with the triangle (lowest at phase 0), Q15: tape.h's
// parabola refined once, 0.775 p + 0.225 p|p|, its harmonics below -50 dB.
inline i32 sine_wave(u32 phase) {
    const i32 p = tape::sine(phase - 0x40000000u);
    return p + m15(m15(p, p < 0 ? -p : p) - p, 7373);
}

// How the shape control mixes each oscillator, Q15: sine, triangle, saw, and
// the second saw subtracted to make a pulse of `width`.
struct Shape {
    i32 sine, triangle, saw, pulse;
    u32 width;
};
inline Shape shape_of(i32 control) {
    const i32 s = q15(control);
    Shape m;
    m.width = 0x80000000u;
    m.sine = 0;
    if (s < shape_triangle) {
        // Sine to triangle, in phase: the sine at 0.82 has the triangle's level.
        const i32 b = s * 4;
        m.sine = m15(32767 - b, 26870);
        m.triangle = b;
        m.saw = m.pulse = 0;
    } else if (s < shape_saw) {
        // Triangle to saw: equally loud apart, and nearly uncorrelated, so the
        // middle of the fade is lifted 2 dB to keep most of its level.
        const i32 b = (s - shape_triangle) * 4, a = 32767 - b;
        const i32 lift = 32768 + ((a * b) >> 15);                  // 1 .. 1.25
        m.saw = (b * lift) >> 15;
        m.triangle = clamp((a * lift) >> 15, 0, 32767);
        m.pulse = 0;
    } else if (s < shape_square) {
        // Saw to square, settling 3 dB down as the square's even harmonics go.
        const i32 c = clamp(((s - shape_saw) * 3413) >> 10, 0, 32767);
        const i32 gain = 32767 - ((c * 9830) >> 15);
        m.triangle = 0;
        m.saw = gain;
        m.pulse = (c * gain) >> 15;
    } else {
        // The square narrows to a 15% pulse, its taller peak held below one.
        const i32 t = s - shape_square;                             // 0..6553
        m.width = static_cast<u32>(16384 - ((t * 1792) >> 10)) << 17;
        m.triangle = 0;
        m.saw = m.pulse = 22937 - ((t * 614) >> 10);                // 0.7 .. 0.58
    }
    return m;
}

// One oscillator over a block, added into `out`: its increment glides from the
// last block's to `target`. Sine, Triangle, Saw and Pulse say which parts the
// shape uses.
template <bool Sine, bool Triangle, bool Saw, bool Pulse>
inline void oscillate(i32* out, u32& phase_state, u32& increment_state, u32 target, const Shape& m) {
    u32 phase = phase_state, increment = increment_state;
    const i32 slope = (static_cast<i32>(target) - static_cast<i32>(increment)) / 64;
    const u32 widest = (increment > target ? increment : target) | 1u;
    const u32 reciprocal = 0xffffffffu / widest;
    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        const u32 step = increment;
        increment += static_cast<u32>(slope);
        phase += step;
        i32 value = 0;
        if (Saw) value += m15(saw(phase, step, reciprocal), m.saw);
        if (Sine) value += m15(sine_wave(phase), m.sine);
        if (Triangle) {
            const u32 folded = phase >> 16;
            value += m15(2 * static_cast<i32>(folded < 32768u ? folded : 65535u - folded) - 32767, m.triangle);
        }
        if (Pulse) value -= m15(saw(phase + m.width, step, reciprocal), m.pulse);
        out[i] += value;
    }
    phase_state = phase;
    increment_state = target;
}
inline void oscillate(i32* out, u32& phase, u32& increment, u32 target, const Shape& m) {
    if (m.pulse) oscillate<false, false, true, true>(out, phase, increment, target, m);
    else if (m.sine) oscillate<true, true, false, false>(out, phase, increment, target, m);
    else if (m.triangle) oscillate<false, true, true, false>(out, phase, increment, target, m);
    else oscillate<false, false, true, false>(out, phase, increment, target, m);
}

// x (Q31, |x| < 2^31 / 3) times c (Q15) with one 16 x 16 multiply; the low
// 16 bits of x are dropped (-90 dB), which these filters never hear.
// The top half as a 16-bit value, written (a logical shift, then a 16-bit
// truncation) so GCC keeps it 16 bits wide and multiplies 16 x 16.
inline opxy::i16 top16(i32 x) { return static_cast<opxy::i16>(static_cast<u32>(x) >> 16); }
inline i32 m31(i32 x, i32 c) { return static_cast<i32>(top16(x)) * static_cast<opxy::i16>(c) * 2; }

// The tape's next block: its pitch, with the start sag, and how well the
// worn head touches it (a new random target every 0.19 s, eased).
inline void advance(Shared& shared, opxy::Parameters p) {
    ++shared.clock;
    const tape::Depths d = tape::depths(p[WOW], p[FLUTTER], p[SAG]);
    i32 bend = tape::bend(shared.clock, d);
    if (shared.sagging && d.sag) bend += tape::sag_bend(shared.clock - shared.sag_start, shared.sag_from, d);
    shared.bend = bend;
    shared.sag_rate = d.sag_rate;
    shared.contact = q15(p[AGE]) ? (tape::smooth_noise(tape::seed ^ 0x6u, shared.clock, 7) + 32768) >> 1 : 0;
}

// log2 of a phase increment, Q16 octaves, straight between octaves (within
// 0.09 of an octave): enough for the filter to follow the keyboard.
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
const u32 reference = 21426343u;                 // the increment of 220 Hz, where warmth's cutoffs are named
// How far warmth closes the low-pass from 17.8 kHz, Q16 octaves: to 5.1, 2.1,
// 0.9 and 0.37 kHz at 25, 50, 75 and 100%, an even step in brightness a quarter.
const tape::Curve warmth_curve = {{0, 117965, 203162, 281805, 367002}};
// The head's loss with age, Q16 octaves above 20 Hz: open, then 4.8, 2.6,
// 1.7 and 1.1 kHz at 25, 50, 75 and 100% (two poles).
const tape::Curve head_curve = {{720896, 517734, 458752, 419430, 380109}};
}

// Callbacks run serially per voice; owner is a stable nonzero voice ID.
inline void initialize(Voice& voice, Shared& shared, u32 owner) {
    voice.seed = tape::hash(owner * 0x9e3779b9u + 0x7f4a7c15u);
    voice.noise = voice.seed | 1u;
    voice.notes = 0;
    voice.clock = 0;
    voice.fresh = 1;
    voice.slop = 0;
    // Free-running oscillators, starting apart.
    for (unsigned k = 0; k < 3; ++k) voice.phase[k] = tape::hash(voice.seed + k + 1);
    voice.increment[0] = voice.increment[1] = 0;
    voice.low[0] = voice.low[1] = 0;
    voice.head[0] = voice.head[1] = voice.hiss = 0;
    voice.level = 0;
    voice.offset[0] = voice.offset[1] = 0;
    voice.cycle = shared.cycle - 1;   // not yet counted in this cycle
}

inline void start(Voice& voice, Shared& shared, u32) {
    // The tape catches up: the sag restarts from whatever is left of the last.
    const i32 left = shared.sagging ? tape::sag(shared.clock - shared.sag_start, shared.sag_from, shared.sag_rate) : 0;
    shared.sag_from = left;
    shared.sag_start = shared.clock;
    shared.sagging = 1;
    // Each note lands its own random distance off pitch.
    ++voice.notes;
    voice.slop = tape::random(voice.seed ^ 0x51u, voice.notes);
    voice.fresh = 1;
}

inline void destroy(Voice&, Shared&, u32) {}

inline void render(Voice& voice, Shared& shared, u32, opxy::Output output,
                   opxy::Parameters p, opxy::Pitch pitch) {
    using namespace engine;
    // The census: the first voice to render twice in a cycle moves the tape on.
    if (voice.cycle == shared.cycle) {
        advance(shared, p);
        ++shared.cycle;
    }
    voice.cycle = shared.cycle;
    ++voice.clock;

    // Pitch: the tape, this note's slop and the voice's own wander, then the
    // oscillators detuned either side, oscillator 2 wandering a little apart.
    const i32 drift = tape::along(p[DRIFT], drift_curve);
    const i32 detune = tape::along(p[DETUNE], detune_curve) >> 1;
    const i32 wander = (tape::smooth_noise(voice.seed ^ 0x11u, voice.clock, 10) * drift) >> 15;
    const i32 apart = (tape::smooth_noise(voice.seed ^ 0x12u, voice.clock, 9) * (drift >> 1)) >> 15;
    const i32 common = shared.bend + ((voice.slop * ((drift * 3) >> 2)) >> 15) + wander;
    voice.offset[0] = common - detune;
    voice.offset[1] = common + apart + detune;
    const u32 target[2] = {transpose(pitch[0], voice.offset[0]), transpose(pitch[0], voice.offset[1])};
    if (voice.fresh) {
        voice.increment[0] = target[0];
        voice.increment[1] = target[1];
        voice.fresh = 0;
    }
    const u32 sub_increment = voice.increment[0] >> 1;
    const i32 sub_slope = (static_cast<i32>(target[0] >> 1) - static_cast<i32>(sub_increment)) / 64;

    i32 mix[opxy::block_frames];
    for (unsigned i = 0; i < opxy::block_frames; ++i) mix[i] = 0;
    const Shape shape = shape_of(p[SHAPE]);
    oscillate(mix, voice.phase[0], voice.increment[0], target[0], shape);
    oscillate(mix, voice.phase[1], voice.increment[1], target[1], shape);

    // Warmth: a darker low-pass (17.8 kHz down to 370 Hz at 220 Hz, following
    // the keyboard by up to half as it closes), more sub, a breath of noise,
    // and with age a harder drive into the saturation.
    const i32 warmth = q15(p[WARMTH]), age = q15(p[AGE]);
    const i32 warmth_2 = (warmth * warmth) >> 15;
    const i32 sub = (warmth * 24576) >> 15;                         // up to 0.75
    const i32 breath = (warmth_2 * 2621) >> 15;                     // up to 0.08
    const i32 darker = tape::along(p[WARMTH], warmth_curve);
    const i32 follow = (((log2_q16(pitch[0]) - log2_q16(reference)) >> 4) * (warmth >> 1)) >> 11;
    const i32 low = pole(642253 - darker + follow) >> 16;           // Q15: 9.8 octaves above 20 Hz, less darker
    const i32 heat = clamp(((warmth * 19661) >> 15) + ((age * 26214) >> 15), 0, 32767);   // 0.6 w + 0.8 a
    const i32 octaves = (heat * 5939) >> 10;                        // drive 0 .. 2.9 octaves
    const i32 drive = power2_q28(octaves) >> 16;                    // Q12, 1 .. 7.5
    const i32 makeup = clamp(power2_q28(-((octaves * 3) >> 2)) >> 13, 0, 32767);   // drive^-0.75, Q15
    // Age: the head's loss (two poles, open down to 1.3 kHz), its worn
    // contact dulling it further now and then (up to an octave at full), and hiss.
    const i32 lift = (((age * age) >> 15) * 65536) >> 15;           // Q16 octaves, up to 1
    const i32 head = pole(tape::along(p[AGE], head_curve) - (((shared.contact >> 1) * lift) >> 14)) >> 16;   // Q15
    const i32 hiss = ((((age * age) >> 15) + age) * 450) >> 15;     // up to 0.021 of full scale, after x 3
    const i32 level = makeup;                                       // Q15
    const i32 level_step = (level - voice.level) / 64;

    u32 sub_phase = voice.phase[2], sub_step = sub_increment, noise = voice.noise;
    i32 low_1 = voice.low[0], low_2 = voice.low[1], worn = voice.head[0], heard = voice.head[1];
    i32 last = voice.hiss, gain = voice.level;
    // Two passes, each small enough for the Blackfin's registers: the sub,
    // the breath and the saturation into mix (in place), then the filters,
    // hiss and the ceiling. The same arithmetic in the same order per sample.
    i32 whites[opxy::block_frames];
    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        sub_phase += sub_step;
        sub_step += static_cast<u32>(sub_slope);
        const u32 folded = sub_phase >> 16;
        // 65535 - folded is folded ^ 0xffff for 16-bit values: the fold without a branch.
        const u32 mirrored = folded ^ ((0u - (folded >> 15)) & 0xffffu);
        const i32 sub_wave = 2 * static_cast<i32>(mirrored) - 32767;
        noise ^= noise << 13; noise ^= noise >> 17; noise ^= noise << 5;
        const i32 white = static_cast<i32>(noise >> 17) - 16384;   // Q15, +-0.5
        whites[i] = white;
        const i32 x = (mix[i] + m15(sub_wave, sub) + m15(white, breath)) * 8192;
        const i32 u = clamp((x >> 16) * drive, -0x07ffffff, 0x07ffffff) * 16;
        const i32 u15 = top16(u);
        mix[i] = m31(u - m15(m15(m15(u15, u15), u15), 10923) * 65536, gain) / 2;
        gain += level_step;
    }
    if (hiss) {
        for (unsigned i = 0; i < opxy::block_frames; ++i) {
            const i32 warm = mix[i];
            low_1 += m31(warm - low_1, low);
            low_2 += m31(low_1 - low_2, low);
            worn += m31(low_2 - worn, head);
            heard += m31(worn - heard, head);
            const i32 white = whites[i];
            const i32 hissing = (white - last) * hiss;
            last = white;
            const i32 out = heard + hissing;
            output[i] = clamp(out + (out >> 3), -ceiling, ceiling);   // 1.125 x
        }
    } else {
        // No hiss: (white - last) * 0 adds nothing.
        for (unsigned i = 0; i < opxy::block_frames; ++i) {
            const i32 warm = mix[i];
            low_1 += m31(warm - low_1, low);
            low_2 += m31(low_1 - low_2, low);
            worn += m31(low_2 - worn, head);
            heard += m31(worn - heard, head);
            const i32 out = heard;
            output[i] = clamp(out + (out >> 3), -ceiling, ceiling);   // 1.125 x
        }
        last = whites[opxy::block_frames - 1];
    }
    voice.phase[2] = sub_phase;
    voice.noise = noise;
    voice.low[0] = low_1;
    voice.low[1] = low_2;
    voice.head[0] = worn;
    voice.head[1] = heard;
    voice.hiss = last;
    voice.level = level;
}
}
