// Wane's sound checks, compiled for the host and run by
// python sdk.py test --machine <folder>. Each check prints FAIL with a reason;
// the program fails if any does.
#include "../engine.cpp"
#include "../display.cpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
using wane::i32;
using wane::u32;
const unsigned frames = opxy::block_frames;
const i32 full = 0x7fffffff;
const i32 half = 0x18000000;   // definition.json outputPeakQ31
const double cent = 65536.0 / 1200.0;   // Q16 octaves per cent
int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}
i32 word(double value) { return static_cast<i32>(value * 2147483647.0 + 0.5); }
u32 increment(double hz) { return static_cast<u32>(hz / 44100.0 * 4294967296.0); }

// The defaults, in definition.json's order.
struct Controls {
    i32 p[8];
    Controls() {
        const double defaults[8] = {0.5, 0.4, 0, 0.35, 0, 0, 0, 0};
        for (unsigned i = 0; i < 8; ++i) p[i] = word(defaults[i]);
    }
    Controls& set(unsigned slot, double value) { p[slot] = word(value); return *this; }
};

// A tape and up to eight voices, rendered as the device does: each sounding
// voice once per block.
struct Rig {
    wane::Shared shared;
    wane::Voice voices[8];
    i32 out[8][frames];
    u32 pitch[frames];
    explicit Rig(double hz = 220) {
        std::memset(&shared, 0, sizeof shared);
        std::memset(voices, 0, sizeof voices);
        tune(hz);
    }
    void tune(double hz) { for (unsigned i = 0; i < frames; ++i) pitch[i] = increment(hz); }
    void play(unsigned v) { wane::initialize(voices[v], shared, v + 1); wane::start(voices[v], shared, v + 1); }
    void render(unsigned v, const Controls& c) { wane::render(voices[v], shared, v + 1, out[v], c.p, pitch); }
};

double rms(const double* x, unsigned n) {
    double sum = 0;
    for (unsigned i = 0; i < n; ++i) sum += x[i] * x[i];
    return std::sqrt(sum / n);
}
// The amplitude of one frequency in a signal (Goertzel, Hann-windowed).
double amplitude(const double* x, unsigned n, double hz) {
    const double w = 2 * M_PI * hz / 44100.0, k = 2 * std::cos(w);
    double s1 = 0, s2 = 0, window = 0;
    for (unsigned i = 0; i < n; ++i) {
        const double hann = 0.5 - 0.5 * std::cos(2 * M_PI * i / n);
        const double s0 = x[i] * hann + k * s1 - s2;
        s2 = s1;
        s1 = s0;
        window += hann;
    }
    return 2 * std::sqrt(std::fabs(s1 * s1 + s2 * s2 - k * s1 * s2)) / window;
}
// High-frequency share: the first difference's power over the signal's.
double brightness(const double* x, unsigned n) {
    double d = 0, s = 0;
    for (unsigned i = 1; i < n; ++i) { d += (x[i] - x[i - 1]) * (x[i] - x[i - 1]); s += x[i] * x[i]; }
    return d / s;
}
double correlation(const double* a, const double* b, unsigned n) {
    double ma = 0, mb = 0;
    for (unsigned i = 0; i < n; ++i) { ma += a[i]; mb += b[i]; }
    ma /= n; mb /= n;
    double ab = 0, aa = 0, bb = 0;
    for (unsigned i = 0; i < n; ++i) { ab += (a[i] - ma) * (b[i] - mb); aa += (a[i] - ma) * (a[i] - ma); bb += (b[i] - mb) * (b[i] - mb); }
    return ab / std::sqrt(aa * bb + 1e-30);
}
// One voice's sound for `blocks` blocks after `settle` blocks, as doubles;
// `aligned` starts both oscillators in phase, so without detune they are one.
const unsigned max_samples = 1 << 16;
double sound[max_samples];
Rig solo;
unsigned capture(const Controls& c, double hz, unsigned blocks, bool aligned = false, unsigned settle = 64) {
    solo = Rig(hz);
    solo.play(0);
    if (aligned) solo.voices[0].phase[1] = solo.voices[0].phase[0];
    for (unsigned b = 0; b < settle; ++b) solo.render(0, c);
    for (unsigned b = 0; b < blocks; ++b) {
        solo.render(0, c);
        for (unsigned i = 0; i < frames; ++i) sound[b * frames + i] = solo.out[0][i] / 2147483648.0;
    }
    return blocks * frames;
}

