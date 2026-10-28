// Moth's screen: a moth, wings spread. Each wing's margin is the folded
// waveform, one cycle wrapped along it (the forewing the first half, the
// hindwing the second): each half-cycle swells a lobe, and every fold splits
// it into scallops; the right wing shows the wave half a cycle on and turned
// over, so bias, or FM at an odd ratio, makes the wings differ. FM's
// modulator is drawn as a strobe at the note's own rate: the nearest
// half-harmonic stands still and what is left over crawls.
//
// The body is the vactrol: the LED's leads bent up as antennae, the LED its
// head, the photocell's serpentine track the abdomen. Each held note keeps
// its own vactrol (the laws audio uses, laws.h): its LED flashes as the note
// is struck, its cell lights the wings, the lamp's halo and its own eyespot,
// and fades at glow's pace. In the lit wing the gate's waveform shows: vca
// keeps its scallops as it shrinks, lp melts them. A strike flicks the
// antennae and knocks scales off the wings; drift makes the edges wander while
// the moth sounds. The screen cannot hear the voices: it follows the held
// notes and the controls. Integer arithmetic only; nothing moves while
// nothing sounds.
//
// The OP-XY redraws what changes, so the screen changes little: the moth
// stays put, the membrane's pieces are redrawn only once the wave has moved
// them about a pixel, and the gate's light, the LED and the eyespots are
// drawn in steps, redrawn only when they step.
#include "display.h"
#include "laws.h"
#include "wing.h"

