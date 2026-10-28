#include "core.h"
#include "sine.h"

// Bloom: a bank of up to 24 sine partials whose levels and pitches drift by
// seeded, smoothed random walks. spectrum.h decides every partial's level and
// pitch once a block; here they are ramped across the block and summed.
namespace bloom {

inline void start(Voice& voice, Shared& shared, u32) {
    voice.seed = note_seed(shared.notes++);
    voice.time = 0;
    voice.sounding = 0;
    // Seeded starting phases keep the partials from all peaking together;
    // amplitudes start at zero and ramp in over the first block.
    for (u32 k = 0; k < partial_count; ++k) {
        voice.partials[k].phase = hash(voice.seed ^ (k * 0x61c88647u));
        voice.partials[k].amplitude = 0;
    }
}

inline void initialize(Voice& voice, Shared& shared, u32 owner) {
    voice.cycle = shared.cycle;
    start(voice, shared, owner);
}

inline void destroy(Voice&, Shared&, u32) {}

inline void render(Voice& voice, Shared& shared, u32, opxy::Output output,
                   opxy::Parameters parameters, opxy::Pitch pitch) {
    const Controls c = decode(parameters);
    // The first voice to render in a new block moves the weather on: a voice
    // that rendered in the current cycle already is back for the next block.
    if (voice.cycle == shared.cycle) {
        ++shared.cycle;
        shared.clock += c.step >> 1;   // the weather moves at half the partials' pace
    }
    voice.cycle = shared.cycle;

    const u32 base = pitch[0];
    const i32 note = log2_of(base);
    Spectrum s;
    shape(c, voice.seed, voice.time, weather(shared.clock), note, s);
    if (!base) for (u32 k = 0; k < partial_count; ++k) s.level[k] = 0;
    voice.time += c.step;

    // Partials still fading out from the last block keep rendering until silent.
    u32 n = s.sounding > voice.sounding ? s.sounding : voice.sounding;
    voice.sounding = s.sounding;
    n = (n + 1) & ~1u;   // in pairs

    i32 mix[opxy::block_frames];
    for (u32 i = 0; i < opxy::block_frames; ++i) mix[i] = 0;
    for (u32 k = 0; k < n; k += 2) {
        Partial& p = voice.partials[k];
        Partial& q = voice.partials[k + 1];
        const i32 to_p = s.level[k] << 16, to_q = s.level[k + 1] << 16;
        if (!(p.amplitude | q.amplitude | to_p | to_q)) continue;
        // Increments capped below Nyquist; anything above has faded to zero.
        const i32 fp = note + s.pitch[k], fq = note + s.pitch[k + 1];
        const u32 inc_p = pow2(fp < log2_cutoff ? fp : log2_cutoff);
        const u32 inc_q = pow2(fq < log2_cutoff ? fq : log2_cutoff);
        const i32 dp = (to_p - p.amplitude) >> 6, dq = (to_q - q.amplitude) >> 6;
        u32 phase_p = p.phase, phase_q = q.phase;
        i32 amp_p = p.amplitude, amp_q = q.amplitude;
        for (u32 i = 0; i < opxy::block_frames; ++i) {
            mix[i] += i32(sine_table[phase_p >> 20]) * opxy::i16(amp_p >> 16)
                    + i32(sine_table[phase_q >> 20]) * opxy::i16(amp_q >> 16);
            phase_p += inc_p;
            phase_q += inc_q;
            amp_p += dp;
            amp_q += dq;
        }
        p.phase = phase_p;
        q.phase = phase_q;
        p.amplitude = to_p;
        q.amplitude = to_q;
    }
    // Q30 sum of Q15 sines times Q15 levels (summing to at most peak_level): Q31.
    for (u32 i = 0; i < opxy::block_frames; ++i) output[i] = mix[i] * 2;
}

}  // namespace bloom