// Every extreme stays within half scale, writes every sample, sounds, and
// carries no DC; a restart and a voice taken mid-tape stay bounded too.
Rig rig;
void extremes() {
    const double shapes[6] = {0, 0.25, 0.5, 0.65, 0.8, 1};
    const double pitches[3] = {27.5, 440, 4186};
    double loudest = 0;
    long clamped = 0, samples = 0;
    bool bounded = true, written = true, sounding = true, centred = true;
    for (unsigned s = 0; s < 6; ++s)
        for (unsigned corner = 0; corner < 128; ++corner)
            for (unsigned h = 0; h < 3; ++h) {
                Controls c;
                c.set(wane::controls::SHAPE, shapes[s]);
                for (unsigned k = 0; k < 7; ++k) c.p[k + 1] = (corner >> k) & 1 ? full : 0;
                rig = Rig(pitches[h]);
                rig.play(0);
                double sum = 0, power = 0;
                for (unsigned b = 0; b < 160; ++b) {
                    if (b == 80) wane::start(rig.voices[0], rig.shared, 1);
                    if (b == 100) rig.play(1);   // a second voice joins a running tape
                    for (unsigned v = 0; v < (b >= 100 ? 2u : 1u); ++v) {
                        for (unsigned i = 0; i < frames; ++i) rig.out[v][i] = full;   // never produced
                        rig.render(v, c);
                        for (unsigned i = 0; i < frames; ++i) {
                            const i32 x = rig.out[v][i];
                            written = written && x != full;
                            bounded = bounded && x >= -half && x <= half;
                            clamped += x == half || x == -half;
                            ++samples;
                            const double y = std::fabs(x / 2147483648.0);
                            if (x != full && y > loudest) loudest = y;
                            if (v == 0 && b >= 16) { sum += x / 2147483648.0; power += (x / 2147483648.0) * (x / 2147483648.0); }
                        }
                    }
                }
                const double n = 144.0 * frames;
                // Full warmth's 0.9 kHz low-pass takes a 4 kHz triangle down
                // about 30 dB; that is still sound, not silence.
                if (std::sqrt(power / n) <= 0.003 && sounding) {
                    std::printf("quiet: shape %.2f corner %u at %.1f Hz: %.4f\n", shapes[s], corner, pitches[h], std::sqrt(power / n));
                    sounding = false;
                }
                centred = centred && std::fabs(sum / n) < 0.02;
            }
    check(bounded, "an extreme setting exceeds half scale");
    check(written, "render left samples unwritten");
    check(sounding, "an extreme setting is (nearly) silent");
    check(centred, "an extreme setting carries DC");
    // The final clamp is a guard, not the sound: it should almost never engage.
    check(clamped * 10000 < samples, "the output clamp engages too often");
    std::printf("extremes: loudest %.3f of full scale, clamped %ld of %ld samples\n", loudest, clamped, samples);
}

// With every colouring control at zero the sound is clean: no wobble, sag,
// drift or hiss, wherever on the tape a note is played.
void defaults_clean() {
    rig = Rig(330);
    Controls c;
    for (unsigned v = 0; v < 4; ++v) rig.play(v);
    bool still = true;
    const i32 spread = wane::tape::depth(c.p[wane::controls::DETUNE], wane::controls::detune_most) / 2;
    for (unsigned b = 0; b < 4000; ++b) {
        if (b % 700 == 350) wane::start(rig.voices[b % 4], rig.shared, b % 4 + 1);
        for (unsigned v = 0; v < 4; ++v) {
            rig.render(v, c);
            still = still && rig.shared.bend == 0
                 && rig.voices[v].offset[0] == -spread && rig.voices[v].offset[1] == spread;
        }
    }
    check(still, "defaults: the pitch or level moves with colouring controls at zero");
    // The same voice played at two places on the tape sounds the same.
    wane::Voice a = rig.voices[0], b = rig.voices[0];
    wane::Shared early = rig.shared, late = rig.shared;
    late.clock += 123457;
    i32 x[frames], y[frames];
    bool same = true;
    for (unsigned k = 0; k < 200; ++k) {
        wane::render(a, early, 1, x, c.p, rig.pitch);
        wane::render(b, late, 1, y, c.p, rig.pitch);
        same = same && std::memcmp(x, y, sizeof x) == 0;
    }
    check(same, "defaults: the sound depends on where the tape is");
}

