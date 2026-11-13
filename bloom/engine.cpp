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

// The sounding passes, each over N samples: 64 at the full rate, 32 at half
// rate (twice the phase step, from where the full rate's sample 0 is: see
// Bank). Out of line, so each loop has the registers to itself and the
// render path stays short; each table address is worked out a sample ahead,
// so it is ready when read. Levels stay below 2^15, so an amplitude's top
// half multiplies as it is.
// The top half of a Q31 amplitude as a 16-bit value: written so (a logical
// shift, then a 16-bit truncation) GCC multiplies it 16 x 16 in one instruction.
inline opxy::i16 top_half(i32 amplitude) { return static_cast<opxy::i16>(static_cast<u32>(amplitude) >> 16); }
namespace {
// One partial into mix (written, for the first), its amplitude ramping by
// `step` a sample; returns its phase.
template <bool First, u32 N>
__attribute__((noinline)) u32 sound_one(i32* mix, u32 phase, u32 increment, i32 amplitude, i32 step) {
    const opxy::i16* at = sine_table + (phase >> 21);
    for (i32* out = mix; out != mix + N; ++out) {
        const i32 sine = *at;
        phase += increment;
        at = sine_table + (phase >> 21);
        const i32 one = sine * top_half(amplitude);
        *out = First ? one : *out + one;
        amplitude += step;
    }
    return phase;
}
// Two partials in one pass, so mix is read and written once a sample for the
// two (inlined into render, the adding pass spilled two values to the stack
// each sample).
template <bool First, u32 N>
__attribute__((noinline)) void sound_two(i32* mix, u32& phase_a, u32 increment_a, i32 amplitude_a, i32 step_a,
                                         u32& phase_b, u32 increment_b, i32 amplitude_b, i32 step_b) {
    u32 a = phase_a, b = phase_b;
    const opxy::i16* at_a = sine_table + (a >> 21);
    const opxy::i16* at_b = sine_table + (b >> 21);
    for (i32* out = mix; out != mix + N; ++out) {
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

// One rate's partials, sounded two at a time into mix: each waits here until
// a second comes (or the block ends), then both sound in one pass. The first
// pass writes mix; finish clears it if none came. A partial's arguments are
// the full rate's (sample i reads the phase i steps on and sounds amplitude
// + i step); at half rate, sample m is the full rate's 2m, so the steps
// double. Its phase is
// written back where it says (nowhere for the old rate's copy of a partial
// changing rate, whose end phase the new rate's copy writes).
template <u32 N>
struct Bank {
    i32* mix;
    bool written, waiting;
    u32 phase, increment;
    i32 amplitude, step;
    u32* at;
    void add(u32* where, u32 p, u32 inc, i32 a, i32 d) {
        if (N != opxy::block_frames) inc <<= 1, d <<= 1;   // half rate: two steps a sample
        if (!waiting) {
            at = where;
            phase = p;
            increment = inc;
            amplitude = a;
            step = d;
            waiting = true;
            return;
        }
        u32 p0 = phase;
        if (written) sound_two<false, N>(mix, p0, increment, amplitude, step, p, inc, a, d);
        else sound_two<true, N>(mix, p0, increment, amplitude, step, p, inc, a, d);
        written = true;
        waiting = false;
        if (at) *at = p0;
        if (where) *where = p;
    }
    void finish() {
        if (waiting) {
            const u32 p = written ? sound_one<false, N>(mix, phase, increment, amplitude, step)
                                  : sound_one<true, N>(mix, phase, increment, amplitude, step);
            if (at) *at = p;
            written = true;
            waiting = false;
        }
        if (!written) for (u32 i = 0; i < N; ++i) mix[i] = 0;
    }
};

// The half-band filter's odd-phase coefficients, Q15: an odd full-rate sample
// is the sum of c_t times the pair of half-rate samples t + 1/2 either side
// (19 taps at the full rate, every other one zero). Equiripple, rounded and
// searched as integers: flat within 0.003 dB to 6.6 kHz, above every half-rate
// partial, and the half-rate images (above 15.45 kHz) 70 dB down. (Hollow's.)
const opxy::i16 kHalfBand[kHalfTaps] = {20301, -5421, 2036, -664, 142};
// Two taps' share of every odd sample, t and t + 1, added to sum (written,
// for the first). x: each odd sample's left neighbour, the mix's top bits.
template <u32 t>
__attribute__((noinline)) void taps(const opxy::i16* x, i32* sum) {
    const opxy::i16 a = kHalfBand[t], b = kHalfBand[t + 1];
    for (i32 j = 0; j < static_cast<i32>(kHalfFrames); ++j)
        sum[j] = (t ? sum[j] : 0) + x[j - static_cast<i32>(t)] * a + x[j + static_cast<i32>(t) + 1] * a +
                 x[j - static_cast<i32>(t) - 1] * b + x[j + static_cast<i32>(t) + 2] * b;
}
// Back to the full rate, kLag samples late: low holds the last block's
// kHalfHistory half-rate samples, then this block's. Even samples are the
// half-rate ones as they are; odd ones the filter's value halfway to the
// next, from the mix over 2^15, rounded (a voice's mix stays under
// peak_level x 2^15, so that fits 16 bits).
__attribute__((noinline)) void upsample(const i32* low, i32* out) {
    opxy::i16 top[kHalfHistory + kHalfFrames];
    for (u32 i = 0; i < kHalfHistory + kHalfFrames; ++i) top[i] = static_cast<opxy::i16>((low[i] + 16384) >> 15);
    const opxy::i16* const x = top + kHalfTaps - 1;
    i32 odd[kHalfFrames];
    taps<0>(x, odd);
    taps<2>(x, odd);
    const opxy::i16 c4 = kHalfBand[4];
    for (i32 j = 0; j < static_cast<i32>(kHalfFrames); ++j) {
        out[2 * j] = low[j + kHalfTaps - 1];
        out[2 * j + 1] = odd[j] + x[j - 4] * c4 + x[j + 5] * c4;
    }
}
// Below this increment a partial goes to half rate (5.8 kHz), and from 6 kHz
// back to the full rate, so one near the edge doesn't change every block.
const u32 kHalfEnter = 566088377u, kHalfLeave = 584349292u;
}  // namespace

inline void start(Voice& voice, Shared& shared, u32) {
    voice.seed = note_seed(shared.notes++);
    voice.time = 0;
    voice.sounding = 0;
    voice.random = voice.seed | 1u;
    voice.halved = 0;
    voice.tail = 0;
    for (u32 i = 0; i < kHalfHistory; ++i) voice.low[i] = 0;
    for (u32 i = 0; i < kLag; ++i) voice.late[i] = 0;
    // Seeded starting phases keep the partials (and their beats) from all
    // peaking together; amplitudes start at zero and ramp in over the first block.
    for (u32 k = 0; k < partial_count; ++k) {
        voice.phase[k] = hash(voice.seed ^ (k * 0x61c88647u));
        voice.beat[k] = hash(voice.phase[k] + 0x2545f491u);
        voice.spread[k] = pair_spread(voice.seed, k);
        voice.amplitude[k] = 0;
        voice.wobble[k] = 0;
        const u32 h = clock_hash(voice.seed, k);   // clock_offset's and clock_rate's, hashed once
        voice.offset[k] = h ^ (h << 11);
        voice.rate[k] = static_cast<u16>(192u + (h & 127u) + ((h >> 7) & 31u));
        voice.point[k] = 0xffff;
    }
}

// Every partial's level drift, exactly as drifts() gives it, hashing new
// lattice values only when a partial's clock passes a lattice point.
inline void voice_drifts(const Controls& c, Voice& voice, i32 (&out)[partial_count]) {
    for (u32 k = 0; k < partial_count; ++k) {
        if (!c.breath) { out[k] = 0; continue; }
        const u32 clock = (voice.time >> 8) * voice.rate[k] + voice.offset[k], n = clock >> 20;
        if (n != voice.point[k]) {
            voice.point[k] = static_cast<u16>(n);
            voice.from[k] = static_cast<i16>(lattice(voice.seed, 2 * k, n));
            voice.to[k] = static_cast<i16>(lattice(voice.seed, 2 * k, n + 1));
        }
        out[k] = between(voice.from[k], voice.to[k], clock);
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
inline Step partial_step(const Controls& c, Voice& voice, u32& random, i32 note, i32 pitch, i32 level, u32 k) {
    if (c.blur) {
        // A one-pole low-pass of white noise, 8..38 Hz, scaled so its spread
        // stays about 0.45 whatever the rate.
        random = random * 1664525u + 1013904223u;
        const i32 white = (static_cast<i32>(random) >> 16) * static_cast<i32>(c.blur_gain) >> 8;
        i32& wobble = voice.wobble[k];
        wobble += (white - wobble) * static_cast<i32>(c.blur_alpha) >> 12;
        pitch += (c.blur * static_cast<i32>(partial_reach(k)) >> 8) * wobble >> 15;
    }
    const i32 f = note + pitch;
    Step s;
    s.increment = pow2(f < log2_cutoff ? f : log2_cutoff);   // capped below Nyquist; above it has faded to zero
    i32 envelope = 32768;
    if (c.haze_depth) {
        voice.beat[k] += beat_step(c, voice.spread[k], s.increment);
        const i32 depth = static_cast<i32>(c.haze_depth * partial_reach(k) >> 8);
        envelope += (depth * sine_table[(voice.beat[k] + 0x40000000u) >> 21] >> 15) - depth;
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
    if (!voice.sounding && !voice.tail && (!base || note >= log2_cutoff)) {
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
        const i32 floor = voice.amplitude[k] ? level_floor - (level_floor >> 2) : level_floor;
        if (s.level[k] < floor) s.level[k] = 0;
        else s.sounding = k + 1;
    }

    // Partials still fading out from the last block keep rendering until silent.
    u32 n = s.sounding > voice.sounding ? s.sounding : voice.sounding;
    voice.sounding = s.sounding;
    n = (n + 1) & ~1u;   // in pairs
    if (!n && !voice.tail) {   // every partial culled, none fading, nothing still in the filter: silence
        for (u32 i = 0; i < opxy::block_frames; ++i) output[i] = 0;
        return;
    }

    // The half-rate mix after the filter's history from the last block, and
    // the full rate's.
    i32 low[kHalfHistory + kHalfFrames], full[opxy::block_frames];
    Bank<kHalfFrames> slow = {low + kHalfHistory, false, false, 0, 0, 0, 0, nullptr};
    Bank<opxy::block_frames> fast = {full, false, false, 0, 0, 0, 0, nullptr};
    u32 random = voice.random;
    for (u32 k = 0; k < n; k += 2) {
        if (!(voice.amplitude[k] | voice.amplitude[k + 1] | s.level[k] | s.level[k + 1])) continue;
        const Step steps[2] = {partial_step(c, voice, random, note, s.pitch[k], s.level[k], k),
                               partial_step(c, voice, random, note, s.pitch[k + 1], s.level[k + 1], k + 1)};
        for (u32 j = 0; j < 2; ++j) {
            const Step& t = steps[j];
            u32& phase = voice.phase[k + j];
            i32& amplitude = voice.amplitude[k + j];
            // Silent all block (none now, none targeted): it would add exactly
            // zero, so only its phase moves on, as the loop would move it.
            if (!(amplitude | t.target)) {
                phase += t.increment * opxy::block_frames;
                continue;
            }
            const u32 bit = 1u << (k + j);
            const bool was = (voice.halved & bit) != 0;
            const bool now = t.increment < (was ? kHalfLeave : kHalfEnter);
            if (now) voice.halved |= bit;
            else voice.halved &= ~bit;
            if (amplitude && was != now) {
                // Changing rate: it fades out at the old rate as it fades in
                // at the new over this block, the two ramps summing to its own.
                if (was) slow.add(nullptr, phase, t.increment, amplitude, -amplitude >> 6);
                else fast.add(nullptr, phase, t.increment, amplitude, -amplitude >> 6);
                if (now) slow.add(&phase, phase, t.increment, 0, t.target >> 6);
                else fast.add(&phase, phase, t.increment, 0, t.target >> 6);
            } else {
                const i32 d = (t.target - amplitude) >> 6;
                if (now) slow.add(&phase, phase, t.increment, amplitude, d);
                else fast.add(&phase, phase, t.increment, amplitude, d);
            }
            amplitude = t.target;
        }
    }
    voice.random = random;
    // Out: the half rate's partials back at the full rate, the full rate's
    // delayed to meet them; each part skipped when it and what it still owes
    // are silent.
    const bool halves = slow.written || slow.waiting || (voice.tail & 1u);
    const bool wholes = fast.written || fast.waiting || (voice.tail & 2u);
    u32 tail = 0;
    if (halves) {
        slow.finish();
        u32 held = 0;
        for (u32 i = 0; i < kHalfHistory; ++i) low[i] = voice.low[i];
        for (u32 i = 0; i < kHalfHistory; ++i) held |= static_cast<u32>(voice.low[i] = low[kHalfFrames + i]);
        upsample(low, output);
        if (held) tail = 1u;
    } else {
        for (u32 i = 0; i < opxy::block_frames; ++i) output[i] = 0;
    }
    if (wholes) {
        fast.finish();
        u32 left = 0;
        for (u32 i = 0; i < kLag; ++i) output[i] += voice.late[i];
        for (u32 i = kLag; i < opxy::block_frames; ++i) output[i] += full[i - kLag];
        for (u32 i = 0; i < kLag; ++i) left |= static_cast<u32>(voice.late[i] = full[opxy::block_frames - kLag + i]);
        if (left) tail |= 2u;
    }
    voice.tail = tail;
    // Q30 sum of Q15 sines times Q15 levels (summing to at most peak_level),
    // at 1.156: at most 0.39 of full scale, and a chord of eight peaks near full scale.
    for (u32 i = 0; i < opxy::block_frames; ++i) output[i] = output[i] + (output[i] >> 3) + (output[i] >> 5);
}

}  // namespace bloom
