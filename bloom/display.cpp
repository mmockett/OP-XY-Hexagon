#include "display.h"
#include "spectrum.h"

namespace bloom {
// Header captions in the units the sound uses (spectrum.h holds the laws).
inline void put_number(char*& at, u32 value) {
    char digits[10];
    u32 n = 0;
    do { digits[n++] = static_cast<char>('0' + value % 10); value /= 10; } while (value && n < 10);
    while (n) *at++ = digits[--n];
}
// value in hundredths (places 2: "2.00") or tenths (places 1: "7.5").
inline void put_decimal(char*& at, u32 value, u32 places) {
    const u32 scale = places == 2 ? 100u : 10u;
    put_number(at, value / scale);
    *at++ = '.';
    if (places == 2 && value % 100 < 10) *at++ = '0';
    put_number(at, value % scale);
}
inline void put_text(char*& at, const char* text) { while (*text) *at++ = *text++; }
inline void caption(char (&out)[opxy::draw::header_capacity], u32 slot, i32 value) {
    char text[opxy::draw::header_capacity];
    char* at = text;
    if (slot == 0) {            // tilt, dB per octave
        const i32 db = tilt_db(value);
        if (db < 0) *at++ = '-';
        else if (db > 0) *at++ = '+';
        put_number(at, static_cast<u32>(db < 0 ? -db : db));
        put_text(at, "dB");
    } else if (slot == 1) {     // shape: saw, sq and how far to square, sqr, then the pulse width
        const u32 square = shape_square_percent(value);
        if (shape_pulse(value)) { put_text(at, "pw"); put_number(at, shape_width_percent(value)); }
        else if (!square) put_text(at, "saw");
        else if (square >= 100) put_text(at, "sqr");
        else { put_text(at, "sq"); put_number(at, square); }
    } else if (slot == 2) {     // stretch, partial 2's ratio
        put_decimal(at, stretch_x100(value), 2);
    } else if (slot == 3) {     // vowel, off or the nearest
        const char* const names[5] = {"oo", "oh", "ah", "eh", "ee"};
        put_text(at, unit16(value) ? names[vowel_shown(value)] : "off");
    } else if (slot == 5) {     // blur, cents of spread
        put_number(at, blur_cents(value));
        *at++ = 'c';
    } else if (slot == 6) {     // haze, cents between each pair
        put_number(at, haze_cents(value));
        *at++ = 'c';
    } else if (slot == 7) {     // time, the drift's period, or hold
        const u32 ms = time_ms(value);
        if (!ms) put_text(at, "hold");
        else if (ms < 1000) { put_number(at, ms < 100 ? ms : (ms + 5) / 10 * 10); put_text(at, "ms"); }
        else {
            if (ms < 10000) put_decimal(at, (ms + 50) / 100, 1);
            else put_number(at, (ms + 500) / 1000);
            *at++ = 's';
        }
    } else {                    // breath: an amount
        opxy::draw::header_amount(out, opxy::draw::header_capacity, value);
        return;
    }
    *at = 0;
    opxy::draw::copy(out, opxy::draw::header_capacity, text);
}

// ---- The meadow -----------------------------------------------------------
// The board: each partial a stem and blossom over a low striped sun, placed
// across a log axis (partial 1 at x 30, 420 px for a ratio of 60) and as tall
// as the square root of its level. The levels and pitches are spectrum.h's
// own, for one display seed at C3, so tilt, shape, stretch, vowel, breath and
// the weather draw exactly as they sound. Behind the meadow the vowel's two
// formants rise as hills; blur rings each blossom with a halo as wide as its
// spread, and haze splits each blossom into its detuned pair.
using opxy::u8;
using opxy::u16;
using opxy::draw::unit;
const i32 base = 206;                   // the horizon
const u32 display_seed = 0x6b10b5edu;   // one meadow, the same every time
const i32 display_note = 1546838;       // C3's phase increment, log2 Q16: places the vowel
// Shades (palette entries; tools/make_panel.py lists the greys).
const u32 black = 0, faint = 1, halo_grey = 2, sun_grey = 3, ground_grey = 4, rest_grey = 5, stem_grey = 6,
          frozen_stem = 9, pollen_grey = 11, frozen_ground = 13, white = 15;
// Blossoms from the lowest partial to the highest, in five bands.
const u8 blossom_ramp[5] = {14, 12, 10, 8, 7};
// Pollen: the tip each mote rises from (Q16 of the tips drawn), its speed in
// px per second (whole, so the 90 s wrap of tau is seamless) and its start, Q10 px.
const u16 mote_tip[mote_count] = {51264, 55967, 41932, 5240, 32639, 52997, 48392,
                                   5801, 28390, 64567, 59486, 31555, 23015, 32179};
const u8 mote_speed[mote_count] = {10, 15, 14, 13, 11, 16, 15, 13, 13, 13, 9, 14, 14, 12};
const u32 mote_start[mote_count] = {50593, 46255, 30951, 32160, 32058, 1222, 36393,
                                    37547, 24657, 52166, 20939, 25953, 14611, 60660};
const u32 tau_wrap = 90 * 1024;   // 90 s, Q10: a whole number of every mote's rises, beats and sways
// The hills: one outline for each formant, 68 px wide (the lift's reach either side).
const i32 hill_half = 34;         // px
const u32 hill_top = hill_points - 2;

// x on the meadow's log axis for a log2 ratio (Q16), Q10 px: 420 / log2(60) px an octave.
inline i32 axis_x(i32 log2_ratio) { return 30 * unit + log2_ratio * 1138 / 1024; }
inline i32 axis_ratio(i32 x) { return (x - 30 * unit) * 1024 / 1138; }
// The angle, tenths of a degree, of something turning `cycles` times every 90 s at time tau.
inline i32 cycle_tenths(u32 tau, u32 cycles) { return static_cast<i32>((tau * cycles % tau_wrap) * 3600u / tau_wrap); }

// Eases value (Q16) toward target with a 28.7 ms half-life: within 1% after 0.2 s, then snaps.
inline i32 ease(i32 value, i32 target, i32 elapsed) {
    const i32 remaining = static_cast<i32>(pow2((16 << 16) - elapsed * 2229));   // Q16
    value = target + (value - target) * (remaining >> 2) / 16384;
    const i32 gap = value - target;
    return gap < 512 && gap > -512 ? target : value;
}

inline void display_reset(DisplayState& state) {
    state.header.reset();
    state.notes.reset();
    state.time = 0;
    state.clock = 0;
    state.tau = 0;
    state.open = 0;
    state.pop = 0;
    state.serial = 0;
    state.track = 0;
    state.initialized = 0;
}

// The vowel's hills: each formant's lift as a hill outline centred where it
// falls on the meadow, 12 px an octave of level (up to 60 px), or hidden
// (a flat sliver) when vowel is off.
inline void hills(Frame& frame, const Controls& c, const Wander& w, u32 shade) {
    using namespace opxy::draw;
    i32 formant[2] = {0, 0};
    if (c.vowel_depth) formants(w.vowel, formant[0], formant[1]);
    for (u32 h = 0; h < hill_count; ++h) {
        Path<hill_points>& path = frame.paths[hill_first + h];
        u32 key = 0x9e3779b9u * (h + 1);
        for (u32 i = 0; i < hill_top; ++i) {
            const i32 x = -hill_half * unit + static_cast<i32>(i) * (2 * hill_half * unit) / static_cast<i32>(hill_top - 1);
            i32 height = c.vowel_depth ? formant_lift(x * 1024 / 1138, c.vowel_depth) * 3 >> 4 : 0;   // Q10 px
            if (height < 307) height = 307;   // a sliver keeps the outline's area
            path.points[i].x = x;
            path.points[i].y = -height;
            key = (key ^ static_cast<u32>(height)) * 0x01000193u;
        }
        path.points[hill_top].x = hill_half * unit;
        path.points[hill_top].y = 410;
        path.points[hill_top + 1].x = -hill_half * unit;
        path.points[hill_top + 1].y = 410;
        path.key = key | 1u;
        if (c.vowel_depth) place(frame, hill_first + h, axis_x(formant[h] - display_note), base * unit, unit, shade, stroke);
        else hide(frame, hill_first + h);
    }
}

inline void display(DisplayState& state, const opxy::draw::Input& input, Frame& frame) {
    using namespace opxy::draw;
    const char* const titles[8] = {"Tilt", "Shape", "Stretch", "Vowel", "Breath", "Blur", "Haze", "Time"};
    char values[8][header_capacity];
    for (opxy::u32 i = 0; i < 8; ++i) caption(values[i], i, input.slots[i]);
    const opxy::u8 slots[2][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}};
    page_header(state.header, input, frame, titles, values, header_first, slots);

