#include "display.h"

// Bend's screen, ported from its design board: on the left the owner's-manual
// figure, the input phase across and the output phase up, the straight map
// dashed and the bent map bright, and the cosine it is read through; on the
// right the two lines as a dot-matrix trace over two of line 1's cycles, line
// 2 the fainter twin at its interval; below, a dial for each held note's bend
// (the CZ's DCW, with its contour) and the eight waves, the chosen one lit.
// The maps and waves are pd.h's own, so the screen draws what the audio
// plays; the notes' contours and line 2's slide are recreated from elapsed
// time. Integer arithmetic only.
namespace bend {
namespace screen {
using namespace opxy::draw;
using opxy::i16;

// Geometry in pixels (the board's), Q10 where it is placed.
const i32 figure_size = 124;                               // 20..144 across, 54..178 down
const i32 figure_x = 82 * unit, figure_y = 116 * unit;     // its centre: the map outlines' origin
const i32 scope_half = 51;                                 // the scope's trace: +-51 px about y 116
const i32 dial_x = 27 * unit, dial_step = 16179, dial_y = 202 * unit;   // 15.8 px apart
const i32 key_x = 190 * unit, key_step = 35072, key_y = 192 * unit;     // 34.25 px apart
const i32 ribbon = 307;                                    // the map ribbon's half width, Q10 (stroked 0.6 px)
const u32 long_ago = 0x10000u;                             // blocks: a contour long settled
const u32 samples = 64, crossings = 15, outline = 32;

// Exponential eases as the share left after dt (Q10 s): Q16 octaves a Q10
// second for a time constant tau, 92.33 / tau.
const u32 tau_35 = 2638, tau_30 = 3078;
const i32 dots_step = 1024;   // the dots follow bend's contour in 1/32 steps
const u32 slices = 6;         // the dots redrawn in sixths, one a frame: all within 0.1 s
inline i32 left(i32 dt, u32 rate) { return pd::decay(static_cast<u32>(dt) * rate); }   // Q15 (32768 is all)
// Within `near` (a hundredth of the travel) of the target it arrives: every
// ease is there within 0.2 s.
inline i32 eased(i32 value, i32 target, i32 keep, i32 near) {
    const i32 d = value - target;
    const i32 next = target + (d >> 15) * keep + (((d & 32767) * keep) >> 15);
    return next - target < near && target - next < near ? target : next;
}

// A well-mixed 32-bit hash (lowbias32), and a jitter in -32768..32767.
inline u32 hash(u32 x) {
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
inline i32 jitter(u32 n, u32 roll) { return static_cast<i32>(hash(n * 0x9e3779b9u ^ roll * 0x85ebca6bu) >> 16) - 32768; }

// Each dial's breath rate, as a note's (pd::contour): 0.27 Hz and up to 0.06 Hz more.
inline u32 breath(u32 k) { return pd::breath_slowest + ((hash(k + 4) >> 16) * pd::breath_spread >> 16); }

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

// ---- Captions -----------------------------------------------------------------
inline void put_number(char*& at, u32 value) {
    char digits[10];
    u32 n = 0;
    do { digits[n++] = static_cast<char>('0' + value % 10); value /= 10; } while (value && n < 10);
    while (n) *at++ = digits[--n];
}
// detune in cents: one decimal below 10 ("2.5c"), whole from there ("23c").
inline void caption_cents(char (&out)[header_capacity], u32 tenths) {
    char text[header_capacity];
    char* at = text;
    if (tenths < 100) { put_number(at, tenths / 10); *at++ = '.'; put_number(at, tenths % 10); }
    else put_number(at, (tenths + 5) / 10);
    *at++ = 'c';
    *at = 0;
    copy(out, header_capacity, text);
}
// lofi's rate in kHz: one decimal below 10 ("5.8k"), whole from there ("23k").
inline void caption_rate(char (&out)[header_capacity], u32 tens) {
    char text[header_capacity];
    char* at = text;
    const u32 hundreds = (tens + 5) / 10;
    if (hundreds < 100) { put_number(at, hundreds / 10); *at++ = '.'; put_number(at, hundreds % 10); }
    else put_number(at, (tens + 50) / 100);
    *at++ = 'k';
    *at = 0;
    copy(out, header_capacity, text);
}

// ---- The figure -----------------------------------------------------------------
// The map's output phase, unwrapped, Q24 cycles (the audio's wraps, Q32).
inline i32 unwrapped(const pd::Line& l, i32 x) {
    if (!l.knots) return static_cast<i32>((l.r >> 8) * static_cast<u32>(x));
    u32 p = (l.b >> 8) * static_cast<u32>(x);
    for (u32 i = 0; i < 3; ++i) {
        const u32 c = static_cast<u32>(l.c[i] >> 8);
        p += c * static_cast<u32>(pd::magnitude(x - l.k[i])) - c * static_cast<u32>(l.k[i]);
    }
    return static_cast<i32>(p);
}
struct Point { i32 x, y; u32 keep; };   // Q12 of the figure: input phase across, output phase up
inline i32 area(const Point& a, const Point& b, const Point& c) {
    const i32 s = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
    return s < 0 ? -s : s;
}
// The map as at most 32 points: the output phase wrapped to one cycle, each
// wrap a vertical drop, noise's jitter on the samples between, simplified by
// least area (the board's mapPoints).
inline u32 map_points(Point (&kept)[outline], const pd::Line& l, i32 noise, u32 roll) {
    Point p[samples + 1 + 2 * crossings];
    u32 n = 0, wraps = 0;
    i32 last_x = 0, last_v = 0;
    const i32 shake = (noise * 287) >> 15;   // 0.07 of a cycle at full, Q12
    for (u32 i = 0; i <= samples; ++i) {
        const i32 x = static_cast<i32>(i * 65535u / samples);
        const i32 v = unwrapped(l, l.skew ? pd::skewed(x, l.skew) : x);
        if (i) {
            // Each whole cycle crossed between the samples, where it is crossed.
            const i32 rise = v - last_v;
            const i32 per = (rise < 0 ? -rise : rise) >> 12;   // Q12 cycles across the step
            for (i32 k = (rise > 0 ? last_v : v) >> 24; per && wraps < crossings;) {
                ++k;
                const i32 at = k << 24;
                if (at >= (rise > 0 ? v : last_v)) break;
                if (k < 1) continue;
                const i32 t = ((rise > 0 ? at - last_v : last_v - at) >> 12) * 4096 / per;   // Q12 along the step
                const i32 px = (last_x + (((x - last_x) * t) >> 12)) >> 4;
                p[n].x = px; p[n].y = rise > 0 ? 4096 : 0; p[n].keep = 1; ++n;
                p[n].x = px; p[n].y = rise > 0 ? 0 : 4096; p[n].keep = 1; ++n;
                ++wraps;
            }
        }
        i32 y = v <= 0 ? 0 : (v & 0xffffff) ? (v & 0xffffff) >> 12 : 4096;
        if (shake && i && i < samples) y = pd::clamp(y + ((shake * jitter(i * 3 + 1, roll)) >> 15), 0, 4096);
        p[n].x = x >> 4; p[n].y = y; p[n].keep = i == 0 || i == samples; ++n;
        last_x = x;
        last_v = v;
    }
    while (n > outline) {
        u32 best = 0;
        i32 least = 0x7fffffff;
        for (u32 i = 1; i + 1 < n; ++i) {
            if (p[i].keep) continue;
            const i32 a = area(p[i - 1], p[i], p[i + 1]);
            if (a < least) { least = a; best = i; }
        }
        if (!best) break;
        for (u32 i = best; i + 1 < n; ++i) p[i] = p[i + 1];
        --n;
    }
    if (n > outline) n = outline;
    for (u32 i = 0; i < n; ++i) kept[i] = p[i];
    return n;
}
// A unit vector along (x, y) (Q10 px), Q12; (0, 0) if there is none.
inline void unit_of(i32 x, i32 y, i32& ux, i32& uy) {
    const i32 sx = x >> 4, sy = y >> 4;
    const u32 length = root(static_cast<u32>(sx * sx + sy * sy));
    if (!length) { ux = uy = 0; return; }
    ux = sx * 4096 / static_cast<i32>(length);
    uy = sy * 4096 / static_cast<i32>(length);
}
// The points as a closed ribbon about the figure's centre: each offset both
// ways along its mitred normal, the mitre held at sharp turns, out along one
// edge and back along the other (the board's ribbon). Every point distinct.
inline void map_ribbon(Frame::path_type& path, const Point (&q)[outline], u32 n) {
    Point px[outline];
    for (u32 i = 0; i < n; ++i) {
        px[i].x = q[i].x * figure_size / 4 - figure_size * 512;   // Q12 of 124 px about the centre, Q10
        px[i].y = figure_size * 512 - q[i].y * figure_size / 4;
    }
    for (u32 i = 0; i < outline; ++i) {
        const u32 j = i < n ? i : n - 1;
        const Point& a = px[j ? j - 1 : 0];
        const Point& b = px[j];
        const Point& c = px[j + 1 < n ? j + 1 : n - 1];
        i32 t1x, t1y, t2x, t2y;
        unit_of(b.x - a.x, b.y - a.y, t1x, t1y);
        unit_of(c.x - b.x, c.y - b.y, t2x, t2y);
        if (!t1x && !t1y) { t1x = t2x; t1y = t2y; }
        if (!t2x && !t2y) { t2x = t1x; t2y = t1y; }
        i32 mx, my;
        unit_of((-t1y - t2y) * 16, (t1x + t2x) * 16, mx, my);
        if (!mx && !my) { mx = -t1y; my = t1x; }
        i32 along = (mx * -t1y + my * t1x) >> 12;
        if (along < 2048) along = 2048;
        const i32 reach = ribbon * 4096 / along;
        const i32 ox = (mx * reach) >> 12, oy = (my * reach) >> 12;
        // Past the map's last point (never, in practice) the ribbon runs on a pixel a point.
        const i32 extra = static_cast<i32>(i - j) * unit;
        path.points[i].x = b.x + ox + ((t1x * extra) >> 12);
        path.points[i].y = b.y + oy + ((t1y * extra) >> 12);
        path.points[2 * outline - 1 - i].x = b.x - ox + ((t1x * extra) >> 12);
        path.points[2 * outline - 1 - i].y = b.y - oy + ((t1y * extra) >> 12);
    }
    for (u32 i = 1; i < 2 * outline; ++i) {
        bool moved = true;
        while (moved) {
            moved = false;
            for (u32 k = 0; k < i; ++k)
                if (path.points[i].x == path.points[k].x && path.points[i].y == path.points[k].y) {
                    ++path.points[i].x;
                    moved = true;
                    break;
                }
        }
    }
}
inline void draw_map(Frame::path_type& path, u32 wave, i32 d, i32 warp, i32 noise, u32 roll) {
    pd::Line l;
    pd::shape(l, wave, d, warp);
    Point q[outline];
    map_ribbon(path, q, map_points(q, l, noise, roll));
}
}

inline void display_reset(DisplayState& state) {
    state.header.reset();
    state.notes.reset();
    state.initialized = 0;   // the next frame takes a baseline and draws everything afresh
}

inline void display(DisplayState& state, const opxy::draw::Input& input, Frame& frame) {
    using namespace opxy::draw;
    using namespace screen;
    const i32* slot = input.slots;

    // The header, in the units the board writes.
    const char* const titles[8] = {"wave", "bend", "detune", "ring", "interval", "warp", "noise", "lofi"};
    char values[8][header_capacity];
    const u32 wave = pd::choice(slot[WAVE], pd::waves), interval = pd::choice(slot[INTERVAL], pd::intervals);
    copy(values[WAVE], header_capacity, pd::wave_names[wave]);
    header_amount(values[BEND], header_capacity, slot[BEND]);
    caption_cents(values[DETUNE], pd::detune_tenths(slot[DETUNE]));
    header_amount(values[RING], header_capacity, slot[RING]);
    copy(values[INTERVAL], header_capacity, pd::interval_names[interval]);
    header_amount(values[WARP], header_capacity, slot[WARP]);
    header_amount(values[NOISE], header_capacity, slot[NOISE]);
    caption_rate(values[LOFI], pd::lofi_rate_tens(slot[LOFI]));
    const opxy::u8 pages[2][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}};
    page_header(state.header, input, frame, titles, values, node::header, pages);

