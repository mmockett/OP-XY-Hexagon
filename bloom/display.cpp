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
    // Built at the text slots' size, so a number of any width fits while it is
    // written (GCC's stringop-overflow check counts all ten digits); copy()
    // then keeps the header's seven characters, which every caption fits.
    char text[opxy::draw::text_capacity];
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
        else if (ms < 100) { put_number(at, ms); put_text(at, "ms"); }
        else {
            if (ms < 995) put_decimal(at, (ms + 5) / 10, 2);   // "100ms" is too wide: 0.10s
            else if (ms < 10000) put_decimal(at, (ms + 50) / 100, 1);
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
// The stock look's silhouette: a low sun on the horizon, one flat light disc
// with its stripes knocked out, and the meadow of partials standing in front
// of it as dark stems, each with a blossom, across a log axis (partial 1 at
// x 40, 88 px an octave) and as tall as its level, 128 px for the loudest
// down to a 4 px stub 54 dB below it. The levels and pitches are spectrum.h's
// own, for one display seed at C3, so tilt, shape, stretch, vowel, breath and
// the weather draw exactly as they sound. The vowel's two formants rise as
// black mounds cut into the sun; blur rings each blossom with a dark halo as
// wide as its spread, and haze splits each blossom into its detuned pair.
// Tilt raises the sun as the sound brightens, and the weather (breath) lifts
// and lowers it. Under the horizon four status dots carry the grey code,
// hollow, dark, mid and light, each at its encoder's travel on the page the
// encoders turn.
//
// What it costs: the device redraws what changes. So what moves does so in whole
// pixels and stepped sizes, and the frame persists between calls: each frame
// works out where the whole meadow should be, then redraws only the partials
// furthest from it, up to meadow_budget nodes. The sun takes its turn.
using opxy::u8;
using opxy::u16;
using opxy::draw::unit;
const i32 base = 200;                   // the horizon
const u32 display_seed = 0x6b10b5edu;   // one meadow, the same every time
const i32 display_note = 1546838;       // C3's phase increment, log2 Q16: places the vowel
// Shades (palette entries; tools/make_panel.py lists them).
const u32 sun_shade = 1, hill_shade = 2, stem_shade = 3, halo_shade = 4, dot_shade = 5;
const u32 sun_turns = 3;           // the sun moves one frame in this many
const u32 tau_wrap = 90 * 1024;    // 90 s, Q10: a whole number of every beat, shimmer and sway
const i32 sun_x = 240;             // px; it sits 18 px into the ground (tilt 0) to 40 px over it (tilt 100)
const i32 hill_high = 34;          // a hill at full depth, px (tools/make_panel.py)
const i32 dot_track = 30, dot_travel = 60, dot_y = 212;   // px: each dot's track from 30 + 120 i, 60 px long
// The redraw: nodes of the meadow changed a frame at most.
const u32 meadow_budget = 15;
// Sizes, authored in tools/make_panel.py.
const i32 blossom_authored = 4, halo_authored = 12;   // px
const i32 blossom_most = 8 * unit;   // scale 2, the SDK's limit
const i32 halo_most = 18 * unit;

// x on the meadow's log axis for a log2 ratio (Q16), Q10 px: 88 px an octave.
inline i32 axis_x(i32 log2_ratio) { return 40 * unit + log2_ratio * 1408 / 1024; }
// The angle, tenths of a degree, of something turning `cycles` times every 90 s at time tau.
inline i32 cycle_tenths(u32 tau, u32 cycles) { return static_cast<i32>((tau * cycles % tau_wrap) * 3600u / tau_wrap); }
// Q10 to the nearest whole pixel.
inline i32 whole(i32 value) { return (value + unit / 2) & ~(unit - 1); }
// A radius, Q10 px, to quarter pixels.
inline i32 quarters(i32 value) { return (value + unit / 8) & ~(unit / 4 - 1); }
inline i32 distance(i32 a, i32 b) { return a < b ? b - a : a - b; }

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
    state.tick = 0;
    state.turn = 0;
    state.open = 0;
    state.pop = 0;
    state.serial = 0;
    state.track = 0;
    state.initialized = 0;
}