    // Notes: held opens the blossoms, each start swells them; a reset, a track
    // change or reduced motion shows the end state at once.
    state.notes.update(input);
    const bool held = input.held_notes > 0 && input.held_notes <= 128;
    const bool snap = !state.initialized || state.track != input.track || input.reduced_motion;
    const i32 elapsed = clamp(input.elapsed, 0, 102);
    const i32 opened = held ? 65536 : 0;
    if (snap) {
        state.open = opened;
        state.pop = 0;
    } else {
        if (held && input.note_serial != state.serial) state.pop = 65536;
        state.open = ease(state.open, opened, elapsed);
        state.pop = ease(state.pop, 0, elapsed);
    }
    state.initialized = 1;
    state.track = input.track;
    state.serial = input.note_serial;

    // The drift moves only while the blossoms are open, slowing as they close.
    const Controls c = decode(input.slots);
    const bool frozen = bloom::held(input.slots[7]);   // time at hold: the scene turns white and still
    if (!input.reduced_motion && state.open) {
        const u32 moving = static_cast<u32>(elapsed * state.open) >> 10;   // Q16 seconds
        // c.step a block, 689 blocks a second.
        const u32 units = ((c.step * 689u) >> 8) * (moving >> 4) >> 4;
        state.time += units;
        state.clock += units >> 2;   // the weather moves at a quarter of the partials' pace, as the engine's
        state.tau = (state.tau + moving / 64) % tau_wrap;
    }