    // Time, and the notes: a dial lights for each held pitch (up to eight),
    // its contour starting with its note; a note played again restarts the
    // newest. A reset or a new track only takes a baseline.
    const NoteEvents events = state.notes.update(input);
    const u32 held = input.held_notes <= 128 ? (input.held_notes > 8 ? 8 : input.held_notes) : 0;
    const bool reduced = input.reduced_motion != 0;
    const bool fresh = !state.initialized || state.track != input.track;
    const i32 dt = fresh ? 0 : clamp(input.elapsed, 0, 102);
    state.rest += static_cast<u32>(dt) * 11025u;   // 689.0625 audio blocks a second
    state.blocks += state.rest >> 14;
    state.rest &= 16383u;
    const u32 now = state.blocks;
    if (fresh) {
        for (u32 k = 0; k < 8; ++k) state.start[k] = now - long_ago;
        state.newest = held ? held - 1 : 0;
    } else {
        const bool started = input.note_serial != state.serial || events.hold_started;
        for (u32 k = state.held; k < held; ++k) state.start[k] = now;
        if (started && held && held <= state.held) state.start[held - 1] = now;
        if (held) state.newest = held - 1;
    }
    state.held = held;
    state.serial = input.note_serial;
    state.track = input.track;

    // Turns and notes ease out within 0.15 s; reduced motion and resets snap.
    const bool snap = fresh || reduced;
    const i32 controls = snap ? 0 : left(dt, tau_35);
    bool turning = false;   // a control still easing: the dots follow every frame
    for (u32 i = 0; i < 8; ++i) {
        const i32 target = pd::q15(slot[i]), was = state.value[i];
        state.value[i] = snap ? target : eased(state.value[i], target, controls, 328);
        turning = turning || state.value[i] != was;
    }
    for (u32 k = 0; k < 8; ++k) {
        const i32 target = k < held ? 32768 : 0;
        state.lit[k] = snap ? target : eased(state.lit[k], target, left(dt, k < held ? tau_30 : tau_35), 328);
    }
    const i32 octaves = pd::interval_octaves[interval];
    turning = turning || state.octaves != octaves;
    state.octaves = snap ? octaves : eased(state.octaves, octaves, controls, 655);
    if (fresh || state.wave != wave) {
        state.previous = fresh ? wave : state.wave;
        state.wave = wave;
        state.fade = fresh ? 32768 : 0;
    }
    state.fade = reduced ? 32768 : pd::clamp(state.fade + dt * 32768 / 154, 0, 32768);   // 0.15 s
    const i32 key = static_cast<i32>(wave) << 12;
    state.key = snap ? key : eased(state.key, key, left(dt, tau_35), 256);
    const i32 bend = state.value[BEND], warp = state.value[WARP], noise = state.value[NOISE];
    const i32 ring = state.value[RING], lofi = state.value[LOFI];

