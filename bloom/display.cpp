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
// The board: each partial a stem and blossom over a low striped sun, placed
// across a log axis (partial 1 at x 30, 420 px for a ratio of 60) and as tall
// as the square root of its level. The levels and pitches are spectrum.h's
// own, for one display seed at C3, so tilt, shape, stretch, vowel, breath and
// the weather draw exactly as they sound. Behind the meadow the vowel's two
// formants rise as hills; blur rings each blossom with a halo as wide as its
// spread, and haze splits each blossom into its detuned pair.
//
// What it costs: the device redraws what changes. So everything moves in whole
// pixels and stepped sizes, and the frame persists between calls: each frame
// works out where the whole meadow should be, then redraws only the partials
// furthest from it, up to meadow_budget nodes (a change sweeps across the
// meadow in a few frames). The sun moves as one outline, pollen in turns.
using opxy::u8;
using opxy::u16;
using opxy::draw::unit;
const i32 base = 206;                   // the horizon
const u32 display_seed = 0x6b10b5edu;   // one meadow, the same every time
const i32 display_note = 1546838;       // C3's phase increment, log2 Q16: places the vowel
// Shades (palette entries; tools/make_panel.py lists the greys).
const u32 faint = 1, halo_grey = 2, sun_grey = 3, ground_grey = 4, rest_grey = 5, stem_grey = 6,
          frozen_stem = 9, pollen_grey = 11, frozen_ground = 13, white = 15;
// Blossoms from the lowest partial to the highest, in five bands.
const u8 blossom_ramp[5] = {14, 12, 10, 8, 7};
// Pollen: its speed in px per second (whole, so the 90 s wrap of tau is
// seamless), the partial it rises above (Q16 of those drawn) and its start, Q10 px.
const u16 mote_tip[mote_count] = {51264, 55967, 41932, 5240, 32639, 52997, 48392,
                                   5801, 28390, 64567, 59486, 31555, 23015, 32179};
const u8 mote_speed[mote_count] = {10, 15, 14, 13, 11, 16, 15, 13, 13, 13, 9, 14, 14, 12};
const u32 mote_start[mote_count] = {50593, 46255, 30951, 32160, 32058, 1222, 36393,
                                    37547, 24657, 52166, 20939, 25953, 14611, 60660};
const i32 mote_floor = base - 24;  // where it rises from, whatever the blossom below is doing
const u32 mote_turns = 4;          // each mote moves one frame in this many
const u32 tau_wrap = 90 * 1024;    // 90 s, Q10: a whole number of every mote's rises, beats and sways
// The hills: one outline for each formant, 68 px wide (the lift's reach either side).
const i32 hill_half = 34;          // px
const u32 hill_top = hill_points - 2;
// The redraw: nodes of the meadow changed a frame at most.
const u32 meadow_budget = 11;
const i32 stride_most = 6 * unit;
const u32 turned_frames = 15;      // a control counts as turning for this long, 0.25 s
// Sizes, authored in tools/make_panel.py.
const i32 blossom_authored = 4, halo_authored = 10;   // px
const i32 blossom_most = 5376;     // 5.25 px: with its stroke a blossom stays under 100 px^2
const i32 halo_most = 18 * unit;
// Fast drifts are drawn slower: past a 0.6 s period the screen shows 1/16 of the rest.
const u32 fastest_step = 2536;     // time_step at 600 ms

// x on the meadow's log axis for a log2 ratio (Q16), Q10 px: 420 / log2(60) px an octave.
inline i32 axis_x(i32 log2_ratio) { return 30 * unit + log2_ratio * 1138 / 1024; }
// The angle, tenths of a degree, of something turning `cycles` times every 90 s at time tau.
inline i32 cycle_tenths(u32 tau, u32 cycles) { return static_cast<i32>((tau * cycles % tau_wrap) * 3600u / tau_wrap); }
// Q10 to the nearest whole pixel, and to the middle of the nearest column of pixels.
inline i32 whole(i32 value) { return (value + unit / 2) & ~(unit - 1); }
inline i32 column(i32 value) { return (value & ~(unit - 1)) + unit / 2; }
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
    state.turned = 0;
    state.setting = 0;
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
        if (c.vowel_depth) place(frame, hill_first + h, whole(axis_x(formant[h] - display_note)), base * unit, unit, shade, stroke);
        else hide(frame, hill_first + h);
    }
}