// The vowel's hills: a black mound where each formant falls on the meadow,
// rising out of the ground with the vowel's depth (the band below the horizon
// hides the rest), or hidden when vowel is off.
inline void hills(Frame& frame, const Controls& c, const Wander& w) {
    using namespace opxy::draw;
    if (!c.vowel_depth) {
        for (u32 h = 0; h < hill_count; ++h) hide(frame, hill_first + h);
        return;
    }
    i32 formant[2] = {0, 0};
    formants(w.vowel, formant[0], formant[1]);
    const i32 sunk = static_cast<i32>(((32768u - c.vowel_depth) * static_cast<u32>(hill_high * unit)) >> 15);
    for (u32 h = 0; h < hill_count; ++h)
        place(frame, hill_first + h, whole(axis_x(formant[h] - display_note)), whole(base * unit + sunk), unit, hill_shade, fill_and_stroke);
}

// Where a partial's nodes should be. x off the screen hides it; halo or far 0
// hides that part. Q10 px.
struct Plant {
    i32 x, y;
    i32 left, right;   // the blossom's x and its twin's
    i32 near, far;     // their radii
    i32 halo;          // the ring's radius
};
const i32 off_screen = opxy::draw::hidden_x * unit;

inline i32 blossom_scale(i32 radius) { return radius / blossom_authored; }
inline i32 halo_scale(i32 radius) { return radius / halo_authored; }
// How far a node is from where it should be, quarter pixels; 0 when it is there.
inline i32 gap(const opxy::draw::Node& node, i32 x, i32 y, i32 scale, i32 size) {
    using namespace opxy::draw;
    const bool away = x >= off_screen, was_away = node.x >= off_screen;
    if (away && was_away) return 0;
    if (away != was_away) return 16;
    // A blossom opening shows more than a stem shifting a little.
    const i32 sized = node.scale != scale ? 3 * distance(node.scale, scale) * size / unit + 1 : 0;
    const i32 moved = distance(node.x, x) + distance(node.y, y) + sized;
    return moved ? (moved >> 8) + 1 : 0;
}
inline i32 plant_gap(const Frame& frame, u32 k, const Plant& p, u32& nodes) {
    const u32 stem = partial_first + partial_nodes * k;
    const bool shown = p.x < off_screen;
    const i32 parts[4] = {
        gap(frame.nodes[stem], p.x, p.y, unit, 0),
        gap(frame.nodes[stem + 1], shown && p.halo ? p.x : off_screen, p.y, halo_scale(p.halo), halo_authored),
        gap(frame.nodes[stem + 2], shown ? p.left : off_screen, p.y, blossom_scale(p.near), blossom_authored),
        gap(frame.nodes[stem + 3], shown && p.far ? p.right : off_screen, p.y, blossom_scale(p.far), blossom_authored)};
    i32 total = 0;
    nodes = 0;
    for (u32 i = 0; i < 4; ++i) { total += parts[i]; if (parts[i]) ++nodes; }
    return total;
}
inline void plant_draw(Frame& frame, u32 k, const Plant& p) {
    using namespace opxy::draw;
    const u32 stem = partial_first + partial_nodes * k;
    if (p.x >= off_screen) {
        for (u32 i = 0; i < partial_nodes; ++i) hide(frame, stem + i);
        return;
    }
    place(frame, stem, p.x, p.y, unit, stem_shade, fill_and_stroke);
    if (p.halo) place(frame, stem + 1, p.x, p.y, halo_scale(p.halo), halo_shade, fill_and_stroke);
    else hide(frame, stem + 1);
    place(frame, stem + 2, p.left, p.y, blossom_scale(p.near), stem_shade, fill_and_stroke);
    if (p.far) place(frame, stem + 3, p.right, p.y, blossom_scale(p.far), stem_shade, fill_and_stroke);
    else hide(frame, stem + 3);
}

