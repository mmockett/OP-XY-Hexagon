#include "core.h"

// Bend: phase distortion after the Casio CZ. Each note plays two lines; each
// reads a cosine through a bent phase map (pd.h), bend moving the map from the
// straight line (a pure sine) to the chosen wave, and each note's own DCW
// contour adding to bend as it starts. Line 2 sounds at the interval, detuned,
// and ring blends it into its product with line 1. Warp skews and overruns the
// maps, noise modulates their phases and lofi plays the voice through a coarse
// DAC.
//
// Integer arithmetic only, 32-bit. A block is planned once (maps, rates,
// gains); then each line runs in two passes over the block, pointer walks with
// few live values, and a last pass mixes, removes DC and scales.
namespace bend {
namespace engine {
using pd::q15;
using pd::clamp;

// Each wave's level, Q14: the reso waves' windows leave them a third of a sine's power.
const i32 wave_gain[pd::waves] = {16384, 16384, 16384, 16384, 16384, 24576, 24576, 20480};
// The voice's output gain: a line at its peak is 0.047 of full scale.
const i32 gain = 1536;
// The output's ceiling, Q31 (definition.json's outputPeakQ31).
const i32 ceiling = 0x18000000;
// Line 2 fades out above 16 kHz, gone at pd::fastest.
const u32 fade_from = 1558345273u;

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

// Each wave's map as the map pass reads it, holding only the constants it
// needs so the pass keeps them in registers; knots that never move are
// written in. `at` gives the output phase (Q32) of a skewed input phase x.
struct OneKnot {   // saw and pulse: A + B x + c |x - k|
    u32 a, b, c;
    i32 k;
    explicit OneKnot(const pd::Line& l) : a(l.a), b(l.b), c(static_cast<u32>(l.c[0])), k(l.k[0]) {}
    u32 at(i32 x) const { return a + b * static_cast<u32>(x) + c * static_cast<u32>(pd::magnitude(x - k)); }
};
struct Square {    // A + h (x - |x - q| + |x - 1/2| - |x - 1/2 - q|)
    u32 a, h;
    i32 q, q2;
    explicit Square(const pd::Line& l) : a(l.a), h(l.b), q(l.k[0]), q2(l.k[2]) {}
    u32 at(i32 x) const {
        return a + h * static_cast<u32>(x - pd::magnitude(x - q) + pd::magnitude(x - 32768) - pd::magnitude(x - q2));
    }
};
struct Dsine {     // folds at 1/4, 1/2 and 3/4 in the ratio -3 : 2 : 1
    u32 a, b, c;
    explicit Dsine(const pd::Line& l) : a(l.a), b(l.b), c(static_cast<u32>(l.c[2])) {}
    u32 at(i32 x) const {
        return a + b * static_cast<u32>(x) +
               c * static_cast<u32>(2 * pd::magnitude(x - 32768) + pd::magnitude(x - 49152) - 3 * pd::magnitude(x - 16384));
    }
};
struct SawPulse {  // knots at 4% and the half
    u32 a, b, c0, c1;
    explicit SawPulse(const pd::Line& l)
        : a(l.a), b(l.b), c0(static_cast<u32>(l.c[0])), c1(static_cast<u32>(l.c[1])) {}
    u32 at(i32 x) const {
        return a + b * static_cast<u32>(x) + c0 * static_cast<u32>(pd::magnitude(x - 2621)) +
               c1 * static_cast<u32>(pd::magnitude(x - 32768));
    }
};

// Each line runs in two passes over the block: the map pass walks the input
// phase and writes each sample's output phase (Q32); the cosine pass reads the
// cosine there, with the noise, under a reso wave's window. Then the step the
// line makes where its cycle ends (warp's overrun, the saw and trapezoid
// windows' edge) is smoothed by a two-sample polynomial BLEP: each line plays
// a sample late, so the sample before the step can take its share.
template <class Map, bool Warp>
inline u32 map_pass(u32* out, u32 phase, u32 increment, const Map map, i32 skew) {
    for (u32* o = out; o != out + opxy::block_frames; ++o) {
        phase += increment;
        const i32 x = static_cast<i32>(phase >> 16);
        *o = map.at(Warp ? pd::skewed(x, skew) : x);
    }
    return phase;
}
// The reso waves' map pass: the cosine at `rate` (Q16, gliding by `glide` a
// sample) against the input phase.
template <bool Warp>
inline u32 reso_pass(u32* out, u32 phase, u32 increment, u32 rate, i32 glide, i32 skew) {
    for (u32* o = out; o != out + opxy::block_frames; ++o) {
        phase += increment;
        rate += static_cast<u32>(glide);
        const i32 x = static_cast<i32>(phase >> 16);
        *o = rate * static_cast<u32>(Warp ? pd::skewed(x, skew) : x);
    }
    return phase;
}
// The cosine pass, in place: each output phase becomes the line's sample
// (Q15), with the noise added to the phase and, for a reso wave, under its
// window, which follows the input phase from `phase` before the block.
template <bool Noise, u32 Window>
inline void cosine_pass(i32* line, const u32* noise, u32 phase, u32 increment) {
    for (i32* o = line; o != line + opxy::block_frames; ++o) {
        u32 p = static_cast<u32>(*o);
        if (Noise) p += *noise++;
        i32 v = pd::cosine(p);
        if (Window) {
            phase += increment;
            v = (v * pd::window(Window, static_cast<i32>(phase >> 16))) >> 16;
        }
        *o = v;
    }
}
// The step at each wrap of the cycle in the block, smoothed by the BLEP. The
// wraps are found from the line's phase before the block and its increment
// rather than in the passes. `jump` is the step (the cycle's first value less
// its last, Q15), `inverse` 2^32 over the increment, which turns a phase past
// the wrap into its fraction of a sample, and `line` holds the sample held
// back from the last block, then this block's 64.
inline void smooth(i32* line, u32 phase, u32 increment, u32 inverse, i32 jump) {
    if (increment < 65536u) return;   // below 0.7 Hz there is no step to hear (and the estimate needs more)
    u32 step = 0;
    for (;;) {
        const u32 room = ~phase;                              // to the wrap, less one
        u32 whole = ((room >> 16) * inverse) >> 16;           // steps that fit, at most two short
        if (whole >= opxy::block_frames) return;
        u32 left = room - whole * increment;
        while (left >= increment) { left -= increment; ++whole; }
        step += whole + 1;                                    // the step that wraps
        if (step > opxy::block_frames) return;
        phase = increment - 1u - left;                        // its phase past the wrap
        const i32 t = static_cast<i32>((phase * inverse) >> 17);   // Q15 of a sample past the step
        const i32 u = 32768 - t;
        line[step - 1] += (jump * ((t * t) >> 15)) >> 16;
        line[step] -= (jump * ((u * u) >> 15)) >> 16;
    }
}

// The noise block, Q32 cycles: white noise low-passed (at `pole`, Q15) and
// scaled (wrapping: a whole cycle of deviation is none).
inline void noise_block(u32* out, u32& seed_state, i32& low_state, i32 pole, u32 scale) {
    u32 seed = seed_state;
    i32 low = low_state;
    for (u32* o = out; o != out + opxy::block_frames; ++o) {
        seed = seed * 1664525u + 1013904223u;
        low += ((static_cast<i32>(seed) >> 16) - low) * pole >> 15;
        *o = static_cast<u32>(low) * scale;
    }
    seed_state = seed;
    low_state = low;
}

// What a line plays this block.
struct Plan {
    pd::Line line;
    u32 increment;
    i32 jump;         // the step where its cycle ends, Q15
    u32 inverse;      // 2^32 over the increment
    i32 gain;         // Q14
};

// One line's block into line[1..64] (line[0] is the sample held back from the
// last block); returns its phase.
template <bool Warp>
inline u32 play(i32* line, u32 wave, const Plan& plan, u32 phase, u32 rate, i32 glide, const u32* noise) {
    const pd::Line& l = plan.line;
    const u32 increment = plan.increment, before = phase;
    u32* phases = reinterpret_cast<u32*>(line + 1);
    switch (wave) {
    case SAW: case PULSE: phase = map_pass<OneKnot, Warp>(phases, phase, increment, OneKnot(l), l.skew); break;
    case SQUARE: phase = map_pass<Square, Warp>(phases, phase, increment, Square(l), l.skew); break;
    case DSINE: phase = map_pass<Dsine, Warp>(phases, phase, increment, Dsine(l), l.skew); break;
    case SAWPULSE: phase = map_pass<SawPulse, Warp>(phases, phase, increment, SawPulse(l), l.skew); break;
    default: phase = reso_pass<Warp>(phases, phase, increment, rate, glide, l.skew); break;
    }
    switch (wave) {
    case RESO1: noise ? cosine_pass<true, RESO1>(line + 1, noise, before, increment) : cosine_pass<false, RESO1>(line + 1, noise, before, increment); break;
    case RESO2: noise ? cosine_pass<true, RESO2>(line + 1, noise, before, increment) : cosine_pass<false, RESO2>(line + 1, noise, before, increment); break;
    case RESO3: noise ? cosine_pass<true, RESO3>(line + 1, noise, before, increment) : cosine_pass<false, RESO3>(line + 1, noise, before, increment); break;
    default: noise ? cosine_pass<true, 0>(line + 1, noise, before, increment) : cosine_pass<false, 0>(line + 1, noise, before, increment); break;
    }
    if (plan.jump) smooth(line, before, increment, plan.inverse, plan.jump);
    return phase;
}

// The last pass: ring blends line 2 into its product with line 1, the lines
// are summed (Q24), the DC the bent waves carry is removed (one pole at
// 6.9 Hz), lofi holds and quantizes, and the level glides (Ramp) in a new
// voice's first block or as the wave's level changes.
struct Mix {
    i32 keep, ring;   // line 2's share as itself and as the ring product, Q15 (its fade included)
    i32 level, level_step;
    u32 clock_step;   // lofi's sample clock a sample, Q32
    i32 half, mask;   // lofi's quantizer, Q24
};
template <bool Lofi, bool Ramp>
inline void mix(i32* out, const i32* one, const i32* two, const Mix& m, Voice& voice) {
    i32 in = voice.dc_in, dc = voice.dc_out, level = voice.level, held = voice.held;
    u32 clock = voice.clock;
    const i32 keep = m.keep, ring = m.ring, step = m.level_step;
    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        const i32 a = one[i], b = two[i];
        const i32 sum = (a + ((b * keep + ((a * b) >> 15) * ring) >> 15)) << 9;   // Q24
        dc += sum - in - (dc >> 10);
        in = sum;
        i32 v = dc;
        if (Lofi) {
            clock += m.clock_step;
            if (clock < m.clock_step) held = (dc + m.half) & m.mask;
            v = held;
        }
        out[i] = clamp((v >> 8) * level, -ceiling, ceiling);
        if (Ramp) level += step;
    }
    voice.dc_in = in;
    voice.dc_out = dc;
    voice.level = level;
    voice.held = held;
    voice.clock = clock;
}
}

