#include "display.h"

// Orbit's screen, the design board's (Orbit.dc.html) at the OP-XY's size.
// Left, the vector's field: the gate with each corner's wave beside it, the
// orbit dotted round its centre, and a point for each held note travelling
// it from the top, the newest with a ring and a spoke from the centre. Right,
// a scope: the wave the newest point mixes, held in steps by grit, its
// detuned twin drifting behind it, and each corner's share as pips.
// The screen cannot hear the voices: it moves each note's point from the
// controls and the time since the note started, as the audio does
// (vector.h). Every turn eases in about 0.15 s; it is still when nothing
// plays. Integer arithmetic only.
namespace orbit {
namespace screen {
using namespace opxy::draw;

const i32 field_x = 122 * unit, field_y = 129 * unit;          // the gate's centre; its corners lie 64 px out
const i32 spot_x[4] = {122, 214, 122, 30}, spot_y[4] = {52, 129, 206, 129};   // each corner's wave
const i32 scope_x = 364 * unit, scope_y = 112 * unit;          // the scope's outlines' origin (their middle)
const i32 scope_scale = 1659;                                   // 1.62: they are authored at 1/1.62
// Authored sizes (tools/make_panel.py): each radius as drawn, its stroke included.
const i32 trail_r = 1254, point_r = 2790, ring_r = 4096, ping_r = 7168;   // Q10 px
const u32 long_ago = 1u << 20;   // Q10 s: a note older than any motion

inline i32 shade_of(i32 grey_q10) { return clamp((grey_q10 + 512) >> 10, 0, 15); }   // grey(i), rounded

// A field point (Q14) on the screen, Q10 px.
inline i32 screen_x(const Point& p) { return field_x + p.x * 4; }
inline i32 screen_y(const Point& p) { return field_y + p.y * 4; }

// value eased toward target by `keep` (Q16: the share of the distance left).
inline i32 ease(i32 value, i32 target, u32 keep) {
    return target + ((value - target) * static_cast<i32>(keep >> 1) >> 15);
}

// e^(-t / 0.05) for t Q10 seconds, Q10: a new point's pop.
inline i32 pop_decay(i32 t) {
    const i32 octaves = t * 1847;   // t / 0.05 / ln 2, Q16 octaves
    return octaves >= 16 * 65536 ? 0 : static_cast<i32>(exp2_q16(26 * 65536 - octaves) >> 16);
}

// The mixed wave at phase p (Q16 cycles) for weights w (Q14), crossfading
// from the old bank as the new grows in: Q14, from the board's 64 samples.
struct Mixer {
    const signed char* now[4];
    const signed char* was[4];
    i32 w[4], change;
    i32 at(u32 p) const {
        const u32 i = (p >> 10) & 63u, j = (i + 1) & 63u;
        const i32 f = static_cast<i32>(p & 1023u);
        i32 y = 0, z = 0;
        for (u32 k = 0; k < 4; ++k) {
            y += w[k] * ((now[k][i] * (1024 - f) + now[k][j] * f) >> 3);
            if (change < 32767) z += w[k] * ((was[k][i] * (1024 - f) + was[k][j] * f) >> 3);
        }
        // Q14 x Q7 x Q7: Q28; to Q14, a sample of 127 at full weight is one.
        y = y >> 14;
        z = z >> 14;
        const i32 mixed = change < 32767 ? z + static_cast<i32>((static_cast<i32>(y - z) >> 1) * change >> 14) : y;
        return mixed * 129 >> 7;   // 127 is one
    }
};

// A ribbon (the board's thin line): the points out, then back along each
// point's normal by h (Q10), into an outline of 2n points.
inline void ribbon(const opxy::draw::Point* top, u32 n, i32 h, opxy::draw::Point* out) {
    for (u32 i = 0; i < n; ++i) {
        out[i] = top[i];
        const opxy::draw::Point& a = top[i ? i - 1 : 0];
        const opxy::draw::Point& b = top[i + 1 < n ? i + 1 : n - 1];
        const i32 tx = (b.x - a.x) >> 4, ty = (b.y - a.y) >> 4;
        const i32 length = static_cast<i32>(root(static_cast<u32>(tx * tx + ty * ty)));
        opxy::draw::Point& back = out[2 * n - 1 - i];
        back.x = top[i].x - (length ? ty * h / length : 0);
        back.y = top[i].y + (length ? tx * h / length : h);
    }
}
// Sets an outline when its points change.
template <class P>
inline void outline(P& path, const opxy::draw::Point* points) {
    bool same = true;
    for (u32 i = 0; i < Frame::point_count; ++i)
        same = same && path.points[i].x == points[i].x && path.points[i].y == points[i].y;
    if (same && path.key) return;
    for (u32 i = 0; i < Frame::point_count; ++i) path.points[i] = points[i];
    path.key = path.key + 1 ? path.key + 1 : 1;
}
inline void dot(Frame& frame, u32 node, const Point& p, i32 scale, i32 grey, Paint paint) {
    if (scale < 16 || (paint == stroke && grey < 512)) { hide(frame, node); return; }
    place(frame, node, screen_x(p), screen_y(p), clamp(scale, 16, 2 * unit), static_cast<u32>(shade_of(grey)), paint);
}
}

inline void display_reset(DisplayState& state) {
    state.header.reset();
    state.notes.reset();
    state.initialized = 0;   // the next frame takes a baseline and shows the end state
}

inline void display(DisplayState& state, const opxy::draw::Input& input, Frame& frame) {
    using namespace opxy::draw;
    using namespace screen;
    const i32* slots = input.slots;

    // The header, in the controls' units, as the board writes them.
    const char* const titles[8] = {"Bank", "X", "Y", "Orbit", "Rate", "Path", "Grit", "Detune"};
    char values[8][header_capacity];
    copy(values[BANK], header_capacity, bank_name(bank_of(slots[BANK])));
    caption_axis(values[X], slots[X]);
    caption_axis(values[Y], slots[Y]);
    header_amount(values[ORBIT], header_capacity, slots[ORBIT]);
    caption_rate(values[RATE], slots[RATE]);
    copy(values[PATH], header_capacity, path_name(slots[PATH]));
    caption_grit(values[GRIT], slots[GRIT]);
    caption_detune(values[DETUNE], slots[DETUNE]);
    const opxy::u8 pages[2][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}};
    page_header(state.header, input, frame, titles, values, node::header, pages);

