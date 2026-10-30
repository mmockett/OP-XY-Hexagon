// Moth's screen: a moth, wings spread, drawn in line. Each wing's edge is
// the folded waveform, one cycle wrapped along its margin (the forewing the
// first half, the hindwing the second): each half-cycle swells a lobe, and
// every fold splits it into scallops; the right wing shows the wave half a
// cycle on and turned over, so bias, or FM at an odd ratio, makes the wings
// differ. FM's modulator is drawn as a strobe at the note's own rate: the
// nearest half-harmonic stands still and what is left over crawls, and at a
// high index it frays the edge, all 62 of its margin points.
//
// The body is the vactrol: the LED's leads bent up as antennae, the LED its
// head, the photocell's serpentine track the abdomen. Each held note keeps
// its own vactrol (the laws audio uses, laws.h): its LED flashes as the note
// is struck, its cell lights its own eyespot and, with the others, the wings:
// inside each edge a lit edge grows toward it and brightens, and fades at
// glow's pace. In the lit edge the gate's waveform shows: vca keeps its
// scallops as it shrinks, lp melts them. A strike flicks the antennae and
// knocks scales off the wings; drift sways the moth and makes its edges
// wander while it sounds. The screen cannot hear the voices: it follows the
// held notes and the controls. Integer arithmetic only; nothing moves while
// nothing sounds.
//
// The OP-XY redraws what changes, so the screen changes little: the wings
// are outlines, the veins and eyespots stay put on them, the wave and the
// sway move in steps of an eighth of a second, the sway in whole pixels and
// steps of 0.4 degrees, reaching one group of pieces a frame, and the light,
// the LED and the eyespots are drawn in steps, redrawn only when they step.
//
// While a note sounds the light breathes: each lit edge swells and draws in,
// a ripple running along it, slower the longer glow is and deeper the brighter
// the gate, and each voice's pupil throbs with it. The dark edges, the veins
// and the body hold still for it. The two lit edges step in turn, in 64 steps
// a breath, so this costs about one piece a frame and stops with the sound.
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
const u32 kStepShift = 13;      // the wave and the sway step every 2^13 Q16 seconds: an eighth of a second
const i32 kTilt = 4;            // the sway's steps, tenths of a degree
const u32 kWingGroup[2] = {0, 1}, kLedGroup = 2, kCellGroup = 3, kEyeGroup[2] = {4, 5}, kCellFirst = 9;
const i32 kBreathFast = 45875, kBreathSpan = 34734;  // breaths a second, Q16: 0.7 at glow 0, 0.17 at glow 100
const i32 kSwell = 655, kRipple = 393, kThrob = 4915;  // their depths: 2% and 1.2% of the reach, 15% of a pupil, Q15
const u32 kBreathStep = 26;     // a lit edge is taken 64 times a breath (a Q32 cycle's top six bits)
const i32 kApart = unit / 8;    // the closest two neighbouring points may be
const i32 kRootClear = 3584;    // the margin keeps 3.5 px from the moth's axis beside the root, Q10

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
inline Pose posed(const Sway& s) { const Pose p = {rotation(s.tilt), s.bob * unit}; return p; }
// A sway step toward v: kept until v is three quarters of a step past it.
inline i32 stepped(i32 now, i32 v, i32 step) {
    const i32 off = v - now * step;
    if (off * 4 < 3 * step && off * 4 > -3 * step) return now;
    return v >= 0 ? (v + step / 2) / step : -((-v + step / 2) / step);
}

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

inline i32 magnitude(i32 v) { return v < 0 ? -v : v; }