// Every voice bends with the one tape: deviations of different voices move
// together, and they are the tape's pure function of its clock.
void shared_tape() {
    rig = Rig(220);
    Controls c;
    c.set(wane::controls::WOW, 1).set(wane::controls::FLUTTER, 1).set(wane::controls::DRIFT, 0.5)
     .set(wane::controls::DETUNE, 0);
    for (unsigned v = 0; v < 3; ++v) rig.play(v);
    const unsigned blocks = 6000;
    static double first[6000], second[6000], third[6000];
    bool mirrored = true;
    const wane::tape::Depths d = wane::tape::depths(c.p[wane::controls::WOW], c.p[wane::controls::FLUTTER],
                                                    c.p[wane::controls::SAG]);
    for (unsigned b = 0; b < blocks; ++b) {
        for (unsigned v = 0; v < 3; ++v) rig.render(v, c);
        first[b] = rig.voices[0].offset[0] / cent;
        second[b] = rig.voices[1].offset[0] / cent;
        third[b] = rig.voices[2].offset[1] / cent;
        mirrored = mirrored && (b == 0 || rig.shared.bend == wane::tape::bend(rig.shared.clock, d));
    }
    check(mirrored, "tape: the audio's bend is not tape::bend() of its clock");
    check(correlation(first, second, blocks) > 0.9, "tape: two voices' pitch deviations are not correlated");
    check(correlation(first, third, blocks) > 0.9, "tape: two oscillators' deviations are not correlated");
    double low = 0, high = 0;
    for (unsigned b = 0; b < blocks; ++b) { low = std::fmin(low, first[b]); high = std::fmax(high, first[b]); }
    check(high - low > 40 && low > -120 && high < 60, "tape: full wow and flutter swing the wrong amount");
    // Without drift, every voice follows the tape exactly.
    c.set(wane::controls::DRIFT, 0);
    bool exact = true;
    for (unsigned b = 0; b < 500; ++b)
        for (unsigned v = 0; v < 3; ++v) {
            rig.render(v, c);
            exact = exact && rig.voices[v].offset[0] == rig.shared.bend && rig.voices[v].offset[1] == rig.shared.bend;
        }
    check(exact, "tape: a voice without drift does not follow the tape exactly");
}

// The tape advances once per audio block, however many voices render and in
// whatever order, as voices come and go.
void once_per_block() {
    for (unsigned count = 1; count <= 8; ++count) {
        rig = Rig(220);
        Controls c;
        for (unsigned v = 0; v < count; ++v) rig.play(v);
        const u32 before = rig.shared.clock;
        const unsigned blocks = 1000;
        u32 order = 12345;
        for (unsigned b = 0; b < blocks; ++b) {
            // A shuffled order each block; from half way the last voice sits out
            // one block in four, as a stolen or finished voice would.
            unsigned list[8];
            for (unsigned v = 0; v < count; ++v) list[v] = v;
            for (unsigned v = count; v > 1; --v) {
                order = order * 1664525u + 1013904223u;
                const unsigned j = (order >> 16) % v, t = list[v - 1];
                list[v - 1] = list[j];
                list[j] = t;
            }
            for (unsigned v = 0; v < count; ++v) {
                if (count > 1 && b > blocks / 2 && b % 4 == 0 && list[v] == count - 1) continue;
                rig.render(list[v], c);
            }
        }
        const long advanced = static_cast<long>(rig.shared.clock - before);
        char what[96];
        std::snprintf(what, sizeof what, "tape: %u voices advanced the tape %ld blocks in %u", count, advanced, blocks);
        check(advanced >= static_cast<long>(blocks) - 2 && advanced <= static_cast<long>(blocks), what);
    }
}