    // Time, and how much of each ease is left this frame.
    const NoteEvents events = state.notes.update(input);
    const bool fresh = !state.initialized || state.track != input.track;
    const bool reduced = input.reduced_motion != 0;
    const i32 dt = fresh ? 0 : clamp(input.elapsed, 0, 102);
    const u32 keep = fresh || reduced ? 0u : page_header_detail::remaining(static_cast<u32>(dt));
    const u32 held = input.held_notes <= 128 ? input.held_notes : 0;
    const u32 lit_count = held > 8 ? 8 : held;

    // The controls as drawn, each easing to where it is set.
    for (u32 k = 0; k < 8; ++k) {
        const i32 target = q15(slots[k]);
        state.eased[k] = fresh ? target : ease(state.eased[k], target, keep);
        if (state.eased[k] - target < 8 && target - state.eased[k] < 8) state.eased[k] = target;
    }
    const u32 bank = bank_of(slots[BANK]);
    if (fresh) {
        state.bank = state.from = bank;
        state.change = 32767;
    } else if (bank != state.bank) {
        state.from = state.bank;
        state.bank = bank;
        state.change = 0;
    }
    state.change = keep ? ease(state.change, 32767, keep) : 32767;
    if (state.change > 32700) state.change = 32767;
    i32 c[8];
    for (u32 k = 0; k < 8; ++k) c[k] = state.eased[k] << 16;
    const u32 hundredths = rate_hundredths(c[RATE]);

    // The notes: a point for each held one (up to eight), starting at the top
    // of the path; released, it coasts to a stop and fades. A new start of a
    // pitch already held restarts the newest point.
    const bool started = !fresh && (input.note_serial != state.serial || events.hold_started);
    for (u32 k = 0; k < 8; ++k) {
        Note& v = state.voices[k];
        const bool on = k < lit_count;
        if (fresh) {
            v.on = on;
            v.level = on ? unit : 0;
            v.age = static_cast<i32>(long_ago);
            v.phase = 0;
            continue;
        }
        if (on && !v.on) { v.phase = 0; v.age = 0; state.newest = k; }
        v.on = on;
    }
    if (started && lit_count && lit_count <= state.held) {
        Note& v = state.voices[lit_count - 1];
        v.phase = 0;
        v.age = 0;
        state.newest = lit_count - 1;
    }
    if (fresh) state.newest = lit_count ? lit_count - 1 : 0;
    if (lit_count && state.newest >= lit_count) state.newest = lit_count - 1;
    state.initialized = 1;
    state.track = input.track;
    state.serial = input.note_serial;
    state.held = lit_count;
    i32 sounding = 0;
    for (u32 k = 0; k < 8; ++k) {
        Note& v = state.voices[k];
        const i32 target = v.on ? unit : 0;
        v.level = keep ? ease(v.level, target, keep) : target;
        if (v.level < 10) v.level = 0;
        if (unit - v.level < 4 && v.on) v.level = unit;
        if (v.age < static_cast<i32>(long_ago)) v.age += dt;
        if (!reduced) v.phase += static_cast<u32>(dt) * hundredths * static_cast<u32>(v.level) * 41u;
        sounding = v.level > sounding ? v.level : sounding;
    }
    const i32 spread = (state.eased[DETUNE] * state.eased[DETUNE]) >> 15;   // cents / 30, Q15
    if (!reduced) state.beat += static_cast<u32>((dt * (4915 + (spread * 39322 >> 15)) >> 3) * sounding);