    // Each dial: bend, and while its note is held its contour's share of what
    // bend leaves (none with reduced motion), turned to the nearest degree.
    i32 bent[8];
    for (u32 k = 0; k < 8; ++k) {
        const i32 contour = reduced ? 0 : (pd::contour(now - state.start[k], 0, breath(k)) * (state.lit[k] >> 1)) >> 14;
        bent[k] = pd::bent(bend, contour);
        const bool on = k < held || state.lit[k] > 1638;
        place(frame, node::needle + k, dial_x + static_cast<i32>(k) * dial_step, dial_y, unit,
              on ? shade::needle_lit : shade::needle_dim, fill_and_stroke);
        turn(frame, node::needle + k, rotation((-135 + ((270 * bent[k] + 16384) >> 15)) * 10));
    }
    const i32 d = bent[state.newest];

    // Line 2 slides against line 1 at a quarter of detune's beat (slowed to be
    // seen), in 1/24 cycle steps, and the noise rolls 4 times a second, only
    // while a note sounds.
    const i32 sounding = state.lit[state.newest];
    if (!reduced && sounding) {
        const u32 tenths = pd::detune_tenths(slot[DETUNE]);
        state.beat += ((tenths * 6291u * static_cast<u32>(dt)) >> 15) * static_cast<u32>(sounding);
        if (sounding > 655) state.roll = now * 4u / 689u;
    }
    const u32 beat = (state.beat / 178956971u) * 178956971u;   // whole 24ths of a cycle

