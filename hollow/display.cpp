// Hollow's screen: a shelf of eight vessels in the body's shape. Each held
// note lights one, left to right; it swells as the note blooms (slower for a
// long ring) and rings on for Ring's time once released. Ripples rise from a
// ringing vessel's mouth, rounder for oo and flatter for ee, doubled as Spread
// detunes; breath motes drift up from it, glass glints, sympathy arcs join
// vessels that ring together, and the air line along the top stirs with all
// of it. Integer arithmetic only; nothing moves while nothing rings.
#include "display.h"
#include "modes.h"

namespace hollow {
namespace screen {
using namespace opxy::draw;

const i32 shelf = 196;
const i32 mouth_height[kBodies] = {64, 78, 72, 66};   // vox, str, pipe, glass
const i32 ripple_radius = 13;                          // as authored
// Palette entries (tools/make_panel.py).
const u32 at_rest = 0, ringing = 1, ripple_ink = 6, arc_ink = 10, air_ink = 13, mote_ink = 14, glint_ink = 15;
const i32 lit_floor = 1638;                            // 0.05: below this a vessel is dark and still
const u32 radian = 683565276u;                         // one radian in Q32 cycles

inline i32 vessel_x(u32 k) { return 51 + 54 * static_cast<i32>(k); }

// A fixed pseudo-random value 0..65535 for n.
inline u32 hash(u32 n) {
    n ^= n >> 16;
    n *= 0x7feb352du;
    n ^= n >> 15;
    n *= 0x846ca68bu;
    n ^= n >> 16;
    return n >> 16;
}
// sin of a phase in Q32 cycles, Q10.
inline i32 wave(u32 phase) { return sine(static_cast<i32>(((phase >> 16) * 3600u) >> 16)); }

// exp(-dt / time), Q16, for dt in Q10 seconds and time in Q16 seconds.
inline i32 decay(i32 dt, u32 time) {
    const u32 time_q10 = time >> 6;
    const u32 ratio = (static_cast<u32>(dt) << 16) / (time_q10 ? time_q10 : 1u);   // Q16
    u32 octaves = times(ratio, 94548u);                                             // times log2 e
    if (octaves > (20u << 16)) octaves = 20u << 16;
    return static_cast<i32>(exp2_q16(-static_cast<i32>(octaves)));
}

struct Ripple { i32 x, y, scale; u32 shade; };

inline void place_air(DisplayState& state, Frame& frame, i32 amp, bool fresh) {
    if (fresh || amp != state.air_amp || (amp && state.tau != state.air_tau)) {
        state.air_amp = amp;
        state.air_tau = state.tau;
        state.air_key = state.air_key + 1 ? state.air_key + 1 : 1;
        for (u32 h = 0; h < 2; ++h) {
            Frame::path_type& path = frame.paths[h];
            for (u32 i = 0; i < 32; ++i) {
                // 32 samples out along the line and back 0.15 px below: a closed ribbon at scale 2.
                const i32 x = -56 * unit + static_cast<i32>(i) * 114688 / 31;
                const u32 screen_x = static_cast<u32>((128 + 224 * static_cast<i32>(h)) * unit + 2 * x);
                // The board's two travelling waves, 160 and 61 px long.
                const u32 slow = screen_x * 26214u + state.tau * 24248u;
                const u32 quick = screen_x * 68760u - state.tau * 54395u;
                const i32 shape = (614 * wave(slow) + 410 * wave(quick)) >> 10;
                const i32 y = (amp * shape >> 10) >> 1;
                path.points[i].x = x;
                path.points[i].y = y;
                path.points[63 - i].x = x;
                path.points[63 - i].y = y + 77;
            }
            path.key = state.air_key;
        }
    }
    for (u32 h = 0; h < 2; ++h) place(frame, h, (128 + 224 * static_cast<i32>(h)) * unit, 56 * unit, 2 * unit, air_ink, stroke);
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
    // Captions in the units the sound uses (modes.h): body family, ring time
    // (as the body scales it), vowel and cents; amounts elsewhere.
    const char* const titles[8] = {"Breath", "Body", "Bright", "Ring", "Air", "Vowel", "Spread", "Sympathy"};
    char values[8][header_capacity];
    for (opxy::u32 i = 0; i < 8; ++i) header_amount(values[i], header_capacity, input.slots[i]);
    body_caption(values[1], header_capacity, control(input.slots[1]));
    ring_caption(values[3], header_capacity, control(input.slots[3]), control(input.slots[1]));
    vowel_caption(values[5], header_capacity, control(input.slots[5]));
    spread_caption(values[6], header_capacity, control(input.slots[6]));
    const opxy::u8 slots[2][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}};
    page_header(state.header, input, frame, titles, values, header_first, slots);

