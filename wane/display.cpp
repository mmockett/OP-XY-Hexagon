#include "display.h"
#include "controls.h"

namespace wane {
// Header captions in the units each control plays: the shape's name, cents for
// the pitch controls, 0..100 amounts for warmth and age.
inline void caption_cents(char* out, opxy::i32 octaves) {
    const opxy::u32 tenths = controls::cent_tenths(octaves);
    char text[6];
    opxy::u32 at = 0;
    if (tenths == 0) {
        text[at++] = '0';
    } else if (tenths >= 100) {
        const opxy::u32 whole = (tenths + 5) / 10;
        if (whole >= 100) text[at++] = static_cast<char>('0' + whole / 100 % 10);
        if (whole >= 10) text[at++] = static_cast<char>('0' + whole / 10 % 10);
        text[at++] = static_cast<char>('0' + whole % 10);
    } else {
        text[at++] = static_cast<char>('0' + tenths / 10);
        text[at++] = '.';
        text[at++] = static_cast<char>('0' + tenths % 10);
    }
    text[at++] = 'c';
    text[at] = 0;
    opxy::draw::copy(out, opxy::draw::header_capacity, text);
}
inline const char* shape_name(opxy::i32 control) {
    const opxy::i32 s = control < 0 ? 0 : control >> 16;
    return s < 1311 ? "sine" : s < 6881 ? "s/tri" : s < 9503 ? "tri" : s < 15073 ? "t/saw"
         : s < 19005 ? "saw" : s < 25559 ? "s/sq" : s < 26870 ? "sq" : "pulse";
}

// The screen below the header: a compact cassette between the oscillator and a
// needle per held note (README, "Screen"; artwork from tools/make_panel.py).
// The tape runs only while notes are held: its reels turn, its ribbon carries
// wow, flutter, drags and sag past the head, the detuned twin slides along the
// oscillator and the needles sway with drift. Age wears the cassette, still.
// Integer arithmetic only.
namespace screen {
using namespace opxy::draw;
using opxy::i32;
using opxy::u32;

// Palette entries (panel.svg.fragment): the label's stripes at each age, the
// tape, the needles and pivots lit and unlit.
const u32 stripe_shades[3][3] = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}};
const u32 needle_dim = 10, needle_lit = 11, pivot_lit = 12, pivot_dim = 13, tape_bright = 14;

// Geometry in Q10 pixels (tools/make_panel.py --tables).
const i32 hub_x[2] = {186573, 304947}, hub_y = 118825;
const i32 tape_x[2] = {197296, 294224}, tape_y = 214082;
const i32 tape_first = -48464, tape_step = 3127;   // 63 places along the tape, 1.12 mm apart
const u32 tape_delay = 3300;                       // blocks between places, Q8: the tape moves 60 mm a second
const i32 tape_px = 2507;                          // 0.9 mm of wobble a unit (60 cents), Q10 pixels
// The pad pressing the tape out at the head, 0.35 mm at its middle.
const i32 head_bump[10] = {975, 901, 711, 480, 276, 136, 57, 21, 6, 2};
const i32 wave_left = -39 * unit, wave_width = 78 * unit, wave_height = 28 * unit;
const i32 needle_x = 393 * unit, needle_step = 10547, needle_y = 196 * unit;
// Each needle sways at two slow rates of its own (0.13 to 0.18 Hz and 0.37 to
// 0.47 Hz), as phase increments a block (the board's hash, through make_panel.py).
const u32 sway_rate[8] = {867304, 1040947, 986193, 952022, 895192, 1066928, 1044923, 967295};
const u32 sway_phase[8] = {2140011990u, 81597587u, 2429416905u, 2506433159u,
                           1645981844u, 3482342842u, 1397767466u, 1732450896u};