// Where a partial's nodes should be. x off the screen hides it; halo or far 0
// hides that part. Positions whole pixels, sizes Q10 px.
struct Plant {
    i32 x, y;
    i32 left, right;   // the blossom's x and its twin's
    i32 near, far;     // their radii
    i32 halo;          // the ring's radius
    u32 grey;
};
const i32 off_screen = opxy::draw::hidden_x * unit;

inline i32 blossom_scale(i32 radius) { return radius / blossom_authored; }
inline i32 halo_scale(i32 radius) { return radius / halo_authored; }
// How far a node is from where it should be, quarter pixels; 0 when it is there.
inline i32 gap(const opxy::draw::Node& node, i32 x, i32 y, i32 scale, i32 size, u32 shade) {
    using namespace opxy::draw;
    const bool away = x >= off_screen, was_away = node.x >= off_screen;
    if (away && was_away) return 0;
    if (away != was_away) return 16;
    // A blossom opening or a grey changing shows more than a stem shifting a little.
    const i32 sized = node.scale != scale ? 3 * distance(node.scale, scale) * size / unit + 1 : 0;
    const i32 moved = distance(node.x, x) + distance(node.y, y) + sized;
    return (moved ? (moved >> 8) + 1 : 0) + (node.shade != shade ? 32 : 0);
}
inline u32 stem_shade(bool frozen) { return frozen ? frozen_stem : stem_grey; }
inline u32 halo_shade(bool frozen) { return frozen ? faint : halo_grey; }
inline i32 plant_gap(const Frame& frame, u32 k, const Plant& p, bool frozen, u32& nodes) {
    const u32 stem = partial_first + partial_nodes * k;
    const bool shown = p.x < off_screen;
    const i32 parts[4] = {
        gap(frame.nodes[stem], p.x, p.y, unit, 0, stem_shade(frozen)),
        gap(frame.nodes[stem + 1], shown && p.halo ? p.x : off_screen, p.y, halo_scale(p.halo), halo_authored, halo_shade(frozen)),
        gap(frame.nodes[stem + 2], shown ? p.left : off_screen, p.y, blossom_scale(p.near), blossom_authored, p.grey),
        gap(frame.nodes[stem + 3], shown && p.far ? p.right : off_screen, p.y, blossom_scale(p.far), blossom_authored, p.grey)};
    i32 total = 0;
    nodes = 0;
    for (u32 i = 0; i < 4; ++i) { total += parts[i]; if (parts[i]) ++nodes; }
    return total;
}
inline void plant_draw(Frame& frame, u32 k, const Plant& p, bool frozen) {
    using namespace opxy::draw;
    const u32 stem = partial_first + partial_nodes * k;
    if (p.x >= off_screen) {
        for (u32 i = 0; i < partial_nodes; ++i) hide(frame, stem + i);
        return;
    }
    place(frame, stem, p.x, p.y, unit, stem_shade(frozen), fill_and_stroke);
    if (p.halo) place(frame, stem + 1, p.x, p.y, halo_scale(p.halo), halo_shade(frozen), stroke);
    else hide(frame, stem + 1);
    place(frame, stem + 2, p.left, p.y, blossom_scale(p.near), p.grey, fill_and_stroke);
    if (p.far) place(frame, stem + 3, p.right, p.y, blossom_scale(p.far), p.grey, fill_and_stroke);
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
    ++state.tick;

    // A control turned just now? (A sum of the slots is enough to notice one moving.)
    u32 setting = 0;
    for (u32 i = 0; i < 8; ++i) setting += static_cast<u32>(input.slots[i]) * (2 * i + 1);
    if (setting != state.setting) state.turned = turned_frames;
    else if (state.turned) --state.turned;
    state.setting = setting;

    // The drift moves only while the blossoms are open, slowing as they close.
    const Controls c = decode(input.slots);
    const bool frozen = bloom::held(input.slots[7]);   // time at hold: the scene turns white and still
    if (!input.reduced_motion && state.open) {
        const u32 moving = static_cast<u32>(elapsed * state.open) >> 10;   // Q16 seconds
        // c.step a block, 689 blocks a second.
        const u32 step = c.step > fastest_step ? fastest_step + ((c.step - fastest_step) >> 4) : c.step;
        const u32 units = ((step * 689u) >> 8) * (moving >> 4) >> 4;
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
    // 4.5 dB an octave with full breath, lifts and lowers it. It eases there in
    // whole pixels, so a quick gust moves it gently.
    const u32 tilt = unit16(input.slots[0]), breath = unit16(input.slots[4]);
    const i32 lean = ((static_cast<i32>(breath >> 1) * (gust >> 1)) >> 19) * 18;   // Q10 px
    i32 sun_y = (base - 16) * unit - static_cast<i32>(tilt * 104u >> 6) - lean;
    if (sun_y < 74 * unit) sun_y = 74 * unit;   // its top stays below the header
    sun_y = whole(sun_y);
    if (!snap) {
        const i32 from = frame.nodes[sun_node].y, off = sun_y - from;
        const i32 stride = distance(sun_y, from) / 3 > unit ? distance(sun_y, from) / 3 & ~(unit - 1) : unit;
        sun_y = off > 0 ? from + (off < stride ? off : stride) : off < 0 ? from - (-off < stride ? -off : stride) : from;
    }
    place(frame, sun_node, 380 * unit + unit / 2, sun_y, unit, frozen ? faint : sun_grey, stroke);

    hills(frame, c, wander(c, display_seed, state.time), frozen ? frozen_ground : ground_grey);

    // The meadow, where it should be.
    const i32 open_scale = 36045 + (29491 * state.open >> 16);   // 0.55 closed .. 1 open, Q16
    const i32 pop_scale = 65536 + (9830 * state.pop >> 16);      // up to 1.15 on a note start
    const u32 blur = blur_cents(input.slots[5]), haze = haze_cents(input.slots[6]);
    const bool alive = state.open > 19661 && !frozen && !input.reduced_motion;   // beats and shimmer while notes are held
    // A slow breeze runs across the meadow for as long as notes are held, at a
    // pace set by time: from 8 s a sweep to 1.2 s.
    const u32 breeze = 11u + ((65535u - unit16(input.slots[7])) * 64u >> 16);
    Plant plants[partial_count];
    i32 tip_x[partial_count];
    u32 tips = 0;
    for (u32 k = 0; k < partial_count; ++k) {
        Plant& p = plants[k];
        const i32 x = axis_x(s.pitch[k]);
        if (x > 460 * unit) {
            p.x = off_screen;
            p.y = hidden_y * unit;
            p.left = p.right = off_screen;
            p.near = p.far = unit;
            p.halo = 0;
            p.grey = 0;
            continue;
        }
        // Height and size by the square root of the level against the loudest;
        // a silent partial (an even one at square) keeps a stub.
        const u32 relative = (static_cast<u32>(s.level[k]) << 15) / static_cast<u32>(top);
        u32 root_level = root(relative << 15);   // Q15
        if (root_level < 733) root_level = 733;
        i32 y = base * unit - 14 * unit - static_cast<i32>(132u * root_level >> 5);
        if (alive) y += 2 * sine(cycle_tenths(state.tau, breeze) + static_cast<i32>(k) * 170);
        p.x = column(x);
        p.y = whole(y);
        tip_x[tips++] = p.x;
        const i32 radius = (((1638 + static_cast<i32>(root_level * 4506u >> 15)) * open_scale >> 16) * pop_scale) >> 16;
        // Blur: a halo as wide as the partial's spread (0.1 px a cent), shimmering
        // while held. The crowded upper partials share theirs: every second, then every fourth.
        p.halo = 0;
        if (blur && !(k >= 16 ? (k + 1) & 3 : k >= 8 ? (k + 1) & 1 : 0)) {
            i32 r = radius + 1024 + static_cast<i32>(blur * partial_reach(k) * 102u >> 8);
            if (alive) r += r * sine(cycle_tenths(state.tau, 45 + 7 * k) + static_cast<i32>(k) * 1291) >> 13;
            if (r > p.y - 38 * unit) r = p.y - 38 * unit;
            if (r > halo_most) r = halo_most;
            p.halo = r < 3 * unit ? 3 * unit : quarters(r);
        }
        const u32 band = k * 5 / (partial_count - 1);
        p.grey = frozen ? white : state.open < 32768 ? rest_grey : blossom_ramp[band > 4 ? 4 : band];
        // Haze: the pair half its detune either side, 0.15 px a cent, plus a
        // pixel as its beating comes in over the first quarter of the control
        // (the most audible part), beating (slowed down to be seen) while held.
        // Under a pixel apart they are one.
        i32 near = radius, far = radius;
        const i32 apart = static_cast<i32>((haze * 154u + (c.haze_depth >> 4)) * pair_spread(display_seed, k) >> 9);
        if (apart >= unit / 2) {
            if (alive) {
                u32 beats = 9u + (haze * (k + 1) >> 2);   // per 90 s: 0.1 to 3 Hz
                if (beats > 270) beats = 270;
                const i32 swing = sine(cycle_tenths(state.tau, beats) + 900);   // Q10 cosine
                near = radius * (2048 + swing / 2) >> 11;
                far = radius * (2048 - swing / 2) >> 11;
            }
            p.left = column(x - apart);
            p.right = column(x + apart);
        } else {
            p.left = p.right = p.x;
            far = 0;
        }
        p.near = quarters(clamp(near, unit, blossom_most));
        p.far = far ? quarters(clamp(far, unit, blossom_most)) : 0;
        if (p.far && p.left == p.right) p.far = 0;
    }

    // Redraw the partials furthest from there, within the budget, but first
    // the next in turn, so that none waits long. The first frame sets
    // everything; so does reduced motion, which jumps.
    if (snap) {
        for (u32 k = 0; k < partial_count; ++k) plant_draw(frame, k, plants[k], frozen);
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
                const i32 away = plant_gap(frame, k, plants[k], frozen, nodes);
                if (away > furthest && nodes <= budget) {
                    furthest = away;
                    pick = k;
                    cost = nodes;
                    if (turn) break;
                }
            }
            if (pick == partial_count) break;
            if (turn) { state.turn = (pick + 1) % partial_count; turn = false; }
            // While only the drift moves it a stem climbs at most stride_most a
            // redraw, so a fast drift simmers rather than flickers. A turned
            // control doesn't wait.
            Plant& p = plants[pick];
            const i32 from = frame.nodes[partial_first + partial_nodes * pick].y;
            if (!state.turned && p.x < off_screen && frame.nodes[partial_first + partial_nodes * pick].x < off_screen)
                p.y = clamp(p.y, from - stride_most, from + stride_most);
            plant_draw(frame, pick, p, frozen);
            done[pick] = true;
            budget -= cost;
        }
    }

    // Pollen drifting up from the meadow as breath stirs it; a little whenever
    // a note is held. Each mote takes its turn to move.
    const u32 motes = state.open > 3277 && tips ? (frozen ? 3u : 2u + ((breath * 12u + 32768u) >> 16)) : 0u;
    for (u32 i = 0; i < mote_count; ++i) {
        const u32 node = mote_first + i;
        if (!snap && (state.tick + i) % mote_turns) continue;
        if (i >= motes) { hide(frame, node); continue; }
        const u32 tip = static_cast<u32>(mote_tip[i]) * tips >> 16;
        const i32 rise = static_cast<i32>((state.tau * mote_speed[i] + mote_start[i]) % tau_wrap);
        const i32 y = mote_floor * unit - rise;
        const i32 sway = 6 * sine(static_cast<i32>(state.tau * 720u >> 10) + static_cast<i32>(i) * 573);
        place(frame, node, whole(tip_x[tip] + sway), whole(y), unit, pollen_grey, fill_and_stroke);
    }
    place(frame, horizon_node, 16 * unit, base * unit, unit, frozen ? frozen_ground : ground_grey, fill_and_stroke);
}
}