inline void display(DisplayState& state, const opxy::draw::Input& input, Frame& frame) {
    using namespace opxy::draw;
    const char* const titles[8] = {"Tilt", "Shape", "Stretch", "Vowel", "Breath", "Blur", "Haze", "Time"};
    char values[8][header_capacity];
    for (opxy::u32 i = 0; i < 8; ++i) caption(values[i], i, input.slots[i]);
    const opxy::u8 slots[2][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}};
    page_header(state.header, input, frame, titles, values, header_first, slots);

    // Notes: held opens the blossoms, each start swells them; a reset, a track
    // change or reduced motion shows the end state at once. With the track's amp
    // envelope known they open as the sound does: over the attack, to the
    // sustain's level while notes are held, closing over the release (amp.h).
    const NoteEvents events = state.notes.update(input);
    const bool held = input.held_notes > 0 && input.held_notes <= 128;
    const bool snap = !state.initialized || state.track != input.track || input.reduced_motion;
    const i32 elapsed = clamp(input.elapsed, 0, 102);
    const bool struck = !snap && (input.note_serial != state.serial || events.hold_started);
    const i32 opened = envelope_known(input.amp) ? amp::follow(state.amp, input.amp, held, struck, elapsed, snap) << 1
                                                 : held ? 65536 : 0;
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
    ++state.tick;

    // The drift moves only while the blossoms are open, slowing as they close.
    const Controls c = decode(input.slots);
    const bool frozen = bloom::held(input.slots[7]);   // time at hold: the scene stands still
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

    // The sun: tilt raises it as the sound brightens, from 18 px into the
    // ground to 40 px over it; the weather's lean, up to 4.5 dB an octave with
    // full breath, lifts and lowers it. It moves in whole pixels, in its turn.
    const u32 tilt = unit16(input.slots[0]), breath = unit16(input.slots[4]);
    if (snap || state.tick % sun_turns == 0) {
        const i32 lean = ((static_cast<i32>(breath >> 1) * (gust >> 1)) >> 19) * 18;   // Q10 px
        i32 sun_y = (base + 18) * unit - static_cast<i32>(tilt * 58u >> 6) - lean;
        if (sun_y < 130 * unit) sun_y = 130 * unit;   // its top stays 30 px under the header
        place(frame, sun_node, sun_x * unit, whole(sun_y), unit, sun_shade, fill_and_stroke);
    }

    hills(frame, c, wander(c, display_seed, state.time));

    // The meadow, where it should be.
    const i32 open_scale = 36045 + (29491 * state.open >> 16);   // 0.55 closed .. 1 open, Q16
    const i32 pop_scale = 65536 + (9830 * state.pop >> 16);      // up to 1.15 on a note start
    const u32 blur = blur_cents(input.slots[5]), haze = haze_cents(input.slots[6]);
    // Beats and shimmer while notes are held, even at hold, which stills the
    // drift but not the pairs' beating; the breeze stops at hold.
    const bool alive = state.open > 19661 && !input.reduced_motion, breezy = alive && !frozen;
    // A slow breeze nods the meadow a pixel or so for as long as notes are held,
    // at a pace set by time: from 8 s a sweep to 1.2 s.
    const u32 breeze = 11u + ((65535u - unit16(input.slots[7])) * 64u >> 16);
    Plant plants[partial_count];
    for (u32 k = 0; k < partial_count; ++k) {
        Plant& p = plants[k];
        const i32 x = axis_x(s.pitch[k]);
        if (x > 470 * unit) {
            p.x = off_screen;
            p.y = hidden_y * unit;
            p.left = p.right = off_screen;
            p.near = p.far = unit;
            p.halo = 0;
            continue;
        }
        // Height by the level in dB against the loudest, 128 px down to a 4 px
        // stub 54 dB (nine octaves of amplitude) below it, where a silent
        // partial (an even one at square) stays; size by its square root.
        const u32 relative = (static_cast<u32>(s.level[k]) << 15) / static_cast<u32>(top);
        u32 root_level = root(relative << 15);   // Q15
        if (root_level < 733) root_level = 733;
        const i32 octaves = relative ? log2_of(relative) - 15 * 65536 + 9 * 65536 : 0;   // above the floor, log2 Q16
        const i32 tall = octaves <= 0 ? 0 : octaves >= 9 * 65536 ? 128 * unit : octaves * 128 / 576;   // Q10 px
        i32 y = base * unit - 4 * unit - tall;
        if (breezy) y += (3 * sine(cycle_tenths(state.tau, breeze) + static_cast<i32>(k) * 170) / 2) * (state.open >> 6) >> 10;   // in as they open
        p.x = x;
        p.y = whole(y);
        const i32 radius = (((1638 + static_cast<i32>(root_level * 4506u >> 15)) * open_scale >> 16) * pop_scale) >> 16;
        // Blur: a halo as wide as the partial's spread (0.1 px a cent), shimmering while held.
        p.halo = 0;
        if (blur) {
            i32 r = radius + static_cast<i32>(blur * partial_reach(k) * 102u >> 8);
            if (alive) r += r * sine(cycle_tenths(state.tau, 45 + 7 * k) + static_cast<i32>(k) * 1291) >> 13;
            if (r > p.y - 38 * unit) r = p.y - 38 * unit;
            if (r > halo_most) r = halo_most;
            p.halo = clamp(quarters(r), unit / 4, halo_most);
        }
        // Haze: the pair half its detune either side, 0.15 px a cent, beating
        // (slowed down to be seen) while held.
        i32 near = radius, far = radius;
        if (haze) {
            const i32 apart = static_cast<i32>(haze * pair_spread(display_seed, k) * 154u >> 9);
            if (alive) {
                u32 beats = 9u + (haze * (k + 1) >> 2);   // per 90 s: 0.1 to 3 Hz
                if (beats > 270) beats = 270;
                const i32 swing = sine(cycle_tenths(state.tau, beats) + 900);   // Q10 cosine
                near = radius * (2048 + swing / 2) >> 11;
                far = radius * (2048 - swing / 2) >> 11;
            }
            p.left = x - apart;
            p.right = x + apart;
        } else {
            p.left = p.right = x;
            far = 0;
        }
        p.near = quarters(clamp(near, unit / 4, blossom_most));
        p.far = far ? quarters(clamp(far, unit / 4, blossom_most)) : 0;
    }

    // Redraw the partials furthest from there, within the budget, but first
    // the next in turn, so that none waits long. The first frame sets
    // everything; so does reduced motion, which jumps.
    if (snap) {
        for (u32 k = 0; k < partial_count; ++k) plant_draw(frame, k, plants[k]);
    } else {
        u32 budget = meadow_budget;
        bool done[partial_count] = {};
        bool turn = true;
        while (budget) {
            i32 furthest = 0;
            u32 pick = partial_count, cost = 0;
            for (u32 n = 0; n < partial_count; ++n) {
                const u32 k = (n + state.turn) % partial_count;
                if (done[k]) continue;
                u32 nodes;
                const i32 away = plant_gap(frame, k, plants[k], nodes);
                if (away > furthest && nodes <= budget) {
                    furthest = away;
                    pick = k;
                    cost = nodes;
                    if (turn) break;
                }
            }
            if (pick == partial_count) break;
            if (turn) { state.turn = (pick + 1) % partial_count; turn = false; }
            plant_draw(frame, pick, plants[pick]);
            done[pick] = true;
            budget -= cost;
        }
    }

    // The grey code: a status dot for each encoder at its control's travel,
    // on the page the encoders turn.
    for (u32 i = 0; i < dot_count; ++i) {
        const i32 travel = static_cast<i32>(unit16(input.parameters[i]) * static_cast<u32>(dot_travel) >> 6);   // Q10 px
        place(frame, dot_first + i, whole((dot_track + 120 * static_cast<i32>(i)) * unit + travel), dot_y * unit, unit,
              dot_shade + i, fill_and_stroke);
    }
}
}
