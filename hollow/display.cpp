// Hollow's screen: an engraved plate of acoustics. Fig. 1 is the body Body is
// nearest, in section, with its standing wave drawn along it (the vocal
// tract, the string, the air column, the glass's rim) and its sympathetic
// partner beside it (the nasal cavity, the open string, the neighbouring
// pipe, the second glass). Fig. 2 is Koenig's analyser: a Helmholtz resonator
// per mode, sized for its pitch, whose flame burns as high as the mode rings.
// Everything is drawn from modes.h, so the picture is the sound's: each
// mode's level, ratio, ring and widening are the engine's own. Integer
// arithmetic only; nothing moves while nothing rings.
//
// The device redraws what changes, so the screen spends its redraws in turns.
// A part is only looked at on its turn and moves in whole pixels and stepped
// greys. That bounds a frame however hard the controls are set: four flames
// and their cores, six motes, four streaks, two strands.
#include "display.h"

namespace hollow {
namespace screen {
using namespace opxy::draw;

// The engine's drive (engine.cpp, render): the bow's and the breath's
// amplitudes at full level, the bow's own hiss and the sympathetic level.
const i32 kBowAmplitude = 3000, kBreathAmplitude = 750, kBowHiss = 2600, kSympathy = 24000;
// The screen draws the body as it sounds for A3, 220 Hz (a phase increment).
const u32 kReference = 21426141u;

// Palette entries (tools/make_panel.py).
const u32 wave_ink = 0, rest_ink = 1, partner_ink = 2, rim_ink = 3, copy_ink = 4, flame_ink = 5, resonator_rest = 9,
          resonator_lit = 10, mote_ink = 11, mote_fading = 12, air_ink = 13, bow_ink = 14, bow_faint = 15;
const i32 lit_floor = 820;   // 0.025: below this a mode is still
// A part's turn comes round every this many frames (the jets every two).
const u32 kFlameTurns = 2, kResonatorTurns = 4, kMoteTurns = 4, kStreakTurns = 4;
// A change of body takes the old figure down and draws the new one up, this
// many nodes a frame.
const u32 kFigureStep = 16;
// A strand reloads once a point has moved a quarter of a pixel (Q10, path units at scale 2).
const i32 kStrandStill = 128;

// A quarter of a sine, Q14, 64 steps.
const opxy::i16 kQuarter[65] = {
    0, 402, 804, 1205, 1606, 2006, 2404, 2801, 3196, 3590, 3981, 4370, 4756, 5139, 5520, 5897, 6270, 6639, 7005,
    7366, 7723, 8076, 8423, 8765, 9102, 9434, 9760, 10080, 10394, 10702, 11003, 11297, 11585, 11866, 12140, 12406,
    12665, 12916, 13160, 13395, 13623, 13842, 14053, 14256, 14449, 14635, 14811, 14978, 15137, 15286, 15426, 15557,
    15679, 15791, 15893, 15986, 16069, 16143, 16207, 16261, 16305, 16340, 16364, 16379, 16384};
// sin of a phase in Q32 turns, Q14.
inline i32 sin14(u32 phase) {
    u32 at = (phase >> 16) & 0x3fffu;            // 0..16383 within the quarter
    if (phase & 0x40000000u) at = 16384 - at;   // the second and fourth quarters run back
    const u32 i = at >> 8, f = at & 255u;
    const i32 low = kQuarter[i], high = kQuarter[i < 64 ? i + 1 : 64];
    const i32 v = low + ((high - low) * static_cast<i32>(f) >> 8);
    return phase & 0x80000000u ? -v : v;
}
inline i32 cos14(u32 phase) { return sin14(phase + 0x40000000u); }

// A fixed pseudo-random value 0..65535 for n.
inline u32 hash(u32 n) {
    n ^= n >> 16;
    n *= 0x7feb352du;
    n ^= n >> 15;
    n *= 0x846ca68bu;
    n ^= n >> 16;
    return n >> 16;
}
// Smooth value noise for seed n at u (Q16 steps), -16384..16384.
inline i32 noise(u32 n, u32 u) {
    const u32 at = u >> 16, f = (u & 0xffffu) >> 1;             // Q15
    const i32 a = static_cast<i32>(hash(n * 7919u + at) >> 1) - 16384;
    const i32 b = static_cast<i32>(hash(n * 7919u + at + 1) >> 1) - 16384;
    const u32 s = (f * f >> 15) * (98304u - 2 * f) >> 15;     // smoothstep, Q15
    return a + ((b - a) * static_cast<i32>(s) >> 15);
}
// exp(-dt / time), Q16, for dt in Q10 seconds and time in Q16 seconds.
inline i32 decay(i32 dt, u32 time) {
    const u32 time_q10 = time >> 6;
    const u32 ratio = (static_cast<u32>(dt) << 16) / (time_q10 ? time_q10 : 1u);   // Q16
    u32 octaves = times(ratio, 94548u);                                             // times log2 e
    if (octaves > (20u << 16)) octaves = 20u << 16;
    return static_cast<i32>(exp2_q16(-static_cast<i32>(octaves)));
}
inline u32 isqrt(u32 v) {
    u32 r = 0, b = 1u << 30;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) {
            v -= r + b;
            r = (r >> 1) + b;
        } else {
            r >>= 1;
        }
        b >>= 2;
    }
    return r;
}