namespace moth {
namespace screen {
using namespace opxy::draw;

const i32 kCX = 240 * unit, kCY = 124 * unit;
const i32 kDepth = 7209;        // how far a lobe swells past the dips: 0.22 of the wing's reach, Q15
const u32 kBeat = 13107;        // an antenna's flick: 0.2 s, Q16 seconds
const u32 kFlash = 3932;        // the LED's flash: 60 ms, Q16 seconds
const i32 kEaseLog2 = -293206;  // log2 of 45 ms, Q16: controls settle within 0.2 s
const i32 kVeinAt[kVeins] = {3277, 9830, 17039, 24248, 32768, 40632, 48497, 56361, 62259};  // Q16 along the margin
const i32 kSpotAt[4] = {18350, 30802, 47186, 57016};   // Q16 along the margin
const i32 kSpotReach[4] = {19661, 20972, 19661, 17039}; // Q15 of the margin's reach there

// Greys are palette entries 0..15 (tools/make_panel.py): black to white.
inline u32 grey(i32 q8) { const i32 g = (q8 + 128) >> 8; return static_cast<u32>(g < 0 ? 0 : g > 15 ? 15 : g); }

// x^p for x and p in Q15.
inline i32 power(i32 x, i32 p) {
    if (x >= 32767) return 32767;
    if (x <= 0) return 0;
    const i32 octaves = log2_q16(static_cast<u32>(x)) - (15 << 16);
    return static_cast<i32>(exp2_q16((octaves >> 6) * p >> 9) >> 1);
}

inline u32 root(u32 v) {
    u32 r = 0, bit = 1u << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else r >>= 1;
        bit >>= 2;
    }
    return r;
}

inline u32 hash(u32 n) {
    n ^= n >> 16;
    n *= 0x7feb352du;
    n ^= n >> 15;
    n *= 0x846ca68bu;
    n ^= n >> 16;
    return n;
}

// The board's smooth, seeded wander, -32767..32767, at clock tau (Q16 s).
inline i32 wander(u32 tau, u32 salt) {
    const u32 h = hash(salt);
    const u32 slow = times(wander_rate(h, 0), tau) + hash(h + 3);
    const u32 quick = times(wander_rate(h, 1), tau) + hash(h + 11);
    return (19661 * sine15(kSine15, slow) + 13107 * sine15(kSine15, quick)) >> 15;
}

// 1 - e^(-dt / tau), Q16, for log2(dt / tau) in Q16.
inline i32 toward(i32 log2_ratio) { return 65536 - static_cast<i32>(decay(log2_ratio)); }

struct Point2 { i32 x, y; };
// A moth-local point (Q10 px from the centre) on screen, the moth turned by
// sway and lifted by bob.
struct Pose { Turn sway; i32 bob; };
inline Point2 on_screen(const Pose& pose, i32 x, i32 y) {
    Point2 p = {kCX + ((x * pose.sway.cos - y * pose.sway.sin) >> 10), kCY + pose.bob + ((x * pose.sway.sin + y * pose.sway.cos) >> 10)};
    return p;
}
inline Turn compose(Turn a, Turn b) {
    const Turn t = {clamp((a.cos * b.cos - a.sin * b.sin) >> 10, -unit, unit), clamp((a.sin * b.cos + a.cos * b.sin) >> 10, -unit, unit)};
    return t;
}
inline void pose(Frame& frame, u32 node, const Pose& p, i32 x, i32 y, i32 scale, Turn turn, u32 shade, Paint paint) {
    const Point2 s = on_screen(p, x, y);
    place(frame, node, s.x, s.y, scale, shade, paint);
    opxy::draw::turn(frame, node, compose(p.sway, turn));
}
// As pose, but leaves the node as it is when it is already within tol pixels
// (Q10) of the place, tol_scale (Q10) of the scale and tol_turn of the turn,
// in the same paint: so the OP-XY redraws only what has visibly moved. exact
// writes it regardless (the first frame after a reset sets every element).
inline void settle(Frame& frame, u32 node, const Pose& p, i32 x, i32 y, i32 scale, Turn turn, u32 shade, Paint paint,
                   i32 tol, i32 tol_scale, i32 tol_turn, bool exact) {
    const Point2 at = on_screen(p, x, y);
    const Turn t = compose(p.sway, turn);
    const Node& n = frame.nodes[node];
    const i32 dx = n.x - at.x, dy = n.y - at.y, ds = n.scale - scale, dc = n.cos - t.cos, dn = n.sin - t.sin;
    if (!exact && n.shade == shade && n.paint == paint && dx <= tol && -dx <= tol && dy <= tol && -dy <= tol &&
        ds <= tol_scale && -ds <= tol_scale && (dc < 0 ? -dc : dc) + (dn < 0 ? -dn : dn) <= tol_turn)
        return;
    place(frame, node, at.x, at.y, scale, shade, paint);
    opxy::draw::turn(frame, node, t);
}
// Tolerances: a piece may lag the wave by under a pixel before it is redrawn.
const i32 kNearPlace = 768, kNearScale = 5, kNearTurn = 12;

// The unit turn from direction (x, y), any scale.
inline Turn heading(i32 x, i32 y) {
    while (x > 30000 || x < -30000 || y > 30000 || y < -30000) { x >>= 1; y >>= 1; }
    const i32 length = static_cast<i32>(root(static_cast<u32>(x * x + y * y)));
    const Turn t = {length ? x * unit / length : unit, length ? y * unit / length : 0};
    return t;
}

// One wing's wave (side 1 half a cycle on and turned over), Q15, one value a margin point.
void wave(i32* out, u32 side, const Fold& fold, i32 depth, u32 shown, u32 psi) {
    for (u32 i = 0; i < kMargin; ++i) {
        const u32 u = (i * 65536u + (kMargin - 1) / 2) / (kMargin - 1);  // Q16 along the margin
        // Cycles, Q28: the forewing the first half, the hindwing the second.
        u32 cycles = u < static_cast<u32>(kJoint) ? (u << 11) / static_cast<u32>(kJoint) * 65536u
                                                  : (1u << 27) + ((u - kJoint) << 11) / static_cast<u32>(65536 - kJoint) * 65536u;
        if (side) cycles += 1u << 27;
        const u32 modulator = (times(cycles, shown) << 4) + psi;
        const u32 phase = (cycles << 4) + (static_cast<u32>(sine15(kSine15, modulator) * depth) << 2);
        i32 y = folded(fold, sine15(kSine15, phase)) >> 13;
        y = y > 32767 ? 32767 : y < -32767 ? -32767 : y;
        out[i] = side ? -y : y;
    }
}

// The lp's melting: a one-pole run along the edge both ways, k (Q15) of each step kept.
void melt(i32* y, i32 k) {
    if (k >= 32767) return;
    for (u32 i = 1; i < kMargin; ++i) y[i] = y[i - 1] + ((y[i] - y[i - 1]) * k >> 15);
    for (u32 i = kMargin - 1; i-- > 0;) y[i] = y[i + 1] + ((y[i] - y[i + 1]) * k >> 15);
}

// The margin's reach where the wave is y (Q15): 1 at a crest, 0.78 at a dip, Q12.
inline i32 reach(i32 y) {
    const i32 a = y < 0 ? -y : y;
    return (32768 - (kDepth * (32768 - a) >> 15)) >> 3;
}

// A wing's point (Q10 px from the centre) turned about the centre by flap
// (the wings now stay level: flap is always upright).
inline Point2 lifted(i32 x, i32 y, Turn flap) {
    Point2 p = {(x * flap.cos - y * flap.sin) >> 10, (x * flap.sin + y * flap.cos) >> 10};
    return p;
}
// A margin point of a wing at reach r (Q12), lifted.
inline Point2 margin(u32 side, u32 i, i32 r, Turn flap) {
    const i32 x = kWingX[i] * r >> 12;
    return lifted(side ? -x : x, kWingY[i] * r >> 12, flap);
}

// An edge: the root's two points and the vertices, joined by straight
// chords (a point half way along each, two on the middle one, to make up the
// outline's 64), from the wing's middle at half size (the node is drawn at
// scale 2). The key changes when the points do.
void edge(Frame& frame, DisplayState& state, u32 path, u32 side, const Point2* vertices, Turn flap) {
    Frame::path_type& out = frame.paths[path];
    const i32 middle = side ? 90 * unit : -90 * unit;
    Point2 pts[64];
    const i32 root_x = (side ? 3 : -3) * unit;
    u32 n = 0;
    pts[n++] = lifted(root_x, -13 * unit, flap);
    for (u32 k = 0; k < kVertices; ++k) {
        const Point2 a = lifted(vertices[k].x, vertices[k].y, flap);
        pts[n++] = a;
        if (k + 1 == kVertices) break;
        const Point2 b = lifted(vertices[k + 1].x, vertices[k + 1].y, flap);
        const u32 parts = k == kVertices / 2 ? 3 : 2;
        for (u32 q = 1; q < parts; ++q) {
            pts[n].x = a.x + static_cast<i32>(q) * (b.x - a.x) / static_cast<i32>(parts);
            pts[n++].y = a.y + static_cast<i32>(q) * (b.y - a.y) / static_cast<i32>(parts);
        }
    }
    pts[n] = lifted(root_x, 13 * unit, flap);
    bool changed = !state.keys[path];
    for (u32 i = 0; i < 64; ++i) {
        const i32 x = (pts[i].x - middle) >> 1, y = pts[i].y >> 1;
        changed = changed || out.points[i].x != x || out.points[i].y != y;
        out.points[i].x = x;
        out.points[i].y = y;
    }
    if (changed) state.keys[path] = state.keys[path] + 1 ? state.keys[path] + 1 : 1;
    out.key = state.keys[path];
}

// The lit edge: along the lit wedges' tops, each wedge's chord at its reach
// (three points a wedge, five on the middle one, short of its ends so
// neighbours never share a point).
void lit_edge(Frame& frame, DisplayState& state, u32 path, u32 side, const i32* reaches, Turn flap) {
    Frame::path_type& out = frame.paths[path];
    const i32 middle = side ? 90 * unit : -90 * unit;
    Point2 pts[64];
    const i32 root_x = (side ? 3 : -3) * unit;
    u32 n = 0;
    pts[n++] = lifted(root_x, -13 * unit, flap);
    for (u32 j = 0; j < kLitWedges; ++j) {
        const u32 a = kLitIndex[j], b = kLitIndex[j + 1];
        const i32 r = reaches[j];
        const i32 steps[5] = {5, 64, 128, 192, 251};  // Q8 along the chord
        for (u32 q = 0; q < 5; ++q) {
            if (j != kLitWedges / 2 && (q == 1 || q == 3)) continue;
            const i32 x = kWingX[a] + ((kWingX[b] - kWingX[a]) * steps[q] >> 8);
            const i32 y = kWingY[a] + ((kWingY[b] - kWingY[a]) * steps[q] >> 8);
            const i32 sx = x * r >> 12;
            pts[n++] = lifted(side ? -sx : sx, y * r >> 12, flap);
        }
    }
    pts[n] = lifted(root_x, 13 * unit, flap);
    bool changed = !state.keys[path];
    for (u32 i = 0; i < 64; ++i) {
        const i32 x = (pts[i].x - middle) >> 1, y = pts[i].y >> 1;
        changed = changed || out.points[i].x != x || out.points[i].y != y;
        out.points[i].x = x;
        out.points[i].y = y;
    }
    if (changed) state.keys[path] = state.keys[path] + 1 ? state.keys[path] + 1 : 1;
    out.key = state.keys[path];
}

// A point along the vertices at u (Q16), interpolated.
inline Point2 along(const Point2* pts, i32 u) {
    const i32 f = u * static_cast<i32>(kVertices - 1);
    u32 i = static_cast<u32>(f >> 16);
    if (i > kVertices - 2) i = kVertices - 2;
    const i32 w = f - static_cast<i32>(i << 16);
    Point2 p = {pts[i].x + (((pts[i + 1].x - pts[i].x) >> 4) * (w >> 4) >> 8),
                pts[i].y + (((pts[i + 1].y - pts[i].y) >> 4) * (w >> 4) >> 8)};
    return p;
}

// Whether a moth-local point (Q10 px) lies inside an edge: the root's two
// points (top, bottom) and n vertices (crossing count, in Q4 px so products
// stay in 32 bits).
inline bool within(const Point2* v, u32 n, Point2 top, Point2 bottom, i32 x, i32 y) {
    bool in = false;
    x >>= 6;
    y >>= 6;
    for (u32 i = 0; i <= n + 1; ++i) {
        const Point2 a = i == 0 ? top : i <= n ? v[i - 1] : bottom;
        const Point2 b = i < n ? v[i] : i == n ? bottom : top;
        const i32 x0 = a.x >> 6, y0 = a.y >> 6, x1 = b.x >> 6, y1 = b.y >> 6;
        if ((y0 > y) != (y1 > y) && x < x0 + (y - y0) * (x1 - x0) / (y1 - y0)) in = !in;
    }
    return in;
}

// The dark membrane's teeth: each one's authored chord turned and scaled onto
// its chord between the edge's vertices, its first corner on the first
// vertex, so the membrane's boundary is the edge. Where a sharp fold would
// leave a tooth's inner corners outside the edge, the tooth shrinks along its
// chord toward the farther vertex, where the wedge leaves most to cover.
void teeth(Frame& frame, const Pose& p, u32 first, u32 side, const Point2* vertices, const i32* reaches, bool exact) {
    const Point2 top = {(side ? 3 : -3) * unit, -13 * unit}, bottom = {(side ? 3 : -3) * unit, 13 * unit};
    for (u32 j = 0; j + 1 < kVertices; ++j) {
        const u32 node = first + (kVertices - 1) * side + j;
        const i32 ux = side ? -kChordX[j] : kChordX[j], uy = kChordY[j];
        const i32 vx = vertices[j + 1].x - vertices[j].x, vy = vertices[j + 1].y - vertices[j].y;
        const Turn from = heading(ux, uy), to = heading(vx, vy);
        const Turn bend = {(to.cos * from.cos + to.sin * from.sin) >> 10, (to.sin * from.cos - to.cos * from.sin) >> 10};
        const i32 authored = static_cast<i32>(root(static_cast<u32>((ux >> 4) * (ux >> 4) + (uy >> 4) * (uy >> 4))));
        const i32 now = static_cast<i32>(root(static_cast<u32>((vx >> 4) * (vx >> 4) + (vy >> 4) * (vy >> 4))));
        const i32 scale = clamp(authored ? now * unit / authored : unit, 16, 2 * unit);
        const bool far_end = reaches[j + 1] > reaches[j];
        // Already in place, whole, within a pixel: nothing to test or redraw.
        {
            const Node& n = frame.nodes[node];
            const Point2 at = on_screen(p, vertices[j].x, vertices[j].y);
            const i32 dx = n.x - at.x, dy = n.y - at.y, ds = n.scale - scale, dc = n.cos - bend.cos, dn = n.sin - bend.sin;
            if (!exact && dx <= kNearPlace && -dx <= kNearPlace && dy <= kNearPlace && -dy <= kNearPlace &&
                ds <= kNearScale && -ds <= kNearScale && (dc < 0 ? -dc : dc) + (dn < 0 ? -dn : dn) <= kNearTurn) continue;
        }
        bool placed = false;
        for (i32 share = 4; share >= 1 && !placed; --share) {  // quarters of the chord
            const i32 s = scale * share >> 2;
            const i32 ox = vertices[j].x + (far_end ? vx * (4 - share) >> 2 : 0);
            const i32 oy = vertices[j].y + (far_end ? vy * (4 - share) >> 2 : 0);
            // Each inner corner, a fifth of the way back toward the chord's
            // middle: a corner within a pixel or two of the edge may stay.
            bool inside = true;
            for (u32 c = 0; c < 2 && inside; ++c) {
                const i32 cx = side ? -kToothInner[j][2 * c] : kToothInner[j][2 * c], cy = kToothInner[j][2 * c + 1];
                const i32 bx = (cx - ux / 2) * 4 / 5 + ux / 2, by = (cy - uy / 2) * 4 / 5 + uy / 2;
                const i32 tx = ((bx * bend.cos - by * bend.sin) >> 10) * s >> 10, ty = ((bx * bend.sin + by * bend.cos) >> 10) * s >> 10;
                inside = within(vertices, kVertices, top, bottom, ox + tx, oy + ty);
            }
            if (inside || share == 1) {
                settle(frame, node, p, ox, oy, clamp(s, 16, 2 * unit), bend, 4, keep, kNearPlace, kNearScale, kNearTurn, exact);
                placed = inside;
            }
        }
        if (!placed) hide(frame, node);
    }
}
}