// The tape's motions have the promised character.
void tape_character() {
    using namespace wane::tape;
    const u32 seconds = 120, blocks = seconds * 689;
    // Wow: slow and irregular, 0.3 to 1.5 Hz, never one steady period.
    unsigned crossings = 0;
    double last = 0, gaps = 0, gaps2 = 0, peak = 0;
    for (u32 t = 1; t < blocks; ++t) {
        const bool up = wow(t) >= 0, before = wow(t - 1) >= 0;
        peak = std::fmax(peak, std::fabs(wow(t) / 32767.0));
        if (up != before) {
            if (crossings) { const double g = t - last; gaps += g; gaps2 += g * g; }
            last = t;
            ++crossings;
        }
    }
    const double rate = crossings / 2.0 / seconds, mean = gaps / (crossings - 1);
    const double spread = std::sqrt(gaps2 / (crossings - 1) - mean * mean) / mean;
    std::printf("tape: wow %.2f Hz (gap spread %.2f), ", rate, spread);
    check(rate > 0.3 && rate < 1.5, "wow: its rate is outside 0.3 to 1.5 Hz");
    check(spread > 0.25, "wow: too regular, like a plain sine");
    check(peak > 0.6 && peak <= 1.0, "wow: its depth is not used");
    // Flutter: 6 to 12 Hz.
    crossings = 0;
    for (u32 t = 1; t < blocks; ++t) crossings += (flutter(t) >= 0) != (flutter(t - 1) >= 0);
    const double flutter_rate = crossings / 2.0 / seconds;
    std::printf("flutter %.2f Hz, ", flutter_rate);
    check(flutter_rate > 6 && flutter_rate < 12, "flutter: its rate is outside 6 to 12 Hz");
    // Stretch: occasional drags, a few seconds apart, never negative.
    unsigned drags = 0;
    bool sinking = true;
    for (u32 t = 1; t < blocks; ++t) {
        sinking = sinking && stretch(t) >= 0 && stretch(t) <= one;
        drags += stretch(t) > 0 && stretch(t - 1) == 0;
    }
    std::printf("%u drags in %u s\n", drags, seconds);
    check(sinking, "stretch: out of range");
    check(drags >= seconds / 15 && drags <= seconds / 4, "stretch: drags are not occasional");
    // Pure: the same clock always gives the same tape.
    const Depths d = depths(full, full, full);
    check(bend(98765, d) == bend(98765, d) && wow(5) == wow(5), "tape: not a pure function of the clock");
    // A display clock: one second of Q10 frames is 689 blocks.
    Clock clock;
    clock.reset();
    for (unsigned f = 0; f < 64; ++f) clock.advance(16);
    check(clock.blocks == 689, "tape: the display clock does not keep audio time");
}

// The start sag: each note pulls the tape flat by the sag's depth, and it
// catches up within a second; with sag at zero nothing moves.
void start_sag() {
    rig = Rig(220);
    Controls c;
    c.set(wane::controls::SAG, 1).set(wane::controls::DETUNE, 0);
    rig.play(0);
    for (unsigned b = 0; b < 1000; ++b) rig.render(0, c);
    rig.play(1);   // the second note sags the first one too
    double lowest = 0, first = 0;
    for (unsigned b = 0; b < 700; ++b) {
        rig.render(0, c);
        rig.render(1, c);
        lowest = std::fmin(lowest, rig.voices[0].offset[0] / cent);
        first = rig.voices[0].offset[0] / cent;
    }
    char what[96];
    std::snprintf(what, sizeof what, "sag: a note start sags the tape %.1f cents, not about 45", lowest);
    check(lowest < -40 && lowest > -50, what);
    check(std::fabs(first) < 2, "sag: the tape has not caught up within a second");
    c.set(wane::controls::SAG, 0);
    wane::start(rig.voices[0], rig.shared, 1);
    bool flat = true;
    for (unsigned b = 0; b < 100; ++b) { rig.render(0, c); flat = flat && rig.shared.bend == 0; }
    check(flat, "sag: the tape sags with sag at zero");
}

// Detune spreads the oscillators by its depth in cents; drift moves each note
// and voice on its own; at zero both are exact.
void detune_and_drift() {
    const double settings[3] = {0, 0.4, 1}, cents[3] = {0, 8, 50};
    for (unsigned k = 0; k < 3; ++k) {
        rig = Rig(440);
        Controls c;
        c.set(wane::controls::DETUNE, settings[k]);
        rig.play(0);
        rig.render(0, c);
        rig.render(0, c);
        const double ratio = static_cast<double>(rig.voices[0].increment[1]) / rig.voices[0].increment[0];
        const double measured = 1200 * std::log2(ratio);
        char what[80];
        std::snprintf(what, sizeof what, "detune: %.2f should spread %.0f cents, spreads %.2f", settings[k], cents[k], measured);
        check(std::fabs(measured - cents[k]) < 0.3, what);
        const double centre = 1200 * std::log2(std::sqrt(static_cast<double>(rig.voices[0].increment[1]) *
                                                         rig.voices[0].increment[0]) / increment(440));
        check(std::fabs(centre) < 0.3, "detune: the pair is not centred on the note");
    }
    // Drift: each note starts its own distance off, and the voice wanders.
    rig = Rig(440);
    Controls c;
    c.set(wane::controls::DRIFT, 1).set(wane::controls::DETUNE, 0);
    rig.play(0);
    double starts[12], low = 1e9, high = -1e9;
    for (unsigned n = 0; n < 12; ++n) {
        wane::start(rig.voices[0], rig.shared, 1);
        rig.render(0, c);
        starts[n] = rig.voices[0].offset[0] / cent;
        for (unsigned b = 0; b < 2000; ++b) {
            rig.render(0, c);
            low = std::fmin(low, rig.voices[0].offset[0] / cent);
            high = std::fmax(high, rig.voices[0].offset[0] / cent);
        }
    }
    double s_low = 1e9, s_high = -1e9;
    for (unsigned n = 0; n < 12; ++n) { s_low = std::fmin(s_low, starts[n]); s_high = std::fmax(s_high, starts[n]); }
    check(s_high - s_low > 8, "drift: notes do not start their own distance off pitch");
    check(high - low > 10 && low > -36 && high < 36, "drift: the voice does not wander, or wanders too far");
}