// To the nearest whole pixel (Q10), so a slow part moves only when it visibly does.
inline i32 whole(i32 v) { return (v + 512) & ~1023; }
inline bool showing(const Frame& frame, u32 node) { return frame.nodes[node].x < hidden_x * unit; }

// What the body sounds like this frame: each mode's bow and breath amplitude
// (as engine.cpp drives them, for the reference note), its ring time and the
// rate its breath wanders.
struct Sound {
    i32 bow[kModes], wind[kModes];   // 0..4095 amplitude units
    i32 ring[kModes];                // log2 of the mode's ring time while driven, Q16 seconds
    u32 rms;                         // the modes' steady power, square-rooted
    i32 bowed;                       // the bow's share, Q15
};

inline Sound hear(i32 breath, i32 body, i32 bright, i32 ring, i32 vowel) {
    Sound s;
    const i32 octave = log2_q16(kReference);
    const i32 slope = 131072 - (bright * 9 >> 1), cutoff = bright * 16;
    const i32 open = static_cast<i32>(times(static_cast<u32>(kBowAmplitude), exp2_q16(-bright)));
    const i32 rest = 32767 - breath;
    const i32 bowed = 32767 - (breath * breath >> 15), puff = 32767 - (rest * rest >> 15);
    const i32 blown = puff + ((32767 - puff) * kBowHiss >> 15);
    const i32 widen = breath_widen_log2(breath), seconds = ring_log2_seconds(ring, body);
    const Traits traits = body_traits(body);
    u32 power = 1;
    for (u32 k = 0; k < kModes; ++k) {
        const Shape shape = body_mode(body, k);
        const u32 at = times(kReference, shape.ratio);
        const i32 level = mode_level(shape, traits, vowel, at, octave + shape.log2_ratio);
        const u32 bow_roll = exp2_q16(-((slope >> 4) * (shape.log2_ratio >> 4) >> 8));
        const i32 over = shape.log2_ratio - cutoff;
        const u32 breath_roll = exp2_q16(over > 0 ? -over : 0);
        s.bow[k] = static_cast<i32>(times(static_cast<u32>((level * open >> 15) * bowed >> 15), bow_roll));
        s.wind[k] = static_cast<i32>(times(static_cast<u32>((level * kBreathAmplitude >> 15) * blown >> 15), breath_roll));
        s.ring[k] = seconds - shape.log2_fade - widen;
        power += static_cast<u32>(s.bow[k] * s.bow[k]) + 4u * static_cast<u32>(s.wind[k] * s.wind[k]);
    }
    s.rms = isqrt(power);
    s.bowed = bowed;
    return s;
}

// The wander of a breath-driven mode: two slow noises, Q14.
struct Wander { i32 a, b; };
inline Wander wander(u32 seed, i32 ring_log2, u32 tau) {
    // Wider resonances (a shorter ring) wander faster: 0.5 steps a second at
    // one second, by the square root, between 0.08 and 3.
    i32 rate = static_cast<i32>(exp2_q16(-(ring_log2 >> 1) - 65536));
    rate = clamp(rate, 5243, 196608);
    const u32 u = times(static_cast<u32>(rate), tau);
    Wander w = {noise(seed, u), noise(seed + 101, u + 0x8000u)};
    return w;
}