// What the device needs of an outline, without changing its look: neighbouring
// points at least 1/8 px apart (nearer ones merge and the path stops
// reshaping), and no turn straight back on itself (it draws as a streak). A
// folded edge's steep flanks can do either. A point too near the last goes on
// the way it was heading; a turn within about 3.6 degrees of straight back has
// its tip moved aside by an eighth of its shorter side. Moving one can upset
// its neighbour, so this repeats until none is left. The root's two points
// (0, 63) stay.
void keep_whole(Point (&p)[64]) {
    for (u32 pass = 0; pass < 8; ++pass) {
        bool moved = false;
        for (u32 i = 1; i < 63; ++i) {
            const i32 dx = p[i].x - p[i - 1].x, dy = p[i].y - p[i - 1].y;
            if (magnitude(dx) < kApart && magnitude(dy) < kApart) {
                if (magnitude(dx) >= magnitude(dy)) p[i].x = p[i - 1].x + (dx < 0 ? -kApart : kApart);
                else p[i].y = p[i - 1].y + (dy < 0 ? -kApart : kApart);
                moved = true;
            }
            const i32 ux = p[i].x - p[i - 1].x, uy = p[i].y - p[i - 1].y, vx = p[i + 1].x - p[i].x, vy = p[i + 1].y - p[i].y;
            // Each side scaled to fit 32 bits (the module has no 64-bit multiply): the angle is what counts.
            u32 us = 0, vs = 0;
            while (magnitude(ux >> us) > 16383 || magnitude(uy >> us) > 16383) ++us;
            while (magnitude(vx >> vs) > 16383 || magnitude(vy >> vs) > 16383) ++vs;
            const i32 cross = (ux >> us) * (vy >> vs) - (uy >> us) * (vx >> vs), dot = (ux >> us) * (vx >> vs) + (uy >> us) * (vy >> vs);
            if (dot >= 0 || magnitude(cross) > -dot / 16) continue;
            // Aside, on the side it already leans to.
            const bool first = magnitude(ux) + magnitude(uy) <= magnitude(vx) + magnitude(vy);
            const i32 ax = first ? -uy : -vy, ay = first ? ux : vx;
            p[i].x += (cross < 0 ? -ax : ax) / 8;
            p[i].y += (cross < 0 ? -ay : ay) / 8;
            moved = true;
        }
        // The margin's ends stay off the closing edge, the straight line between the root's points.
        const i32 away = p[63].x > 0 ? -kApart : kApart;
        if (p[62].x == p[63].x) { p[62].x += away; moved = true; }
        if (p[1].x == p[0].x) { p[1].x += away; moved = true; }
        if (!moved) break;
    }
}