inline void display_reset(DisplayState& state) {
    state.header.reset();
    state.notes.reset();
    state.started = 0;
}

inline void display(DisplayState& state, const opxy::draw::Input& input, Frame& frame) {
    using namespace opxy::draw;
    using namespace screen;
    // Captions in the units the sound uses (laws.h).
    const char* const titles[8] = {"fold", "fm", "ratio", "strike", "glow", "bias", "mode", "drift"};
    char values[8][header_capacity];
    for (u32 i = 0; i < 8; ++i) header_amount(values[i], header_capacity, input.slots[i]);
    index_caption(values[kFm], header_capacity, control(input.slots[kFm]));
    ratio_caption(values[kRatio], header_capacity, control(input.slots[kRatio]));
    glow_caption(values[kGlow], header_capacity, control(input.slots[kGlow]));
    mode_caption(values[kMode], header_capacity, control(input.slots[kMode]));
    drift_caption(values[kDrift], header_capacity, control(input.slots[kDrift]));
    const opxy::u8 slots[2][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}};
    page_header(state.header, input, frame, titles, values, kHeaderFirst, slots);

    // A reset or a track change shows the end state at once, as reduced motion always does.
    const bool fresh = !state.started || !state.notes.initialized || state.notes.track != input.track;
    const bool still = fresh || input.reduced_motion;
    state.notes.update(input);
    const u32 held = input.held_notes <= 128 ? input.held_notes : 0;
    const u32 lit = held < 8 ? held : 8;
    const u32 dt = static_cast<u32>(clamp(input.elapsed, 0, 102)) << 6;  // Q16 seconds
    const i32 log2_dt = dt ? log2_q16(dt) - (16 << 16) : -(30 << 16);

    // Controls ease in over about 0.15 s; the ratio glides between its stops.
    const i32 ease = still ? 65536 : toward(log2_dt - kEaseLog2);
    for (u32 c = 0; c < 8; ++c) {
        const i32 target = control(input.slots[c]);
        i32& e = state.eased[c];
        e += static_cast<i32>((static_cast<i32>(target - e) * (ease >> 1)) >> 15);
        if (still || (e - target < 16 && target - e < 16)) e = target;
    }
    {
        const i32 target = static_cast<i32>(ratio_stop(control(input.slots[kRatio])) << 16);
        state.stop += ((target - state.stop) >> 4) * (ease >> 4) >> 8;
        if (still || (state.stop - target < 328 && target - state.stop < 328)) state.stop = target;
    }
    const i32 strike = state.eased[kStrike], glow = state.eased[kGlow], drift = state.eased[kDrift];

    // Each held note keeps a vactrol; a note started and released between
    // frames still pings the next one.
    if (fresh) {
        for (u32 k = 0; k < 8; ++k) {
            state.lamps[k].held = k < lit;
            state.lamps[k].since = kBeat;
            state.lamps[k].cell = k < lit ? (32767 - strike) << 15 : 0;
        }
        for (u32 s = 0; s < kScaleCount; ++s) state.scales[s].life = 0;
        state.seed = input.track * 0x9e3779b9u + 1;
    }
    u32 struck[8];
    u32 strikes = 0;
    if (!fresh) {
        for (u32 k = 0; k < lit; ++k)
            if (!state.lamps[k].held) { state.lamps[k].held = 1; struck[strikes++] = k; }
        const u32 starts = input.note_serial - state.serial, newly = strikes;
        for (u32 m = 0; starts > newly && m < starts - newly && lit + m < 8; ++m) struck[strikes++] = lit + m;
        for (u32 s = 0; s < strikes; ++s) state.lamps[struck[s]].since = 0;
    }
    for (u32 k = lit; k < 8; ++k) state.lamps[k].held = 0;
    state.serial = input.note_serial;
    state.started = 1;

    const Vactrol held_law = vactrol(strike, glow);
    Vactrol released = held_law;
    released.sustain = 0;
    i32 top = 0, sum = 0, led = 0;
    i32 cells[8];
    for (u32 k = 0; k < 8; ++k) {
        Lamp& lamp = state.lamps[k];
        if (still) {
            lamp.cell = lamp.held ? held_law.sustain : 0;
        } else {
            lamp.cell = gate_step(lamp.cell, lamp.since, dt, log2_dt, lamp.held ? held_law : released);
            if (!lamp.held && lamp.cell < 4295000) lamp.cell = 0;  // 0.004: dark
        }
        // The LED: held at 1 - strike, flashing as the note is struck.
        const i32 flash = strike && lamp.since < 6 * kFlash ? static_cast<i32>(decay(log2_q16(lamp.since | 1) - log2_q16(kFlash)) >> 1) : 0;
        const i32 light = lamp.held ? (32767 - strike > flash ? 32767 - strike : flash) : (still ? 0 : flash);
        if (light > led) led = light;
        cells[k] = lamp.cell >> 15;  // Q15
        if (cells[k] > top) top = cells[k];
        sum += cells[k];
        if (lamp.since < (60u << 16)) lamp.since += dt;
    }
    i32 gate = top + ((sum - top) / 5);
    if (gate > 32767) gate = 32767;

    // The clock runs, and inharmonic ratios crawl, only while the moth sounds.
    const u32 stop_lo = static_cast<u32>(state.stop) >> 16, stop_hi = stop_lo + 1 < kStops ? stop_lo + 1 : stop_lo;
    const i32 part = state.stop & 0xffff;
    const i32 near_lo = static_cast<i32>((kRatioQ16[stop_lo] + 16384u) >> 15 << 15), near_hi = static_cast<i32>((kRatioQ16[stop_hi] + 16384u) >> 15 << 15);
    const u32 shown = static_cast<u32>(near_lo + (((near_hi - near_lo) >> 4) * (part >> 4) >> 8));
    if (!input.reduced_motion && !fresh) {
        const u32 sounding = static_cast<u32>(gate * 2 < 32767 ? gate * 2 : 32767);
        const u32 run = dt * sounding >> 15;  // Q16 seconds
        state.tau += run;
        const i32 ratio = static_cast<i32>(kRatioQ16[ratio_stop(control(input.slots[kRatio]))]);
        const i32 off = ratio - static_cast<i32>((static_cast<u32>(ratio) + 16384u) >> 15 << 15);  // Q16
        state.psi += static_cast<u32>(off * 3 * static_cast<i32>(run >> 4)) << 4;
    }

    // The wave: the folder at the drawn fold (wandering with drift), FM at the drawn index.
    i32 drive = drive_q16(state.eased[kFold]);
    drive += (drive >> 4) * (drift_fold_q15(drift) * wander(state.tau, 21) >> 15) >> 11;
    const Fold fold = fold_plan(drive, state.eased[kBias]);
    const i32 depth = static_cast<i32>(times(static_cast<u32>(index_q16(state.eased[kFm])), 5215u));
    i32 dark_wave[2][kMargin], lit_wave[2][kMargin];
    for (u32 side = 0; side < 2; ++side) wave(dark_wave[side], side, fold, depth, shown, state.psi);

    // A strike flicks the antennae (the LED's leads): each turns out about its
    // foot and back within 0.2 s, by up to 12 degrees, the harder the strike
    // the further. Two nodes; the wings never move, so their edges stay on
    // their membranes.
    i32 flick = 0;
    if (!input.reduced_motion)
        for (u32 k = 0; k < 8; ++k)
            if (state.lamps[k].since < kBeat) flick += 120 * strike / 32767 * sine(static_cast<i32>(state.lamps[k].since * 1800u / kBeat)) >> 10;
    if (flick > 120) flick = 120;  // tenths of a degree
    const Turn level = rotation(0);

    // The moth itself stays put: drift shows in its edges' wander.
    const Pose p = {level, 0};
    const bool exact = fresh;

    // The gate as drawn, in sixteen steps: the lit wing, its grey, the halo
    // and the cell redraw only when it steps.
    const i32 drawn = gate <= 328 ? 0 : ((gate + 1024) >> 11 << 11) - (gate + 1024 >= 32768 ? 1 : 0);
    // The gate's look: mode filters (lp, combo) and fades (combo, vca).
    const Mix mix = mode_mix(state.eased[kMode]);
    const i32 closed = 32767 - drawn;
    const i32 k = 32767 - (30802 * (mix.filter * power(closed, 19661) >> 15) >> 15);  // 1 - 0.94 filter closed^0.6
    const i32 rho = 30474 * (32767 - (closed * (13763 + (15073 * mix.fade >> 15)) >> 15)) >> 15;  // 0.93 (1 - closed (0.42 + 0.46 fade))
    const bool glowing = drawn > 0;
    const u32 lit_grey = grey(1024 + (2406 * power(drawn, 27853) >> 15));  // 4 + 9.4 g^0.85

    // The lamp: the vactrol's own light, haloed round the body, fading with glow.
    const Point2 lamp = {0, -16 * unit};
    for (u32 r = 0; r < kHaloCount; ++r) {
        if (!drawn) { hide(frame, kHaloFirst + r); continue; }
        const i32 shade = (static_cast<i32>(r + 1) * power(drawn, 26214) >> 7);  // k g^0.8, Q8
        pose(frame, kHaloFirst + r, p, lamp.x, lamp.y, 901 + (123 * drawn >> 15), level, grey(shade), fill_and_stroke);
    }
    // The LED's glow, in sixteen steps.
    const i32 shine = led > 1638 ? (led + 1024) >> 11 << 11 : 0;
    if (shine) pose(frame, kLedGlow, p, lamp.x, lamp.y, (10 * unit + (12 * unit * (shine > 32767 ? 32767 : shine) >> 15)) / 22, level,
                    grey(1792 + (1280 * shine >> 15)), fill_and_stroke);
    else hide(frame, kLedGlow);

    // The wings: the edges' vertices, then each membrane's wedges, the dark
    // one's teeth, and each edge, dark and, while lit, lit.
    i32 dark_reach[2][kMargin], lit_reach[2][kMargin];
    Point2 edges[2][kVertices];
    for (u32 side = 0; side < 2; ++side) {
        for (u32 i = 0; i < kMargin; ++i) {
            dark_reach[side][i] = reach(dark_wave[side][i]);
            lit_wave[side][i] = dark_wave[side][i];
        }
        melt(lit_wave[side], k);
        for (u32 i = 0; i < kMargin; ++i) lit_reach[side][i] = reach(lit_wave[side][i]) * rho >> 15;
        // Each vertex takes the reach at its margin point, blended toward the
        // reach around it (1, 2, 1) as FM rises: FM's finest wrinkles are finer
        // than 21 vertices can draw, and drawn they would pull the edge
        // away from the membrane.
        i32 dark_at[kVertices], lit_at[kLitWedges + 1], lit_top[kLitWedges];
        const i32 blur = state.eased[kFm];
        for (u32 v = 0; v < kVertices; ++v) {
            const u32 i = kVertexIndex[v], before = i ? i - 1 : i, after = i + 1 < kMargin ? i + 1 : i;
            const i32 soft = (dark_reach[side][before] + 2 * dark_reach[side][i] + dark_reach[side][after]) >> 2;
            dark_at[v] = dark_reach[side][i] + ((soft - dark_reach[side][i]) * blur >> 15);
            edges[side][v] = margin(side, i, dark_at[v], level);
        }
        for (u32 v = 0; v <= kLitWedges; ++v) {
            const u32 i = kLitIndex[v], before = i ? i - 1 : i, after = i + 1 < kMargin ? i + 1 : i;
            const i32 soft = (lit_reach[side][before] + 2 * lit_reach[side][i] + lit_reach[side][after]) >> 2;
            lit_at[v] = lit_reach[side][i] + ((soft - lit_reach[side][i]) * blur >> 15);
        }
        // Each lit wedge reaches the nearer of its ends; the lit edge runs along their tops.
        for (u32 j = 0; j < kLitWedges; ++j) lit_top[j] = lit_at[j] < lit_at[j + 1] ? lit_at[j] : lit_at[j + 1];
        // Under each chord a wedge from the root, reaching the nearer of the
        // chord's vertices, then the teeth on the chords; each redrawn only
        // once the wave has moved it about a pixel.
        for (u32 j = 0; j < kChords; ++j) {
            const i32 dark = dark_at[j] < dark_at[j + 1] ? dark_at[j] : dark_at[j + 1];
            settle(frame, kDarkFirst + kChords * side + j, p, 0, 0, clamp(dark >> 2, 16, 2 * unit), level, 4, keep,
                   0, kNearScale, 0, exact);
        }
        for (u32 j = 0; j < kLitWedges; ++j) {
            const u32 node = kLitFirst + kLitWedges * side + j;
            if (!glowing) { hide(frame, node); continue; }
            settle(frame, node, p, 0, 0, clamp(lit_top[j] >> 2, 16, 2 * unit), level, lit_grey, fill_and_stroke, 0, kNearScale, 0, exact);
        }
        teeth(frame, p, kDarkTeeth, side, edges[side], dark_at, exact);
        edge(frame, state, side, side, edges[side], level);
        const Point2 middle = {side ? 90 * unit : -90 * unit, 0};
        pose(frame, side, p, middle.x, middle.y, 2 * unit, level, 6, stroke);
        lit_edge(frame, state, 2 + side, side, glowing ? lit_top : dark_at, level);
        if (glowing) {
            pose(frame, 2 + side, p, middle.x, middle.y, 2 * unit, level, lit_grey + 1 > 15 ? 15 : lit_grey + 1, stroke);
        } else {
            hide(frame, 2 + side);
        }

        // Veins from the root to the margin.
        for (u32 v = 0; v < kVeins; ++v) {
            const Point2 tip = along(edges[side], kVeinAt[v]);
            const i32 base_x = (side ? 3 : -3) * unit, base_y = ((kVeinAt[v] - 32768) * 22) >> 6;
            const i32 root_x = base_x + ((tip.x - base_x) >> 4) * 41 / 16, root_y = base_y + ((tip.y - base_y) >> 4) * 41 / 16;  // 0.16 of the way
            const i32 dx = tip.x - root_x, dy = tip.y - root_y;
            const i32 length = static_cast<i32>(root(static_cast<u32>((dx >> 4) * (dx >> 4) + (dy >> 4) * (dy >> 4)))) * 16 * 993 >> 10;  // 0.97
            settle(frame, kVeinFirst + kVeins * side + v, p, root_x, root_y, clamp(length * unit / kVeinLength[v], 256, 2 * unit),
                   heading(dx, dy), 7, keep, kNearPlace, kNearScale * 2, kNearTurn, exact);
        }

        // Eyespots, one a voice (odd voices left, even right), each lit by its own vactrol.
        for (u32 j = 0; j < 4; ++j) {
            const u32 i = static_cast<u32>((kSpotAt[j] * static_cast<i32>(kMargin - 1) + 32768) >> 16);
            const i32 r = dark_reach[side][i] * kSpotReach[j] >> 15;
            const Point2 spot = margin(side, i, r, level);
            const i32 g = cells[2 * j + side] > 655 ? (cells[2 * j + side] + 2048) >> 12 << 12 : 0;  // eight steps
            const bool on = g > 0;
            const u32 node = kEyeFirst + 3 * (2 * j + side);
            settle(frame, node, p, spot.x, spot.y, unit, level, on ? 3 : 6, fill_and_stroke, kNearPlace, 0, 0, exact);
            const bool inside = glowing && r < lit_reach[side][i] * 7 / 8;
            settle(frame, node + 1, p, spot.x, spot.y, unit, level, inside ? lit_grey : 4, fill_and_stroke, kNearPlace, 0, 0, exact);
            settle(frame, node + 2, p, spot.x, spot.y, (1434 + (1638 * (g > 32767 ? 32767 : g) >> 15)) / 3, level,
                   on ? grey(1280 + (2560 * g >> 15)) : 3, fill_and_stroke, kNearPlace, 0, 0, exact);
        }
    }

    // The body: the vactrol. The LED's epoxy and die glow with its light, the
    // light crosses to the cell, and the cell's track glows with the gate.
    for (u32 b = 0; b < 2; ++b) {
        // An antenna turns about its foot, (-1.8, -28) or (1.8, -28) px: placed
        // at foot - R foot, turned by R.
        const Turn r = rotation(b ? flick : -flick);
        const i32 fx = b ? 1843 : -1843, fy = -28 * unit;
        pose(frame, kBodyFirst + b, p, fx - ((fx * r.cos - fy * r.sin) >> 10), fy - ((fx * r.sin + fy * r.cos) >> 10), unit, r, 0, keep);
    }
    for (u32 b = 2; b < 14; ++b) pose(frame, kBodyFirst + b, p, 0, 0, unit, level, 0, keep);
    shade(frame, kBodyFirst + 3, grey(512 + (2816 * shine >> 15)), fill_and_stroke);
    shade(frame, kBodyFirst + 6, grey(1536 + (2304 * shine >> 15)), fill_and_stroke);
    if (shine > 983) shade(frame, kBodyFirst + 8, grey(1024 + (2816 * shine >> 15)), fill_and_stroke);
    else hide(frame, kBodyFirst + 8);
    shade(frame, kBodyFirst + 10, grey(768 + (512 * drawn >> 15)), fill_and_stroke);
    shade(frame, kBodyFirst + 11, grey(1280 + (2560 * power(drawn, 26214) >> 15)), fill_and_stroke);

    // Scales knocked loose: a strike sheds up to four, more and faster the
    // harder it pings, from the wings' edges; they tumble outward and settle
    // as they fade.
    if (!input.reduced_motion && !fresh)
        for (u32 s = 0; s < strikes; ++s) {
            const u32 count = static_cast<u32>((strike * 4 + 16384) >> 15);
            for (u32 n = 0; n < count; ++n) {
                const u32 h = hash(state.seed * 7919u + n * 131u);
                const u32 side = h & 1, i = 2 + (hash(h + 1) >> 16) * (kVertices - 4) / 65536;
                const Point2 from = on_screen(p, edges[side][i].x, edges[side][i].y);
                const i32 dx = from.x - kCX, dy = from.y - kCY;
                const Turn out = heading(dx, dy);
                const i32 speed = (12 + static_cast<i32>((hash(h + 2) >> 16) * 30 >> 16)) * (16384 + strike) >> 15;  // px a second
                Scale& scale = state.scales[state.next];
                state.next = (state.next + 1) % kScaleCount;
                scale.x = from.x;
                scale.y = from.y;
                scale.vx = out.cos * speed;
                scale.vy = out.sin * speed - 6 * unit;
                scale.turn = static_cast<i32>((hash(h + 3) >> 16) * 3600 >> 16);
                scale.spin = (static_cast<i32>(hash(h + 4) >> 16) - 32768) * 8021 >> 16;  // up to 7 rad a second either way
                scale.age = 0;
                scale.life = 58982u + ((hash(h + 5) >> 16) * 45875u >> 16);  // 0.9 to 1.6 s
            }
            ++state.seed;
        }
    const i32 drag = static_cast<i32>(decay(log2_dt - log2_q16(29491) + (16 << 16)));  // e^(-dt / 0.45 s), Q16
    for (u32 s = 0; s < kScaleCount; ++s) {
        Scale& scale = state.scales[s];
        if (input.reduced_motion || scale.age >= scale.life) {
            scale.life = 0;
            hide(frame, kScaleFirst + s);
            continue;
        }
        scale.age += dt;
        const i32 step = static_cast<i32>(dt >> 6);  // Q10 seconds
        scale.vx = static_cast<i32>((static_cast<i32>(scale.vx >> 4) * (drag >> 4)) >> 8);
        scale.vy = static_cast<i32>((static_cast<i32>(scale.vy >> 4) * (drag >> 4)) >> 8) + 26 * step;
        scale.x += scale.vx * step >> 10;
        scale.y += scale.vy * step >> 10;
        scale.turn += scale.spin * step >> 10;
        if (scale.age >= scale.life || scale.y < 40 * unit || scale.y > node_bottom * unit || scale.x < node_left * unit || scale.x > node_right * unit) {
            if (scale.age >= scale.life) scale.life = 0;
            hide(frame, kScaleFirst + s);
            continue;
        }
        const i32 fade = 32767 - static_cast<i32>((scale.age << 7) / (scale.life >> 8));  // Q15
        place(frame, kScaleFirst + s, scale.x, scale.y, unit, grey(768 + (2560 * fade >> 15)), fill_and_stroke);
        opxy::draw::turn(frame, kScaleFirst + s, rotation(scale.turn));
    }
}
}