// A strand's displacement at each sample (Q10 px) from its modes' amplitudes
// (Q10 px) and shapes.
inline void sum_modes(i32* d, u32 samples, const i32* amp, const u32 (*shape)[2], u32 modes) {
    for (u32 i = 0; i < samples; ++i) d[i] = 0;
    for (u32 k = 0; k < modes; ++k) {
        if (!amp[k]) continue;
        u32 phase = shape[k][1];
        for (u32 i = 0; i < samples; ++i, phase += shape[k][0]) d[i] += amp[k] * sin14(phase) >> 14;
    }
}

// Draws a strand into outline p: each rest point moved along its way by d
// (plus a fixed shift), as a thin ribbon, or a loop round a rim. The ribbon is
// 0.4 px wide, so its ends stay 1/8 px across at any angle, and it starts
// halfway along one side, so it closes over a long edge. Unless a point has
// moved at least `still` the outline keeps its points and its key.
inline void draw_strand(Frame& frame, u32 p, u32 family, u32 which, const i32* d, i32 shift, const i32* lift, u32 key, i32 still) {
    Frame::path_type& path = frame.paths[p];
    const opxy::i16 (*rest)[2] = strand_rest[family][which];
    const opxy::i16 (*way)[2] = strand_way[family][which];
    const u32 n = strand_samples[family];
    const i32 limit = strand_reach[family][which] + 3072;   // the artwork's room either side
    Point next[64];
    for (u32 i = 0; i < n; ++i) {
        const i32 move = clamp(d[i] + shift, -limit, limit);
        const i32 x = (static_cast<i32>(rest[i][0]) << 6) + (way[i][0] * move >> 12);
        const i32 y = (static_cast<i32>(rest[i][1]) << 6) + (way[i][1] * move >> 12) + (lift ? lift[i] : 0);
        if (n == 64) {
            next[i].x = x >> 1;
            next[i].y = y >> 1;
        } else {
            next[(i + 48) & 63].x = x >> 1;
            next[(i + 48) & 63].y = y >> 1;
            next[(63 - i + 48) & 63].x = (x + (way[i][0] * 410 >> 12)) >> 1;
            next[(63 - i + 48) & 63].y = (y + (way[i][1] * 410 >> 12)) >> 1;
        }
    }
    bool moved = still <= 0;
    for (u32 i = 0; i < 64 && !moved; ++i) {
        const i32 dx = next[i].x - path.points[i].x, dy = next[i].y - path.points[i].y;
        moved = dx >= still || dx <= -still || dy >= still || dy <= -still;
    }
    if (!moved) return;
    for (u32 i = 0; i < 64; ++i) path.points[i] = next[i];
    path.key = key;
}

inline void place_strand(Frame& frame, u32 p, u32 family, u32 which, u32 ink) {
    place(frame, p, strand_anchor[family][which][0] * unit, strand_anchor[family][which][1] * unit, 2 * unit, ink, stroke);
}

// How far Spread's copies sit out from the wave, per body (Q10 of 1 to 4 px).
const i32 kApart[kBodies] = {614, 1024, 1024, 256};
// How far Sympathy leans the partner toward the body before it rings (Q10 px at full).
const i32 kPoise[kBodies] = {2560, 4096, 4608, 1434};

// The vowel's poses for the tongue (dx, dy px Q10, turn in tenths of a
// degree) and the lips' opening (scale Q10): oo, oh, ah, eh, ee.
const i32 kTongue[kVowels][3] = {{-3072, -4096, 50}, {-2048, -2048, 30}, {0, 0, 0}, {2048, -2048, -30}, {3072, -3072, -45}};
const i32 kLips[kVowels][2] = {{430, 2048}, {640, 1024}, {1024, 0}, {760, 0}, {560, -512}};   // scale, forward (Q10 px)
inline i32 vowel_pose(const i32* table, u32 stride, i32 vowel) {
    const i32 v = clamp(vowel, 0, 32767) * static_cast<i32>(kVowels - 1);
    u32 which = static_cast<u32>(v >> 15);
    i32 toward = v & 32767;
    if (which >= kVowels - 1) { which = kVowels - 2; toward = 32768; }
    return mix(table[which * stride], table[(which + 1) * stride], toward);
}
}