    // The field's geometry: the centre, the path, the newest point.
    const Point centre = centre_of(c[X], c[Y]);
    const i32 radius = radius_of(c[ORBIT]), path = state.eased[PATH];
    const Point home = point_at(centre, 0, radius, path, true);
    const Note& newest = state.voices[state.newest];
    const i32 lit = newest.level;
    const Point raw = lit ? point_at(centre, newest.phase, radius, path, true) : home;
    const Point here = {home.x + ((raw.x - home.x) * lit >> 10), home.y + ((raw.y - home.y) * lit >> 10)};
    i32 w[4];
    weights(here, w);

    // The corner waves: each brightens and swells with its share; a bank
    // change shrinks the old ones as the new ones grow. Grit past half holds
    // them in steps. The gate's corner ticks brighten too.
    for (u32 n = node::glyph; n < node::tick; ++n) hide(frame, n);
    const u32 stepped = state.eased[GRIT] > 16384 ? 32u : 0u;
    for (u32 k = 0; k < 4; ++k) {
        const i32 root_w = static_cast<i32>(root(static_cast<u32>(w[k]) << 14));   // Q14
        const i32 grey = 6 * unit + ((9 * root_w >> 4) * (563 + (461 * lit >> 10)) >> 10);
        // 0.9 + 0.25 w, Q10, of the waves' artwork, authored ten times larger.
        const i32 scale = (922 + (w[k] * 256 >> 14)) / 10;
        const i32 grown = scale * (state.change >> 5) >> 10;
        if (state.change < 32767) {
            const i32 shrunk = scale - grown;
            if (shrunk > 4) place(frame, node::glyph + stepped + state.from * 4 + k, spot_x[k] * unit, spot_y[k] * unit, shrunk,
                                   static_cast<u32>(shade_of(grey)), fill_and_stroke);
        }
        if (grown > 4) place(frame, node::glyph + stepped + state.bank * 4 + k, spot_x[k] * unit, spot_y[k] * unit, grown,
                              static_cast<u32>(shade_of(grey)), fill_and_stroke);
        place(frame, node::tick + k, 0, 0, unit, static_cast<u32>(shade_of(4 * unit + (w[k] * 6 >> 4))), fill_and_stroke);
    }

    // The orbit, dotted; with steps, its four stations ringed; the centre's cross.
    for (u32 i = 0; i < node::dots; ++i)
        dot(frame, node::dot + i, point_at(centre, static_cast<u32>(i) * 89478485u, radius, path, false), unit, 7 * unit, fill_and_stroke);
    const i32 steps = clamp(path * 3 - 2 * 32768, 0, 32768);   // Q15
    for (u32 k = 0; k < 4; ++k) {
        if (steps > 1638) dot(frame, node::station + k, point_at(centre, k << 30, radius, path, true), unit,
                              3 * unit + (steps * 5 >> 5), stroke);
        else hide(frame, node::station + k);
    }
    const Point middle = gate(centre);
    dot(frame, node::cross, middle, unit, 10 * unit, fill_and_stroke);

    // The vector: a dotted spoke from the centre to the newest point.
    for (u32 i = 0; i < node::spokes; ++i) {
        if (lit <= 20) { hide(frame, node::spoke + i); continue; }
        const Point p = {middle.x + (here.x - middle.x) * static_cast<i32>(i + 1) / 7, middle.y + (here.y - middle.y) * static_cast<i32>(i + 1) / 7};
        dot(frame, node::spoke + i, p, unit, 2 * unit + 3 * lit, fill_and_stroke);
    }