// A wing's outline at the reaches (Q12), one a margin point: the root's two
// points and the margin, from the wing's middle at half size (the node is
// drawn at scale 2). The key changes when the points do.
void outline(Frame& frame, DisplayState& state, u32 path, u32 side, const i32* reaches) {
    Frame::path_type& out = frame.paths[path];
    const i32 middle = side ? 90 * unit : -90 * unit, root_x = (side ? 3 : -3) * unit;
    Point points[64];
    for (u32 i = 0; i < 64; ++i) {
        i32 x, y;
        if (i == 0 || i == 63) {
            // The root's two points, a pixel beyond the margin's ends: the closing edge
            // between them, under the body, can't double back on the margin as a lit
            // edge draws in.
            const u32 end = i ? kMargin - 1 : 0;
            x = root_x;
            y = kWingY[end] * reaches[end] >> 12;
            y += i ? unit : -unit;
        } else {
            const i32 mx = kWingX[i - 1] * reaches[i - 1] >> 12;
            x = side ? -mx : mx;
            y = kWingY[i - 1] * reaches[i - 1] >> 12;
            // A small lit edge stays out of the root's closing line, so it can't cross it.
            if (magnitude(y) < 13 * unit && magnitude(x) < kRootClear) x = side ? kRootClear : -kRootClear;
        }
        points[i].x = (x - middle) >> 1;
        points[i].y = y >> 1;
    }
    keep_whole(points);
    bool changed = !state.keys[path];
    for (u32 i = 0; i < 64; ++i) {
        changed = changed || out.points[i].x != points[i].x || out.points[i].y != points[i].y;
        out.points[i] = points[i];
    }
    if (changed) state.keys[path] = state.keys[path] + 1 ? state.keys[path] + 1 : 1;
    out.key = state.keys[path];
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
        state.breath += run * static_cast<u32>(kBreathFast - (kBreathSpan * glow >> 15));
    }

    // The wave and the sway move in steps: the clock as drawn, and the crawl
    // with it, are taken an eighth of a second at a time.
    if (fresh || (state.tau >> kStepShift) != state.beat) {
        state.beat = state.tau >> kStepShift;
        state.crawl = state.psi;
    }
    const u32 clock = state.beat << kStepShift;

    // The wave: the folder at the drawn fold (wandering with drift), FM at the drawn index.
    i32 drive = drive_q16(state.eased[kFold]);
    drive += (drive >> 4) * (drift_fold_q15(drift) * wander(clock, 21) >> 15) >> 11;
    const Fold fold = fold_plan(drive, state.eased[kBias]);
    const i32 depth = static_cast<i32>(times(static_cast<u32>(index_q16(state.eased[kFm])), 5215u));
    i32 dark_wave[2][kMargin], lit_wave[2][kMargin];
    for (u32 side = 0; side < 2; ++side) wave(dark_wave[side], side, fold, depth, shown, state.crawl);

    // Drift: the whole moth wavers while it sounds, by up to 2.9 degrees and
    // 2.2 px at full drift, in steps of 0.4 degrees and whole pixels. A step
    // reaches one group a frame (each wing's edges and veins, the LED, the
    // cell, each wing's eyespots), so no frame redraws the whole moth.
    {
        const i32 tilt = 29 * (drift * wander(clock, 2) >> 15) >> 15;     // tenths of a degree
        const i32 bob = 2253 * (drift * wander(clock, 9) >> 15) >> 15;    // Q10 px
        state.sway.tilt = stepped(fresh ? 0 : state.sway.tilt, tilt, kTilt);
        state.sway.bob = stepped(fresh ? 0 : state.sway.bob, bob, unit);
        // A strike's flash and flick keep the frames after it busy: the sway waits.
        bool moved = strikes > 0;
        for (u32 k = 0; k < 8; ++k) moved = moved || state.lamps[k].since < kBeat;
        for (u32 g = 0; g < kGroups; ++g) {
            Sway& at = state.posed[g];
            if (still) { at.tilt = state.sway.tilt * kTilt; at.bob = state.sway.bob; continue; }
            if (moved || (at.tilt == state.sway.tilt * kTilt && at.bob == state.sway.bob)) continue;
            at.tilt = state.sway.tilt * kTilt;
            at.bob = state.sway.bob;
            moved = true;
        }
    }

    // A strike flicks the antennae (the LED's leads): each turns out about its
    // foot and back within 0.2 s, by up to 12 degrees in steps of two, the
    // harder the strike the further.
    i32 flick = 0;
    if (!input.reduced_motion)
        for (u32 k = 0; k < 8; ++k)
            if (state.lamps[k].since < kBeat) flick += 120 * strike / 32767 * sine(static_cast<i32>(state.lamps[k].since * 1800u / kBeat)) >> 10;
    if (flick > 120) flick = 120;  // tenths of a degree
    flick = (flick + 10) / 20 * 20;
    const Turn level = rotation(0);

    // The gate as drawn, in sixteen steps: the lit edges, their grey and the
    // cell redraw only when it steps.
    const i32 drawn = gate <= 328 ? 0 : ((gate + 1024) >> 11 << 11) - (gate + 1024 >= 32768 ? 1 : 0);
    // The gate's look: mode filters (lp, combo) and fades (combo, vca).
    const Mix mix = mode_mix(state.eased[kMode]);
    const i32 closed = 32767 - drawn;
    const i32 k = 32767 - (30802 * (mix.filter * power(closed, 19661) >> 15) >> 15);  // 1 - 0.94 filter closed^0.6
    const i32 rho = 30474 * (32767 - (closed * (13763 + (15073 * mix.fade >> 15)) >> 15)) >> 15;  // 0.93 (1 - closed (0.42 + 0.46 fade))
    const bool glowing = drawn > 0;
    const u32 lit_grey = grey(1536 + (2304 * power(drawn, 27853) >> 15));  // 6 + 9 g^0.85
    // The LED's light, in eight steps.
    const i32 shine = led > 1638 ? (led + 2048) >> 12 << 12 : 0;

    // The wings: each edge, and inside it, while lit, the lit edge; the
    // veins, drawn in place, sway with them.
    for (u32 side = 0; side < 2; ++side) {
        const Pose p = posed(state.posed[kWingGroup[side]]);
        i32 reaches[kMargin];
        for (u32 i = 0; i < kMargin; ++i) {
            reaches[i] = reach(dark_wave[side][i]);
            lit_wave[side][i] = dark_wave[side][i];
        }
        outline(frame, state, side, side, reaches);
        const Point2 middle = {side ? 90 * unit : -90 * unit, 0};
        pose(frame, side, p, middle.x, middle.y, 2 * unit, level, 9, stroke);
        if (glowing) {
            melt(lit_wave[side], k);
            // The light breathes: this wing's step of the breath, half a step after
            // the other's, so the two edges never redraw in one frame.
            const u32 breath = ((state.breath + (side ? 1u << (kBreathStep - 1) : 0u)) >> kBreathStep) << kBreathStep;
            const i32 live = input.reduced_motion ? 0 : drawn;
            const i32 swell = kSwell * sine15(kSine15, breath) >> 15;
            // Melted, a dip's light could pass the edge: it stays a few pixels inside.
            for (u32 i = 0; i < kMargin; ++i) {
                const i32 ripple = kRipple * sine15(kSine15, breath * 2 + i * (0x80000000u / (kMargin - 1)) * 4u) >> 15;
                i32 r = reach(lit_wave[side][i]) * rho >> 15;
                r += r * ((swell + ripple) * live >> 15) >> 15;
                const i32 edge = reaches[i] * 31 >> 5;
                reaches[i] = r < edge ? r : edge;
            }
            outline(frame, state, 2 + side, side, reaches);
            pose(frame, 2 + side, p, middle.x, middle.y, 2 * unit, level, lit_grey, stroke);
        } else {
            // Hidden, but every outline keeps a shape and a key.
            if (fresh || !frame.paths[2 + side].key) outline(frame, state, 2 + side, side, reaches);
            hide(frame, 2 + side);
        }
        for (u32 v = 0; v < kVeins; ++v) pose(frame, kVeinFirst + kVeins * side + v, p, 0, 0, unit, level, 6, keep);

        // Eyespots, one a voice (odd voices left, even right), each lit by its own vactrol.
        const Pose e = posed(state.posed[kEyeGroup[side]]);
        for (u32 j = 0; j < 4; ++j) {
            const i32 g = cells[2 * j + side] > 655 ? (cells[2 * j + side] + 2048) >> 12 << 12 : 0;  // eight steps
            const bool on = g > 0;
            const u32 node = kEyeFirst + 2 * (2 * j + side);
            const i32 x = side ? -kEyeX[j] : kEyeX[j];
            pose(frame, node, e, x, kEyeY[j], 2 * unit, level, on ? 10 : 7, stroke);
            // A lit pupil throbs with its voice's own turn of the breath, in four steps.
            const i32 size = (1434 + (1638 * (g > 32767 ? 32767 : g) >> 15)) / 3;
            const i32 throb = on && !input.reduced_motion
                                  ? kThrob * (((sine15(kSine15, state.breath + (2 * j + side) * 0x20000000u) + 32768) >> 14) - 2) >> 16
                                  : 0;
            pose(frame, node + 1, e, x, kEyeY[j], size + (size * throb >> 15), level,
                 on ? grey(1280 + (2560 * g >> 15)) : 4, fill_and_stroke);
        }
    }

    // The body: the vactrol. The LED's epoxy and die glow with its light, the
    // light crosses to the cell, and the cell's track glows with the gate.
    {
        const Pose p = posed(state.posed[kLedGroup]), q = posed(state.posed[kCellGroup]);
        for (u32 b = 0; b < 2; ++b) {
            // An antenna turns about its foot, (-1.8, -28) or (1.8, -28) px: placed
            // at foot - R foot, turned by R.
            const Turn r = rotation(b ? flick : -flick);
            const i32 fx = b ? 1843 : -1843, fy = -28 * unit;
            pose(frame, kBodyFirst + b, p, fx - ((fx * r.cos - fy * r.sin) >> 10), fy - ((fx * r.sin + fy * r.cos) >> 10), unit, r, 0, keep);
        }
        for (u32 b = 2; b < kBodyCount; ++b) pose(frame, kBodyFirst + b, b < kCellFirst ? p : q, 0, 0, unit, level, 0, keep);
    }
    shade(frame, kBodyFirst + 3, grey(512 + (2816 * shine >> 15)), fill_and_stroke);
    shade(frame, kBodyFirst + 6, grey(1536 + (2304 * shine >> 15)), fill_and_stroke);
    if (shine > 983) shade(frame, kBodyFirst + 8, grey(1024 + (2816 * shine >> 15)), fill_and_stroke);
    else hide(frame, kBodyFirst + 8);
    shade(frame, kBodyFirst + 10, grey(768 + (512 * drawn >> 15)), fill_and_stroke);
    shade(frame, kBodyFirst + 11, grey(1280 + (2560 * power(drawn, 26214) >> 15)), fill_and_stroke);

    // Scales knocked loose: a strike sheds up to two, faster the harder it
    // pings, from the wings' edges; they tumble outward and settle as they
    // fade, drawn at whole pixels and in turns of 30 degrees.
    if (!input.reduced_motion && !fresh)
        for (u32 s = 0; s < strikes; ++s) {
            const u32 count = static_cast<u32>((strike * 2 + 16384) >> 15);
            for (u32 n = 0; n < count; ++n) {
                const u32 h = hash(state.seed * 7919u + n * 131u);
                const u32 side = h & 1, i = 6 + (hash(h + 1) >> 16) * (kMargin - 12) / 65536;
                const Pose p = posed(state.posed[kWingGroup[side]]);
                const i32 r = reach(dark_wave[side][i]);
                const i32 mx = kWingX[i] * r >> 12;
                const Point2 from = on_screen(p, side ? -mx : mx, kWingY[i] * r >> 12);
                const Turn out = heading(from.x - kCX, from.y - kCY);
                const i32 speed = (12 + static_cast<i32>((hash(h + 2) >> 16) * 30 >> 16)) * (16384 + strike) >> 15;  // px a second
                Scale& scale = state.scales[state.next];
                state.next = (state.next + 1) % kScaleCount;
                scale.x = from.x;
                scale.y = from.y;
                scale.vx = out.cos * speed;
                scale.vy = out.sin * speed - 6 * unit;
                scale.turn = static_cast<i32>((hash(h + 3) >> 16) * 3600 >> 16);
                scale.spin = (static_cast<i32>(hash(h + 4) >> 16) - 32768) * 3438 >> 16;  // up to 3 rad a second either way
                scale.age = 0;
                scale.life = 39322u + ((hash(h + 5) >> 16) * 26214u >> 16);  // 0.6 to 1 s
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
        i32 turn = scale.turn % 3600;
        if (turn < 0) turn += 3600;
        place(frame, kScaleFirst + s, scale.x >> 10 << 10, scale.y >> 10 << 10, unit, grey(768 + (2560 * fade >> 15)), fill_and_stroke);
        opxy::draw::turn(frame, kScaleFirst + s, rotation((turn + 150) / 300 * 300));
    }
}
}