    // Left: the bent map (to 1/256 of bend), a new wave fading in over the last.
    {
        const i32 drawn_d = d & ~127;
        const u32 drawn = hash(static_cast<u32>(drawn_d) ^ static_cast<u32>(warp) << 15 ^ static_cast<u32>(noise) * 0x2545f491u ^
                               (noise ? state.roll * 0x68e31da4u : 0u) ^ wave << 28 ^ state.previous << 24 ^
                               static_cast<u32>(state.fade < 32768)) | 1u;
        if (fresh || state.map_key != drawn) {
            draw_map(frame.paths[node::map], wave, drawn_d, warp, noise, state.roll);
            frame.paths[node::map].key = drawn;
            if (state.fade < 32768) {
                draw_map(frame.paths[node::fading], state.previous, drawn_d, warp, noise, state.roll);
                frame.paths[node::fading].key = drawn ^ 2u;
            } else if (fresh) {
                frame.paths[node::fading] = frame.paths[node::map];
                frame.paths[node::fading].key = drawn ^ 2u;
            }
            state.map_key = drawn;
        }
        const u32 grey = static_cast<u32>((state.fade * 6 + 16384) >> 15);
        place(frame, node::map, figure_x, figure_y, unit, shade::map_dim + grey, stroke);
        if (state.fade < 32768) place(frame, node::fading, figure_x, figure_y, unit, shade::map_dim + 6 - grey, stroke);
        else hide(frame, node::fading);
    }