const u32 sway_rate_2[8] = {2589680, 2476021, 2819492, 2775483, 2620226, 2632494, 2631459, 2318633};
// The packs, mm squared Q16: the hub's 10.7 mm, and one tape (to 24.4 mm) shared
// between them, Q4 so a Q12 share stays in 32 bits.
const u32 hub_area = 7503217, tape_area = 7694;
const u32 progress_start = 22938, wound_most = 41607659;   // the board starts 35% through; at most 97%
const u32 long_ago = 0x40000000u;
// Age's wear, in the order make_panel.py draws it (its --tables): each piece
// shows while wear_from <= age < wear_until, age in percent as the header shows
// it. Most stay once shown; the label's two peeling corners are drawn in stages
// that replace one another.
const opxy::u8 wear_from[node::wears] = {
    46, 58, 68, 76, 84, 90, 95, 99, 52, 82, 52, 76, 68, 88, 74, 86, 54, 72, 88, 96,
    78, 78, 92, 16, 16, 16, 16, 38, 38, 38, 38, 60, 60, 60, 60, 82, 82, 82, 82, 66,
    66, 66, 66, 88, 88, 88, 88, 24, 44, 63, 80, 94, 12, 34, 50, 58, 70, 76, 86, 92,
    42, 64, 74, 82, 56, 88, 97, 4, 7, 9, 12, 14, 17, 20, 22, 25, 28, 30, 33, 35,
    38, 41, 43, 46, 48, 51, 54, 56, 59, 61, 64, 67, 69, 72, 74, 77, 80, 82, 85, 88,
    90, 93, 95, 40, 55, 68, 78, 86, 95};
const opxy::u8 wear_until[node::wears] = {
    101, 101, 101, 101, 101, 101, 101, 101, 101, 101, 101, 101, 101, 101, 101, 101, 101, 101, 101, 101,
    101, 101, 101, 38, 38, 38, 38, 60, 60, 60, 60, 82, 82, 82, 82, 101, 101, 101, 101, 88,
    88, 88, 88, 101, 101, 101, 101, 101, 101, 101, 101, 101, 101, 101, 101, 101, 101, 101, 101, 101,
    101, 101, 101, 101, 101, 101, 101, 101, 101, 101, 101, 101, 101, 101, 101, 101, 101, 101, 101, 101,
    101, 101, 101, 101, 101, 101, 101, 101, 101, 101, 101, 101, 101, 101, 101, 101, 101, 101, 101, 101,
    101, 101, 101, 101, 101, 101, 101, 101, 101};

inline i32 amount(i32 control) { return control < 0 ? 0 : control >> 16; }   // Q15

inline u32 root(u32 value) {
    u32 result = 0, bit = 1u << 30;
    while (bit > value) bit >>= 2;
    while (bit) {
        if (value >= result + bit) { value -= result + bit; result = (result >> 1) + bit; }
        else result >>= 1;
        bit >>= 2;
    }
    return result;
}

// The oscillator, Q15, at phase p (Q16): sine to triangle over the first
// quarter of shape, triangle to saw to half, saw to square to 80%, then the
// square narrowing to a 15% pulse. The sine is drawn at full height.
inline i32 wave(i32 shape, i32 p) {
    const i32 tri = p < 32768 ? (4 * p - 65536) >> 1 : (196608 - 4 * p) >> 1;
    const i32 saw = p - 32768, square = p < 32768 ? tape::one : -tape::one;
    if (shape < controls::shape_triangle) {
        const i32 sine = tape::sine((static_cast<u32>(p) << 16) - 0x40000000u);
        return sine + ((tri - sine) * (shape * 4) >> 15);
    }
    if (shape < controls::shape_saw) return tri + ((saw - tri) * ((shape - controls::shape_triangle) * 4) >> 15);
    if (shape < controls::shape_square) {
        const i32 w = (shape - controls::shape_saw) * 10 / 3;   // over 9830: 0..32767
        return saw + ((square - saw) * w >> 15);
    }
    const i32 width = 32768 - (shape - controls::shape_square) * 22938 / 6553;
    return p < width ? tape::one : -tape::one;
}

// Two cycles of it from `phase` (Q16), through warmth's one-pole lowpass run
// round twice to settle, as a ribbon: out along the top, back 0.6 px below.
inline void oscillator(Frame::path_type& path, i32 shape, i32 warmth, u32 phase) {
    i32 raw[32];
    for (u32 i = 0; i < 32; ++i) raw[i] = wave(shape, static_cast<i32>((i * 131072u / 31u + phase) & 0xffffu));
    const i32 k = tape::one - (warmth * 24576 >> 15);
    i32 y = raw[31];
    for (u32 pass = 0; pass < 2; ++pass)
        for (u32 i = 0; i < 32; ++i) {
            y += (raw[i] - y) * k >> 15;
            if (!pass) continue;
            const i32 x = wave_left + static_cast<i32>(i) * wave_width / 31, top = -(y * (wave_height >> 5) >> 10);
            path.points[i].x = x;
            path.points[i].y = top;
            path.points[63 - i].x = x;
            path.points[63 - i].y = top + 614;
        }
}

