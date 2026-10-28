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
inline void caption(char (&out)[opxy::draw::header_capacity], u32 slot, i32 value) {
    char text[opxy::draw::header_capacity];
    char* at = text;
    if (slot == 0) {            // tilt, dB per octave
        const i32 db = tilt_db(value);
        if (db < 0) *at++ = '-';
        else if (db > 0) *at++ = '+';
        put_number(at, static_cast<u32>(db < 0 ? -db : db));
        *at++ = 'd'; *at++ = 'B';
    } else if (slot == 2) {     // stretch, partial 2's ratio
        put_decimal(at, stretch_x100(value), 2);
    } else if (slot == 4) {     // partials, a count
        put_number(at, static_cast<u32>(partials_shown(value)));
    } else if (slot == 6) {     // haze, cents of drift
        const u32 tenths = haze_tenths(value);
        if (!tenths) *at++ = '0';
        else if (tenths < 100) put_decimal(at, tenths, 1);
        else put_number(at, (tenths + 5) / 10);
        *at++ = 'c';
    } else if (slot == 7) {     // time, the drift's period, or hold
        const u32 hundredths = time_hundredths(value);
        if (!hundredths) { *at++ = 'h'; *at++ = 'o'; *at++ = 'l'; *at++ = 'd'; }
        else {
            if (hundredths < 1000) put_decimal(at, (hundredths + 5) / 10, 1);
            else put_number(at, (hundredths + 50) / 100);
            *at++ = 's';
        }
    } else {                    // odd, breath, blur: amounts
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
// own, for one display seed, so tilt, odd, stretch, breath, count, blur, haze
// and the weather draw exactly as they sound.
using opxy::u8;
using opxy::u16;
using opxy::draw::unit;
const i32 base = 206;                        // the horizon
const u32 display_seed = 0x6b10b5edu;   // one meadow, the same every time
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
const u32 tau_wrap = 90 * 1024;   // 90 s, Q10: a whole number of every mote's rises and sways

// x on the meadow's log axis for a log2 ratio (Q16), Q10 px: 420 / log2(60) px an octave.
inline i32 axis_x(i32 log2_ratio) { return 30 * unit + log2_ratio * 1138 / 1024; }

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

inline void display(DisplayState& state, const opxy::draw::Input& input, Frame& frame) {
    using namespace opxy::draw;
    const char* const titles[8] = {"Tilt", "Odd", "Stretch", "Breath", "Partials", "Blur", "Haze", "Time"};
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
        const u32 units = ((c.step * 689u) >> 4) * moving >> 12;
        state.time += units;
        state.clock += units >> 1;   // the weather moves at half the partials' pace, as the engine's
        state.tau = (state.tau + moving / 64) % tau_wrap;
    }

    // The spectrum now: note 0 keeps every partial below the cutoff (the screen has no pitch).
    const i32 gust = weather(state.clock);
    Spectrum s;
    shape(c, display_seed, state.time, gust, 0, s);
    const u32 shown = static_cast<u32>(partials_shown(input.slots[4]));
    i32 top = 1;
    for (u32 k = 0; k < shown; ++k) if (s.level[k] > top) top = s.level[k];

    // The sun: tilt raises it as the sound brightens; the weather's lean, up to
    // 3 dB an octave with full breath, lifts and lowers it 4 px a dB.
    const u32 tilt = unit16(input.slots[0]), breath = unit16(input.slots[3]);
    const i32 lean = ((static_cast<i32>(breath >> 1) * (gust >> 1)) >> 19) * 12;   // Q10 px
    i32 sun_y = (base - 16) * unit - static_cast<i32>(tilt * 104u >> 6) - lean;
    if (sun_y < 74 * unit) sun_y = 74 * unit;   // its top stays below the header
    place(frame, sun_node, 380 * unit, sun_y, unit, frozen ? faint : sun_grey, fill_and_stroke);
    for (u32 i = 0; i < stripe_count; ++i) place(frame, stripe_first + i, 380 * unit, sun_y, unit, black, fill_and_stroke);

    // The meadow.
    const i32 open_scale = 36045 + (29491 * state.open >> 16);   // 0.55 closed .. 1 open, Q16
    const i32 pop_scale = 65536 + (9830 * state.pop >> 16);      // up to 1.15 on a note start
    const u32 blur = unit16(input.slots[5]);
    const bool halos = blur > 3277 && state.open > 19661;
    i32 tip_x[partial_count], tip_y[partial_count];
    u32 tips = 0;
    for (u32 k = 0; k < partial_count; ++k) {
        const u32 stem = partial_first + 3 * k, halo = stem + 1, blossom = stem + 2;
        // Stretch moves partial k to k^(1 + s/2); haze wavers it, as much as the
        // engine detunes it and a little more, while notes are held.
        const i32 harmonic = log2_partial[k + 1];
        const i32 stretched = harmonic + ((harmonic >> 2) * static_cast<i32>(c.stretch >> 2) >> 13);
        const i32 haze = (s.pitch[k] - stretched) * 3442 / 1024;
        const i32 x = axis_x(stretched) + (haze * (state.open >> 4) >> 12);
        if (k >= shown || x > 460 * unit) {
            hide(frame, stem);
            hide(frame, halo);
            hide(frame, blossom);
            continue;
        }
        // Height and size by the square root of the level against the loudest;
        // a silent partial (odd at 100) keeps a stub.
        const u32 relative = (static_cast<u32>(s.level[k]) << 15) / static_cast<u32>(top);
        u32 root_level = root(relative << 15);   // Q15
        if (root_level < 733) root_level = 733;
        const i32 y = base * unit - 14 * unit - static_cast<i32>(132u * root_level >> 5);
        tip_x[tips] = x;
        tip_y[tips] = y;
        ++tips;
        place(frame, stem, x, y, unit, frozen ? frozen_stem : stem_grey, fill_and_stroke);
        const i32 radius = (((1638 + static_cast<i32>(root_level * 4506u >> 15)) * open_scale >> 16) * pop_scale) >> 16;
        if (halos) {
            // 1.6 to 3 times the blossom, as wide as blur spreads the drift; never into the header.
            i32 r = radius * (1638 + static_cast<i32>(blur * 1434u >> 16)) >> 10;
            if (r > y - 38 * unit) r = y - 38 * unit;
            place(frame, halo, x, y, r / 12, frozen ? faint : halo_grey, fill_and_stroke);
        } else {
            hide(frame, halo);
        }
        const u32 band = shown > 1 ? k * 5 / (shown - 1) : 0;
        const u32 grey = frozen ? white : state.open < 32768 ? rest_grey : blossom_ramp[band > 4 ? 4 : band];
        place(frame, blossom, x, y, radius / 4, grey, fill_and_stroke);
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