    const i32 breath = control(input.slots[0]), body = control(input.slots[1]), bright = control(input.slots[2]);
    const i32 ring = control(input.slots[3]), air = control(input.slots[4]), vowel = control(input.slots[5]);
    const i32 spread = control(input.slots[6]), sympathy = control(input.slots[7]);
    // The screen draws the nearest family and the nearest vowel's ripple.
    u32 family = static_cast<u32>((body * 3 + 16384) >> 15);
    if (family > 3) family = 3;
    u32 aspect = static_cast<u32>((vowel * 4 + 16384) >> 15);
    if (aspect >= aspect_count) aspect = aspect_count - 1;
    const i32 mouth = shelf - mouth_height[family];

    // Notes: each held note lights a vessel, left to right. A track change or
    // reset shows the end state at once, as reduced motion always does.
    const bool fresh = !state.started || !state.notes.initialized || state.notes.track != input.track;
    state.notes.update(input);
    const u32 held = input.held_notes <= 128 ? input.held_notes : 0;
    const u32 lit = held < 8 ? held : 8;
    const i32 dt = clamp(input.elapsed, 0, 102);
    if (fresh) {
        for (u32 k = 0; k < 8; ++k) state.energy[k] = k < lit ? 32768 : 0;
        state.tau = 0;
    } else {
        // A note started and released between frames still rings its vessel.
        const u32 starts = input.note_serial - state.serial, grew = held > state.held ? held - state.held : 0;
        const u32 quick = starts > grew ? (starts - grew < 8 ? starts - grew : 8) : 0;
        for (u32 m = 0; m < quick; ++m) {
            const u32 k = lit + m < 8 ? lit + m : 7;
            if (state.energy[k] < 9830) state.energy[k] = 9830;
        }
        // A vessel swells toward full while its note is held (more slowly with
        // more breath and a longer ring, as a high-Q body blooms) and rings out
        // over Ring's time once released: ring time from the engine's mapping.
        const u32 seconds = exp2_q16(ring_log2_seconds(ring, body));   // Q16
        const u32 swell = 13107u + static_cast<u32>(26214 * breath >> 15) + (seconds / 6 < 163840u ? seconds / 6 : 163840u);
        const i32 rise = decay(dt, swell), fall = decay(dt, seconds / 3);
        for (u32 k = 0; k < 8; ++k) {
            i32& e = state.energy[k];
            if (input.reduced_motion) e = k < lit ? 32768 : 0;
            else if (k < lit) e += (32768 - e) * ((65536 - rise) >> 1) >> 15;
            else e -= e * ((65536 - fall) >> 1) >> 15;
            if (e < lit_floor && k >= lit) e = 0;
        }
    }
    state.serial = input.note_serial;
    state.held = held;
    state.started = 1;
    i32 sounding = 0, total = 0;
    for (u32 k = 0; k < 8; ++k) {
        if (state.energy[k] > sounding) sounding = state.energy[k];
        total += state.energy[k];
    }
    // The clock runs as fast as the loudest vessel rings, so motion fades with the sound.
    if (!input.reduced_motion && !fresh) {
        const u32 pace = static_cast<u32>(sounding * 3 / 2 < 32768 ? sounding * 3 / 2 : 32768);
        state.tau += (static_cast<u32>(dt) << 6) * pace >> 15;
    }
    const u32 tau = state.tau;

    // Air: the line along the top stirs with what sounds, more with more air.
    const i32 stir = total / 3 < 32768 ? total / 3 : 32768;
    place_air(state, frame, (1024 + (7168 * air >> 15)) * stir >> 15, fresh);

    // Vessels: the body's family, dark at rest, glowing as each one rings.
    for (u32 f = 0; f < kBodies; ++f)
        for (u32 k = 0; k < 8; ++k) {
            const u32 node = vessel_first + 8 * f + k;
            if (f != family) { hide(frame, node); continue; }
            const i32 e = state.energy[k];
            const i32 glow = e * 5 >> 15;
            place(frame, node, vessel_x(k) * unit, shelf * unit, unit,
                  e >= lit_floor ? ringing + static_cast<u32>(glow < 4 ? glow : 4) : at_rest, fill_and_stroke);
        }
    // A pipe's mouth stays dark, outlined as its pipe is.
    for (u32 k = 0; k < 8; ++k) {
        if (family != 2) { hide(frame, mouth_first + k); continue; }
        place(frame, mouth_first + k, vessel_x(k) * unit, shelf * unit, unit,
              state.energy[k] >= lit_floor ? ringing : at_rest, fill_and_stroke);
    }

    // Sympathy: an arc between vessels ringing together, within three of each
    // other, brighter as both ring, pulsing slowly.
    u32 n = 0;
    for (u32 i = 0; i < 8; ++i)
        for (u32 j = i + 1; j < 8 && j <= i + 3; ++j, ++n) {
            const i32 e = state.energy[i] < state.energy[j] ? state.energy[i] : state.energy[j];
            if (sympathy <= 655 || e < lit_floor) { hide(frame, arc_first + n); continue; }
            const i32 pulse = 21299 + (11469 * wave(tau * 29491u + i * 888634858u + j * radian) >> 10);
            const i32 level = (sympathy * e >> 15) * pulse >> 15;
            const i32 step = level * 4 >> 15;
            place(frame, arc_first + n, vessel_x(i) * unit, (mouth - 4) * unit, unit,
                  step > 0 ? arc_ink + static_cast<u32>((step < 3 ? step : 3) - 1) : ripple_ink, fill_and_stroke);
        }

