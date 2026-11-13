#include "core.h"
#include "sine.h"

// Bloom: a bank of 24 sine partials. spectrum.h decides every partial's
// level and pitch once a block (tilt, shape, stretch, vowel and breath's
// drift); here haze turns each partial into a detuned pair (a beat envelope),
// blur spreads its pitch by smoothed noise, and the partials are ramped across
// the block and summed.
namespace bloom {

// Below this level, Q15, a partial is left out: 46 dB under loud_level, a voice's loudness.
const i32 level_floor = 38;

// One partial over a block, added into mix, its amplitude ramping by `step` a
// sample; returns its phase. One partial a pass keeps every value in a
// register, and the pointer walk compiles to a Blackfin hardware loop. Levels
// stay below 2^15, so the amplitude's top half multiplies as it is.
// The top half of a Q31 amplitude as a 16-bit value: written so (a logical
// shift, then a 16-bit truncation) GCC multiplies it 16 x 16 in one instruction.
inline opxy::i16 top_half(i32 amplitude) { return static_cast<opxy::i16>(static_cast<u32>(amplitude) >> 16); }
inline u32 sound(i32* mix, u32 phase, u32 increment, i32 amplitude, i32 step) {
    for (i32* out = mix; out != mix + opxy::block_frames; ++out) {
        *out += sine_table[phase >> 21] * top_half(amplitude);
        phase += increment;
        amplitude += step;
    }
    return phase;
}
// The same, writing rather than adding: the block's first sounding partial.
inline u32 sound_first(i32* mix, u32 phase, u32 increment, i32 amplitude, i32 step) {
    for (i32* out = mix; out != mix + opxy::block_frames; ++out) {
        *out = sine_table[phase >> 21] * top_half(amplitude);
        phase += increment;
        amplitude += step;
    }
    return phase;
}
// Both partials of a pair in one pass, so mix is read and written once a
// sample for the two. Each table address is worked out a sample ahead, so it
// is ready when read. Out of line, so the loop has the registers to itself:
// inlined, the adding pass spilled two values to the stack each sample. The
// sum is the two single passes' to the bit: the partials' products add in
// another order, and nothing nears overflow.
namespace {
template <bool First>
__attribute__((noinline)) void sound_two(i32* mix, u32& phase_a, u32 increment_a, i32 amplitude_a, i32 step_a,
                                         u32& phase_b, u32 increment_b, i32 amplitude_b, i32 step_b) {
    u32 a = phase_a, b = phase_b;
    const opxy::i16* at_a = sine_table + (a >> 21);
    const opxy::i16* at_b = sine_table + (b >> 21);
    for (i32* out = mix; out != mix + opxy::block_frames; ++out) {
        const i32 sine_a = *at_a, sine_b = *at_b;
        a += increment_a;
        b += increment_b;
        at_a = sine_table + (a >> 21);
        at_b = sine_table + (b >> 21);
        const i32 both = sine_a * top_half(amplitude_a) + sine_b * top_half(amplitude_b);
        *out = First ? both : *out + both;
        amplitude_a += step_a;
        amplitude_b += step_b;
    }
    phase_a = a;
    phase_b = b;
}
}  // namespace

inline void start(Voice& voice, Shared& shared, u32) {
    voice.seed = note_seed(shared.notes++);
    voice.time = 0;
    voice.sounding = 0;
    voice.random = voice.seed | 1u;
    // Seeded starting phases keep the partials (and their beats) from all
    // peaking together; amplitudes start at zero and ramp in over the first block.
    for (u32 k = 0; k < partial_count; ++k) {
        Partial& p = voice.partials[k];
        p.phase = hash(voice.seed ^ (k * 0x61c88647u));
        p.beat = hash(p.phase + 0x2545f491u);
        p.spread = pair_spread(voice.seed, k);
        p.amplitude = 0;
        p.wobble = 0;
        const u32 h = clock_hash(voice.seed, k);   // clock_offset's and clock_rate's, hashed once
        p.offset = h ^ (h << 11);
        p.rate = static_cast<u16>(192u + (h & 127u) + ((h >> 7) & 31u));
        p.point = 0xffff;
    }
}

// Every partial's level drift, exactly as drifts() gives it, hashing new
// lattice values only when a partial's clock passes a lattice point.
inline void voice_drifts(const Controls& c, Voice& voice, i32 (&out)[partial_count]) {
    for (u32 k = 0; k < partial_count; ++k) {
        Partial& p = voice.partials[k];
        if (!c.breath) { out[k] = 0; continue; }
        const u32 clock = (voice.time >> 8) * p.rate + p.offset, n = clock >> 20;
        if (n != p.point) {
            p.point = static_cast<u16>(n);
            p.from = static_cast<i16>(lattice(voice.seed, 2 * k, n));
            p.to = static_cast<i16>(lattice(voice.seed, 2 * k, n + 1));
        }
        out[k] = between(p.from, p.to, clock);
    }
}

inline void initialize(Voice& voice, Shared& shared, u32) {
    voice.cycle = shared.cycle;
    // The SDK always calls start next, which sets every partial; only the note
    // counter moves here, as the start this used to call moved it.
    ++shared.notes;
}

inline void destroy(Voice&, Shared&, u32) {}

// One partial's block: its increment (with blur's spread) and its target
// amplitude (with haze's beat), Q31.
struct Step {
    u32 increment;
    i32 target;
};
inline Step partial_step(const Controls& c, Partial& p, u32& random, i32 note, i32 pitch, i32 level, u32 k) {
    if (c.blur) {
        // A one-pole low-pass of white noise, 8..38 Hz, scaled so its spread
        // stays about 0.45 whatever the rate.
        random = random * 1664525u + 1013904223u;
        const i32 white = (static_cast<i32>(random) >> 16) * static_cast<i32>(c.blur_gain) >> 8;
        p.wobble += (white - p.wobble) * static_cast<i32>(c.blur_alpha) >> 12;
        pitch += (c.blur * static_cast<i32>(partial_reach(k)) >> 8) * p.wobble >> 15;
    }
    const i32 f = note + pitch;
    Step s;
    s.increment = pow2(f < log2_cutoff ? f : log2_cutoff);   // capped below Nyquist; above it has faded to zero
    i32 envelope = 32768;
    if (c.haze_depth) {
        p.beat += beat_step(c, p.spread, s.increment);
        const i32 depth = static_cast<i32>(c.haze_depth * partial_reach(k) >> 8);
        envelope += (depth * sine_table[(p.beat + 0x40000000u) >> 21] >> 15) - depth;
    }
    s.target = (level * envelope >> 15) << 16;
    return s;
}

inline void render(Voice& voice, Shared& shared, u32, opxy::Output output,
                   opxy::Parameters parameters, opxy::Pitch pitch) {
    const Controls c = decode(parameters);
    // The first voice to render in a new block moves the weather on: a voice
    // that rendered in the current cycle already is back for the next block.
    if (voice.cycle == shared.cycle) {
        ++shared.cycle;
        shared.clock += c.step >> 2;   // the weather moves at a quarter of the partials' pace
    }
    voice.cycle = shared.cycle;

    const u32 base = pitch[0];
    const i32 note = log2_of(base);
    // Silent: no pitch, or a fundamental at or above the cutoff (so every
    // partial is, and shape would give every one level zero), with nothing
    // still fading. Then nothing would sound, no phase would move and only
    // the note's time goes on, so the spectrum is not worked out.
    if (!voice.sounding && (!base || note >= log2_cutoff)) {
        voice.time += c.step;
        for (u32 i = 0; i < opxy::block_frames; ++i) output[i] = 0;
        return;
    }
    Spectrum s;
    i32 drift[partial_count];
    voice_drifts(c, voice, drift);
    shape(c, voice.seed, voice.time, weather(shared.clock), note, drift, s, true);
    if (!base) for (u32 k = 0; k < partial_count; ++k) s.level[k] = 0;
    voice.time += c.step;
    // A partial too quiet to hear beside the rest costs as much as the loudest: leave
    // it out. One already sounding stays until a little lower, so it doesn't flutter.
    s.sounding = 0;
    for (u32 k = 0; k < partial_count; ++k) {
        const i32 floor = voice.partials[k].amplitude ? level_floor - (level_floor >> 2) : level_floor;
        if (s.level[k] < floor) s.level[k] = 0;
        else s.sounding = k + 1;
    }

    // Partials still fading out from the last block keep rendering until silent.
    u32 n = s.sounding > voice.sounding ? s.sounding : voice.sounding;
    voice.sounding = s.sounding;
    n = (n + 1) & ~1u;   // in pairs
    if (!n) {   // every partial culled, and none fading: silence
        for (u32 i = 0; i < opxy::block_frames; ++i) output[i] = 0;
        return;
    }

    i32 mix[opxy::block_frames];
    bool written = false;
    u32 random = voice.random;
    for (u32 k = 0; k < n; k += 2) {
        Partial& p = voice.partials[k];
        Partial& q = voice.partials[k + 1];
        if (!(p.amplitude | q.amplitude | s.level[k] | s.level[k + 1])) continue;
        const Step sp = partial_step(c, p, random, note, s.pitch[k], s.level[k], k);
        const Step sq = partial_step(c, q, random, note, s.pitch[k + 1], s.level[k + 1], k + 1);
        // Both sounding: one pass. A partial silent all block (none now, none
        // targeted) would add exactly zero, so only its phase moves on, as the
        // loop would move it.
        const bool sound_p = (p.amplitude | sp.target) != 0, sound_q = (q.amplitude | sq.target) != 0;
        const i32 dp = (sp.target - p.amplitude) >> 6, dq = (sq.target - q.amplitude) >> 6;
        if (sound_p && sound_q) {
            if (written) sound_two<false>(mix, p.phase, sp.increment, p.amplitude, dp, q.phase, sq.increment, q.amplitude, dq);
            else sound_two<true>(mix, p.phase, sp.increment, p.amplitude, dp, q.phase, sq.increment, q.amplitude, dq);
            written = true;
        } else {
            Partial* const both[2] = {&p, &q};
            const Step* const steps[2] = {&sp, &sq};
            const i32 ramps[2] = {dp, dq};
            for (u32 j = 0; j < 2; ++j) {
                Partial& r = *both[j];
                const Step& t = *steps[j];
                if (!(r.amplitude | t.target)) {
                    r.phase += t.increment * opxy::block_frames;
                    continue;
                }
                r.phase = written ? sound(mix, r.phase, t.increment, r.amplitude, ramps[j])
                                  : sound_first(mix, r.phase, t.increment, r.amplitude, ramps[j]);
                written = true;
            }
        }
        p.amplitude = sp.target;
        q.amplitude = sq.target;
    }
    if (!written) for (u32 i = 0; i < opxy::block_frames; ++i) mix[i] = 0;
    voice.random = random;
    // Q30 sum of Q15 sines times Q15 levels (summing to at most peak_level),
    // at 1.156: at most 0.39 of full scale, and a chord of eight peaks near full scale.
    for (u32 i = 0; i < opxy::block_frames; ++i) output[i] = mix[i] + (mix[i] >> 3) + (mix[i] >> 5);
}

}  // namespace bloom