    // The points: each held note's, the newest brightest, with a trail of
    // where it has just been (never longer than the note has played), a ring
    // round the newest and a ping as each starts.
    const u32 root_h = root(hundredths << 16);   // sqrt(hundredths), Q8
    const i32 span = clamp(3932 + static_cast<i32>(1311 * root_h >> 8), 0, 29491);   // Q16 cycles: 0.06 + 0.2 sqrt(Hz), at most 0.45
    bool ringed = false;
    for (u32 k = 0; k < 8; ++k) {
        const Note& v = state.voices[k];
        if (!v.level) {
            for (u32 i = 0; i < node::trails; ++i) hide(frame, node::trail + k * node::trails + i);
            hide(frame, node::point + k);
            hide(frame, node::ping + k);
            continue;
        }
        u32 rank = 0;
        for (u32 j = 0; j < 8; ++j)
            if (j != k && state.voices[j].level && (state.voices[j].age < v.age || (state.voices[j].age == v.age && j > k))) ++rank;
        const i32 bright = (15 * unit - static_cast<i32>(rank) * 1331) * v.level >> 10;   // Q10 grey
        const i32 age = v.age < 10240 ? v.age : 10240;
        const i32 back = clamp(age * static_cast<i32>(hundredths) * 41 >> 6, 0, span);   // Q16 cycles: age x Hz
        for (u32 i = 1; i <= node::trails; ++i) {
            const u32 at = v.phase - (static_cast<u32>(back * static_cast<i32>(i) >> 3) << 16);
            const i32 r = 512 + 1331 * (9 - static_cast<i32>(i)) / 9;   // Q10: 0.5 + 1.3 (1 - i / 9)
            dot(frame, node::trail + k * node::trails + i - 1, point_at(centre, at, radius, path, true),
                (r << 10) / trail_r, (bright * (10 - static_cast<i32>(i)) / 10) * 3 >> 2, fill_and_stroke);
        }
        const Point p = point_at(centre, v.phase, radius, path, true);
        const i32 pop = reduced ? unit : unit + (717 * pop_decay(v.age) >> 10);
        dot(frame, node::point + k, p, ((rank ? 1843 : 2662) * pop >> 10 << 10) / point_r, bright, fill_and_stroke);
        if (!rank && !ringed) {
            dot(frame, node::ring, p, (4710 * pop >> 10 << 10) / ring_r, bright * 614 >> 10, stroke);
            ringed = true;
        }
        if (!reduced && v.age < 205) {   // a ping, 0.2 s
            const i32 q = v.age * 5;      // Q10
            dot(frame, node::ping + k, p, ((3 * unit + 11 * q) << 10) / ping_r, 12 * (unit - q) * v.level >> 10, stroke);
        } else {
            hide(frame, node::ping + k);
        }
    }
    if (!ringed) hide(frame, node::ring);
    // At rest, a hollow ring where the next note will start.
    if (lit < 1003) dot(frame, node::home, home, unit, 10 * (unit - lit), stroke);
    else hide(frame, node::home);