// Shape: sine, triangle, saw, square and pulse, band-limited.
void shapes() {
    Controls c;
    c.set(wane::controls::DETUNE, 0).set(wane::controls::WARMTH, 0);
    const double f = 441;
    unsigned n = capture(c.set(wane::controls::SHAPE, 0), f, 1024, true);
    const double sine1 = amplitude(sound, n, f), sine2 = amplitude(sound, n, 2 * f), sine3 = amplitude(sound, n, 3 * f);
    n = capture(c.set(wane::controls::SHAPE, 0.125), f, 1024, true);
    const double between1 = amplitude(sound, n, f);
    n = capture(c.set(wane::controls::SHAPE, 0.25), f, 1024, true);
    const double tri1 = amplitude(sound, n, f), tri2 = amplitude(sound, n, 2 * f), tri3 = amplitude(sound, n, 3 * f);
    const double tri_bright = brightness(sound, n);
    n = capture(c.set(wane::controls::SHAPE, 0.5), f, 1024, true);
    const double saw1 = amplitude(sound, n, f), saw2 = amplitude(sound, n, 2 * f), saw3 = amplitude(sound, n, 3 * f);
    const double saw_bright = brightness(sound, n);
    n = capture(c.set(wane::controls::SHAPE, 0.8), f, 1024, true);
    const double square1 = amplitude(sound, n, f), square2 = amplitude(sound, n, 2 * f), square3 = amplitude(sound, n, 3 * f);
    n = capture(c.set(wane::controls::SHAPE, 1), f, 1024, true);
    const double pulse1 = amplitude(sound, n, f), pulse2 = amplitude(sound, n, 2 * f);
    std::printf("shape: sine h2 %.4f h3 %.4f, sine/tri level %.3f %.3f, triangle h3 %.3f, saw h2 %.3f h3 %.3f, "
                "square h2 %.3f h3 %.3f, pulse h2 %.3f\n", sine2 / sine1, sine3 / sine1, sine1 / tri1, between1 / tri1,
                tri3 / tri1, saw2 / saw1, saw3 / saw1, square2 / square1, square3 / square1, pulse2 / pulse1);
    // The sine's own harmonics (the warm filter and saturation add a little).
    check(sine2 < 0.01 * sine1 && sine3 < 0.01 * sine1, "shape: 0 is not a sine");
    // In phase with the triangle and as loud: no dip on the way between them.
    check(std::fabs(sine1 / tri1 - 1) < 0.1 && std::fabs(between1 / tri1 - 1) < 0.1, "shape: sine to triangle changes level");
    check(tri2 < 0.02 * tri1 && std::fabs(tri3 / tri1 - 1 / 9.0) < 0.03, "shape: 0.25 is not a triangle");
    check(std::fabs(saw2 / saw1 - 0.5) < 0.05 && std::fabs(saw3 / saw1 - 1 / 3.0) < 0.05, "shape: 0.5 is not a saw");
    check(square2 < 0.03 * square1 && std::fabs(square3 / square1 - 1 / 3.0) < 0.05, "shape: 0.8 is not a square");
    check(pulse2 > 0.5 * pulse1, "shape: 1 is not a narrow pulse");
    check(saw_bright > 3 * tri_bright, "shape: the saw is not brighter than the triangle");
    // Band-limited: a saw at 3 kHz folds its 15th harmonic (45 kHz) to 900 Hz;
    // PolyBLEP keeps it well below the naive saw's -23.5 dB.
    n = capture(c.set(wane::controls::SHAPE, 0.5), 3000, 1024, true);
    const double alias = amplitude(sound, n, 900) / amplitude(sound, n, 3000);
    char what[80];
    std::snprintf(what, sizeof what, "shape: the saw aliases (%.1f dB at 900 Hz)", 20 * std::log10(alias));
    std::printf("shape: 3 kHz saw alias at 900 Hz %.1f dB\n", 20 * std::log10(alias));
    check(alias < 0.01, what);
}