    // Right: the two lines as dots over two of line 1's cycles, line 2 becoming
    // the ring product as ring rises. lofi holds and steps the dots as its DAC
    // would; noise jitters their phases by its depth. Each dot sits on a whole
    // pixel. While a control eases (and on a reset, or with reduced motion)
    // every dot follows each frame; while notes alone move the scene a sixth of
    // the dots is redrawn a frame, so a frame changes only the dots that moved.
    {
        const bool all = snap || turning;
        if (all || d - state.dots_bend >= dots_step || state.dots_bend - d >= dots_step) state.dots_bend = d;
        pd::Line one;
        pd::shape(one, wave, state.dots_bend, warp);
        const i32 shake = pd::noise_depth(noise << 16);   // +-the noise's rms, Q16 cycles
        const bool coarse = lofi > 327;
        const u32 rate = pd::lofi_rate_tens(lofi << 16);               // tens of Hz: rate / 22 samples an A3 cycle
        const u32 bits = 16u - pd::lofi_bits(lofi << 16);             // the DAC's step, a power of two in Q15
        const i32 whole = state.octaves >> 16;                          // line 2's pitch against line 1, Q16
        const u32 ratio = static_cast<u32>(pd::exp2_fraction(static_cast<u32>(state.octaves & 0xffff)) >> 13);
        const u32 twin_ratio = whole >= 0 ? ratio << whole : ratio >> -whole;
        const u32 offset = (static_cast<u32>(state.value[DETUNE]) * 3932u) << 1;   // 0.06 of a cycle at full detune, Q32
        const u32 ink2 = ring > 16384 ? shade::twin_ring : shade::twin;
        const u32 slice = state.turn++ % slices;
        for (u32 i = all ? 0 : slice; i < node::dots + node::twins; i += all ? 1 : slices) {
            const bool first = i < node::dots;
            const u32 k = first ? i : i - node::dots, count = first ? node::dots : node::twins;
            u32 c = k * 131072u / (count - 1);   // Q16 cycles of line 1
            if (coarse) c = (c * (rate >> 2) / (22u * 16384u)) * 22u * 65536u / rate;
            const u32 shake1 = static_cast<u32>((shake * jitter(k * 13 + (first ? 7 : 11), state.roll)) >> 15) << 16;
            i32 v = pd::output(one, wave, static_cast<i32>(c & 0xffffu), shake1);
            if (!first) {
                const u32 phase = c * twin_ratio + beat + offset;
                const u32 shake2 = static_cast<u32>((shake * jitter(k * 29 + 3, state.roll ^ 0x5bd1e995u)) >> 15) << 16;
                const i32 o2 = pd::output(one, wave, static_cast<i32>(phase >> 16), shake2);
                v = o2 + (((((o2 * v) >> 15) - o2) * ring) >> 15);
            }
            if (coarse) v = ((v + ((1 << bits) >> 1)) >> bits) << bits;
            // Authored on the zero line at its place across: only its height moves, in whole pixels.
            const i32 rise = (v * scope_half + 16384) >> 15;
            place(frame, (first ? node::dot : node::twin) + k, 0, -rise * unit, unit, first ? shade::dot : ink2, fill_and_stroke);
        }
        // Line 2's ink follows ring at once.
        if (!all)
            for (u32 k = 0; k < node::twins; ++k) opxy::draw::shade(frame, node::twin + k, ink2, fill_and_stroke);
    }

    // Below: the eight waves, the chosen one lit on its key.
    for (u32 k = 0; k < 8; ++k)
        place(frame, node::icon + k, 0, 0, unit, k == wave ? shade::icon_lit : shade::icon_dim, fill_and_stroke);
    place(frame, node::key, key_x + ((state.key * key_step) >> 12), key_y, unit, 0, keep);
    state.initialized = 1;
}
}