    // The scope: one cycle of the wave the newest point mixes, normalised;
    // grit holds it in steps and coarsens its levels; detune draws the twin
    // behind it, drifting; the body under the wave is a comb of strokes.
    Mixer mixer;
    for (u32 k = 0; k < 4; ++k) {
        mixer.now[k] = face[state.bank][k];
        mixer.was[k] = face[state.from][k];
        mixer.w[k] = w[k];
    }
    mixer.change = state.change;
    i32 loudest = 0;
    for (u32 i = 0; i < 64; ++i) {
        const i32 y = mixer.at(i << 10);
        loudest = clamp(y < 0 ? -y : y, loudest, 1 << 20);
    }
    const i32 norm = (one << 14) / (loudest > 9830 ? loudest : 9830);   // Q14: the loudest point at one, lifted at most 1/0.6
    const i32 grit = state.eased[GRIT];
    const i32 m = clamp(grit * 16 + (grit * 43 >> 6), 0, 32768);      // grit / 0.06, Q15
    const i32 morph = (m * m >> 15) * (98304 - 2 * m) >> 15;           // smoothstep: any grit at all is held in steps
    const i32 hold = clamp(static_cast<i32>((14 * exp2_q16(16 * 65536 - (grit * 2781 >> 10)) + 32768) >> 16), 5, 14);   // steps a cycle
    const i32 fall = static_cast<i32>(log2_q16(static_cast<u32>(32768 - grit))) - 15 * 65536;   // log2(1 - grit), Q16
    const i32 levels = clamp(3 + static_cast<i32>((13 * exp2_q16(16 * 65536 + fall + fall / 5) + 32768) >> 16), 3, 16);   // 3 + 13 (1 - g)^1.2
    struct Scope {
        const Mixer* mixer;
        i32 norm, morph, held, levels;
        i32 y(u32 p) const { return mixer->at(p) * norm >> 14; }
        i32 stepped(u32 p) const {
            const i32 v = y(p) * levels;
            return (v >= 0 ? (v + 8192) >> 14 : -((8192 - v) >> 14)) * one / levels;
        }
        // One of the line's 32 points, Q10 authored px: clean, on the wave;
        // with grit, its step's held level, the step's first and last points
        // slid out to its edges so the risers stand upright.
        opxy::draw::Point at(u32 i, u32 off) const {
            const u32 n = static_cast<u32>(held), k = i * n / 31;
            const i32 cx = static_cast<i32>(i * 65536 / 31);
            const bool first = i == 0 || (i - 1) * n / 31 != k, last = i == 31 || (i + 1) * n / 31 != k;
            const i32 sx = first ? static_cast<i32>(k * 65536 / n) + 13 : last ? static_cast<i32>((k + 1) * 65536 / n) - 13 : cx;
            const i32 cy = y(static_cast<u32>(cx) + off), sy = stepped(k * 65536 / n + off);
            const i32 x = cx + ((sx - cx) * (morph >> 3) >> 12), v = cy + ((sy - cy) * (morph >> 3) >> 12);
            const opxy::draw::Point point = {(x - 32768) * 2014 >> 10, -(v * 1817 >> 10)};
            return point;
        }
        // The body's top at x (Q16), Q10 authored px.
        i32 top(i32 cx) const {
            const u32 n = static_cast<u32>(held), k = static_cast<u32>(cx) * n >> 16;
            const i32 cy = y(static_cast<u32>(cx)), sy = stepped(k * 65536 / n);
            return -((cy + ((sy - cy) * (morph >> 3) >> 12)) * 1817 >> 10);
        }
    };
    const Scope scope = {&mixer, norm, morph, hold, levels};
    opxy::draw::Point line[32], points[64];
    for (u32 i = 0; i < 32; ++i) line[i] = scope.at(i, 0);
    ribbon(line, 32, 307, points);
    outline(frame.paths[node::wave], points);
    place(frame, node::wave, scope_x, scope_y, scope_scale, static_cast<u32>(shade_of(8 * unit + 6 * lit)), stroke);
    // The twin, drawn whenever detune is up (and kept shaped, hidden, when it is not).
    const bool twin = state.eased[DETUNE] > 328;
    const i32 drift = twin ? (2621 + (spread * 26214 >> 15)) * sine(state.beat >> 2) >> 15 : 0;   // Q16 cycles: (0.04 + 0.4 c/30) sin
    for (u32 i = 0; i < 32; ++i) line[i] = scope.at(i, static_cast<u32>(drift));
    ribbon(line, 32, 179, points);
    outline(frame.paths[node::twin], points);
    if (twin) place(frame, node::twin, scope_x, scope_y, scope_scale, static_cast<u32>(shade_of(4 * unit + 3 * lit)), stroke);
    else hide(frame, node::twin);
    for (u32 i = 0; i < 32; ++i) {
        const i32 x = static_cast<i32>((2 * i + 1) * 1024);   // (i + 0.5) / 32, Q16
        i32 t = scope.top(x);
        if (t > 0 && t < 410) t = 410;            // clear of the base
        else if (t <= 0 && t > -205) t = -205;
        const opxy::draw::Point base = {(x - 32768) * 2014 >> 10, 205}, tip = {base.x, t};
        points[2 * i] = i & 1 ? tip : base;
        points[2 * i + 1] = i & 1 ? base : tip;
    }
    outline(frame.paths[node::comb], points);
    place(frame, node::comb, scope_x, scope_y, scope_scale,
          static_cast<u32>(shade_of(unit + ((2 * morph >> 5) * (410 + (614 * lit >> 10)) >> 10))), stroke);

    // Each corner's share, as a row of eight pips.
    for (u32 k = 0; k < 4; ++k) {
        const i32 n = (w[k] * 8 + 8192) >> 14;
        for (u32 j = 0; j < 8; ++j)
            place(frame, node::pip + k * 8 + j, 0, 0, unit,
                  static_cast<u32>(static_cast<i32>(j) < n ? shade_of(8 * unit + 5 * lit) : 3), fill_and_stroke);
    }
}
}
