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
// plays, and a bead sweeps the scope's wave for as long as a note sounds.
// Integer arithmetic only.
//
// The device redraws what changes, so the screen spends a budget: at most
// `budget_per_frame` nodes change a frame, however many notes sound. Every part
// works out where it should be each frame and asks; what the budget refuses
// asks again next frame, so the picture always arrives. A point moves only by
// whole pixels. Trails are dots dropped behind a point that stay where they
// fell and dim in steps, not dots that all move every frame. Older notes share
// what the newest leaves, and notes struck together draw once.
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
const i32 budget_per_frame = 18;  // nodes changed a frame
const u32 older_points = 3;       // older notes' points redrawn a frame

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

// A ribbon (the board's thin line): the points, running left to right, out and
// then back along each point's normal by h (Q10), into an outline of 2n points.
// It starts on the longest edge of the way out, so that edge is the closing
// one: the device draws a closing edge under 1/4 px as a long sliver. Points
// are kept apart in x, as neighbours within 1/8 px merge.
inline void ribbon(opxy::draw::Point* top, u32 n, i32 h, opxy::draw::Point* out) {
    for (u32 i = 1; i < n; ++i)
        if (top[i].x < top[i - 1].x + 256) top[i].x = top[i - 1].x + 256;
    opxy::draw::Point loop[64];
    for (u32 i = 0; i < n; ++i) {
        loop[i] = top[i];
        const opxy::draw::Point& a = top[i ? i - 1 : 0];
        const opxy::draw::Point& b = top[i + 1 < n ? i + 1 : n - 1];
        const i32 tx = (b.x - a.x) >> 4, ty = (b.y - a.y) >> 4;
        const i32 length = static_cast<i32>(root(static_cast<u32>(tx * tx + ty * ty)));
        opxy::draw::Point& back = loop[2 * n - 1 - i];
        // Sideways no further than half way to a neighbour, or it crosses the line.
        const i32 room = (b.x - top[i].x < top[i].x - a.x ? b.x - top[i].x : top[i].x - a.x) / 2;
        // The ends are cut square; peaks and valleys take the back straight below.
        const bool end = !i || i + 1 == n || (a.y < top[i].y) == (b.y < top[i].y);
        back.x = top[i].x - (end ? 0 : clamp(length ? ty * h / length : 0, -room, room));
        back.y = top[i].y + (end || !length ? h : tx * h / length);
        if (i && back.x < loop[2 * n - i].x + 192) back.x = loop[2 * n - i].x + 192;
    }
    u32 start = 1;
    i32 longest = -1;
    for (u32 i = 1; i < n; ++i) {
        const i32 dx = top[i].x - top[i - 1].x, dy = top[i].y - top[i - 1].y;
        const i32 size = dx + (dy < 0 ? -dy : dy);
        if (size > longest) { longest = size; start = i; }
    }
    for (u32 i = 0; i < 2 * n; ++i) out[i] = loop[(start + i) % (2 * n)];
}
// Sets an outline when its points change; says if they did.
template <class P>
inline bool outline(P& path, const opxy::draw::Point* points) {
    bool same = true;
    for (u32 i = 0; i < Frame::point_count; ++i)
        same = same && path.points[i].x == points[i].x && path.points[i].y == points[i].y;
    if (same && path.key) return false;
    for (u32 i = 0; i < Frame::point_count; ++i) path.points[i] = points[i];
    path.key = path.key + 1 ? path.key + 1 : 1;
    return true;
}

// What this frame may still change.
struct Budget { i32 left; };

inline bool concealed(const opxy::draw::Node& n) { return n.x >= hidden_x * unit; }
inline i32 distance(i32 a, i32 b) { return a > b ? a - b : b - a; }
// Sets a node if it has strayed further than slack (Q10 px, Q10 of scale) or
// its grey has changed, and the budget allows.
inline void ask(Frame& frame, Budget& budget, u32 node, i32 x, i32 y, i32 scale, u32 grey, Paint paint,
                i32 slack = 0, i32 scale_slack = 0) {
    const Node& now = frame.nodes[node];
    if (!concealed(now) && now.shade == grey && now.paint == paint && distance(now.x, x) <= slack &&
        distance(now.y, y) <= slack && distance(now.scale, scale) <= scale_slack) return;
    if (budget.left <= 0) return;
    --budget.left;
    place(frame, node, x, y, scale, grey, paint);
}
inline void conceal(Frame& frame, Budget& budget, u32 node) {
    if (concealed(frame.nodes[node]) || budget.left <= 0) return;
    --budget.left;
    hide(frame, node);
}
// A dot in the field; too small or too dim to see, hidden.
inline void dot(Frame& frame, Budget& budget, u32 node, const Point& p, i32 scale, i32 grey, Paint paint,
                i32 slack = 0, i32 scale_slack = 0) {
    if (scale < 16 || (paint == stroke && grey < 512)) { conceal(frame, budget, node); return; }
    ask(frame, budget, node, screen_x(p), screen_y(p), clamp(scale, 16, 2 * unit), static_cast<u32>(shade_of(grey)), paint, slack, scale_slack);
}

