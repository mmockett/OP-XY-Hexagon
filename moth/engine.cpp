#include "core.h"
#include "laws.h"
#include <opxy/q31.hpp>

// Moth: a west-coast complex oscillator. A sine carrier, phase-modulated by a
// modulator sine (fm sets the index, ratio the modulator's frequency), runs
// into a sine wavefolder (fold sets its drive, bias offsets its input for
// even harmonics), then through a lowpass gate modelled as a vactrol: an LED
// that follows the note, flashing at a strike, lights a photocell that opens
// in a millisecond and closes at glow's pace. Mode sets whether the gate
// closes a low-pass (lp), fades the level (vca) or both (combo).
//
// Folding and FM spread a sine's energy far up the spectrum, so the
// oscillators and the folder run at twice the rate (88.2 kHz) and an
// elliptic half-band (two paths of allpasses) brings them down; and a
// governor holds the folded FM's bandwidth by Carson's rule,
// f0 (1 + (I + 1) r)(A + 1), under 45 kHz, trimming the drive and the index
// together near the top of the keyboard, where they would otherwise alias.
// The folder is sin(A x + c), read from a sine table with interpolation and
// scaled once a block so its peak is 1, so at fold 0 the carrier passes
// through untouched. Bias's offset is predicted (sin(c) J0(A)) and removed,
// so a biased note starts without a thump.
//
// Every sample loop is a pointer walk over the block with few live values,
// in a function of its own, so each compiles to a zero-overhead hardware
// loop on the Blackfin with every value in a register: the modulator, the
// carrier, the folder, the decimator's two paths, its offset removal, the
// gate's two poles and the output fade. They use 32-bit integer products,
// one instruction each on the Blackfin and on a host alike, except the
// gate's, which need Q31's precision near its floor. Everything that changes
// only once a block (the governor, the folder's plan, the vactrol, drift) is
// planned outside them.
namespace moth {
namespace {
using opxy::q31::multiply;
const u32 kOver = 2 * opxy::block_frames;  // samples a block at twice the rate
const u32 kLowestIncrement = 779000u;      // about 8 Hz

// The half-band decimator: two paths of allpasses, the polyphase form of an
// elliptic half-band (order 9), Q14. Flat to 20 kHz, 3 dB down at 22.05 kHz,
// 44 dB at 25 kHz and at least 63 dB from 26 kHz up, so what it folds back
// lands above 20 kHz or 63 dB down.
const i32 kAllpass0 = 1507, kAllpass1 = 5195, kAllpass2 = 9576, kAllpass3 = 13995;

// The governor: log2 of 45 kHz as an increment, Q16; the modulator's
// index fades out as the modulator rises from 16 to 22 kHz.
const i32 kBandLog2 = 2099062, kModulatorFade = 2001292, kModulatorLimit = 2031401;
const i32 kLeastFold = 1872;  // log2 of 1.02: the folder's least drive plus one

// J0, the folder's mean over a sine's cycle: J0 at every quarter radian, Q15.
const i32 kBessel[33] = {32767, 32257, 30751, 28319, 25073, 21164, 16771, 12092, 7336, 2711, -1585,
                         -5378, -8521, -10903, -12456, -13153, -13013, -12098, -10503, -8360, -5819,
                         -3050, -224, 2489, 4936, 6982, 8523, 9485, 9833, 9568, 8727, 7380, 5624};

const i32 kCutFloor = 917894;  // the gate's low-pass never closes below 3 Hz, Q31
// The output: a full-scale fold with the gate fully open sounds at 0.105 of
// full scale (Q31, times 4 for the output's shift). The decimator's phase
// and the offset's removal can raise a folded wave's peak by up to 1.86
// times, so a voice stays within 0.2 of full scale, under the declared peak
// (0.27, definition.json outputPeakQ31), and the clamp only guarantees it.
const i32 kLevel = 901943132;  // 0.105 * 2^33
const i32 kPeak = 579820584;    // 0.27 of full scale

// The modulator: each sample's offset to the carrier's phase, depth a
// quarter of the index's phase per unit (I / 2 pi times 2^15).
__attribute__((noinline)) u32 modulate(u32* offsets, u32 phase, u32 increment, i32 depth) {
    const i32* const table = kSine15;
    for (u32* out = offsets; out != offsets + kOver; ++out) {
        phase += increment;
        *out = static_cast<u32>(sine15(table, phase) * depth) << 2;
    }
    return phase;
}

// The carrier, its phase moved by the modulator, in place of the offsets, Q15.
__attribute__((noinline)) u32 carry(u32* io, u32 phase, u32 increment) {
    const i32* const table = kSine15;
    for (u32* p = io; p != io + kOver; ++p) {
        phase += increment;
        *p = static_cast<u32>(sine15(table, phase + *p));
    }
    return phase;
}

// The folder: sin(A x + c), scaled so its peak is 1, Q28. drive is A / 2 pi
// times 2^14, bias the phase of c, scale 64 times the folder's gain.
__attribute__((noinline)) void fold(i32* out, const u32* in, i32 drive, u32 bias, i32 scale) {
    const i32* const table = kSine22;
    for (i32* o = out; o != out + kOver; ++o) {
        const u32 at = (static_cast<u32>(static_cast<i32>(*in++) * drive) << 3) + bias;
        *o = sine22(table, at) * scale;
    }
}

// One path of the half-band: every second sample through two allpasses,
// y = a (x - y') + x', in place. The difference keeps its top 16 bits for
// the product, 84 dB under full scale.
__attribute__((noinline)) void allpass(i32* io, Path& path, i32 first, i32 second) {
    i32 x = path.x, a = path.a, b = path.b;
    for (u32 n = 0; n < opxy::block_frames; ++n) {
        i32* const p = io + 2 * n;
        const i32 in = *p;
        const i32 u = ((in - a) >> 14) * first + x;
        const i32 v = ((u - b) >> 14) * second + a;
        x = in;
        a = u;
        b = v;
        *p = v;
    }
    path.x = x;
    path.a = a;
    path.b = b;
}

// Back to the device's rate: the half of the two paths' sum, its offset
// removed by a follower (a one-pole at 14 Hz), Q28.
__attribute__((noinline)) i32 decimate(i32* out, const i32* in, i32 dc) {
    for (i32* o = out; o != out + opxy::block_frames; ++o, in += 2) {
        const i32 y = ((in[0] + in[1]) >> 1) - dc;
        dc += y >> 9;
        *o = y;
    }
    return dc;
}

// The gate's low-pass: two one-poles, their coefficient ramping across the block.
__attribute__((noinline)) void close(i32* io, i32 cut, i32 step, i32& low1, i32& low2) {
    i32 a = low1, b = low2;
    for (i32* x = io; x != io + opxy::block_frames; ++x) {
        cut += step;
        a += multiply(*x - a, cut);
        b += multiply(a - b, cut);
        *x = b;
    }
    low1 = a;
    low2 = b;
}

// The gate's fade and the output level, ramping across the block, within the declared peak.
__attribute__((noinline)) void fade(opxy::Output output, const i32* in, i32 level, i32 step) {
    for (i32* out = output; out != output + opxy::block_frames; ++out) {
        level += step;
        const i32 y = multiply(*in++ << 1, level);
        *out = y > kPeak ? kPeak : y < -kPeak ? -kPeak : y;
    }
}

// Drift's wander from a slow and a quick phase, -32767..32767.
inline i32 wander(u32 slow, u32 quick) {
    return (19661 * sine15(kSine15, slow) + 13107 * sine15(kSine15, quick)) >> 15;
}

inline u32 hash(u32 n) {
    n ^= n >> 16;
    n *= 0x7feb352du;
    n ^= n >> 15;
    n *= 0x846ca68bu;
    n ^= n >> 16;
    return n;
}
}

// Callbacks run serially per voice; owner is a stable nonzero voice ID.
inline void initialize(Voice& voice, Shared&, u32 owner) {
    voice.seed = hash(owner * 0x9e3779b9u + 0x632be5abu);
    // Free-running oscillators, as an analogue voice's: each voice starts at
    // its own phase, so a chord's voices never peak together.
    voice.carrier = hash(voice.seed + 1);
    voice.modulator = hash(voice.seed + 2);
    voice.even.x = voice.even.a = voice.even.b = 0;
    voice.odd.x = voice.odd.a = voice.odd.b = 0;
    voice.dc = voice.estimate = 0;
    voice.low1 = voice.low2 = 0;
    voice.cell = 0;
    voice.since = 0;
    voice.cut = voice.level = 0;
    for (u32 c = 0; c < 8; ++c) voice.controls[c] = 0;
    voice.ratio = 65536;
    voice.primed = 0;
    for (u32 j = 0; j < 4; ++j) voice.wander[j] = hash(voice.seed + 3 + j);
}

// A note strikes the vactrol; the photocell keeps what light it still holds.
inline void start(Voice& voice, Shared&, u32) { voice.since = 0; }
inline void destroy(Voice&, Shared&, u32) {}

// Controls: fold, fm, ratio, strike; glow, bias, mode, drift.
inline void render(Voice& voice, Shared&, u32, opxy::Output output, opxy::Parameters parameters, opxy::Pitch pitch) {
    // Controls glide over a few blocks, so turning one never steps the sound.
    for (u32 c = 0; c < 8; ++c) {
        const i32 target = control(parameters[c]);
        voice.controls[c] = voice.primed ? voice.controls[c] + ((target - voice.controls[c]) >> 2) : target;
    }
    const i32 fold_control = voice.controls[kFold], fm = voice.controls[kFm], strike = voice.controls[kStrike],
              glow = voice.controls[kGlow], bias = voice.controls[kBias], mode = voice.controls[kMode],
              drift = voice.controls[kDrift];
    // The ratio glides between its stops over a few blocks.
    const u32 stop = kRatioQ16[ratio_stop(control(parameters[kRatio]))];
    voice.ratio = voice.primed ? voice.ratio + static_cast<u32>((static_cast<i32>(stop) - static_cast<i32>(voice.ratio)) >> 2) : stop;
    const u32 ratio = voice.ratio;

    // Drift: the pitch wanders up to 25 cents, the modulator on its own as
    // far again (so FM's sidebands drift against the carrier, as two analogue
    // oscillators do), and the drive up to 25% either way.
    for (u32 j = 0; j < 4; ++j) voice.wander[j] += times(wander_rate(j < 2 ? voice.seed : voice.seed >> 16, j & 1), kBlockSeconds);
    u32 increment = pitch[0] < kLowestIncrement ? kLowestIncrement : pitch[0];
    u32 tuning = ratio;
    i32 drive = drive_q16(fold_control);
    if (drift) {
        const i32 reach = drift_cents_q16(drift) >> 6;
        const i32 cents = reach * wander(voice.wander[0], voice.wander[1]) >> 9;  // Q16
        increment = times(increment, exp2_q16((cents >> 6) * 3495 >> 16));      // cents to octaves
        const i32 apart = reach * wander(voice.wander[1] + 0x55555555u, voice.wander[2] + 0xaaaaaaaau) >> 9;
        tuning = times(tuning, exp2_q16((apart >> 6) * 3495 >> 16));
        drive += (drive >> 4) * (drift_fold_q15(drift) * wander(voice.wander[2], voice.wander[3]) >> 15) >> 11;
    }

    // The governor: the folded FM's bandwidth by Carson's rule,
    // f0 (1 + (I + 1) r)(A + 1), stays under 45 kHz; above it the drive and the index give way equally
    // (in octaves), and either takes the rest once the other is spent.
    i32 index = index_q16(fm);
    const i32 note = log2_q16(increment), spread = log2_q16(ratio) - (16 << 16);
    const i32 tone = note + spread;  // the modulator's frequency, as an increment
    if (tone > kModulatorFade) {
        const i32 left = kModulatorLimit - tone;
        index = left <= 0 ? 0 : static_cast<i32>(times(static_cast<u32>(index), static_cast<u32>(left) * 65536u / (kModulatorLimit - kModulatorFade)));
    }
    {
        const i32 fold_octaves = log2_q16(static_cast<u32>(drive) + 65536u) - (16 << 16);
        // Carson: the FM reaches f0 + (I + 1) r f0; a small index reaches the
        // first sideband, r f0 above, at I / 2, so the 1 grows in with 2 I.
        const u32 reach = static_cast<u32>(index) + (index < 32768 ? 2u * static_cast<u32>(index) : 65536u);
        const i32 fm_octaves = log2_q16(times(reach, ratio) + 65536u) - (16 << 16);
        i32 excess = note + fold_octaves + fm_octaves - kBandLog2;
        if (excess > 0) {
            i32 cut_fold = excess >> 1, cut_fm = excess - cut_fold;
            if (fold_octaves - cut_fold < kLeastFold) { cut_fm += cut_fold - (fold_octaves - kLeastFold); cut_fold = fold_octaves - kLeastFold; }
            if (fm_octaves - cut_fm < 0) { cut_fold += cut_fm - fm_octaves; cut_fm = fm_octaves; }
            if (fold_octaves - cut_fold < kLeastFold) cut_fold = fold_octaves - kLeastFold;
            if (cut_fold > 0) drive = static_cast<i32>(exp2_q16(fold_octaves - cut_fold)) - 65536;
            if (cut_fm > 0) {
                // Back from the reach (I + min(1, 2 I)) r to the index.
                const u32 swing = ((exp2_q16(fm_octaves - cut_fm) - 65536u) << 8) / (ratio >> 8);
                index = static_cast<i32>(swing < 98304u ? swing / 3u : swing - 65536u);
            }
            if (drive < 1311) drive = 1311;
        }
    }

    // The folder's plan (laws.h): its multiplier, offset and gain.
    const Fold plan = fold_plan(drive, bias);
    // Its offset, predicted: sin(c) J0(A) for a sine at its input. The
    // follower takes what the prediction misses (FM moves the mean a little).
    u32 at = static_cast<u32>(drive) >> 14, part = static_cast<u32>(drive) & 16383u;
    if (at > 31) { at = 31; part = 16383; }
    const i32 bessel = kBessel[at] + ((kBessel[at + 1] - kBessel[at]) * static_cast<i32>(part) >> 14);
    const i32 estimate = ((sine22(kSine22, plan.offset) >> 8) * bessel >> 7) * plan.gain;  // Q28
    voice.dc += estimate - voice.estimate;
    voice.estimate = estimate;

    // The vactrol, then what it sets: the low-pass's coefficient (1.25 of the
    // light, so it is open from 0.8 up and near 9 kHz times the light below)
    // and the level, each following the light as far as the mode lets it.
    voice.cell = gate_step(voice.cell, voice.since, kBlockSeconds, kBlockLog2, vactrol(strike, glow));
    if (voice.since < kPulse) voice.since += kBlockSeconds;
    const Mix mix = mode_mix(mode);
    const i32 lit_filter = lit(voice.cell, mix.filter), lit_level = lit(voice.cell, mix.fade);  // Q30
    i32 cut = lit_filter >= 858993459 ? 0x7fffffff : lit_filter * 2 + (lit_filter >> 1);
    if (cut < kCutFloor) cut = kCutFloor;
    // A strike's ping is brief, so a harder strike also sounds up to 1.6 dB
    // louder: what a struck note gives up in length it keeps in weight.
    const i32 level = multiply(lit_level, kLevel + (kLevel >> 15) * (strike * 26 >> 7)) << 1;
    if (!voice.primed) {
        voice.cut = cut;
        voice.level = 0;
    }
    voice.primed = 1;

    // The oscillators and the folder at twice the rate, then back down.
    i32 work[kOver];
    u32 scratch[kOver];
    const u32 half = increment >> 1;
    voice.modulator = modulate(scratch, voice.modulator, times(half, tuning), static_cast<i32>(times(static_cast<u32>(index), 5215u)));
    voice.carrier = carry(scratch, voice.carrier, half);
    fold(work, scratch, plan.turns, plan.offset, plan.gain);
    allpass(work, voice.even, kAllpass1, kAllpass3);
    allpass(work + 1, voice.odd, kAllpass0, kAllpass2);
    i32* const low = reinterpret_cast<i32*>(scratch);
    voice.dc = decimate(low, work, voice.dc);
    close(low, voice.cut, (cut - voice.cut) >> 6, voice.low1, voice.low2);
    fade(output, low, voice.level, (level - voice.level) >> 6);
    voice.cut = cut;
    voice.level = level;
}
}