// What each tape control does to the tape's speed: the depths the audio plays
// (tape::depths, Q16 octaves), wow and its drags, flutter, and the sag at a
// start with its recovery.
typedef tape::Depths Amounts;

// The sag at tape time c, Q15: it rises at each note start and recovers as the
// audio's does (tape::sag); the last two starts cover the tape's history.
inline i32 sag_at(const DisplayState& s, const Amounts& a, u32 c) {
    if (static_cast<i32>(c - s.sag_last) >= 0) {
        const u32 gap = s.sag_last - s.sag_before;
        const i32 from = gap < 0x10000000u ? tape::sag(gap, 0, a.sag_rate) : 0;
        return tape::sag(c - s.sag_last, from, a.sag_rate);
    }
    if (static_cast<i32>(c - s.sag_before) >= 0) return tape::sag(c - s.sag_before, 0, a.sag_rate);
    return 0;
}

// The tape's speed off true at tape time c, Q15 units of 60 cents, using the
// same motions and depths the audio plays (tape.h); held to -3..2.3 units, so
// the tape rises no higher than the capstans and sinks no lower than the shell.
inline i32 wobble(const DisplayState& s, const Amounts& a, u32 c) {
    i32 w = 0;   // Q16 octaves
    if (a.wow) w += (tape::wow(c) * a.wow >> 15) - (tape::stretch(c) * a.stretch >> 15);
    if (a.flutter) w += tape::flutter(c) * a.flutter >> 15;
    if (a.sag) w -= sag_at(s, a, c) * a.sag >> 15;
    return clamp(w * 10, -3 * tape::one, 75366);   // 3277 is 60 cents
}

// The tape across the foot, in two halves of 32 points, out along its top and
// back 0.7 px below. The left end is now; further right, the tape played longer ago.
inline void tape_ribbon(Frame& frame, const DisplayState& s, const Amounts& a) {
    const u32 now = s.clock.blocks;
    for (u32 g = 0; g < 63; ++g) {
        const i32 w = wobble(s, a, now - (g * tape_delay >> 8));
        const i32 off = g > 31 ? g - 31 : 31 - g;
        const i32 y = (w * tape_px >> 15) + (off < 10 ? head_bump[off] : 0);
        for (u32 h = 0; h < 2; ++h) {
            if (g < 31 * h || g > 31 * h + 31) continue;
            const u32 i = g - 31 * h;
            Frame::path_type& path = frame.paths[node::tape + h];
            const i32 x = tape_first + static_cast<i32>(i) * tape_step;
            path.points[i].x = x;
            path.points[i].y = y;
            path.points[63 - i].x = x;
            path.points[63 - i].y = y + 717;
        }
    }
}
}

inline void display_reset(DisplayState& state) {
    state.header.reset();
    state.notes.reset();
    state.initialized = 0;   // the next frame re-baselines notes and redraws every outline
}