// The path as it is drawn this frame, and each trail's length.
struct Field {
    Point centre;
    i32 radius, path, span;   // span: how far back a trail reaches, Q16 cycles
    bool reduced;
};
// A dot of a trail, by how far back it lies (a quarter of the trail each): its
// radius, Q10 px, and its share of the point's grey, Q10.
const i32 trail_radius[4] = {1618, 1321, 1024, 737}, trail_share[4] = {653, 499, 346, 192};

inline bool settled_on(const Node& n, const Point& p) {
    return !concealed(n) && distance(n.x, screen_x(p)) < 512 && distance(n.y, screen_y(p)) < 512;
}
// A note's trail. The newest note keeps eight dots, older ones four over the
// same reach. A dot stays where it fell, and steps down the taper as newer
// ones land, so each new dot changes the dot itself and the ones that cross a
// step. When dots cannot land often enough (a fast rate, or a thin budget) the
// ring shortens, so the trail never reaches further than it should.
inline void trail(Frame& frame, Budget& budget, Note& v, u32 k, u32 rank, const Field& f, const Point& here, i32 bright) {
    const u32 first = node::trail + k * node::trails, most = rank ? 4u : 8u;
    const u32 gap = static_cast<u32>(f.span) / most;   // Q16 cycles
    const u32 since = (v.phase - v.laid) >> 16;
    if (v.count > most) v.count = most;
    if (f.reduced) {   // no motion: the trail is laid out behind the point at once
        v.count = most;
        if (v.filled < most && budget.left > 0) {
            const u32 s = (v.head + most - v.filled) % most;
            const Point p = point_at(f.centre, v.phase - ((v.filled + 1) * gap << 16), f.radius, f.path, true);
            --budget.left;
            place(frame, first + s, screen_x(p), screen_y(p), unit, 0, fill_and_stroke);
            ++v.filled;
        }
    } else if (since >= gap && gap && budget.left > 0) {
        const u32 ring = since < 2 * gap ? most : static_cast<u32>(clamp(static_cast<i32>(static_cast<u32>(f.span) / since), 1, static_cast<i32>(most)));
        const bool held = v.filled && settled_on(frame.nodes[first + v.head], here) &&
                          (v.filled < v.count || settled_on(frame.nodes[first + (v.head + 1) % v.count], here));
        v.laid = since < 2 * gap ? v.laid + (gap << 16) : v.phase;
        if (!held) {   // not a point that has stopped with its trail caught up
            v.count = ring;
            v.head = (v.head + 1) % ring;
            v.filled = v.filled + 1 < ring ? v.filled + 1 : ring;
            --budget.left;
            place(frame, first + v.head, screen_x(here), screen_y(here), (trail_radius[0] << 10) / trail_r,
                  static_cast<u32>(shade_of((bright * trail_share[0]) >> 10)), fill_and_stroke);
        }
    }
    for (u32 s = 0; s < node::trails; ++s) {
        const u32 d = s < v.count ? (v.head + v.count - s) % v.count : node::trails;
        if (d >= v.filled) { conceal(frame, budget, first + s); continue; }
        const u32 step = d * 4 / v.count;   // an older note's four dots take every other step
        const u32 t = rank ? step & 2u : step;
        const i32 scale = (trail_radius[t] << 10) / trail_r;
        const u32 grey = static_cast<u32>(shade_of((bright * trail_share[t]) >> 10));
        Node& n = frame.nodes[first + s];
        if ((n.scale != scale || n.shade != grey) && budget.left > 0) {
            --budget.left;
            n.scale = scale;
            n.shade = grey;
        }
    }
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
            v.phase = v.laid = 0;
            v.head = v.filled = 0;
            v.count = node::trails;
            continue;
        }
        if (on && !v.on) {
            v.phase = v.laid = 0;
            v.age = 0;
            v.head = v.filled = 0;
            v.count = node::trails;
            state.newest = k;
        }
        v.on = on;
    }
    if (started && lit_count && lit_count <= state.held) {
        Note& v = state.voices[lit_count - 1];
        v.phase = v.laid = 0;
        v.age = 0;
        v.head = v.filled = 0;
        v.count = node::trails;
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

    // The scope: one cycle of the wave the newest point mixes, normalised;
    // grit holds it in steps and coarsens its levels; detune draws the twin
    // behind it, drifting; the body under the wave is a comb of strokes. One
    // outline is redrawn a frame: the wave every other frame, the twin and the
    // comb taking turns in between.
    Budget budget = {budget_per_frame + (fresh ? 1 << 20 : 0)};
    const u32 turn = state.frames++;
    if (events.hold_started) state.sweep = 0;
    if (!reduced) state.sweep += static_cast<u32>(dt) * 3145728u;   // 0.75 sweeps a second
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
    const u32 slot = turn & 3u;
    if (fresh || !(slot & 1u)) {
        for (u32 i = 0; i < 32; ++i) line[i] = scope.at(i, 0);
        ribbon(line, 32, 307, points);
        if (outline(frame.paths[node::wave], points)) --budget.left;
        place(frame, node::wave, scope_x, scope_y, scope_scale, static_cast<u32>(shade_of(8 * unit + 6 * lit)), stroke);
        // The beam: as long as a note sounds a bead sweeps the wave, whatever
        // the controls are, so a held note never looks finished. It rides the
        // line just drawn.
        if (lit > 20 && !reduced) {
            const u32 along = (state.sweep >> 16) * 31;   // Q16 of the line's 31 spans
            const u32 i = along >> 16;
            const i32 f = static_cast<i32>((along >> 6) & 1023u);
            const i32 bx = line[i].x + ((line[i + 1].x - line[i].x) * f >> 10), by = line[i].y + ((line[i + 1].y - line[i].y) * f >> 10);
            ask(frame, budget, node::beam, scope_x + (bx * scope_scale >> 10), scope_y + (by * scope_scale >> 10), unit,
                static_cast<u32>(shade_of(9 * lit + 6 * unit)), fill_and_stroke);
        } else {
            conceal(frame, budget, node::beam);
        }
    }
    // The twin, drawn whenever detune is up.
    const bool twin = state.eased[DETUNE] > 328;
    if (fresh || slot == 1) {
        const i32 drift = twin ? (2621 + (spread * 26214 >> 15)) * sine(state.beat >> 2) >> 15 : 0;   // Q16 cycles: (0.04 + 0.4 c/30) sin
        if (twin || fresh) {
            for (u32 i = 0; i < 32; ++i) line[i] = scope.at(i, static_cast<u32>(drift));
            ribbon(line, 32, 192, points);
            if (outline(frame.paths[node::twin], points)) --budget.left;
        }
        if (twin) place(frame, node::twin, scope_x, scope_y, scope_scale, static_cast<u32>(shade_of(4 * unit + 3 * lit)), stroke);
        else hide(frame, node::twin);
    }
    if (fresh || slot == 3) {
        i32 tips[32], sum = 0;
        for (u32 i = 0; i < 32; ++i) {
            const i32 x = static_cast<i32>((2 * i + 1) * 1024);   // (i + 0.5) / 32, Q16
            i32 t = scope.top(x);
            if (t > 0 && t < 410) t = 410;            // clear of the base
            else if (t <= 0 && t > -205) t = -205;
            // A pair of strokes is joined across its tips: they stay on one side
            // of the base, or the outline crosses itself there.
            if (i & 1) {
                if (tips[i - 1] > 205 && t < 205) t = 410;
                else if (tips[i - 1] < 205 && t > 205) t = -205;
            }
            tips[i] = t;
            sum += t - 205;
            const opxy::draw::Point base = {(x - 32768) * 2014 >> 10, 205}, tip = {base.x, t};
            points[2 * i] = i & 1 ? tip : base;
            points[2 * i + 1] = i & 1 ? base : tip;
        }
        // Strokes up and strokes down can cancel to no area at all, which the
        // device draws as streaks: the first pair's a pixel longer.
        if (sum > -1024 && sum < 1024) {
            const i32 longer = tips[0] > 205 ? 1024 : -1024;
            points[1].y += longer;
            points[2].y += longer;
        }
        if (outline(frame.paths[node::comb], points)) --budget.left;
        place(frame, node::comb, scope_x, scope_y, scope_scale,
              static_cast<u32>(shade_of(unit + ((2 * morph >> 5) * (410 + (614 * lit >> 10)) >> 10))), stroke);
    }
    // The notes, from the newest. Each is a point with a trail; the newest has
    // a ring and a spoke, and each has a ping as it starts. A note struck with a
    // newer one, or with no orbit to travel, lies under it and draws nothing.
    const u32 root_h = root(hundredths << 16);   // sqrt(hundredths), Q8
    const i32 span = clamp(3932 + static_cast<i32>(1311 * root_h >> 8), 0, 29491);   // Q16 cycles: 0.06 + 0.2 sqrt(Hz), at most 0.45
    const Field field = {centre, radius, path, span, reduced};
    u32 rank[8];
    bool under[8];
    u32 first = 8;
    for (u32 k = 0; k < 8; ++k) {
        const Note& v = state.voices[k];
        rank[k] = 0;
        under[k] = false;
        if (!v.level) continue;
        for (u32 j = 0; j < 8; ++j) {
            const Note& u = state.voices[j];
            if (j == k || !u.level || !(u.age < v.age || (u.age == v.age && j > k))) continue;
            ++rank[k];
            const i32 apart = static_cast<i32>(v.phase - u.phase);
            if (radius < 256 || (apart > -(1 << 25) && apart < (1 << 25))) under[k] = true;
        }
        if (!rank[k]) first = k;
    }
    const auto bright_of = [&](u32 k) { return (15 * unit - static_cast<i32>(rank[k]) * 1331) * state.voices[k].level >> 10; };
    const auto draw_point = [&](u32 k, const Point& p, bool newest) {
        const Note& v = state.voices[k];
        const i32 pop = reduced ? unit : unit + (717 * pop_decay(v.age) >> 10);
        dot(frame, budget, node::point + k, p, ((newest ? 2662 : 1843) * pop >> 10 << 10) / point_r, bright_of(k), fill_and_stroke, unit, 48);
        if (newest) dot(frame, budget, node::ring, p, (4710 * pop >> 10 << 10) / ring_r, bright_of(k) * 614 >> 10, stroke, unit, 48);
        if (!reduced && v.age < 205) {   // a ping, 0.2 s
            const i32 q = v.age * 5;      // Q10
            dot(frame, budget, node::ping + k, p, ((3 * unit + 11 * q) << 10) / ping_r, 12 * (unit - q) * v.level >> 10, stroke, 2 * unit, 200);
        } else {
            conceal(frame, budget, node::ping + k);
        }
    };
    Point newest_at = home;
    if (first < 8) {
        newest_at = point_at(centre, state.voices[first].phase, radius, path, true);
        draw_point(first, newest_at, true);
    } else {
        conceal(frame, budget, node::ring);
    }

    // The vector: a dotted spoke from the centre to the newest point, its odd
    // and even dots taking turns.
    const Point middle = gate(centre);
    for (u32 i = 0; i < node::spokes; ++i) {
        if (lit <= 20) { conceal(frame, budget, node::spoke + i); continue; }
        if (!fresh && (i & 1u) != (turn & 1u)) continue;
        const Point p = {middle.x + (here.x - middle.x) * static_cast<i32>(i + 1) / 7, middle.y + (here.y - middle.y) * static_cast<i32>(i + 1) / 7};
        dot(frame, budget, node::spoke + i, p, unit, 2 * unit + 3 * lit, fill_and_stroke, unit);
    }

    // The corner waves: each brightens and swells with its share, by whole
    // pixels of its width; a bank change shrinks the old ones as the new ones
    // grow. Grit past half holds them in steps. The gate's corner ticks brighten
    // too. A corner a frame, or all four while the bank changes.
    const u32 stepped = state.eased[GRIT] > 16384 ? 32u : 0u;
    for (u32 k = 0; k < 4; ++k) {
        if (!fresh && state.change == 32767 && (turn & 3u) != k) continue;
        const i32 root_w = static_cast<i32>(root(static_cast<u32>(w[k]) << 14));   // Q14
        const i32 grey = 6 * unit + ((9 * root_w >> 4) * (563 + (461 * lit >> 10)) >> 10);
        // 0.9 + 0.25 w, Q10.
        const i32 scale = 922 + ((w[k] * 256 >> 14) + 20) / 40 * 40;
        const i32 grown = scale * (state.change >> 5) >> 10;
        const u32 now = node::glyph + stepped + state.bank * 4 + k, was = node::glyph + stepped + state.from * 4 + k;
        const i32 shrunk = state.change < 32767 && was != now ? scale - grown : 0;
        for (u32 b = 0; b < 16; ++b) {
            const u32 n = node::glyph + (b >> 3) * 32 + (b & 7u) * 4 + k;
            if ((n != now || grown <= 40) && (n != was || shrunk <= 40)) conceal(frame, budget, n);
        }
        if (grown > 40) ask(frame, budget, now, spot_x[k] * unit, spot_y[k] * unit, grown, static_cast<u32>(shade_of(grey)), fill_and_stroke);
        if (shrunk > 40) ask(frame, budget, was, spot_x[k] * unit, spot_y[k] * unit, shrunk, static_cast<u32>(shade_of(grey)), fill_and_stroke);
        ask(frame, budget, node::tick + k, 0, 0, unit, static_cast<u32>(shade_of(4 * unit + (w[k] * 6 >> 4))), fill_and_stroke);
    }

    // Each corner's share, as a row of eight pips, a row a frame. They light in
    // three greys as a note comes and goes.
    for (u32 k = 0; k < 4; ++k) {
        if (!fresh && ((turn >> 1) & 3u) != k) continue;
        const i32 n = (w[k] * 8 + 8192) >> 14;
        const u32 on = static_cast<u32>(8 + 5 * (lit >> 9) / 2);
        for (u32 j = 0; j < 8; ++j)
            ask(frame, budget, node::pip + k * 8 + j, 0, 0, unit, static_cast<u32>(j) < static_cast<u32>(n) ? on : 3u, fill_and_stroke);
    }

    // The orbit. While a control moves it, parts go by the whole pixel and a
    // quarter of the dots at a time; once it stops all land exactly.
    const bool moving = state.eased[X] != q15(slots[X]) || state.eased[Y] != q15(slots[Y]) ||
                        state.eased[ORBIT] != q15(slots[ORBIT]) || state.eased[PATH] != q15(slots[PATH]);
    const i32 slack = moving ? unit : 0;
    dot(frame, budget, node::cross, middle, unit, 10 * unit, fill_and_stroke, slack);
    // At rest, a hollow ring where the next note will start.
    if (lit < 1003) dot(frame, budget, node::home, home, unit, 10 * (unit - lit), stroke, slack);
    else conceal(frame, budget, node::home);
    const i32 steps = clamp(path * 3 - 2 * 32768, 0, 32768);   // Q15
    for (u32 k = 0; k < 4; ++k) {
        if (steps > 1638) dot(frame, budget, node::station + k, point_at(centre, k << 30, radius, path, true), unit,
                              3 * unit + (steps * 5 >> 5), stroke, slack);
        else conceal(frame, budget, node::station + k);
    }
    for (u32 i = 0; i < node::dots; ++i) {
        if (!fresh && (i & 3u) != (turn & 3u)) continue;
        dot(frame, budget, node::dot + i, point_at(centre, static_cast<u32>(i) * 89478485u, radius, path, false), unit, 7 * unit, fill_and_stroke, slack);
    }

    // The newest note's trail, then the older notes: a few points a frame in
    // turn, and their trails with what the budget has left.
    if (first < 8) trail(frame, budget, state.voices[first], first, 0, field, newest_at, bright_of(first));
    u32 points_left = fresh ? 8u : older_points;
    for (u32 n = 0; n < 8; ++n) {
        const u32 k = (turn * 3 + n) & 7u;
        if (k == first) continue;
        Note& v = state.voices[k];
        if (!v.level || under[k]) {
            conceal(frame, budget, node::point + k);
            conceal(frame, budget, node::ping + k);
            for (u32 i = 0; i < node::trails; ++i) conceal(frame, budget, node::trail + k * node::trails + i);
            v.filled = 0;
            v.laid = v.phase;
            continue;
        }
        if (points_left) {
            const i32 left = budget.left;
            draw_point(k, point_at(centre, v.phase, radius, path, true), false);
            if (budget.left != left) --points_left;
        }
    }
    for (u32 n = 0; n < 8; ++n) {
        const u32 k = (turn * 3 + n) & 7u;
        if (k == first || !state.voices[k].level || under[k]) continue;
        trail(frame, budget, state.voices[k], k, rank[k], field, point_at(centre, state.voices[k].phase, radius, path, true), bright_of(k));
    }
}
}