inline void display_reset(DisplayState& state) {
    state.header.reset();
    state.notes.reset();
    state.started = 0;
    state.tick = 0;
}

inline void display(DisplayState& state, const opxy::draw::Input& input, Frame& frame) {
    using namespace opxy::draw;
    using namespace screen;
    // Captions in the units the sound uses (modes.h): body family, ring time
    // (as the body scales it), vowel and cents; amounts elsewhere.
    const char* const titles[8] = {"Breath", "Body", "Bright", "Ring", "Air", "Vowel", "Spread", "Sympathy"};
    char values[8][header_capacity];
    for (u32 i = 0; i < 8; ++i) header_amount(values[i], header_capacity, input.slots[i]);
    body_caption(values[1], header_capacity, control(input.slots[1]));
    ring_caption(values[3], header_capacity, control(input.slots[3]), control(input.slots[1]));
    vowel_caption(values[5], header_capacity, control(input.slots[5]));
    spread_caption(values[6], header_capacity, control(input.slots[6]));
    const opxy::u8 slots[2][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}};
    page_header(state.header, input, frame, titles, values, header_first, slots);

    // A track change or reset shows the end state at once, as reduced motion always does.
    const bool fresh = !state.started || !state.notes.initialized || state.notes.track != input.track;
    const bool jump = fresh || input.reduced_motion;
    state.notes.update(input);
    const i32 dt = clamp(input.elapsed, 0, 102);

    // The controls as the screen shows them: each eases to a turn in about
    // 0.15 s (a 0.045 s time constant).
    const i32 ease = 65536 - decay(dt, 2949);
    for (u32 c = 0; c < 8; ++c) {
        const i32 target = control(input.slots[c]);
        i32& v = state.shown[c];
        if (jump) v = target;
        else v += (target - v) * (ease >> 1) >> 15;
        if (v - target < 48 && target - v < 48) v = target;
    }
    const i32 breath = state.shown[0], body = state.shown[1], bright = state.shown[2], ring = state.shown[3];
    const i32 air = state.shown[4], vowel = state.shown[5], spread = state.shown[6], sympathy = state.shown[7];
    u32 wanted = static_cast<u32>((body * 3 + 16384) >> 15);
    if (wanted > 3) wanted = 3;
    const u32 tick = state.tick++;
    const Sound sound = hear(breath, body, bright, ring, vowel);
    const i32 widen = breath_widen_log2(breath);

    // Notes: each mode blooms while a note is held (as fast as the engine's
    // resonance fills: slower with a longer ring, sooner with more breath,
    // high modes first) and rings out over its own ring time once released.
    const u32 held = input.held_notes <= 128 ? input.held_notes : 0;
    const i32 coupling = sympathy * (11469 + (21299 * sympathy >> 15)) >> 15;   // as the engine's, Q15
    const i32 echo_ring = ring_log2_seconds(ring, body) + coupling * 3 - widen;  // up to 2.8 times longer
    if (fresh) state.tau = 0;
    if (jump) {
        for (u32 k = 0; k < kModes; ++k) state.energy[k] = held ? 32768 : 0;
        state.echo = held >= 2 && coupling ? 32768 : 0;
    } else {
        for (u32 k = 0; k < kModes; ++k) {
            i32& e = state.energy[k];
            if (held) e += (32768 - e) * ((65536 - decay(dt, exp2_q16(sound.ring[k] - 169408) + 3277)) >> 1) >> 15;
            else e -= e * ((65536 - decay(dt, exp2_q16(sound.ring[k] - 131072))) >> 1) >> 15;
            if (!held && e < lit_floor) e = 0;
        }
        // A note started and released between frames still rings.
        const u32 starts = input.note_serial - state.serial;
        if (starts && !held && !state.held)
            for (u32 k = 0; k < kModes; ++k) state.energy[k] = state.energy[k] > 9830 ? state.energy[k] : 9830;
        // The sympathetic partner blooms slowly while two or more notes ring together.
        i32& e = state.echo;
        if (held >= 2 && coupling) e += (32768 - e) * ((65536 - decay(dt, exp2_q16(echo_ring - 103872))) >> 1) >> 15;
        else e -= e * ((65536 - decay(dt, exp2_q16(echo_ring - 131072))) >> 1) >> 15;
        if (e < lit_floor && !(held >= 2 && coupling)) e = 0;
    }
    state.serial = input.note_serial;
    state.held = held;
    state.started = 1;
    i32 loud = state.echo;
    for (u32 k = 0; k < kModes; ++k) loud = state.energy[k] > loud ? state.energy[k] : loud;
    // The clock runs as fast as the loudest mode rings, so motion fades with the sound.
    if (!jump) {
        const u32 pace = static_cast<u32>(loud * 3 / 2 < 32768 ? loud * 3 / 2 : 32768);
        state.tau += (static_cast<u32>(dt) << 6) * pace >> 15;
    }
    const u32 tau = state.tau;
    const bool ringing = loud >= lit_floor;

    // Fig. 1: the current body, in place; the others hidden. A change of body
    // takes the old figure down nearest part first, then draws the new one up
    // from the back, a few nodes a frame.
    if (jump) {
        for (u32 f = 0; f < kBodies; ++f)
            for (u32 n = figure_first[f]; n < figure_first[f] + figure_count[f]; ++n) {
                if (f == wanted) place(frame, n, 0, 0, unit, 0, keep);
                else hide(frame, n);
            }
        state.figure = wanted;
        state.built = figure_count[wanted];
    } else {
        u32 room = kFigureStep;
        if (wanted != state.figure) {
            while (state.built && room) {
                --state.built;
                --room;
                hide(frame, figure_first[state.figure] + state.built);
            }
            if (!state.built) state.figure = wanted;
        }
        if (wanted == state.figure)
            while (state.built < figure_count[wanted] && room) {
                place(frame, figure_first[wanted] + state.built, 0, 0, unit, 0, keep);
                ++state.built;
                --room;
            }
    }
    const u32 family = state.figure;
    const u32 built_to = figure_first[family] + state.built;   // this figure's nodes below this are up
    const bool new_body = fresh || state.strand_body != family;
    state.strand_body = family;
    i32 lift[32];
    const i32* follow = 0;
    if (family == 0) {
        // The tongue takes the vowel's place; the lips round and part; the
        // tract over the tongue follows it.
        const i32 dx = whole(vowel_pose(&kTongue[0][0], 3, vowel)), dy = whole(vowel_pose(&kTongue[0][1], 3, vowel));
        const i32 tenths = (vowel_pose(&kTongue[0][2], 3, vowel) + 505) / 10 * 10 - 500;   // whole degrees
        if (tongue_node < built_to) {
            place(frame, tongue_node, tongue_origin[0] + dx, tongue_origin[1] + dy, unit, 0, keep);
            turn(frame, tongue_node, rotation(tenths));
        }
        for (u32 i = 0; i < 32; ++i) {
            // The tongue's rise under this sample: its shift, and its turn about the root.
            const i32 x = strand_anchor[0][0][0] * unit + (static_cast<i32>(strand_rest[0][0][i][0]) << 6) - tongue_origin[0];
            const i32 rise = dy + ((x >> 4) * (tenths * 1787 / 1000) >> 6);   // Q10 px; a tenth of a degree is 1.787 Q10 radians
            lift[i] = rise * tract_follow[i] >> 8;
        }
        follow = lift;
        if (mouth_node < built_to)
            place(frame, mouth_node, mouth_origin[0] + whole(vowel_pose(&kLips[0][1], 2, vowel)), mouth_origin[1],
                  vowel_pose(&kLips[0][0], 2, vowel) & ~31, 0, keep);
    } else if (family == 1) {
        // The bow draws while the string sounds, and lifts away as Breath takes over.
        const i32 travel = ringing ? whole(10 * sin14(tau * 13107u) >> 4) : 0;   // 0.2 Hz, +-10 px (Q10)
        const u32 ink = sound.bowed > 16384 ? bow_ink : bow_faint;
        if (bow_hair_node < built_to) place(frame, bow_hair_node, bow_x, bow_y + travel, unit, ink, fill_and_stroke);
        if (bow_stick_node < built_to) place(frame, bow_stick_node, bow_x, bow_y + travel, unit, ink, fill_and_stroke);
    } else if (family == 2) {
        // Each pipe's wind sheet sways across its mouth as the pipe speaks.
        for (u32 j = 0; j < 2; ++j) {
            const u32 jet = j ? side_jet_node : main_jet_node;
            if (jet >= built_to || !(jump || new_body || (tick & 1) == j)) continue;
            const i32 e = j ? state.echo : state.energy[0];
            const i32 sway = (e >> 5) * sin14(tau * (j ? 340787u : 262144u)) >> 14;   // 4 and 5.2 Hz, up to 10 degrees
            const i32 flutter = (e >> 6) * (32767 - sound.bowed) >> 15;
            const i32 tenths = ((sway * 100 >> 10) + ((flutter * noise(j + 7, tau * 3) >> 14) * 100 >> 10) + 1012) / 25 * 25 - 1000;
            place(frame, jet, jet_origin[j][0], jet_origin[j][1], unit, 0, keep);
            turn(frame, jet, rotation(tenths));
        }
    }

    // The strands. Each mode turns at its own rate; the breath's part wanders.
    i32 amp[kModes], up[kModes], down[kModes];
    const i32 reach = strand_reach[family][0];
    const i32 cents = spread_cents_q8(spread), wet = spread_wet(spread);
    const u32 drift = static_cast<u32>(cents * 3);   // the copies' beat, Q16 of the mode's rate: 0.35 at 30 cents
    for (u32 k = 0; k < kModes; ++k) {
        const u32 phase = mode_rate[family][k] * tau + (hash(k + 17) << 16);
        const Wander w = wander(k * 31 + family, sound.ring[k], tau);
        const i32 e = state.energy[k];
        const i32 wind2 = sound.wind[k] * 2;
        const i32 noisy = ((wind2 * w.a >> 14) * cos14(phase) >> 14) + ((wind2 * w.b >> 14) * sin14(phase) >> 14);
        const i32 scale = static_cast<i32>((static_cast<u32>(reach) << 4) / sound.rms);   // Q10 px per amplitude unit, Q4
        amp[k] = ((sound.bow[k] * cos14(phase) >> 14) + noisy) * (e >> 3) >> 12;            // amplitude units
        amp[k] = amp[k] * scale >> 4;
        // Spread's copies: the same mode beating ahead and behind, swaying
        // at the ensemble's slow rates.
        const u32 beat = times(mode_rate[family][k], drift);
        const u32 sway = beat * 15860u;
        const u32 ahead = beat * tau + static_cast<u32>(sin14(times(kSwayRates[0], tau) << 16) * static_cast<i32>(sway >> 14));
        const u32 behind = beat * tau + static_cast<u32>(sin14((times(kSwayRates[1], tau) << 16) + 0x5000000u) * static_cast<i32>(sway >> 14));
        up[k] = ((((sound.bow[k] * cos14(phase + ahead)) >> 14) + noisy) * (e >> 3) >> 12) * scale >> 4;
        down[k] = ((((sound.bow[k] * cos14(phase - behind)) >> 14) + noisy) * (e >> 3) >> 12) * scale >> 4;
    }
    // The partner rings at its own pitch (a fifth or so above), its breath wandering too.
    i32 echo_amp[3];
    {
        const i32 level = (coupling * kSympathy >> 15) * (state.echo >> 3) >> 12;            // Q15
        const i32 crowd = 19661 + 4369 * static_cast<i32>(held > 4 ? 3 : held > 1 ? held - 1 : 0);
        const i32 r = (strand_reach[family][1] >> 2) * (level * crowd >> 15) >> 13;          // Q10 px
        const i32 parts[3] = {32767, 13107, 5898};
        for (u32 j = 0; j < 3; ++j) {
            const u32 phase = partner_rate[family][j] * tau + (hash(j + 41) << 16);
            const Wander w = wander(j * 13 + 5, echo_ring, tau);
            const i32 bowed = (sound.bowed * cos14(phase) >> 14);
            const i32 blown = ((32767 - sound.bowed) * ((w.a * cos14(phase) + w.b * sin14(phase)) >> 14) >> 14);
            echo_amp[j] = (r * parts[j] >> 15) * ((bowed + blown) >> 3) >> 12;
        }
        // Before it rings it leans toward the body as far as Sympathy couples them.
        echo_amp[0] += (kPoise[family] * (coupling >> 3) >> 12) * ((32768 - state.echo) >> 3) >> 12;
    }
    // The wave and its partner reload on one frame, the copies on the next, and
    // each only once it has moved. Fading out, they settle exactly on rest.
    state.key = state.key + 4 > state.key ? state.key + 4 : 4;
    const u32 n = strand_samples[family];
    const bool glass = family == 3;
    const bool ensemble = wet > lit_floor && ringing;
    const bool partnered = glass || coupling > 0 || state.echo >= lit_floor;
    const i32 still = fresh ? 0 : new_body || !ringing ? 1 : kStrandStill;
    i32 d[64];
    if (new_body || !(tick & 1)) {
        sum_modes(d, n, amp, mode_shape[family], kModes);
        draw_strand(frame, 0, family, 0, d, 0, follow, state.key, still);
    }
    if (new_body || (partnered && (!(tick & 1) || !showing(frame, 1)))) {
        sum_modes(d, n, echo_amp, partner_shape[family], 3);
        draw_strand(frame, 1, family, 1, d, 0, 0, state.key + 1, showing(frame, 1) || fresh ? still : 1);
    }
    if (new_body || (ensemble && ((tick & 1) || !showing(frame, 2)))) {
        // The copies sit either side, as far out as the detune (closer in the
        // airway, and barely off the rim).
        const i32 apart = (1024 + (cents * 13 >> 5)) * kApart[family] >> 10;
        const i32 copy_still = showing(frame, 2) || fresh ? still : 1;
        sum_modes(d, n, up, mode_shape[family], kModes);
        draw_strand(frame, 2, family, 0, d, apart, follow, state.key + 2, copy_still);
        sum_modes(d, n, down, mode_shape[family], kModes);
        draw_strand(frame, 3, family, 0, d, -apart, follow, state.key + 3, copy_still);
    }
    place_strand(frame, 0, family, 0, ringing ? wave_ink : glass ? rim_ink : rest_ink);
    // The partner shows once Sympathy couples it, brighter with more and
    // brightest ringing; the second glass's rim always shows.
    const u32 partner_shade = state.echo >= lit_floor ? rim_ink
                              : glass ? (sympathy > 16384 ? rim_ink : partner_ink)
                                      : (sympathy > 16384 ? partner_ink : rest_ink);
    if (partnered) place_strand(frame, 1, family, 1, partner_shade);
    else hide(frame, 1);
    for (u32 p = 2; p < 4; ++p) {
        if (ensemble) place_strand(frame, p, family, 0, copy_ink);
        else hide(frame, p);
    }

    // Fig. 2: Koenig's analyser. Each resonator is as large as its mode's
    // pitch needs (radius by the ratio to the -2/3, as for a fixed neck); each
    // flame burns as high as its mode rings, flickering with the breath.
    i32 peak = 1, strength[kModes];
    for (u32 k = 0; k < kModes; ++k) {
        strength[k] = static_cast<i32>(isqrt(static_cast<u32>(sound.bow[k] * sound.bow[k] + 4 * sound.wind[k] * sound.wind[k])));
        peak = strength[k] > peak ? strength[k] : peak;
    }
    for (u32 k = 0; k < kModes; ++k) {
        const i32 rings = (strength[k] << 15) / peak * (state.energy[k] >> 3) >> 12;    // Q15
        if (jump || k % kResonatorTurns == tick % kResonatorTurns) {
            const Shape shape = body_mode(body, k);
            const i32 size = static_cast<i32>(exp2_q16(-(shape.log2_ratio * 2 / 3)) >> 6);   // Q10
            const i32 scale = (size > 120 ? size : 120) & ~15;
            // It lights above 0.2 and dims below 0.15, so it doesn't chatter on the line.
            const u32 node = resonator_first + k;
            const bool lit = rings > (frame.nodes[node].shade == resonator_lit ? 4915 : 6554);
            place(frame, node, slot_x[k], neck_y, scale, lit ? resonator_lit : resonator_rest, fill_and_stroke);
            place(frame, shine_first + k, slot_x[k], neck_y, scale, 0, keep);
        }
        if (!(jump || k % kFlameTurns == tick % kFlameTurns)) continue;
        i32 height = 102 + (922 * rings >> 15);   // Q10: a pilot flame at rest
        if (ringing) {
            const i32 lick = 40 * sin14(tau * 458752u + k * 0x2f000000u) >> 14;
            const i32 gust = ((32767 - sound.bowed) * 180 >> 15) * noise(k + 61, tau * 4) >> 14;
            height += height * (lick + gust) >> 10;
        }
        // In steps of about a pixel, starting from the pilot's exact height.
        height = 102 + (clamp(height, 60, 1100) + 1908) / 20 * 20 - 2000;
        const u32 ink = flame_ink + static_cast<u32>(clamp((height - 102) * 4 / 922, 0, 3));
        place(frame, flame_first + k, slot_x[k], burner_y, height, ink, fill_and_stroke);
        if (height > 260) place(frame, core_first + k, slot_x[k], burner_y, height, 0, keep);
        else hide(frame, core_first + k);
    }

    // Breath leaves the body as motes (more with more breath), and Air's hiss
    // as faster streaks, while it sounds.
    const i32 sx = breath_source[family][0] * unit, sy = breath_source[family][1] * unit;
    const i32 vx = breath_source[family][2], vy = breath_source[family][3];
    const i32 length = static_cast<i32>(isqrt(static_cast<u32>(vx * vx + vy * vy) << 16));   // Q8
    const i32 ux = (vx << 18) / length, uy = (vy << 18) / length;                           // Q10, its unit direction
    const i32 puff = 32767 - sound.bowed;
    const u32 motes = ringing ? static_cast<u32>((puff * static_cast<i32>(mote_count) + 16384) >> 15) : 0;
    for (u32 m = 0; m < mote_count; ++m) {
        if (!jump && m % kMoteTurns != tick % kMoteTurns) continue;
        const u32 node = mote_first + m;
        if (m >= motes) { hide(frame, node); continue; }
        const u32 life = (tau * (19661u + hash(m + 3) * 16384u / 65536u)) + (hash(m + 11) << 16);   // Q32 of its life
        const i32 age = static_cast<i32>(life >> 22);                                                 // 0..1023
        const i32 along = age * 22 / 10;                    // Q10 s: 2.2 s of drift
        const i32 across = (static_cast<i32>(hash(m + 23)) - 32768) * (4 + 12 * age / 1024) >> 15;   // px
        const i32 sway = 2 * sin14(tau * 32768u + m * 0x3b000000u) >> 4;                           // Q10 px, 0.5 Hz
        const i32 x = sx + vx * along - uy * across + (sway * -uy >> 10);
        const i32 y = sy + vy * along + ux * across + (sway * ux >> 10);
        place(frame, node, x, y, unit, age < 560 ? mote_ink : mote_fading, fill_and_stroke);
    }
    const u32 streaks = ringing ? static_cast<u32>((air * static_cast<i32>(air_count) + 16384) >> 15) : 0;
    for (u32 m = 0; m < air_count; ++m) {
        if (!jump && (m + 2) % kStreakTurns != tick % kStreakTurns) continue;
        const u32 node = air_first + m;
        if (m >= streaks) { hide(frame, node); continue; }
        const u32 life = (tau * (45875u + hash(m + 5) * 26214u / 65536u)) + (hash(m + 13) << 16);
        const i32 age = static_cast<i32>(life >> 22);
        const i32 along = age * 30 / 10;                     // Q10 s: 3 s of drift, half again as fast as the motes
        const i32 across = (static_cast<i32>(hash(m + 29)) - 32768) * (6 + 22 * age / 1024) >> 15;
        const i32 x = sx + (vx * 3 >> 1) * along - uy * across;
        const i32 y = sy + (vy * 3 >> 1) * along + ux * across;
        place(frame, node, x, y, unit, air_ink, fill_and_stroke);
        const Turn way = {ux, uy};
        turn(frame, node, way);
    }
}
}
