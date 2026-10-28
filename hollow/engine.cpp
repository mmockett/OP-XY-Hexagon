#include "core.h"
#include "sine.h"
#include <opxy/q31.hpp>

// Hollow: a resonator choir. A breath (noise) and a soft bow (a pulse train at
// the note's pitch, whose harmonics fall away as Bright closes) drive eight
// tuned resonant modes per voice, whose body morphs from a sung vowel through
// a string and a stopped pipe to glass. Every held note registers its pitch
// in Shared, and each voice keeps quiet modes at other held notes' pitches,
// so a chord's notes ring into one another.
//
// Each mode is a damped resonator solved in phasor form. Its complex amplitude
// is updated once a block exactly as a resonator's envelope evolves when
// driven near its own frequency: it decays by the mode's ring, rises toward
// the bow's steady push and is jostled by the breath's noise, so a long ring
// turns breath into slowly drifting, pure tones and a short one into noisy
// bands. A sine oscillator then sounds that amplitude at the mode's frequency,
// ramped across the block. The per-sample work is a table read and two 16x16
// multiplies a mode; every exponential, damping and gain is planned per block;
// and since the per-block decay factor is below 1 at every setting, no mode
// can grow at any Ring, which a recursive high-Q filter in fixed point cannot
// promise as cheaply.
namespace hollow {
namespace {
using opxy::q31::multiply;
const u32 kLowestIncrement = 779000u;  // about 8 Hz
const i32 kHalf = 1 << 30;
const i32 kAmplitudeLimit = 4095 << 16;  // a mode's largest amplitude, so twelve modes sum within range

inline i32 clamp_amplitude(i32 x) {
    return x > kAmplitudeLimit ? kAmplitudeLimit : x < -kAmplitudeLimit ? -kAmplitudeLimit : x;
}

// Uniform noise, -32768..32767.
inline i32 noise15(u32& seed) {
    seed = seed * 1664525u + 1013904223u;
    return static_cast<i32>(seed >> 16) - 32768;
}

// How a mode moves this block, planned from its drive and decay.
struct Push {
    i32 decay;  // Q31: the amplitude kept from block to block
    i32 bow;    // the steady amplitude the bow drives it to (amplitude << 16)
    i32 noise;  // the breath's push per block, amplitude units (0..4095)
};

// The plan for a mode: its per-block loss x = 2^log2_loss (from Ring and its
// fade), the bow's steady amplitude and the breath's stationary amplitude (both
// 0..4095). Breath: a one-pole on uniform noise keeps a stationary amplitude
// of push * 0.577 / sqrt(1 - decay^2), so the push is the target amplitude
// times sqrt(2x) / 0.577, near enough for every Ring.
inline Push plan(i32 log2_loss, i32 bow, i32 breath) {
    Push p;
    const u32 loss = exp2_q16(log2_loss);                              // Q16
    const u32 e = loss > 0x7fffffu ? 0x7fffffu : loss;                 // under 128
    const i32 keep = 15 * 65536 - static_cast<i32>(times(e, 94548u));  // log2(decay * 2^31): decay = e^-x
    const u32 decay = keep < -16 * 65536 ? 0 : exp2_q16(keep);
    p.decay = decay > 0x7fffffffu ? 0x7fffffff : static_cast<i32>(decay);
    p.bow = bow << 16;
    i32 root = (log2_loss + 65536) >> 1;  // log2 sqrt(2x)
    if (root > 0) root = 0;
    p.noise = static_cast<i32>(times(static_cast<u32>(breath), exp2_q16(root + 50352)));  // / 0.577: +0.768 octave
    return p;
}

// Sounds a mode into acc over a block, its amplitudes ramping from (a, b) by
// (da, db) a sample, and returns its phase. Kept lean for the Blackfin: one
// table pointer for both reads, and amplitudes within +-4095 (clamp_amplitude),
// so their top halves multiply as they are.
inline u32 sound(i32* acc, u32 phase, u32 increment, i32 a, i32 b, i32 da, i32 db) {
    for (i32* out = acc; out != acc + opxy::block_frames; ++out) {
        a += da;
        b += db;
        const opxy::i16* const at = kSine + (phase >> 22);
        *out += at[0] * (a >> 16) + at[256] * (b >> 16);
        phase += increment;
    }
    return phase;
}

// One block of a mode: update its amplitude, then sound it, ramped, into acc.
inline void ring(Mode& m, const Push& p, u32 increment, u32& seed, i32* acc) {
    const i32 a0 = m.a, b0 = m.b;
    const i32 a1 = clamp_amplitude(multiply(a0 - p.bow, p.decay) + p.bow + noise15(seed) * p.noise * 2);
    const i32 b1 = clamp_amplitude(multiply(b0, p.decay) + noise15(seed) * p.noise * 2);
    m.phase = sound(acc, m.phase, increment, a0, b0, (a1 - a0) >> 6, (b1 - b0) >> 6);
    m.a = a1;
    m.b = b1;
}

// A mode's last block: undriven and faded to silence, then cleared.
inline void release(Mode& m, u32 increment, i32* acc) {
    sound(acc, m.phase, increment, m.a, m.b, -(m.a >> 6), -(m.b >> 6));
    m.phase = 0;
    m.a = m.b = 0;
}

// Soft limit to half scale: unchanged below a quarter of full scale, then a
// quadratic knee, y = m - o^2 / 2^31 for o = m - 2^29, reaching half scale at
// three quarters. Branch-free: the comparisons are selects.
inline i32 soft_limit(i32 x) {
    const i32 knee = kHalf / 2;
    const i32 m = x < 0 ? -x : x;
    i32 over = m - knee;
    over = over < 0 ? 0 : over > kHalf ? kHalf : over;
    const i32 root = over >> 15;  // at most 32768
    i32 y = (m < knee ? m : knee) + over - (root * root >> 1);
    y = y > kHalf ? kHalf : y;  // the truncated root can overshoot by a few thousandths of a dB
    return x < 0 ? -y : y;
}

// ---------------------------------------------------------------------------
// The registry: one entry per sounding voice, keyed by owner, so destroy can
// always find and clear its own. Work is bounded by kNotes everywhere.
Note* claim(Shared& shared, Voice& voice, u32 owner) {
    if (voice.slot && voice.slot <= kNotes && shared.notes[voice.slot - 1].owner == owner)
        return &shared.notes[voice.slot - 1];
    const u32 now = shared.clock;
    u32 chosen = kNotes;
    for (u32 j = 0; j < kNotes && chosen == kNotes; ++j)
        if (shared.notes[j].owner == owner) chosen = j;
    for (u32 j = 0; j < kNotes && chosen == kNotes; ++j)
        if (!shared.notes[j].owner || now - shared.notes[j].stamp > kFresh) chosen = j;
    if (chosen == kNotes) {  // all fresh: take the least recently refreshed
        chosen = 0;
        for (u32 j = 1; j < kNotes; ++j)
            if (now - shared.notes[j].stamp > now - shared.notes[chosen].stamp) chosen = j;
    }
    Note& note = shared.notes[chosen];
    if (note.owner != owner) note.increment = 0;
    note.owner = owner;
    note.stamp = now;
    voice.slot = chosen + 1;
    return &note;
}

// Whether a registry entry is another note this voice should ring with.
bool sounding(const Shared& shared, const Note& note, u32 owner, u32 increment) {
    const u32 apart = note.increment > increment ? note.increment - increment : increment - note.increment;
    return note.owner && note.owner != owner && note.increment && shared.clock - note.stamp <= kFresh &&
           note.increment < kFadeIncrement && apart > (increment >> 6);  // not within a quarter tone
}

// Spread: which modes follow the flattened set (1) and which stay on the note.
// The two sets of partials drift against each other; the fundamental stays put.
const u32 kSpreadSet[kModes] = {0, 1, 0, 1, 1, 0, 1, 0};
// The bow's own hiss: a share of the breath even at Breath 0 (Q15).
const i32 kBowHiss = 2600;
// Amplitudes at full drive (0..4095): the bow's and the breath's; the
// sympathetic modes' level at full Sympathy (Q15 of the fundamental's); the
// output gain (Q10) and Air at full (Q15). The output and Air sit 3 dB down
// so eight notes keep headroom: a full chord peaks near 0.6 of full scale,
// and about 1.0 with Breath and Air at full.
const i32 kBowAmplitude = 3000, kBreathAmplitude = 750, kSympathy = 14000;
const i32 kOutput = 4525, kAir = 23170;
// log2 (Q16) of the modes' steady power the output gain holds.
const i32 kPower = 1343488;  // 2^20.5: the voice at its defaults
}

// Callbacks run serially per voice; owner is a stable nonzero voice ID.
inline void initialize(Voice& voice, Shared&, u32 owner) {
    for (u32 k = 0; k < kModes; ++k) {
        voice.modes[k].phase = k * 0x9e3779b9u;  // spread phases keep the bow's peaks low
        voice.modes[k].a = voice.modes[k].b = 0;
    }
    for (u32 j = 0; j < kEchoes; ++j) {
        voice.echoes[j].owner = voice.echoes[j].increment = 0;
        voice.echoes[j].mode.phase = 0;
        voice.echoes[j].mode.a = voice.echoes[j].mode.b = 0;
    }
    voice.seed = owner * 2654435761u | 1u;
    voice.hiss = voice.slow = 0;
    for (u32 c = 0; c < 8; ++c) voice.controls[c] = 0;
    voice.primed = 0;
    voice.slot = 0;
}

// A note claims its registry entry at once; its pitch arrives with its first block.
inline void start(Voice& voice, Shared& shared, u32 owner) { claim(shared, voice, owner); }

// Releases everything this owner registered: no other voice rings at its pitch
// after this, from the next block on.
inline void destroy(Voice& voice, Shared& shared, u32 owner) {
    for (u32 j = 0; j < kNotes; ++j)
        if (shared.notes[j].owner == owner) shared.notes[j].owner = shared.notes[j].increment = shared.notes[j].stamp = 0;
    voice.slot = 0;
}

// Controls: Breath, Body, Bright, Ring; Air, Vowel, Spread, Sympathy.
inline void render(Voice& voice, Shared& shared, u32 owner, opxy::Output output,
                   opxy::Parameters parameters, opxy::Pitch pitch) {
    // Controls glide over a few blocks, so Body and Vowel morph rather than step.
    for (u32 c = 0; c < 8; ++c) {
        const i32 target = control(parameters[c]);
        voice.controls[c] = voice.primed ? voice.controls[c] + ((target - voice.controls[c]) >> 2) : target;
    }
    voice.primed = 1;
    const i32 breath = voice.controls[0], body = voice.controls[1], bright = voice.controls[2],
              ring_control = voice.controls[3], air = voice.controls[4], vowel = voice.controls[5],
              spread = voice.controls[6], sympathy = voice.controls[7];
    const u32 increment = pitch[0] < kLowestIncrement ? kLowestIncrement : pitch[0];
    const i32 octave = log2_q16(increment);

    // This note's pitch, for the other voices.
    ++shared.clock;
    Note* note = claim(shared, voice, owner);
    note->increment = increment;
    note->stamp = shared.clock;

    // Ring: the fundamental's per-block amplitude loss is 6.9078 * 64 / (T60 fs),
    // 2^(-6.640 - log2 T60).
    const i32 log2_loss = -435177 - ring_log2_seconds(ring_control, body);
    // Bright: the bow's harmonics fall from 9 dB an octave (closed) to 2 (open),
    // the bow 3 dB softer when open; the breath's low-pass opens from 2 to 128
    // times the note.
    const i32 slope = 98304 - (bright * 2381 >> 10);  // Q16, amplitude octaves per octave: 1.5 to 0.31
    const i32 cutoff = 65536 + bright * 12;            // Q16 octaves above the note
    const i32 open = static_cast<i32>(times(static_cast<u32>(kBowAmplitude), exp2_q16(-bright)));  // -0.5 octave at full
    // Breath: the bow's share and the breath's (Q15), crossing at nearly equal
    // power: 1 - b^2 and 1 - (1 - b)^2.
    const i32 rest = 32767 - breath;
    const i32 bowed = 32767 - (breath * breath >> 15), puff = 32767 - (rest * rest >> 15);
    const i32 blown = puff + ((32767 - puff) * kBowHiss >> 15);
    // Spread: the flattened set's ratio, up to 15 cents flat (565 in Q16).
    const u32 flat = static_cast<u32>(65536 - (spread * 565 >> 15));

    i32 acc[opxy::block_frames];
    for (unsigned i = 0; i < opxy::block_frames; ++i) acc[i] = 0;
    u32 seed = voice.seed;

    // The body's modes.
    const Traits traits = body_traits(body);
    i32 fundamental = 0;  // the first mode's level, for the sympathetic modes
    u32 power = 0;        // the modes' steady power, roughly: bow^2 + 4 breath^2
    for (u32 k = 0; k < kModes; ++k) {
        const Shape shape = body_mode(body, k);
        const u32 at = times(increment, kSpreadSet[k] ? times(shape.ratio, flat) : shape.ratio);
        const i32 level = mode_level(shape, traits, vowel, at, octave + shape.log2_ratio);
        if (!k) fundamental = level;
        Mode& m = voice.modes[k];
        if (!level) {  // above the top: fade out whatever still rings, then rest
            if (m.a || m.b) release(m, at < kTopIncrement ? at : kTopIncrement, acc);
            continue;
        }
        // The bow's push falls by Bright's slope per octave of the harmonic;
        // the breath's falls an octave per octave above the cutoff, a one-pole's slope.
        const u32 bow_roll = exp2_q16(-((slope >> 4) * (shape.log2_ratio >> 4) >> 8));
        const i32 over = shape.log2_ratio - cutoff;
        const u32 breath_roll = exp2_q16(over > 0 ? -over : 0);
        const i32 drive = level * open >> 15;  // 0..4095 at full level
        const i32 hush = level * kBreathAmplitude >> 15;
        const i32 bow = static_cast<i32>(times(static_cast<u32>(drive * bowed >> 15), bow_roll));
        const i32 wind = static_cast<i32>(times(static_cast<u32>(hush * blown >> 15), breath_roll));
        power += static_cast<u32>(bow * bow) + 4u * static_cast<u32>(wind * wind);
        ring(m, plan(log2_loss + shape.log2_fade, bow, wind), at, seed, acc);
    }

    // Sympathy: quiet modes at other held notes' pitches, ringing as long as
    // this note's fundamental. The breath drives each; the bow drives one as
    // far as one of this note's harmonics falls within its resonance (an
    // octave or a twelfth above rings strongly; a third, hardly). Bound echoes
    // keep their note while it sounds; a note that has gone fades out over this
    // block and frees its echo.
    const i32 echo_level = (sympathy * sympathy >> 15) * kSympathy >> 15;  // Q15 of the fundamental's level
    for (u32 e = 0; e < kEchoes; ++e) {
        Echo& echo = voice.echoes[e];
        if (!echo.owner) continue;
        bool live = false;
        for (u32 j = 0; j < kNotes; ++j)
            if (shared.notes[j].owner == echo.owner) {
                live = echo_level > 0 && sounding(shared, shared.notes[j], owner, increment);
                if (live) echo.increment = shared.notes[j].increment;
            }
        if (!live) {
            release(echo.mode, echo.increment, acc);
            echo.owner = echo.increment = 0;
        }
    }
    if (echo_level > 0)
        for (u32 j = 0; j < kNotes; ++j) {
            const Note& other = shared.notes[j];
            if (!sounding(shared, other, owner, increment)) continue;
            u32 bound = kEchoes, free = kEchoes;
            for (u32 e = 0; e < kEchoes; ++e) {
                if (voice.echoes[e].owner == other.owner) bound = e;
                if (!voice.echoes[e].owner && free == kEchoes) free = e;
            }
            if (bound == kEchoes && free < kEchoes) {
                bound = free;
                voice.echoes[bound].owner = other.owner;
                voice.echoes[bound].increment = other.increment;
                voice.echoes[bound].mode.phase = 0;
                voice.echoes[bound].mode.a = voice.echoes[bound].mode.b = 0;
            }
            if (bound == kEchoes) continue;  // every echo is busy with an earlier note
            Echo& echo = voice.echoes[bound];
            const i32 level = fundamental * echo_level >> 15;
            const i32 over = log2_q16(other.increment) - octave - cutoff;
            const u32 breath_roll = exp2_q16(over > 0 ? -over : 0);
            const i32 wind = static_cast<i32>(times(static_cast<u32>((level * kBreathAmplitude >> 15) * blown >> 15), breath_roll));
            // The nearest harmonic of this note, and how far the echo sits
            // from it in bandwidths (a resonance's half-width in increments is
            // its per-sample loss times 2^32 / 2 pi: 163 times the per-block loss in Q16).
            i32 bow = 0;
            const u32 harmonic = (other.increment + (increment >> 1)) / increment;
            if (harmonic >= 1 && harmonic <= 16) {
                const u32 near = increment * harmonic;
                const u32 apart = other.increment > near ? other.increment - near : near - other.increment;
                const u32 width = exp2_q16(log2_loss) * 163u + 1u;
                if (apart < 64u * width) {
                    const u32 bandwidths = (apart << 4) / ((width >> 4) | 1u);  // Q8
                    const u32 reach = (256u << 16) / (256u + bandwidths);       // Q16: 1 / (1 + distance)
                    const i32 rise = log2_q16(harmonic << 16) - (16 << 16);
                    const u32 roll = exp2_q16(-((slope >> 4) * (rise >> 4) >> 8));
                    bow = static_cast<i32>(times(times(static_cast<u32>((level * open >> 15) * bowed >> 15), roll), reach));
                }
            }
            ring(echo.mode, plan(log2_loss, bow, wind), other.increment, seed, acc);
        }

    // Out: the modes at an output gain that holds the body's steady power (so
    // every body, vowel, Bright and note sounds about as loud, within -6 dB to
    // +12 dB), then Air, then the soft limit. The sympathetic modes ride on top.
    i32 even = (kPower - log2_q16(power | 1u)) >> 1;  // log2 of the gain that brings power to kPower
    even = even < -65536 ? -65536 : even > 131072 ? 131072 : even;
    u32 gain = times(static_cast<u32>(kOutput), exp2_q16(even));  // Q10
    if (gain > 32767u) gain = 32767u;
    const opxy::i16 gain16 = static_cast<opxy::i16>(gain);
    const i32 ceiling = 0x4fffffff >> 6;  // with Air, within 1.75 * 2^30
    const opxy::i16 air_gain = static_cast<opxy::i16>((air * air >> 15) * kAir >> 15);
    i32 hiss = voice.hiss, slow = voice.slow;
    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        // acc times the Q10 gain, as two 16x16 products: its top half, and its
        // low half less its last bit.
        i32 high = static_cast<opxy::i16>(acc[i] >> 16) * gain16;
        high = high > ceiling ? ceiling : high < -ceiling ? -ceiling : high;
        i32 y = high * 64 + (static_cast<opxy::i16>((acc[i] & 0xffff) >> 1) * gain16 >> 9);
        if (air_gain) {
            seed = seed * 1664525u + 1013904223u;
            hiss += ((static_cast<i32>(seed) >> 2) - hiss) >> 1;  // about 5 kHz
            slow += (hiss - slow) >> 4;                           // about 450 Hz
            i32 band = (hiss - slow) >> 15;
            band = band > 32767 ? 32767 : band < -32767 ? -32767 : band;
            const i32 breathy = static_cast<opxy::i16>(band) * air_gain;
            y += breathy > kHalf / 2 ? kHalf / 2 : breathy < -kHalf / 2 ? -kHalf / 2 : breathy;
        }
        output[i] = soft_limit(y);
    }
    voice.hiss = hiss;
    voice.slow = slow;
    voice.seed = seed;
}
}