    // Ripples rising from each ringing mouth: slower and wider the longer it
    // rings, fading as they rise and brighter with Bright, doubled and offset
    // as Spread detunes half the modes. A pool holds 29 of the 48 a full
    // chord with spread could show: the faintest go first.
    Ripple ripples[48];
    u32 count = 0;
    const u32 rate = 16384u + static_cast<u32>(36045 * (32767 - ring) >> 15);   // Q16 cycles a second
    const i32 reach = 8192 + (14336 * ring >> 15);                              // Q10 px
    const i32 lift = 14746 + (18022 * bright >> 15);
    const bool doubled = spread > lit_floor;
    const i32 offset = spread * 3072 >> 15;                                       // up to 3 px each way
    for (u32 k = 0; k < 8; ++k) {
        const i32 e = state.energy[k];
        if (e < lit_floor) continue;
        const i32 strength = e * lift >> 15;
        for (u32 i = 0; i < 3; ++i) {
            const u32 p = (tau * rate + i * 0x55555555u + hash(k + 11) * 19661u) >> 16;   // Q16 of its rise
            const i32 scale = (4096 + static_cast<i32>(p * static_cast<u32>(reach) >> 16)) / ripple_radius;
            const i32 level = (static_cast<i32>(65536 - p) >> 1) * strength >> 15;
            const i32 y = (mouth - 6) * unit - static_cast<i32>(p >> 2);
            const i32 x = vessel_x(k) * unit;
            if (doubled) {
                ripples[count++] = Ripple{x - offset, y, scale, static_cast<u32>(level * 4 >> 15)};
                ripples[count++] = Ripple{x + offset, y, scale, static_cast<u32>((level * 22938 >> 15) * 4 >> 15)};
            } else {
                ripples[count++] = Ripple{x, y, scale, static_cast<u32>(level * 4 >> 15)};
            }
        }
    }
    u32 used = 0;
    for (u32 s = 4; s-- > 0;)
        for (u32 r = 0; r < count && used < ripple_pool; ++r) {
            const u32 shade = ripples[r].shade < 3 ? ripples[r].shade : 3;
            if (shade != s) continue;
            place(frame, ripple_first + ripple_pool * aspect + used++, ripples[r].x, ripples[r].y, ripples[r].scale,
                  ripple_ink + shade, stroke);
        }
    for (u32 a = 0; a < aspect_count; ++a)
        for (u32 r = a == aspect ? used : 0; r < ripple_pool; ++r) hide(frame, ripple_first + ripple_pool * a + r);

    // Breath: up to three motes drift up from each ringing vessel.
    const u32 motes = static_cast<u32>((breath * 3 + 16384) >> 15);
    for (u32 k = 0; k < 8; ++k)
        for (u32 i = 0; i < 3; ++i) {
            const u32 node = mote_first + 3 * k + i;
            if (i >= motes || state.energy[k] < lit_floor) { hide(frame, node); continue; }
            const u32 speed = (7u * 65536u + 6u * hash(3 * k + i + 101)) / 46;   // Q16 rises a second
            const u32 rise = (tau * speed + hash(7 * k + i + 211) * 56988u) >> 16;
            const i32 sway = 5 * wave(tau * 19661u + i * 2 * radian + k * radian);
            place(frame, node, vessel_x(k) * unit + sway, (mouth - 4) * unit - static_cast<i32>(rise * 46 >> 6), unit,
                  mote_ink, fill_and_stroke);
        }

    // Glass: glints catch on the bowl, more as it brightens.
    const u32 glints = family == 3 ? static_cast<u32>((bright * 2 + 16384) >> 15) : 0;
    for (u32 k = 0; k < 8; ++k)
        for (u32 i = 0; i < 2; ++i) {
            const u32 node = glint_first + 2 * k + i;
            if (i >= glints || state.energy[k] < lit_floor) { hide(frame, node); continue; }
            const u32 tick = (tau * 3u + hash(k + 5 * i + 307) * 3u) >> 16;
            if (hash(tick * 13 + 7 * k + i + 401) > 36045) { hide(frame, node); continue; }
            const i32 x = vessel_x(k) * unit + ((static_cast<i32>(hash(tick + k + i + 503)) - 32768) * 22 >> 6);
            const i32 y = (shelf - 8) * unit - static_cast<i32>(hash(3 * tick + k + i + 601) * 56 >> 6);
            place(frame, node, x, y, unit, glint_ink, fill_and_stroke);
        }
}
}
