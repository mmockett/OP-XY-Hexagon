// Bend's screen against the track's amp envelope (SDK 1.10), run by sdk.py
// test. With the envelope unknown every frame is as it was before the screen
// read it: a checksum of a scripted session (notes held, staggered, restruck,
// released and played between frames, every control turned over a chord,
// reduced motion, a track change, unknown note activity) taken from the
// screen before the change. A short envelope against a long one, by stock's
// curves (amp_curve.h): the long attack swells the motion in, and the motion
// stops soon after a short release but goes on through a long one, ending
// with the sound. A larger attack word swells slower and a larger release
// word (stock's inverted release) ends the motion sooner. And the churn with
// a note held and through a long release stays within budget, with none once
// the release is over.
#include "../display.cpp"
#include <cstdio>
#include <cstring>

namespace {
using namespace opxy::draw;
using opxy::i32;
using opxy::u32;
typedef bend::DisplayState State;
typedef bend::Frame Frame;
int failures = 0;
State state;
Frame frame, last;

const double defaults[8] = {0.0625, 0.25, 0, 0, 0.1875, 0, 0, 0};
// The screen's note-driven motion as it stands, for the attack check.
i32 amount(const State& s) { return s.lit[0]; }
// The checksum of the scripted session with the envelope unknown, before the
// screen read the envelope.
const u32 kBefore = 710129302u;
const double kMean = 25;   // changed nodes a frame on average, held and through the release
const int kPeak = 36;

i32 q31(double v) { return static_cast<i32>(v * 2147483647.0 + (v < 1 ? 0.5 : 0)); }
const Envelope unknown = {envelope_unavailable, envelope_unavailable, envelope_unavailable, envelope_unavailable};
// A short envelope (instant attack and decay, full sustain, release word
// 0x7f000000: stock's 6 ms) and a long one (attack word 0x40000000, 1.45 s;
// full sustain; release word 0x40000000, 3 s to -66 dB). A larger release
// word is a shorter release.
const Envelope shortest = {0, 0, 0x7fffffff, 0x7f000000};
const Envelope longest = {0x40000000, 0, 0x7fffffff, 0x40000000};
Envelope with(i32 attack, i32 release) { const Envelope e = {attack, 0, 0x7fffffff, release}; return e; }

u32 mix(u32 h, u32 v) { return (h ^ v) * 16777619u; }
u32 sum(u32 h, const Frame& f) {
    for (u32 i = 0; i < Frame::node_count; ++i) {
        const Node& n = f.nodes[i];
        h = mix(mix(mix(mix(mix(mix(mix(h, static_cast<u32>(n.x)), static_cast<u32>(n.y)), static_cast<u32>(n.scale)),
                            static_cast<u32>(n.cos)), static_cast<u32>(n.sin)), n.shade), static_cast<u32>(n.paint));
    }
    for (u32 p = 0; p < Frame::path_count; ++p) {
        h = mix(h, f.paths[p].key);
        for (u32 i = 0; i < Frame::point_count; ++i)
            h = mix(mix(h, static_cast<u32>(f.paths[p].points[i].x)), static_cast<u32>(f.paths[p].points[i].y));
    }
    for (u32 t = 0; t < Frame::text_count; ++t) {
        const Text& x = f.texts[t];
        h = mix(mix(mix(h, static_cast<u32>(x.x)), static_cast<u32>(x.y)), static_cast<u32>(x.scale));
        for (u32 c = 0; c < text_capacity && x.chars[c]; ++c) h = mix(h, static_cast<u32>(x.chars[c]));
    }
    for (u32 k = 0; k < opxy::page_controls; ++k)
        for (u32 c = 0; c < header_capacity && f.header[k][c]; ++c) h = mix(h, static_cast<u32>(f.header[k][c]));
    return h;
}

void prepare(Input& input, const double* c, const Envelope& amp) {
    std::memset(&input, 0, sizeof input);
    for (int k = 0; k < 8; ++k) input.slots[k] = q31(c[k]);
    for (int k = 0; k < 4; ++k) { input.parameters[k] = input.slots[k]; input.shown[k] = static_cast<opxy::u8>(k); }
    input.track = 1;
    input.elapsed = 17;
    input.amp = amp;
}
void begin(Input& input, const double* c, const Envelope& amp) {
    prepare(input, c, amp);
    std::memset(&frame, 0, sizeof frame);
    bend::display_reset(state);
    bend::display(state, input, frame);
    for (int t = 0; t < 30; ++t) bend::display(state, input, frame);
}
bool same(const Node& a, const Node& b) {
    return a.x == b.x && a.y == b.y && a.scale == b.scale && a.cos == b.cos && a.sin == b.sin &&
           a.shade == b.shade && a.paint == b.paint;
}
// One frame: how many nodes it changed.
int step(const Input& input) {
    last = frame;
    bend::display(state, input, frame);
    if (!valid(frame)) { std::printf("FAIL a frame is not valid\n"); ++failures; }
    int changed = 0;
    for (u32 i = 0; i < Frame::node_count; ++i)
        if (!same(frame.nodes[i], last.nodes[i]) || (i < Frame::path_count && frame.paths[i].key != last.paths[i].key)) ++changed;
    return changed;
}
void start(Input& input, u32 notes) { input.note_serial += notes; input.held_notes += notes; }
void release(Input& input, u32 notes) { input.note_off_serial += notes; input.held_notes -= notes; }

// The scripted session, every frame summed.
u32 session() {
    Input input;
    begin(input, defaults, unknown);
    u32 h = sum(2166136261u, frame);
    int t = 0;
    const auto run = [&](int frames) {
        for (int i = 0; i < frames; ++i, ++t) {
            input.elapsed = t % 37 == 0 ? 33 : t % 101 == 0 ? 0 : t % 211 == 0 ? 400 : 17;
            bend::display(state, input, frame);
            h = sum(h, frame);
        }
    };
    run(20);
    start(input, 1); run(60);                              // one note
    for (int k = 0; k < 2; ++k) { start(input, 1); run(10); }   // two more, staggered
    run(50);
    release(input, 1); run(30);                            // one let go of
    ++input.note_serial; run(30);                          // a held pitch struck again
    release(input, input.held_notes); run(120);            // all let go of
    ++input.note_serial; ++input.note_off_serial; run(60); // a note played between frames
    start(input, 8); run(30);                              // eight held: every control turned
    for (int k = 0; k < 8; ++k) {
        for (int v = 0; v < 128; v += 3) { input.slots[k] = q31(v / 127.0); if (k < 4) input.parameters[k] = input.slots[k]; run(1); }
        input.slots[k] = q31(defaults[k]); if (k < 4) input.parameters[k] = input.slots[k];
        run(4);
    }
    release(input, 8); run(120);
    input.reduced_motion = 1; start(input, 2); run(30); release(input, 2); run(30); input.reduced_motion = 0;
    run(10);
    input.track = 2; start(input, 1); run(30); release(input, 1); run(90);   // another track
    input.held_notes = notes_unavailable; run(20);         // note activity unknown
    input.held_notes = 0; start(input, 3); run(40);
    bend::display_reset(state); run(30);                   // a reset with notes held
    release(input, 3); run(120);
    return h;
}

struct Played { double mean; int peak, rest, moving; i32 early; };
// A chord of `notes` held for `hold` frames, then released and watched for
// `after`: the churn while it plays and through the release, the frames after
// the release in which anything still moved, and the motion 0.3 s in.
Played play(const Envelope& amp, u32 notes, int hold, int after, const double* c) {
    Input input;
    begin(input, c, amp);
    Played r = {0, 0, 0, 0, 0};
    double total = 0;
    int counted = 0;
    start(input, notes);
    for (int t = 0; t < hold + after; ++t) {
        if (t == hold) release(input, notes);
        const int changed = step(input);
        if (t == 18) r.early = amount(state);
        if (changed > r.peak) r.peak = changed;
        if (t >= hold && changed) r.moving = t - hold + 1;
        if (t < hold || changed) { total += changed; ++counted; }
    }
    for (int t = 0; t < 60; ++t) r.rest += step(input);
    r.mean = counted ? total / counted : 0;
    return r;
}
}

