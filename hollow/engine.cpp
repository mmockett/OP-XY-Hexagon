#include "core.h"
#include "sine.h"
#include <opxy/q15.hpp>
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
//
// Spread is an ensemble built on the same phasors: each mode also sounds as a
// sharp and a flat copy, detuned by up to 30 cents and swaying, as three
// players of a section would. A copy differs from its mode only by a phase
// that runs ahead or behind at its detune, so the three together are the
// mode's amplitude turned and scaled once a block by centre + side (e^iu +
// e^id): a true unison's beating and drift, for a few multiplies a block and
// nothing more per sample.
namespace hollow {
namespace {
using opxy::q31::multiply;
const u32 kLowestIncrement = 779000u;  // about 8 Hz
const i32 kHalf = 1 << 30;
const i32 kAmplitudeLimit = 4095 << 16;  // a mode's largest amplitude, so twelve modes sum within range

// 16 x 16 multiplies of chosen halves: written as 16-bit values, so GCC
// multiplies them in one Blackfin instruction rather than 32 x 32.
inline opxy::i16 low_half(u32 x) { return static_cast<opxy::i16>(x); }
inline opxy::i16 high_half(u32 x) { return static_cast<opxy::i16>(x >> 16); }
inline i32 low_by_high(u32 x, i32 y) { return low_half(x) * high_half(static_cast<u32>(y)); }
inline i32 high_by_high(u32 x, i32 y) { return high_half(x) * high_half(static_cast<u32>(y)); }
inline i32 low_by_low(i32 x, i32 y) { return low_half(static_cast<u32>(x)) * low_half(static_cast<u32>(y)); }

inline i32 clamp_amplitude(i32 x) {
    return x > kAmplitudeLimit ? kAmplitudeLimit : x < -kAmplitudeLimit ? -kAmplitudeLimit : x;
}

// Uniform noise, -32768..32767.
inline i32 noise15(u32& seed) {
    seed = seed * 1664525u + 1013904223u;
    return static_cast<i32>(seed >> 16) - 32768;
}

// modes.h's exp2_q16, the same table and arithmetic, its step (at most
// 5,557 x 4,095) multiplied 16 x 16.
inline u32 power_of_two(i32 x) {
    const i32 whole = x >> 16;
    const u32 fraction = static_cast<u32>(x) & 0xffffu, at = fraction >> 12, rest = fraction & 0xfffu;
    const u32 m = exp2_table[at] + (static_cast<u32>(low_by_low(static_cast<i32>(exp2_table[at + 1] - exp2_table[at]), static_cast<i32>(rest))) >> 12);
    if (whole >= 15) return m << 15;
    if (whole >= 0) return m << whole;
    return whole <= -31 ? 0 : m >> -whole;
}
// times(a, b) for a below 2^16, as it is then: two multiplies, not four.
inline u32 times_small(u32 a, u32 b) { return a * (b >> 16) + (a * (b & 0xffffu) >> 16); }

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
    const u32 loss = power_of_two(log2_loss);                              // Q16
    const u32 e = loss > 0x7fffffu ? 0x7fffffu : loss;                 // under 128
    const i32 keep = 15 * 65536 - static_cast<i32>(times(e, 94548u));  // log2(decay * 2^31): decay = e^-x
    const u32 decay = keep < -16 * 65536 ? 0 : power_of_two(keep);
    p.decay = decay > 0x7fffffffu ? 0x7fffffff : static_cast<i32>(decay);
    p.bow = bow << 16;
    i32 root = (log2_loss + 65536) >> 1;  // log2 sqrt(2x)
    if (root > 0) root = 0;
    p.noise = static_cast<i32>(times_small(static_cast<u32>(breath), power_of_two(root + 50352)));  // / 0.577: +0.768 octave
    return p;
}

// Sounds a mode into acc over a block, at half rate, its amplitudes ramping
// from (a, b) by (da, db) a sample, and returns its phase. Kept lean for the
// Blackfin: the table address worked out a sample ahead, so it is ready when
// read, and amplitudes within +-4095 (clamp_amplitude), so their top halves
// multiply as they are, 16 x 16. Out of line, so the loop has the registers
// to itself, and summed with q31::add, which GCC keeps in data registers (a
// plain sum went through address registers, stalling each sample). It
// saturates only where the plain sum would wrap, which no test or preset
// reaches.
__attribute__((noinline)) u32 sound(i32* acc, u32 phase, u32 increment, i32 a, i32 b, i32 da, i32 db) {
    const opxy::i16* at = kSine + (phase >> 22);
    for (unsigned i = 0; i < kHalfFrames; ++i) {
        const i32 sine = at[0], cosine = at[256];
        phase += increment;
        at = kSine + (phase >> 22);
        a += da;
        b += db;
        acc[i] = opxy::q31::add(acc[i], sine * high_half(static_cast<u32>(a)) + cosine * high_half(static_cast<u32>(b)));
    }
    return phase;
}
__attribute__((noinline)) u32 sound_first(i32* acc, u32 phase, u32 increment, i32 a, i32 b, i32 da, i32 db) {
    const opxy::i16* at = kSine + (phase >> 22);
    for (unsigned i = 0; i < kHalfFrames; ++i) {
        const i32 sine = at[0], cosine = at[256];
        phase += increment;
        at = kSine + (phase >> 22);
        a += da;
        b += db;
        acc[i] = sine * high_half(static_cast<u32>(a)) + cosine * high_half(static_cast<u32>(b));
    }
    return phase;
}
// acc is unwritten until the first mode sounds; that one writes it. The
// arguments are the full rate's (sample n reads the phase n steps on and
// sounds a + (n + 1) da); the half rate's sample m is the full rate's 2m:
// twice the steps, from a - da.
inline u32 sound_any(i32* acc, bool& written, u32 phase, u32 increment, i32 a, i32 b, i32 da, i32 db) {
    if (written) return sound(acc, phase, increment << 1, a - da, b - db, da * 2, db * 2);
    written = true;
    return sound_first(acc, phase, increment << 1, a - da, b - db, da * 2, db * 2);
}

// Spread's ensemble this block: the share each copy sounds at (Q15; centre
// plus both sides is at most 1, so the sum's amplitude stays inside the
// mode's) and the copies' detunes as ratios less one (Q19), sharp and flat.
struct Chorus { i32 centre, side; u32 up, down; };

// Planned once a block for the voice: the copies' detune sways with two slow
// and two quick sines of the voice's own clock, phases and rates.
inline Chorus ensemble(const Voice& voice, i32 spread) {
    Chorus c;
    c.side = spread_wet(spread) >> 1;
    c.centre = 32767 - 2 * c.side;
    c.up = c.down = 0;
    if (!c.side) return c;
    const i32 cents = spread_cents_q8(spread);
    i32 sway[4];
    for (u32 j = 0; j < 4; ++j) {
        const u32 rate = (kSwayRates[j] * 6087u) >> 6;                      // Q32 cycles a block (689 a second)
        const u32 own = rate + ((rate >> 3) * (voice.sway >> 24) >> 8);     // up to 12% faster for this voice
        sway[j] = kSine[(voice.age * own + voice.sway * (2 * j + 1)) >> 22];  // Q15
    }
    // Cents (Q8) to a ratio less one (Q19): ln 2 / 1200 * 2^11 = 1.1829.
    const i32 up = cents * (32768 + (kSwaySlow * sway[0] >> 15) + (kSwayQuick * sway[2] >> 15)) >> 15;
    const i32 down = cents * (32768 + (kSwaySlow * sway[1] >> 15) + (kSwayQuick * sway[3] >> 15)) >> 15;
    c.up = static_cast<u32>(up * 4845 >> 12);
    c.down = static_cast<u32>(down * 4845 >> 12);
    return c;
}

// How far a copy's phase moves from its mode's in a block: the mode's
// increment times 64 times the detune, at most an eighth of a cycle, so the
// block's linear ramp follows the copies' beating.
inline u32 drift(u32 increment, u32 detune) {
    const u32 d = (increment >> 13) * detune;
    return d > (1u << 29) ? 1u << 29 : d;
}

// One block of a mode: update its amplitude, then sound it, ramped, into acc:
// with Spread, as the sum of the mode and its two copies.
inline void ring(Mode& m, const Push& p, u32 increment, u32& seed, const Chorus& c, i32* acc, bool& written) {
    const i32 a1 = clamp_amplitude(multiply(m.a - p.bow, p.decay) + p.bow + noise15(seed) * p.noise * 2);
    const i32 b1 = clamp_amplitude(multiply(m.b, p.decay) + noise15(seed) * p.noise * 2);
    i32 sa = a1, sb = b1;
    if (c.side) {
        // The copies at phases up and down from the mode's: their sum is the
        // mode's amplitude turned and scaled by centre + side (e^iu + e^id).
        m.up += drift(increment, c.up);
        m.down -= drift(increment, c.down);
        const opxy::i16* const u = kSine + (m.up >> 22);
        const opxy::i16* const d = kSine + (m.down >> 22);
        const i32 real = c.centre + (c.side * (u[256] + d[256]) >> 15), imaginary = c.side * (u[0] + d[0]) >> 15;
        sa = (a1 >> 15) * real - (b1 >> 15) * imaginary;
        sb = (a1 >> 15) * imaginary + (b1 >> 15) * real;
    }
    m.phase = sound_any(acc, written, m.phase, increment, m.sa, m.sb, (sa - m.sa) >> 6, (sb - m.sb) >> 6);
    m.a = a1;
    m.b = b1;
    m.sa = sa;
    m.sb = sb;
}

// A mode's last block: undriven and faded to silence, then cleared.
inline void release(Mode& m, u32 increment, i32* acc, bool& written) {
    sound_any(acc, written, m.phase, increment, m.sa, m.sb, -(m.sa >> 6), -(m.sb >> 6));
    m.phase = m.up = m.down = 0;
    m.a = m.b = m.sa = m.sb = 0;
}

// The voice's level, 1.28 x (41/32, +2.15 dB), then a soft limit to half
// scale: unchanged below 0.32 of full scale (a quarter before the level), then
// a quadratic knee, y = m - o^2 / (4 (2^30 - knee)) for o = m - knee, reaching
// half scale at 0.68. Most samples sit below the knee and return at once.
inline i32 soft_limit(i32 x) {
    const i32 knee = 687865856, reach = 771751936, most = 1139213886;  // most x 41/32 = knee + reach
    x = x > most ? most : x < -most ? -most : x;
    x = x + (x >> 2) + (x >> 5);
    const i32 m = x < 0 ? -x : x;
    if (m <= knee) return x;
    const i32 over = m - knee < reach ? m - knee : reach;
    const i32 root = over >> 15;                 // at most 23552, so it squares 16 x 16
    const i32 t = low_by_low(root, root);        // o^2 / 2^30; times 0.6953 (2^30 / (4 (2^30 - knee)) is 0.6957)
    i32 y = knee + over - ((t >> 1) + (t >> 3) + (t >> 4) + (t >> 7));
    y = y > kHalf ? kHalf : y;  // the truncated square can overshoot by a few thousandths of a dB
    return x < 0 ? -y : y;
}

// The voice's output passes, each out of line so its loop has the registers
// to itself.
// acc times the Q10 gain, ramping by step a sample from level (gain << 16),
// as two 16x16 products: its top half, and its low half less its last bit.
// At half rate, so step is the full rate's two.
__attribute__((noinline)) void apply_gain(i32* acc, i32 level, i32 step) {
    const i32 ceiling = 0x4fffffff >> 6;  // with Air, within 1.75 * 2^30
    for (unsigned i = 0; i < kHalfFrames; ++i) {
        level += step;
        i32 high = high_by_high(static_cast<u32>(acc[i]), level);
        high = high > ceiling ? ceiling : high < -ceiling ? -ceiling : high;
        // The low half's operand fits 15 bits, so GCC would multiply it 32 x
        // 32; opxy::q15::product is the same product, 16 x 16.
        acc[i] = high * 64 + (opxy::q15::product(static_cast<i32>(static_cast<u32>(acc[i]) << 16 >> 17), opxy::q15::top(level)) >> 9);
    }
}
// The half-band filter's odd-phase coefficients, Q15: an odd full-rate sample
// is the sum of c_t times the pair of half-rate samples t + 1/2 either side
// (19 taps at the full rate, every other one zero). Equiripple, rounded and
// searched as integers: flat within 0.003 dB to 6.6 kHz, above every mode,
// and the half-rate images (above 15.45 kHz) 70 dB down.
const opxy::i16 kHalfBand[kHalfTaps] = {20301, -5421, 2036, -664, 142};
// Two taps' share of every odd sample, t and t + 1, added to sum (written,
// for the first). x: each odd sample's left neighbour, the output's top 16
// bits. Each product is 16 x 16 on its own, so no operand outgrows 16 bits.
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
// next, from the top 16 bits rounded (the output stays within 1.75 * 2^30).
__attribute__((noinline)) void upsample(const i32* low, i32* out) {
    opxy::i16 top[kHalfHistory + kHalfFrames];
    for (u32 i = 0; i < kHalfHistory + kHalfFrames; ++i) top[i] = static_cast<opxy::i16>((low[i] + 32768) >> 16);
    const opxy::i16* const x = top + kHalfTaps - 1;
    i32 odd[kHalfFrames];
    taps<0>(x, odd);
    taps<2>(x, odd);
    const opxy::i16 c4 = kHalfBand[4];
    for (i32 j = 0; j < static_cast<i32>(kHalfFrames); ++j) {
        out[2 * j] = low[j + kHalfTaps - 1];
        out[2 * j + 1] = (odd[j] + x[j - 4] * c4 + x[j + 5] * c4) * 2;
    }
}
// Air: breath noise, a band from about 450 Hz to 5 kHz, at air_gain (Q15).
__attribute__((noinline)) void add_air(i32* acc, u32& seed_state, i32& hiss_state, i32& slow_state, i32 air_gain) {
    u32 seed = seed_state;
    i32 hiss = hiss_state, slow = slow_state;
    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        seed = seed * 1664525u + 1013904223u;
        hiss += ((static_cast<i32>(seed) >> 2) - hiss) >> 1;  // about 5 kHz
        slow += (hiss - slow) >> 4;                           // about 450 Hz
        i32 band = (hiss - slow) >> 15;
        band = band > 32767 ? 32767 : band < -32767 ? -32767 : band;
        const i32 breathy = opxy::q15::product(band, air_gain);   // 16 x 16: band is clamped to 16 bits
        acc[i] = opxy::q31::add(acc[i], breathy > kHalf / 2 ? kHalf / 2 : breathy < -kHalf / 2 ? -kHalf / 2 : breathy);
    }
    seed_state = seed;
    hiss_state = hiss;
    slow_state = slow;
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

// The bow's own hiss: a share of the breath even at Breath 0 (Q15).
const i32 kBowHiss = 2600;
// Amplitudes at full drive (0..4095): the bow's and the breath's; the
// sympathetic modes' level at full Sympathy (Q15 of the fundamental's); the
// output gain (Q10) and Air at full (Q15). The output and Air sit 3 dB down
// so eight notes keep headroom: a full chord peaks near 0.6 of full scale,
// and a little under 1.0 with Breath, Air, Spread and Sympathy all at full.
const i32 kBowAmplitude = 3000, kBreathAmplitude = 750, kSympathy = 24000;
const i32 kOutput = 4525, kAir = 23170;
// log2 (Q16) of the modes' steady power the output gain holds.
const i32 kPower = 1343488;  // 2^20.5: the voice at its defaults
}

