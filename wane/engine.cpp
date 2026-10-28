#include "core.h"
#include "controls.h"
#include <opxy/q31.hpp>

// Wane: an analogue-style polysynth heard through a worn, shared tape.
//
// Each voice runs two oscillators that morph triangle -> saw -> pulse
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
const i32 half_scale = 0x40000000;
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
    if (phase < step) s += blep(phase, reciprocal);                 // just past the reset
    else if (phase > 0u - step) s -= blep(0u - phase, reciprocal);  // just before it
    return s;
}

// How the shape control mixes each oscillator, Q15: triangle, saw, and the
// second saw subtracted to make a pulse of `width`.
struct Shape {
    i32 triangle, saw, pulse;
    u32 width;
};
inline Shape shape_of(i32 control) {
    const i32 s = q15(control);
    Shape m;
    m.width = 0x80000000u;
    if (s < shape_saw) {
        // Triangle to saw: equally loud apart, and nearly uncorrelated, so the
        // middle of the fade is lifted 2 dB to keep most of its level.
        const i32 b = s * 2, a = 32767 - b;
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
// last block's to `target`. Triangle and Pulse say which parts the shape uses.
template <bool Triangle, bool Pulse>
inline void oscillate(i32* out, u32& phase_state, u32& increment_state, u32 target, const Shape& m) {
    u32 phase = phase_state, increment = increment_state;
    const i32 slope = (static_cast<i32>(target) - static_cast<i32>(increment)) / 64;
    const u32 widest = (increment > target ? increment : target) | 1u;
    const u32 reciprocal = 0xffffffffu / widest;
    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        const u32 step = increment;
        increment += static_cast<u32>(slope);
        phase += step;
        i32 value = m15(saw(phase, step, reciprocal), m.saw);
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
    if (m.pulse) oscillate<false, true>(out, phase, increment, target, m);
    else if (m.triangle) oscillate<true, false>(out, phase, increment, target, m);
    else oscillate<false, false>(out, phase, increment, target, m);
}

// x (Q31, |x| < 2^31 / 3) times c (Q15) with one 16 x 16 multiply; the low
// 16 bits of x are dropped (-90 dB), which these filters never hear.
inline i32 m31(i32 x, i32 c) { return static_cast<i32>(static_cast<opxy::i16>(x >> 16)) * static_cast<opxy::i16>(c) * 2; }

// The tape's next block: its pitch (with the start sag) and dropout gain.
inline void advance(Shared& shared, opxy::Parameters p) {
    ++shared.clock;
    const tape::Depths d = tape::depths(p[WOW], p[FLUTTER], p[SAG]);
    i32 bend = tape::bend(shared.clock, d);
    if (shared.sagging && d.sag) bend += tape::sag_bend(shared.clock - shared.sag_start, shared.sag_from, d);
    shared.bend = bend;
    shared.gain = tape::dropout(shared.clock, q15(p[AGE]));
}
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
    voice.head = voice.hiss = 0;
    voice.level = 0;
    voice.offset[0] = voice.offset[1] = 0;
    voice.cycle = shared.cycle - 1;   // not yet counted in this cycle
    if (!shared.gain) shared.gain = tape::one;   // a tape threaded before any block
}

inline void start(Voice& voice, Shared& shared, u32) {
    // The tape catches up: the sag restarts from whatever is left of the last.
    const i32 left = shared.sagging ? tape::sag(shared.clock - shared.sag_start, shared.sag_from) : 0;
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
    const i32 drift = tape::depth(p[DRIFT], drift_most);
    const i32 detune = tape::depth(p[DETUNE], detune_most) / 2;
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

    // Warmth: a darker low-pass (17.8 kHz down to 0.9 kHz), more sub, a breath
    // of noise, and with age a harder drive into the saturation.
    const i32 warmth = q15(p[WARMTH]), age = q15(p[AGE]);
    const i32 sub = (warmth * 19661) >> 15;                         // up to 0.6
    const i32 breath = (((warmth * warmth) >> 15) * 1638) >> 15;    // up to 0.05
    const i32 low = pole(642253 - ((warmth * 8806) >> 10)) >> 16;   // Q15: 9.8 - 4.3 x warmth octaves
    const i32 heat = clamp(((warmth * 19661) >> 15) + ((age * 13107) >> 15), 0, 32767);   // 0.6 w + 0.4 a
    const i32 octaves = (heat * 4506) >> 10;                        // drive 0 .. 2.2 octaves
    const i32 drive = power2_q28(octaves) >> 16;                    // Q12, 1 .. 4.6
    const i32 makeup = clamp(power2_q28(-octaves) >> 13, 0, 32767); // 1 / drive, Q15
    // Age: the head's loss (open down to 2.6 kHz), hiss, and the tape's dropouts.
    const i32 head = pole(11 * 65536 - age * 8) >> 16;              // Q15
    const i32 hiss = (((age * age) >> 15) * 524) >> 15;             // up to 0.012 of full scale, after x 3
    const i32 level = (makeup * shared.gain) >> 15;                 // Q15
    const i32 level_step = (level - voice.level) / 64;

    u32 sub_phase = voice.phase[2], sub_step = sub_increment, noise = voice.noise;
    i32 low_1 = voice.low[0], low_2 = voice.low[1], heard = voice.head, last = voice.hiss, gain = voice.level;
    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        sub_phase += sub_step;
        sub_step += static_cast<u32>(sub_slope);
        const u32 folded = sub_phase >> 16;
        const i32 sub_wave = 2 * static_cast<i32>(folded < 32768u ? folded : 65535u - folded) - 32767;
        noise ^= noise << 13; noise ^= noise >> 17; noise ^= noise << 5;
        const i32 white = static_cast<i32>(noise >> 17) - 16384;   // Q15, +-0.5
        // The voice in Q31, peaking near a third of full scale.
        const i32 x = (mix[i] + m15(sub_wave, sub) + m15(white, breath)) * 8192;
        low_1 += m31(x - low_1, low);
        low_2 += m31(low_1 - low_2, low);
        // Soft saturation: driven into x - x^3 / 3 (the cube at 16 bits), then
        // levelled back down by `gain`, which also carries the dropouts.
        const i32 u = clamp((low_2 >> 16) * drive, -0x07ffffff, 0x07ffffff) * 16;
        const i32 u15 = u >> 16;
        const i32 warm = u - m15(m15(m15(u15, u15), u15), 10923) * 65536;
        // The head's loss runs at half level, then the voice is tripled: 1.5 x.
        heard += m31(m31(warm, gain) / 2 - heard, head);
        gain += level_step;
        // Hiss: the noise's first difference, bright, under the music.
        const i32 hissing = (white - last) * hiss;
        last = white;
        output[i] = clamp((heard + hissing) * 3, -half_scale, half_scale);
    }
    voice.phase[2] = sub_phase;
    voice.noise = noise;
    voice.low[0] = low_1;
    voice.low[1] = low_2;
    voice.head = heard;
    voice.hiss = last;
    voice.level = level;
}
}