// Warmth and age darken the sound; age adds hiss under the music.
void tone() {
    Controls c;
    c.set(wane::controls::DETUNE, 0);
    double bright[3];
    const double warmth[3] = {0, 0.5, 1};
    for (unsigned k = 0; k < 3; ++k) {
        Controls w(c);
        bright[k] = brightness(sound, capture(w.set(wane::controls::WARMTH, warmth[k]), 220, 512));
    }
    check(bright[0] > 1.5 * bright[1] && bright[1] > 1.5 * bright[2], "warmth: more warmth is not darker");
    Controls aged(c);
    aged.set(wane::controls::WARMTH, 0);
    const unsigned n = capture(aged, 220, 512);
    const double clean = amplitude(sound, n, 15000.5) / rms(sound, n);
    const double clear = brightness(sound, n);
    const unsigned m = capture(aged.set(wane::controls::AGE, 1), 220, 512);
    const double hissy = amplitude(sound, m, 15000.5) / rms(sound, m);
    check(hissy > 3 * clean, "age: no hiss");
    check(brightness(sound, m) < clear, "age: no high-frequency loss");
}

// No integer equivalent of NaN: a minute at the harshest settings keeps
// sounding, never pins at the rail, and its filters stay settled.
void long_run() {
    rig = Rig(55);
    Controls c;
    for (unsigned k = 0; k < 8; ++k) c.p[k] = full;
    rig.play(0);
    bool sane = true;
    double power = 0;
    for (unsigned b = 0; b < 60 * 689; ++b) {
        rig.render(0, c);
        for (unsigned i = 0; i < frames; ++i) {
            sane = sane && rig.out[0][i] != static_cast<i32>(0x80000000u);
            if (b >= 59 * 689) power += (rig.out[0][i] / 2147483648.0) * (rig.out[0][i] / 2147483648.0);
        }
    }
    const double level = std::sqrt(power / (689.0 * frames));
    check(sane && level > 0.03 && level < 0.4, "long run: the voice decays, explodes or pins");
    check(std::abs(rig.voices[0].low[1]) < half && std::abs(rig.voices[0].head) < half, "long run: filter state ran away");
}
}

// The header's captions read in the controls' units.
void captions() {
    char text[opxy::draw::header_capacity];
    const struct { double value; const char* text; } detune[4] = {{0, "0c"}, {0.4, "8.0c"}, {0.5, "13c"}, {1, "50c"}};
    for (unsigned k = 0; k < 4; ++k) {
        wane::caption_cents(text, wane::tape::depth(word(detune[k].value), wane::controls::detune_most));
        char what[64];
        std::snprintf(what, sizeof what, "caption: detune %.1f reads %s, not %s", detune[k].value, text, detune[k].text);
        check(std::strcmp(text, detune[k].text) == 0, what);
    }
    wane::caption_cents(text, wane::tape::depths(full, 0, 0).wow);
    check(std::strcmp(text, "40c") == 0, "caption: full wow is not 40c");
    check(std::strcmp(wane::shape_name(0), "sine") == 0 && std::strcmp(wane::shape_name(word(0.125)), "s/tri") == 0 &&
          std::strcmp(wane::shape_name(word(0.25)), "tri") == 0 && std::strcmp(wane::shape_name(word(0.5)), "saw") == 0 &&
          std::strcmp(wane::shape_name(word(0.8)), "sq") == 0 && std::strcmp(wane::shape_name(full), "pulse") == 0,
          "caption: shape names");
}

int main() {
    captions();
    extremes();
    defaults_clean();
    shared_tape();
    once_per_block();
    tape_character();
    start_sag();
    detune_and_drift();
    shapes();
    tone();
    long_run();
    if (!failures) std::puts("PASS wane sound checks");
    return failures ? 1 : 0;
}