int main() {
    const u32 before = session();
    if (before != kBefore) { std::printf("FAIL with the envelope unknown the frames changed: checksum %u, was %u\n", before, kBefore); ++failures; }

    // A short envelope against a long one, a chord held 2.5 s.
    const Played quick = play(shortest, 3, 150, 400, defaults), slow = play(longest, 3, 150, 400, defaults);
    std::printf("envelope: moving %.2f s after a short release, %.2f s after a long one; at 0.3 s the motion is %d short, %d long\n",
                quick.moving / 60.0, slow.moving / 60.0, quick.early, slow.early);
    if (quick.moving > 30) { std::printf("FAIL the motion runs on %.2f s past a 6 ms release\n", quick.moving / 60.0); ++failures; }
    if (slow.moving < 45) { std::printf("FAIL the motion stops %.2f s into a 3 s release\n", slow.moving / 60.0); ++failures; }
    if (slow.moving > 240) { std::printf("FAIL the motion runs on %.2f s past a 3 s release\n", slow.moving / 60.0); ++failures; }
    if (!(slow.early * 2 < quick.early)) { std::printf("FAIL a 1.45 s attack doesn't hold the motion back: %d against %d\n", slow.early, quick.early); ++failures; }
    if (quick.rest || slow.rest) { std::printf("FAIL %d and %d nodes changed at rest after the release\n", quick.rest, slow.rest); ++failures; }

    // Stock's directions: a larger release word ends the motion sooner (7.9 s,
    // 3 s, 1.35 s and 6 ms to -66 dB), and a larger attack word swells slower
    // (0.36 s, 1.45 s and 5.8 s).
    const i32 releases[4] = {0x20000000, 0x40000000, 0x60000000, 0x7f000000};
    int lasted[4];
    for (int k = 0; k < 4; ++k) lasted[k] = play(with(0, releases[k]), 3, 60, 600, defaults).moving;
    std::printf("envelope: release words 0x20, 0x40, 0x60, 0x7f move on %.2f, %.2f, %.2f and %.2f s\n",
                lasted[0] / 60.0, lasted[1] / 60.0, lasted[2] / 60.0, lasted[3] / 60.0);
    for (int k = 0; k < 3; ++k)
        if (!(lasted[k] > lasted[k + 1])) { std::printf("FAIL release word %#x doesn't end the motion sooner than %#x\n", releases[k + 1], releases[k]); ++failures; }
    const i32 attacks[3] = {0x30000000, 0x40000000, 0x50000000};
    i32 swell[3];
    for (int k = 0; k < 3; ++k) swell[k] = play(with(attacks[k], 0x7f000000), 1, 60, 30, defaults).early;
    std::printf("envelope: at 0.3 s attack words 0x30, 0x40, 0x50 have swelled to %d, %d and %d\n", swell[0], swell[1], swell[2]);
    for (int k = 0; k < 2; ++k)
        if (!(swell[k] > swell[k + 1])) { std::printf("FAIL attack word %#x doesn't swell slower than %#x\n", attacks[k + 1], attacks[k]); ++failures; }

    // Churn with the long envelope: one note and eight, held 3 s and released, at
    // the defaults and at the machine's heaviest settings.
    const double heavy[8] = {0.7, 1, 1, 1, 0.9, 1, 1, 1};
    double worst = 0;
    int peak = 0;
    for (int s = 0; s < 2; ++s)
        for (u32 notes = 1; notes <= 8; notes += 7) {
            const Played r = play(longest, notes, 180, 400, s ? heavy : defaults);
            std::printf("envelope churn, %s, %u note%s: %.1f nodes a frame on average, peak %d, %d at rest\n",
                        s ? "heaviest" : "defaults", notes, notes > 1 ? "s" : "", r.mean, r.peak, r.rest);
            if (r.mean > worst) worst = r.mean;
            if (r.peak > peak) peak = r.peak;
            if (r.rest) { std::printf("FAIL %d nodes changed at rest\n", r.rest); ++failures; }
        }
    if (worst > kMean) { std::printf("FAIL %.1f nodes a frame on average, over %.0f\n", worst, kMean); ++failures; }
    if (peak > kPeak) { std::printf("FAIL %d nodes in a frame, over %d\n", peak, kPeak); ++failures; }
    if (failures) { std::printf("envelope: %d faults\n", failures); return 1; }
    std::printf("envelope: unchanged when unknown; churn at most %.1f a frame on average, peak %d, none at rest\n", worst, peak);
    return 0;
}