// Callbacks run serially per voice; owner is a stable nonzero voice ID.
inline void initialize(Voice& voice, Shared&, u32 owner) {
    // The lines start together (at unison without detune they are one), each
    // voice at its own place in the cycle, so a chord's notes struck at once
    // do not all peak at once.
    const u32 phase = (owner * 0x9e3779b9u) ^ ((owner * 0x9e3779b9u) >> 15);
    for (unsigned k = 0; k < 2; ++k) {
        voice.phase[k] = phase;
        voice.rate[k] = 0;
        voice.last[k] = 0;
        voice.band[k].increment = 0;   // find the band limit afresh
    }
    voice.age = 0;
    voice.from = voice.envelope = 0;
    voice.notes = owner * 0x9e3779b9u;
    voice.breath = pd::breath_slowest;
    voice.seed = owner * 0x85ebca6bu + 0x165667b1u;
    voice.noise = 0;
    voice.noise_level = -1;   // find the pole and scale afresh
    voice.dc_in = voice.dc_out = 0;
    voice.level = 0;   // fades in over the first block
    voice.clock = 0x80000000u;
    voice.held = 0;
    voice.fresh = 1;
}

// Each note restarts the contour from where the last one left it, breathing
// at its own rate; the lines run on, so a note played again does not click.
inline void start(Voice& voice, Shared&, u32) {
    voice.from = voice.envelope;
    voice.age = 0;
    voice.notes = voice.notes * 1664525u + 1013904223u;
    voice.breath = pd::breath_slowest + ((voice.notes >> 16) * pd::breath_spread >> 16);
}