    // The spectrum now, at C3.
    const i32 gust = weather(state.clock);
    Spectrum s;
    shape(c, display_seed, state.time, gust, display_note, s);
    i32 top = 1;
    for (u32 k = 0; k < partial_count; ++k) if (s.level[k] > top) top = s.level[k];

    // The sun: tilt raises it as the sound brightens; the weather's lean, up to
    // 4.5 dB an octave with full breath, lifts and lowers it.
    const u32 tilt = unit16(input.slots[0]), breath = unit16(input.slots[4]);
    const i32 lean = ((static_cast<i32>(breath >> 1) * (gust >> 1)) >> 19) * 18;   // Q10 px
    i32 sun_y = (base - 16) * unit - static_cast<i32>(tilt * 104u >> 6) - lean;
    if (sun_y < 74 * unit) sun_y = 74 * unit;   // its top stays below the header
    place(frame, sun_node, 380 * unit, sun_y, unit, frozen ? faint : sun_grey, fill_and_stroke);
    for (u32 i = 0; i < stripe_count; ++i) place(frame, stripe_first + i, 380 * unit, sun_y, unit, black, fill_and_stroke);

    hills(frame, c, wander(c, display_seed, state.time), frozen ? frozen_ground : ground_grey);

    // The meadow.
    const i32 open_scale = 36045 + (29491 * state.open >> 16);   // 0.55 closed .. 1 open, Q16
    const i32 pop_scale = 65536 + (9830 * state.pop >> 16);      // up to 1.15 on a note start
    const u32 blur = blur_cents(input.slots[5]), haze = haze_cents(input.slots[6]);
    const bool alive = state.open > 19661 && !frozen;            // beats and shimmer while notes are held
    i32 tip_x[partial_count], tip_y[partial_count];
    u32 tips = 0;
    for (u32 k = 0; k < partial_count; ++k) {
        const u32 stem = partial_first + partial_nodes * k, halo = stem + 1, blossom = stem + 2, twin = stem + 3;
        const i32 x = axis_x(s.pitch[k]);
        if (x > 460 * unit) {
            hide(frame, stem);
            hide(frame, halo);
            hide(frame, blossom);
            hide(frame, twin);
            continue;
        }
        // Height and size by the square root of the level against the loudest;
        // a silent partial (an even one at square) keeps a stub.
        const u32 relative = (static_cast<u32>(s.level[k]) << 15) / static_cast<u32>(top);
        u32 root_level = root(relative << 15);   // Q15
        if (root_level < 733) root_level = 733;
        const i32 y = base * unit - 14 * unit - static_cast<i32>(132u * root_level >> 5);
        tip_x[tips] = x;
        tip_y[tips] = y;
        ++tips;
        place(frame, stem, x, y, unit, frozen ? frozen_stem : stem_grey, fill_and_stroke);
        const i32 radius = (((1638 + static_cast<i32>(root_level * 4506u >> 15)) * open_scale >> 16) * pop_scale) >> 16;
        // Blur: a halo as wide as the partial's spread (0.1 px a cent), shimmering while held.
        if (blur) {
            i32 r = radius + static_cast<i32>(blur * partial_reach(k) * 102u >> 8);
            if (alive) r += r * sine(cycle_tenths(state.tau, 45 + 7 * k) + static_cast<i32>(k) * 1291) >> 13;
            if (r > y - 38 * unit) r = y - 38 * unit;
            if (r > 18 * unit) r = 18 * unit;
            place(frame, halo, x, y, r / 12, frozen ? faint : halo_grey, fill_and_stroke);
        } else {
            hide(frame, halo);
        }
        const u32 band = k * 5 / (partial_count - 1);
        const u32 grey = frozen ? white : state.open < 32768 ? rest_grey : blossom_ramp[band > 4 ? 4 : band];
        if (haze) {
            // Haze: the pair half its detune either side, 0.15 px a cent, beating
            // (slowed down to be seen) while held.
            const i32 apart = static_cast<i32>(haze * pair_spread(display_seed, k) * 154u >> 9);
            i32 near = radius, far = radius;
            if (alive) {
                u32 beats = 9u + (haze * (k + 1) >> 2);   // per 90 s: 0.1 to 3 Hz
                if (beats > 270) beats = 270;
                const i32 swing = sine(cycle_tenths(state.tau, beats) + 900);   // Q10 cosine
                near = radius * (2048 + swing / 2) >> 11;
                far = radius * (2048 - swing / 2) >> 11;
            }
            place(frame, blossom, x - apart, y, near / 4 < 1 ? 1 : near / 4, grey, fill_and_stroke);
            place(frame, twin, x + apart, y, far / 4 < 1 ? 1 : far / 4, grey, fill_and_stroke);
        } else {
            place(frame, blossom, x, y, radius / 4, grey, fill_and_stroke);
            hide(frame, twin);
        }
    }

    // Pollen drifting up from the blossoms as breath stirs them.
    const u32 motes = state.open > 3277 && tips && !frozen ? (breath * 14u + 32768u) >> 16 : 0;
    for (u32 i = 0; i < mote_count; ++i) {
        const u32 node = mote_first + i;
        if (i >= motes) { hide(frame, node); continue; }
        const u32 tip = static_cast<u32>(mote_tip[i]) * tips >> 16;
        const i32 rise = static_cast<i32>((state.tau * mote_speed[i] + mote_start[i]) % tau_wrap);
        const i32 y = tip_y[tip] - rise;
        const i32 sway = 6 * sine(static_cast<i32>(state.tau * 720u >> 10) + static_cast<i32>(i) * 573);
        if (y <= 42 * unit) { hide(frame, node); continue; }
        place(frame, node, tip_x[tip] + sway, y, unit, pollen_grey, fill_and_stroke);
    }
    place(frame, horizon_node, 16 * unit, base * unit, unit, frozen ? frozen_ground : ground_grey, fill_and_stroke);
}
}