inline void display(DisplayState& state, const opxy::draw::Input& input, Frame& frame) {
    using namespace opxy::draw;
    using namespace controls;
    using namespace screen;
    const char* const titles[8] = {"Shape", "Detune", "Drift", "Warmth", "Wow", "Flutter", "Sag", "Age"};
    char values[8][header_capacity];
    const tape::Depths tape_depths = tape::depths(input.slots[WOW], input.slots[FLUTTER], input.slots[SAG]);
    copy(values[SHAPE], header_capacity, shape_name(input.slots[SHAPE]));
    const i32 detune_depth = tape::along(input.slots[DETUNE], detune_curve);
    const i32 drift_depth = tape::along(input.slots[DRIFT], drift_curve);
    caption_cents(values[DETUNE], detune_depth);
    caption_cents(values[DRIFT], drift_depth);
    header_amount(values[WARMTH], header_capacity, input.slots[WARMTH]);
    caption_cents(values[WOW], tape_depths.wow);
    caption_cents(values[FLUTTER], tape_depths.flutter);
    caption_cents(values[SAG], tape_depths.sag);
    header_amount(values[AGE], header_capacity, input.slots[AGE]);
    const opxy::u8 slots[2][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}};
    page_header(state.header, input, frame, titles, values, node::header, slots);

    const i32 shape = amount(input.slots[SHAPE]), warmth = amount(input.slots[WARMTH]);
    const i32 age = amount(input.slots[AGE]);
    const Amounts& amounts = tape_depths;

    // Notes: a needle lights for each held pitch (up to eight); each start
    // kicks its needle and sags the tape. A reset or a new track only takes a
    // baseline, and reduced motion leaves the kicks and sags out.
    const NoteEvents events = state.notes.update(input);
    const u32 held = input.held_notes <= 128 ? input.held_notes : 0;
    const u32 lit = held > 8 ? 8 : held;
    const bool reduced = input.reduced_motion != 0;
    const bool fresh = !state.initialized || state.track != input.track;
    u32& now = state.clock.blocks;
    if (fresh) {
        state.clock.rest = 0;
        state.hold = held ? unit : 0;
        state.wound = state.wound > wound_most ? wound_most : state.wound;
        for (u32 k = 0; k < 8; ++k) state.starts[k] = now - long_ago;
        state.sag_last = state.sag_before = now - long_ago;
    } else {
        const bool started = input.note_serial != state.serial || events.hold_started;
        const u32 at = reduced ? now - long_ago : now;
        for (u32 k = state.lit; k < lit; ++k) state.starts[k] = at;
        if (started && lit && lit <= state.lit) state.starts[lit - 1] = at;   // a pitch restruck, or one for another
        if (started) { state.sag_before = state.sag_last; state.sag_last = at; }
    }
    state.initialized = 1;
    state.track = input.track;
    state.serial = input.note_serial;
    state.lit = lit;

    // The tape's speed eases to running while notes are held and back to a
    // stop when they are released, settling within 0.15 s; it is still otherwise.
    const i32 target = held ? unit : 0;
    const i32 dt = clamp(input.elapsed, 0, 102);
    if (reduced) state.hold = 0;
    else if (state.hold != target) {
        state.hold = target + floor_q10((state.hold - target) *
                                        static_cast<i32>(page_header_detail::remaining(static_cast<u32>(dt)) >> 6));
        if (state.hold - target < 8 && target - state.hold < 8) state.hold = target;
    }
    const i32 run = dt * state.hold >> 10;   // Q10 seconds of tape this frame
    const bool moving = run > 0;
    const i32 speed = moving ? wobble(state, amounts, now) : 0;
    if (moving) {
        state.clock.advance(run);
        const i32 rate = clamp(unit + (speed * 307 >> 15), 0, 2 * unit);   // the reels follow the wobble
        state.turn = (state.turn + static_cast<u32>(run * 1089 * rate >> 10)) % (3600u * unit);   // 1.9 rad/s
        state.beat += static_cast<u32>(run * (655 + detune_depth * 15)) << 6;   // 0.01 Hz, and 0.9 Hz at 70 cents
        state.wound += static_cast<u32>(run) * 197u;                                  // 0.3% a second
        if (state.wound > wound_most) state.wound = wound_most;
    }

    // Left: the oscillator, its shape lowpassed by warmth, and the detuned
    // twin behind it, sliding along while the tape runs.
    {
        Frame::path_type& path = frame.paths[node::wave];
        const u32 key = tape::hash(static_cast<u32>(shape) << 16 ^ static_cast<u32>(warmth)) | 1u;
        if (fresh || path.key != key) { oscillator(path, shape, warmth, 0); path.key = key; }
        place(frame, node::wave, 51 * unit, 134 * unit, unit, 0, keep);
        Frame::path_type& twin = frame.paths[node::twin];
        const u32 twin_key = tape::hash(key ^ (state.beat >> 16) * 0x9e3779b9u) | 1u;
        if (fresh || twin.key != twin_key) { oscillator(twin, shape, warmth, state.beat >> 16); twin.key = twin_key; }
        if (detune_depth) place(frame, node::twin, 51 * unit, 134 * unit, unit, 0, keep);
        else hide(frame, node::twin);
    }

    // Right: the needles, lit for held notes, swaying with drift's depth and
    // kicking back as their notes start by the sag's, recovering as the tape does.
    for (u32 k = 0; k < 8; ++k) {
        const bool on = k < lit;
        i32 angle = 0;
        if (on && !reduced) {
            const i32 sway = (tape::sine(now * sway_rate[k] + sway_phase[k]) * 19661 +
                              tape::sine(now * sway_rate_2[k]) * 13107) >> 15;
            angle = (sway * drift_depth >> 15) * 49 >> 9;                      // tenths of a degree: 183 (0.32 rad) at 35 cents
            const u32 since = now - state.starts[k];
            const i32 kick = since < 0x10000000u ? tape::sag(since, 0, amounts.sag_rate) : 0;
            angle -= (kick * tape_depths.sag >> 15) * 21 >> 10;               // 201 (0.35 rad) at 180 cents
        }
        const i32 x = needle_x + static_cast<i32>(k) * needle_step;
        place(frame, node::needle + k, x, needle_y, unit, on ? needle_lit : needle_dim, fill_and_stroke);
        turn(frame, node::needle + k, rotation(angle));
        place(frame, node::pivot + k, x, needle_y, unit, on ? pivot_lit : pivot_dim, fill_and_stroke);
    }

    // The window: the packs share one tape, the played side growing as the
    // other shrinks (their areas summing to one tape's), their winding rings
    // shown within them, and the hubs' splines and clamps turning.
    const u32 progress = (progress_start + (state.wound >> 10)) >> 4;   // Q12
    for (u32 h = 0; h < 2; ++h) {
        const u32 radius = root(hub_area + tape_area * (h ? progress : 4096u - progress));   // Q8 mm
        place(frame, node::pack + h, hub_x[h], hub_y, static_cast<i32>(radius * 4u / 13u), 0, keep);
        for (u32 k = 0; k < node::rings; ++k) {
            if ((27392u + 4096u * (k + 1)) / 10u < radius) place(frame, node::ring + node::rings * h + k, 0, 0, unit, 0, keep);
            else hide(frame, node::ring + node::rings * h + k);
        }
        const Turn reel = rotation(static_cast<i32>(state.turn >> 10));
        for (u32 k = 0; k < node::teeth; ++k) {
            place(frame, node::tooth + node::teeth * h + k, hub_x[h], hub_y, unit, 0, keep);
            turn(frame, node::tooth + node::teeth * h + k, reel);
        }
    }

    // The label's stripes fade in three steps of age.
    const u32 worn = age < 11141 ? 0 : age < 21955 ? 1 : 2;
    for (u32 i = 0; i < 3; ++i) place(frame, node::stripe + i, 0, 0, unit, stripe_shades[worn][i], fill_and_stroke);

    // The tape at the head, its recent speed travelling along it.
    {
        const bool still = !amounts.wow && !amounts.flutter && !amounts.sag;
        const u32 key = (still ? 1u : tape::hash(now ^ static_cast<u32>(amounts.wow) << 1 ^
                         static_cast<u32>(amounts.flutter) << 11 ^ static_cast<u32>(amounts.sag) << 21 ^
                         state.sag_last * 0x85ebca6bu ^ state.sag_before * 0xc2b2ae35u) | 2u);
        if (fresh || frame.paths[node::tape].key != key || frame.paths[node::tape + 1].key != key) {
            tape_ribbon(frame, state, amounts);
            frame.paths[node::tape].key = frame.paths[node::tape + 1].key = key;
        }
        for (u32 h = 0; h < 2; ++h) place(frame, node::tape + h, tape_x[h], tape_y, unit, tape_bright, stroke);
    }

    // Age wears the cassette, still: dust and grime, scratches, scuffed and
    // chipped edges, the label's print rubbed and torn and its corners peeling.
    // Each piece is authored in place and shows or hides at once as age turns.
    const u32 percent = input.slots[AGE] < 0 ? 0 : ((static_cast<u32>(input.slots[AGE]) >> 10) * 100 + (1u << 20)) >> 21;
    for (u32 k = 0; k < node::wears; ++k) {
        if (wear_from[k] <= percent && percent < wear_until[k]) place(frame, node::wear + k, 0, 0, unit, 0, keep);
        else hide(frame, node::wear + k);
    }
}
}