inline void destroy(Voice&, Shared&, u32) {}

inline void render(Voice& voice, Shared&, u32, opxy::Output output, opxy::Parameters p, opxy::Pitch pitch) {
    using namespace engine;
    const u32 wave = pd::choice(p[WAVE], pd::waves), interval = pd::choice(p[INTERVAL], pd::intervals);
    const i32 bend = q15(p[BEND]), ring = q15(p[RING]), warp = q15(p[WARP]);
    const i32 noise = q15(p[NOISE]), lofi = q15(p[LOFI]);

    // Pitch: line 1 on the note; line 2 at the interval and detune's cents
    // above it, or at unison the two lines detuned either side of the note.
    const u32 note = pitch[0] < pd::fastest ? pitch[0] : pd::fastest;
    const i32 cents = pd::detune_octaves(p[DETUNE]);
    i32 offset[2] = {0, pd::interval_octaves[interval] + cents};
    if (interval == 1) { offset[0] = -(cents >> 1); offset[1] = cents - (cents >> 1); }

    // The contour, and bend's share of the map this block.
    const i32 envelope = pd::contour(voice.age, voice.from, voice.breath);
    voice.envelope = envelope;
    ++voice.age;
    const i32 d = pd::bent(bend, envelope);

    Plan plan[2];
    for (unsigned k = 0; k < 2; ++k) {
        Plan& l = plan[k];
        l.increment = offset[k] ? pd::transpose(note, offset[k]) : note;
        // The map, bent no further than the line's pitch allows (the limit
        // found again only when the pitch, wave or warp change).
        Band& band = voice.band[k];
        if (band.increment != l.increment || band.wave != wave || band.warp != warp) {
            band.increment = l.increment;
            band.wave = wave;
            band.warp = warp;
            band.top = pd::ceiling(wave, l.increment, warp);
            band.inverse = 0xffffffffu / (l.increment | 1u);
        }
        pd::shape(l.line, wave, d, band.top.warp, band.top.d);
        if (voice.fresh) voice.rate[k] = l.line.r;
        // The step where the cycle ends, from the map's last value to its
        // first: the reso windows' edge and warp's overrun.
        l.jump = wave == RESO1 || wave == RESO3 || (warp && wave != RESO2)
                   ? pd::output(l.line, wave, 0) - pd::output(l.line, wave, 65535) : 0;
        l.inverse = band.inverse;
        l.gain = 16384;
    }
    if (plan[1].increment > fade_from)
        plan[1].gain = static_cast<i32>(((pd::fastest - plan[1].increment) >> 16) * 16384u /
                                        ((pd::fastest - fade_from) >> 16));

    // Noise: white noise low-passed at 8 times the note (1 to 16 kHz) and
    // scaled to its depth whatever the pole, then added to both lines' phases.
    u32 noise_buffer[opxy::block_frames];
    if (noise) {
        if (voice.noise_note != note || voice.noise_level != noise) {
            voice.noise_note = note;
            voice.noise_level = noise;
            voice.noise_pole = clamp(static_cast<i32>(((note >> 10) * 201) >> 9), 4669, 32767);
            const u32 pole = static_cast<u32>(voice.noise_pole);
            const u32 share = pole * 32768u / (65536u - pole);           // c / (2 - c), Q15
            const u32 spread = (18919u * root(share << 15)) >> 15;     // the low-passed noise's rms, Q15
            voice.noise_scale = (static_cast<u32>(pd::noise_depth(p[NOISE])) << 16) / (spread | 1u);
        }
        noise_block(noise_buffer, voice.seed, voice.noise, voice.noise_pole, voice.noise_scale);
    }

    i32 lines[2][opxy::block_frames + 1];
    for (unsigned k = 0; k < 2; ++k) {
        const Plan& l = plan[k];
        const i32 glide = (static_cast<i32>(l.line.r) - static_cast<i32>(voice.rate[k])) / 64;
        lines[k][0] = voice.last[k];
        voice.phase[k] = (warp ? play<true> : play<false>)(lines[k], wave, l, voice.phase[k], voice.rate[k], glide,
                                                           noise ? noise_buffer : 0);
        voice.last[k] = lines[k][opxy::block_frames];
        voice.rate[k] = l.line.r;
    }

    const i32 level = (gain * wave_gain[wave]) >> 14;
    // A new voice's DC blocker starts at its waves' mean, so the bent waves'
    // offset does not thump.
    if (voice.fresh) {
        i32 sum = 0;
        for (i32 x = 0; x < 65536; x += 2048)
            sum += pd::output(plan[0].line, wave, x) + ((pd::output(plan[1].line, wave, x) * plan[1].gain) >> 14);
        voice.dc_in = sum << 4;   // the mean of 32 samples, Q24
        voice.dc_out = 0;
        voice.fresh = 0;
    }

    Mix m;
    m.keep = ((32767 - ring) * plan[1].gain) >> 14;
    m.ring = (ring * plan[1].gain) >> 14;
    m.level = level;
    m.level_step = (level - voice.level) / 64;
    if (lofi) {
        const i32 rate = pd::decay(pd::lofi_octaves(p[LOFI]));
        if (rate >= 30110) {   // at or above 44.1 kHz: every sample
            m.clock_step = 0xffffffffu;
            if (voice.clock < 0x40000000u) voice.clock = 0x80000000u;
        } else {
            m.clock_step = 142639u * static_cast<u32>(rate);   // 48 kHz x 2^32 / 44.1 kHz, Q15
        }
        const u32 step = 1u << (26 - pd::lofi_bits(p[LOFI]));   // the DAC spans two lines' peaks, Q24
        m.half = static_cast<i32>(step >> 1);
        m.mask = ~static_cast<i32>(step - 1);
        if (m.level_step) mix<true, true>(output, lines[0], lines[1], m, voice);
        else mix<true, false>(output, lines[0], lines[1], m, voice);
    } else {
        if (m.level_step) mix<false, true>(output, lines[0], lines[1], m, voice);
        else mix<false, false>(output, lines[0], lines[1], m, voice);
    }
    voice.level = level;
}
}
