#include "core.h"

// Orbit: a vector synth. Four single-cycle waves sit at the corners of a
// diamond (A top, B right, C bottom, D left), from one of eight banks of
// vintage digital waves (waves.h). A point mixes them bilinearly, as on the
// Prophet VS, and orbits a centre (x, y) along a path (circle, ellipse,
// figure 8, or steps from corner to corner, as a Wavestation's wave sequence
// does) at a rate. Each note's orbit starts at the top of its path.
//
// Once per 64-sample block the voice works out where its point is and mixes
// the four corners into one 128-point wave for the block, from the
// band-limited tables that suit the note (half the harmonics an octave,
// crossfaded), at an even loudness. Each sample then reads
// that wave, interpolated. Detune plays it twice, the copies either side of
// the note; grit plays the result through an old DAC, a zero-order hold at its
// rate and its bits. Any change of the wave or the gains fades in across one
// block from where the last block left off, so the moving vector never clicks.
//
// Integer arithmetic only, 32-bit, with 16 x 16 multiplies in the sample loops.
namespace orbit {
namespace engine {
const i32 loud = 310000000;            // a set's peak, Q31: 15500 (rom_peak) x 20000, 0.144 of full scale
const i32 quietest = 9688;             // a mix this quiet or quieter is lifted 1.6 times, no more
const i32 even = 140900000;            // a set's loudness, Q31 rms: loud / 2.2, unless its peak would pass loud
const i32 ceiling = 434000000;         // both sets at once (1.4 x loud), where the fades are held
const i32 peak = 436000000;            // with grit's rounding: definition.json's outputPeakQ31
const u32 lowest_band = 40807059u;     // the increment of 419 Hz: above it, half the harmonics an octave
const i32 loud_octaves = 1848619;      // log2(loud), Q16: 28.21 octaves

// The block's wave at a phase, Q14: point phase >> 25, interpolated by the
// next 15 bits.
inline i32 read(const i16* table, u32 phase) {
    const i16* at = table + (phase >> 25);
    const i32 a = at[0];
    return a + (static_cast<i16>(at[1] - a) * static_cast<i16>((phase >> 10) & 0x7fffu) >> 15);
}

// The sample loops are plain functions, each a counted walk over the block
// with few values live, so each compiles to a Blackfin hardware loop (the
// two sets' loops stay out of line: inlined, they lose their hardware loops).
//
// One set over the block: the wave times its gain, into `out`; returns its phase.
u32 play(i32* out, const i16* table, u32 phase, u32 increment, i32 gain) {
    const i16 g = static_cast<i16>(gain);
    i32* o = out;
    for (u32 n = 0; n < opxy::block_frames; ++n, ++o) {
        *o = static_cast<i16>(read(table, phase)) * g;
        phase += increment;
    }
    return phase;
}
// Or added to it.
u32 add(i32* out, const i16* table, u32 phase, u32 increment, i32 gain) {
    const i16 g = static_cast<i16>(gain);
    i32* o = out;
    for (u32 n = 0; n < opxy::block_frames; ++n, ++o) {
        *o += static_cast<i16>(read(table, phase)) * g;
        phase += increment;
    }
    return phase;
}
// The fade from the last block, `from` falling to nothing across this one,
// within the ceiling.
void fade(i32* out, i32 from) {
    const i32 slope = -(from >> 6);
    i32* o = out;
    for (u32 n = 0; n < opxy::block_frames; ++n, ++o) {
        *o = clamp(*o + from, -ceiling, ceiling);
        from += slope;
    }
}

// Grit's DAC below 44.1 kHz: its clock, and each sample that takes a new
// value (all ones) or holds the last (zero).
void ticks(u32* tick, u32& clock, u32 increment) {
    u32 c = clock;
    u32* t = tick;
    for (u32 n = 0; n < opxy::block_frames; ++n, ++t) {
        c += increment;
        *t = 0u - static_cast<u32>(c < increment);
    }
    clock = c;
}
// One set through that DAC: its phase moves a whole DAC sample (`step`) at
// each tick and holds between them, so the wave is sampled at the DAC's rate
// and held, aliasing as an old DAC does. Into `out`, or added to it.
u32 play_held(i32* out, const i16* table, u32 phase, u32 step, i32 gain, const u32* tick) {
    const i16 g = static_cast<i16>(gain);
    i32* o = out;
    const u32* t = tick;
    for (u32 n = 0; n < opxy::block_frames; ++n, ++o, ++t) {
        phase += step & *t;
        *o = static_cast<i16>(read(table, phase)) * g;
    }
    return phase;
}
u32 add_held(i32* out, const i16* table, u32 phase, u32 step, i32 gain, const u32* tick) {
    const i16 g = static_cast<i16>(gain);
    i32* o = out;
    const u32* t = tick;
    for (u32 n = 0; n < opxy::block_frames; ++n, ++o, ++t) {
        phase += step & *t;
        *o += static_cast<i16>(read(table, phase)) * g;
    }
    return phase;
}
// And its bits: each sample rounded to the DAC's steps (recip, Q15, turns a
// sample >> 15 into steps; a step is scale << shift).
void bits(i32* out, i32 recip, i32 scale, i32 shift) {
    const i16 r = static_cast<i16>(recip), s = static_cast<i16>(scale);
    i32* o = out;
    for (u32 n = 0; n < opxy::block_frames; ++n, ++o) {
        const i32 level = (static_cast<i16>(*o >> 15) * r + 16384) >> 15;
        *o = (static_cast<i16>(level) * s) << shift;
    }
}

// One band's four corners, weighted (Q14): into `sum` (Q28), or with Add,
// added to it; with Final, into the table (Q14) instead.
template <bool Add, bool Final>
inline void corners(i32* sum, i16* table, const u32* words, i32 a, i32 b, i32 c, i32 d) {
    const i16 wa = static_cast<i16>(a), wb = static_cast<i16>(b), wc = static_cast<i16>(c), wd = static_cast<i16>(d);
    const i16* corner = reinterpret_cast<const i16*>(words);
    i32* o = sum;
    i16* t = table;
    for (u32 n = 0; n < 128; ++n, ++o, ++t, corner += 4) {
        i32 v = corner[0] * wa + corner[1] * wb + corner[2] * wc + corner[3] * wd;
        if (Add) v += *o;
        if (Final) *t = static_cast<i16>(v >> 14);
        else *o = v;
    }
}
// The block's wave from the four corners' tables at `level` and, by `fade`
// (Q14), the band above.
void mix(i16* table, u32 bank, u32 level, i32 fade, const i32 (&w)[4]) {
    i32 sum[128];
    const i32 low = one - fade;
    const u32* below = rom[bank][level][0];
    if (!fade) {
        corners<false, true>(sum, table, below, w[0], w[1], w[2], w[3]);
    } else {
        const u32* above = rom[bank][level + 1][0];
        if (low) {
            corners<false, false>(sum, table, below, w[0] * low >> 14, w[1] * low >> 14, w[2] * low >> 14, w[3] * low >> 14);
            corners<true, true>(sum, table, above, w[0] * fade >> 14, w[1] * fade >> 14, w[2] * fade >> 14, w[3] * fade >> 14);
        } else {
            corners<false, true>(sum, table, above, w[0], w[1], w[2], w[3]);
        }
    }
    table[128] = table[0];   // the guard point
}
// The most a mix can reach, Q14: its corners' peaks, weighted.
inline i32 reach(u32 bank, u32 level, const i32 (&w)[4]) {
    const opxy::i16* p = peaks[bank][level];
    return (w[0] * p[0] + w[1] * p[1] + w[2] * p[2] + w[3] * p[3]) >> 14;
}

// An increment moved by Q16 octaves, within an octave either way.
inline u32 detuned(u32 increment, i32 octaves) {
    if (!octaves) return increment;
    const i32 ratio = static_cast<i32>(exp2_q16(octaves + 29 * 65536));   // Q29
    const u32 moved = static_cast<u32>(opxy::q31::multiply(static_cast<i32>(increment >> 1), ratio));
    return moved >= 0x20000000u ? 0xfffffff8u : moved << 3;   // (increment / 2) x ratio / 2^31 is increment x ratio / 8
}

// A DAC sample's step of phase: the increment moved up by Q16 octaves (0 to
// 2.3), wrapping past a whole cycle as a sampled phase does.
inline u32 stretched(u32 increment, i32 octaves) {
    const i32 ratio = static_cast<i32>(exp2_q16(octaves + 27 * 65536));   // Q27
    return static_cast<u32>(opxy::q31::multiply(static_cast<i32>(increment >> 1), ratio)) << 5;
}
}

inline void initialize(Voice& voice, Shared&, u32 owner) {
    // A first wave (glass, all four corners), silent until the first block fades it in.
    const i32 even[4] = {4096, 4096, 4096, 4096};
    engine::mix(voice.table, 0, 0, 0, even);
    voice.table[table_points - 1] = 0;
    voice.orbit = 0;
    // Free running, each voice starting elsewhere, its twin a third of a cycle on.
    voice.phase[0] = owner * 0x9e3779b9u;
    voice.phase[1] = voice.phase[0] + 0x55555555u;
    voice.mixed = ~0u;
    voice.fade = 0;
    for (u32 k = 0; k < 4; ++k) voice.share[k] = 0;
    voice.lift = 0;
    voice.gain[0] = voice.gain[1] = 0;
    voice.clock = 0;
}

// Each note's orbit starts at the top of its path.
inline void start(Voice& voice, Shared&, u32) { voice.orbit = 0; }

inline void destroy(Voice&, Shared&, u32) {}

inline void render(Voice& voice, Shared&, u32, opxy::Output output, opxy::Parameters p, opxy::Pitch pitch) {
    using namespace engine;
    const u32 increment = pitch[0];
    // What the last block would play now, at each set's phase: the fade's start.
    const i32 was = static_cast<i16>(read(voice.table, voice.phase[0])) * static_cast<i16>(voice.gain[0]) +
                    static_cast<i16>(read(voice.table, voice.phase[1])) * static_cast<i16>(voice.gain[1]);

    // Where the point is: the centre, plus the path at the orbit's phase, within the gate.
    const Point at = point_at(centre_of(p[X], p[Y]), voice.orbit, radius_of(p[ORBIT]), q15(p[PATH]), true);
    voice.orbit += rate_step(p[RATE]);
    i32 w[4];
    weights(at, w);

    // The band: the full tables up to 419 Hz, then half the harmonics an
    // octave, crossfaded, down to a sine from 6.7 kHz.
    const i32 above = clamp(log2_q16(increment) - log2_q16(lowest_band), 0, 4 * 65536);
    const u32 level = above >= 4 * 65536 ? 3u : static_cast<u32>(above >> 16);
    const i32 fade_up = above >= 4 * 65536 ? one : (above & 0xffff) >> 2;

    // Mix the block's wave when anything in it has moved, and set its loudness.
    const u32 bank = bank_of(p[BANK]), mixed = bank * 8 + level;
    if (mixed != voice.mixed || fade_up != voice.fade || w[0] != voice.share[0] || w[1] != voice.share[1] ||
        w[2] != voice.share[2] || w[3] != voice.share[3]) {
        // Its loudness is held even, at `even` (by its power from the weights),
        // unless that could take its loudest point past `loud`.
        mix(voice.table, bank, level, fade_up, w);
        const i32 power = fade_up ? (loudness(bank, level, w) * (one - fade_up) + loudness(bank, level + 1, w) * fade_up) >> 14
                                  : loudness(bank, level, w);
        const i32 loudest = fade_up ? (reach(bank, level, w) * (one - fade_up) + reach(bank, level + 1, w) * fade_up) >> 14
                                    : reach(bank, level, w);
        const i32 by_peak = loud / (loudest > quietest ? loudest : quietest);
        const i32 by_power = power > 0 ? even / power : by_peak;
        voice.lift = by_power < by_peak ? by_power : by_peak;
        voice.mixed = mixed;
        voice.fade = fade_up;
        for (u32 k = 0; k < 4; ++k) voice.share[k] = w[k];
    }

    // Detune: the twin comes in over the first eighth of the turn, the note
    // giving way to it (1 and 0 to 0.7 and 0.7: their power stays even), and
    // the two spread either side of the note.
    const i32 twin = clamp(q15(p[DETUNE]) * 8, 0, 32767);
    const i32 gain0 = voice.lift * (32767 - (twin * 9830 >> 15)) >> 15;
    const i32 gain1 = voice.lift * (twin * 22938 >> 15) >> 15;
    const i32 spread = detune_octaves(p[DETUNE]);

    // The fade: from what the last block would play to what this one does.
    const i32 now = static_cast<i16>(read(voice.table, voice.phase[0])) * static_cast<i16>(gain0) +
                    static_cast<i16>(read(voice.table, voice.phase[1])) * static_cast<i16>(gain1);
    const i32 from = was - now;
    voice.gain[0] = gain0;
    voice.gain[1] = gain1;
    if (!increment) {   // no pitch: silence, and the next block fades in from it
        for (u32 i = 0; i < opxy::block_frames; ++i) output[i] = 0;
        voice.gain[0] = voice.gain[1] = 0;
        return;
    }
    // Grit: from 44.1 kHz down, the DAC's rate (10 kHz at the top), each set
    // sampled at it and held; then its bits, 16 down to 8 (at most 14 are
    // heard here).
    const i32 grit = q15(p[GRIT]);
    const i32 rate = grit ? grit_octaves(p[GRIT]) : 0;
    u32 tick[opxy::block_frames];
    if (rate < 0) ticks(tick, voice.clock, exp2_q16(32 * 65536 + rate));
    if (gain1) {
        const u32 step1 = detuned(increment, spread), step0 = detuned(increment, -spread);
        if (rate < 0) {
            voice.phase[1] = play_held(output, voice.table, voice.phase[1], stretched(step1, -rate), gain1, tick);
            voice.phase[0] = add_held(output, voice.table, voice.phase[0], stretched(step0, -rate), gain0, tick);
        } else {
            voice.phase[1] = play(output, voice.table, voice.phase[1], step1, gain1);
            voice.phase[0] = add(output, voice.table, voice.phase[0], step0, gain0);
        }
    } else {
        voice.phase[0] = rate < 0 ? play_held(output, voice.table, voice.phase[0], stretched(increment, -rate), gain0, tick)
                                  : play(output, voice.table, voice.phase[0], increment, gain0);
        voice.phase[1] = voice.phase[0] + 0x55555555u;   // the twin, ready a third of a cycle on
    }
    if (from) fade(output, from);
    if (!grit) return;
    const i32 depth = grit_bits(p[GRIT]);
    const i32 steps = (depth < 14 * 65536 ? depth : 14 * 65536) - 65536;   // log2 of the steps either side of zero
    const i32 step = loud_octaves - steps;                                // log2 of one step, Q31: 15.2 .. 21.2 octaves
    const i32 shift = (step >> 16) - 14;
    bits(output, static_cast<i32>(exp2_q16(30 * 65536 - step)), static_cast<i32>(exp2_q16(step - shift * 65536)), shift);
}
}