// Callbacks run serially per voice; owner is a stable nonzero voice ID.
inline void clear(Mode& m, u32 phase) {
    m.phase = phase;
    m.a = m.b = m.sa = m.sb = 0;
    m.up = m.down = 0;
}

inline void initialize(Voice& voice, Shared&, u32 owner) {
    for (u32 k = 0; k < kModes; ++k) clear(voice.modes[k], k * 0x9e3779b9u);  // spread phases keep the bow's peaks low
    for (u32 j = 0; j < kEchoes; ++j) {
        voice.echoes[j].owner = voice.echoes[j].increment = 0;
        voice.echoes[j].swell = 0;
        clear(voice.echoes[j].mode, 0);
    }
    voice.seed = owner * 2654435761u | 1u;
    voice.hiss = voice.slow = 0;
    for (u32 c = 0; c < 8; ++c) voice.controls[c] = 0;
    voice.primed = 0;
    voice.slot = 0;
    voice.gain = 0;
    voice.age = 0;
    voice.sway = (owner * 0x85ebca6bu) ^ (owner >> 7) * 0xc2b2ae35u;
    for (u32 i = 0; i < kHalfHistory; ++i) voice.low[i] = 0;  // the filter starts silent
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
    // A note's first block only plans its modes, in silence: the plan is the
    // heaviest work a note does, and with it done here no block both plans and
    // sounds. The note sounds, and other voices hear its pitch, from its
    // second block: everything as before, 64 samples (1.45 ms) later.
    const bool preparing = !voice.primed;
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

    // This note's pitch, for the other voices, once it sounds.
    if (!preparing) {
        ++shared.clock;
        Note* note = claim(shared, voice, owner);
        note->increment = increment;
        note->stamp = shared.clock;
    }

    // Ring: the fundamental's per-block amplitude loss is 6.9078 * 64 / (T60 fs),
    // 2^(-6.640 - log2 T60).
    const i32 log2_loss = -435177 - ring_log2_seconds(ring_control, body);
    // Bright: the bow's harmonics fall from 12 dB an octave (closed) through
    // flat to rising 1.5 dB an octave (open), the bow 3 dB softer when open;
    // the breath's low-pass opens from the note to 256 times it.
    const i32 slope = 131072 - (bright * 9 >> 1);     // Q16, amplitude octaves per octave: 2 to -0.25
    const i32 cutoff = bright * 16;                    // Q16 octaves above the note
    const i32 open = static_cast<i32>(times(static_cast<u32>(kBowAmplitude), power_of_two(-bright)));  // -0.5 octave at full
    // Breath: the bow's share and the breath's (Q15), crossing at nearly equal
    // power: 1 - b^2 and 1 - (1 - b)^2. It also widens the body's resonances.
    const i32 rest = 32767 - breath;
    const i32 bowed = 32767 - (breath * breath >> 15), puff = 32767 - (rest * rest >> 15);
    const i32 blown = puff + ((32767 - puff) * kBowHiss >> 15);
    const i32 widen = breath_widen_log2(breath);
    // The body's modes: plans remade only when what they depend on moves.
    i32 fundamental;      // the first mode's level, for the sympathetic modes
    u32 power;            // the modes' steady power, roughly: bow^2 + 4 breath^2, echoes included
    const i32 key[6] = {breath, body, bright, ring_control, vowel, static_cast<i32>(increment)};
    bool same = voice.planned_valid != 0;
    for (u32 j = 0; j < 6 && same; ++j) same = voice.planned_for[j] == key[j];
    if (!same) {
        const Traits traits = body_traits(body);
        fundamental = 0;
        power = 0;
        for (u32 k = 0; k < kModes; ++k) {
            Planned& q = voice.planned[k];
            const Shape shape = body_mode(body, k);
            const u32 at = times(increment, shape.ratio);
            const i32 level = mode_level(shape, traits, vowel, at, octave + shape.log2_ratio);
            if (!k) fundamental = level;
            q.at = at;
            q.level = level;
            if (!level) continue;
            const u32 bow_roll = power_of_two(-((slope >> 4) * (shape.log2_ratio >> 4) >> 8));
            const i32 over = shape.log2_ratio - cutoff;
            const u32 breath_roll = power_of_two(over > 0 ? -over : 0);
            const i32 drive = level * open >> 15;  // 0..4095 at full level
            const i32 hush = level * kBreathAmplitude >> 15;
            const i32 bow = static_cast<i32>(times_small(static_cast<u32>(drive * bowed >> 15), bow_roll));
            const i32 wind = static_cast<i32>(times_small(static_cast<u32>(hush * blown >> 15), breath_roll));
            power += static_cast<u32>(low_by_low(bow, bow)) + 4u * static_cast<u32>(low_by_low(wind, wind));
            const Push p = plan(log2_loss + shape.log2_fade + widen, bow, wind);
            q.decay = p.decay;
            q.bow = p.bow;
            q.noise = p.noise;
        }
        for (u32 j = 0; j < 6; ++j) voice.planned_for[j] = key[j];
        voice.planned_power = power;
        voice.planned_fundamental = fundamental;
        voice.planned_valid = 1;
    }
    if (preparing) {
        // (It no longer reads the sine table through here: measured on the
        // device, nothing a voice reads stays cached into its next block.)
        for (unsigned i = 0; i < opxy::block_frames; ++i) output[i] = 0;
        return;
    }
    fundamental = voice.planned_fundamental;
    power = voice.planned_power;
    // Spread: the ensemble's copies this block.
    ++voice.age;
    const Chorus chorus = ensemble(voice, spread);

    // The modes' half-rate sum, after the filter's history from the last block.
    i32 low[kHalfHistory + kHalfFrames];
    i32* const acc = low + kHalfHistory;
    bool written = false;
    u32 seed = voice.seed;
    for (u32 k = 0; k < kModes; ++k) {
        const Planned& q = voice.planned[k];
        Mode& m = voice.modes[k];
        if (!q.level) {  // above the top: fade out whatever still rings, then rest
            if (m.a || m.b) release(m, q.at < kTopIncrement ? q.at : kTopIncrement, acc, written);
            continue;
        }
        Push p;
        p.decay = q.decay;
        p.bow = q.bow;
        p.noise = q.noise;
        ring(m, p, q.at, seed, chorus, acc, written);
    }

    // Sympathy: modes at other held notes' pitches. The breath drives each; the
    // bow drives one as far as one of this note's harmonics falls within its
    // resonance (an octave or a twelfth above rings strongly; a third,
    // hardly). As Sympathy rises they grow louder, the breath drives them
    // harder and they ring longer than this note's fundamental (up to 2.8
    // times), so they bloom in slowly. Bound echoes keep their note while it
    // sounds; a note that has gone fades out over this block and frees its echo.
    const i32 coupling = sympathy * (11469 + (21299 * sympathy >> 15)) >> 15;  // Q15: 0.35 s + 0.65 s^2
    const i32 echo_level = coupling * kSympathy >> 15;  // Q15 of the fundamental's level
    const i32 echo_loss = log2_loss - coupling * 3;     // up to 1.5 octaves longer
    const i32 echo_blown = blown + (blown * coupling >> 15);  // up to twice the breath
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
            release(echo.mode, echo.increment, acc, written);
            echo.owner = echo.increment = 0;
            echo.swell = 0;
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
                voice.echoes[bound].swell = 0;
                clear(voice.echoes[bound].mode, 0);
            }
            if (bound == kEchoes) continue;  // every echo is busy with an earlier note
            Echo& echo = voice.echoes[bound];
            const i32 level = fundamental * echo_level >> 15;
            const i32 over = log2_q16(other.increment) - octave - cutoff;
            const u32 breath_roll = power_of_two(over > 0 ? -over : 0);
            const i32 wind = static_cast<i32>(times(static_cast<u32>((level * kBreathAmplitude >> 15) * echo_blown >> 15), breath_roll));
            // The nearest harmonic of this note, and how far the echo sits
            // from it in bandwidths (a resonance's half-width in increments is
            // its per-sample loss times 2^32 / 2 pi: 163 times the per-block loss in Q16).
            i32 bow = 0;
            const u32 harmonic = (other.increment + (increment >> 1)) / increment;
            if (harmonic >= 1 && harmonic <= 16) {
                const u32 near = increment * harmonic;
                const u32 apart = other.increment > near ? other.increment - near : near - other.increment;
                const u32 width = power_of_two(log2_loss) * 163u + 1u;
                if (apart < 64u * width) {
                    const u32 bandwidths = (apart << 4) / ((width >> 4) | 1u);  // Q8
                    const u32 reach = (256u << 16) / (256u + bandwidths);       // Q16: 1 / (1 + distance)
                    const i32 rise = log2_q16(harmonic << 16) - (16 << 16);
                    const u32 roll = power_of_two(-((slope >> 4) * (rise >> 4) >> 8));
                    bow = static_cast<i32>(times(times(static_cast<u32>((level * open >> 15) * bowed >> 15), roll), reach));
                }
            }
            const Push push = plan(echo_loss, bow, wind);
            // Its power counts as far as it has bloomed: 1 - decay^2n of its steady power.
            echo.swell = 0x7fffffff - multiply(multiply(0x7fffffff - echo.swell, push.decay), push.decay);
            power += times(static_cast<u32>(bow * bow) + 4u * static_cast<u32>(wind * wind), static_cast<u32>(echo.swell >> 15));
            ring(echo.mode, push, other.increment, seed, chorus, acc, written);
        }

    // Out: the modes at an output gain that holds their steady power (so every
    // body, vowel, Bright, Spread and note sounds about as loud, within -12 dB
    // to +12 dB), then Air, then the soft limit. The sympathetic modes count
    // in that power, so a chord blooms into one sound without growing louder.
    // Spread's copies beat, so they sum in power: centre^2 + 2 side^2.
    if (!written) for (unsigned i = 0; i < kHalfFrames; ++i) acc[i] = 0;
    const i32 share = (chorus.centre * chorus.centre >> 15) + 2 * (chorus.side * chorus.side >> 15);  // Q15
    power = times(power, static_cast<u32>(share) << 1);
    i32 even = (kPower - log2_q16(power | 1u)) >> 1;  // log2 of the gain that brings power to kPower
    even = even < -131072 ? -131072 : even > 131072 ? 131072 : even;
    u32 gain = times(static_cast<u32>(kOutput), power_of_two(even));  // Q10
    if (gain > 32767u) gain = 32767u;
    // The gain ramps across the block from last block's, so no change of it steps.
    i32 level = static_cast<i32>(voice.gain << 16);
    const i32 step = (static_cast<i32>(gain) - static_cast<i32>(voice.gain)) << 10;
    voice.gain = gain;
    const opxy::i16 air_gain = static_cast<opxy::i16>(air * kAir >> 15);
    // At half rate the gain takes two steps a sample, from where the full
    // rate's sample 0 had it; the filter's history goes before the block.
    apply_gain(acc, level - step, step * 2);
    for (u32 i = 0; i < kHalfHistory; ++i) low[i] = voice.low[i];
    for (u32 i = 0; i < kHalfHistory; ++i) voice.low[i] = low[kHalfFrames + i];
    i32 full[opxy::block_frames];
    upsample(low, full);
    // Air at the full rate, unfiltered; it does not wait the filter's 0.23 ms.
    if (air_gain) add_air(full, seed, voice.hiss, voice.slow, air_gain);
    for (unsigned i = 0; i < opxy::block_frames; ++i) output[i] = soft_limit(full[i]);
    voice.seed = seed;
}
}
