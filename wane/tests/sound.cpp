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
        const double defaults[8] = {0.5, 0.4, 0, 0.2, 0, 0, 0, 0};
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
    const i32 spread = wane::tape::along(c.p[wane::controls::DETUNE], wane::controls::detune_curve) >> 1;
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
    char swing[96];
    std::snprintf(swing, sizeof swing, "tape: full wow and flutter swing %.0f to %.0f cents", low, high);
    check(high - low > 250 && low > -380 && high < 230, swing);
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

// The start sag: each note pulls the tape (every held note with it) flat by
// the sag's depth, and it catches up over a second or two, slower the deeper
// it is; with sag at zero nothing moves.
void start_sag() {
    const double settings[3] = {0.25, 0.5, 1}, depth[3] = {30, 70, 180}, settle[3] = {0.6, 1.1, 2.1};
    double half_way[3];
    for (unsigned k = 0; k < 3; ++k) {
        rig = Rig(220);
        Controls c;
        c.set(wane::controls::SAG, settings[k]).set(wane::controls::DETUNE, 0);
        rig.play(0);
        for (unsigned b = 0; b < 2000; ++b) rig.render(0, c);
        rig.play(1);   // the second note sags the first one too
        double lowest = 0, settled = 0;
        half_way[k] = 0;
        for (unsigned b = 0; b < 3000; ++b) {
            rig.render(0, c);
            rig.render(1, c);
            const double now = rig.voices[0].offset[0] / cent;
            lowest = std::fmin(lowest, now);
            if (now < -3) settled = b / 689.0625;
            if (now < -depth[k] / 2) half_way[k] = b / 689.0625;
        }
        char what[112];
        std::snprintf(what, sizeof what, "sag: %.2f sags a held note %.1f cents, not about %.0f", settings[k], lowest, depth[k]);
        check(lowest < -0.97 * depth[k] && lowest > -1.03 * depth[k] - 0.5, what);
        std::snprintf(what, sizeof what, "sag: %.2f settles within 3 cents after %.2f s, not about %.1f s", settings[k], settled, settle[k]);
        check(settled > 0.7 * settle[k] && settled < 1.3 * settle[k], what);
    }
    check(half_way[0] < half_way[1] && half_way[1] < half_way[2], "sag: a deeper sag does not recover more slowly");
    std::printf("sag: half recovered after %.0f, %.0f and %.0f ms at 25, 50 and 100%%\n",
                half_way[0] * 1000, half_way[1] * 1000, half_way[2] * 1000);
    rig = Rig(220);
    Controls c;
    rig.play(0);
    wane::start(rig.voices[0], rig.shared, 1);
    bool flat = true;
    for (unsigned b = 0; b < 100; ++b) { rig.render(0, c); flat = flat && rig.shared.bend == 0; }
    check(flat, "sag: the tape sags with sag at zero");
}

// Detune spreads the oscillators by its depth in cents; drift moves each note
// and voice on its own; at zero both are exact.
void detune_and_drift() {
    const double settings[4] = {0, 0.4, 0.5, 1}, cents[4] = {0, 8, 11, 70};
    for (unsigned k = 0; k < 4; ++k) {
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
    check(s_high - s_low > 15, "drift: notes do not start their own distance off pitch");
    check(high - low > 30 && low > -64 && high < 64, "drift: the voice does not wander, or wanders too far");
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
    const unsigned n = capture(aged, 220.5, 512);
    const double clean = amplitude(sound, n, 15000.5) / rms(sound, n);
    const double clear = amplitude(sound, n, 4410) / amplitude(sound, n, 220.5);
    const unsigned m = capture(aged.set(wane::controls::AGE, 1), 220.5, 512);
    const double hissy = amplitude(sound, m, 15000.5) / rms(sound, m);
    check(hissy > 3 * clean, "age: no hiss");
    check(amplitude(sound, m, 4410) / amplitude(sound, m, 220.5) < 0.1 * clear, "age: no high-frequency loss");
}

// Every control is plainly heard at a quarter turn and keeps growing to the
// top: the pitch controls in cents, warmth and age in the harmonics' level.
void sweeps() {
    const double quarters[5] = {0, 0.25, 0.5, 0.75, 1};
    // The swing of the tape (wow, flutter) and of one voice's drift over 20 s, in cents.
    double wow[5], flutter[5], drift[5];
    for (unsigned q = 0; q < 5; ++q) {
        double* into[3] = {wow, flutter, drift};
        const unsigned slots[3] = {wane::controls::WOW, wane::controls::FLUTTER, wane::controls::DRIFT};
        for (unsigned k = 0; k < 3; ++k) {
            rig = Rig(220);
            Controls c;
            c.set(wane::controls::DETUNE, 0).set(slots[k], quarters[q]);
            rig.play(0);
            double low = 0, high = 0;
            for (unsigned b = 0; b < 20 * 689; ++b) {
                if (k == 2 && b % 2756 == 0) wane::start(rig.voices[0], rig.shared, 1);   // drift: a new note each 4 s
                rig.render(0, c);
                const double now = rig.voices[0].offset[0] / cent;
                low = std::fmin(low, now);
                high = std::fmax(high, now);
            }
            into[k][q] = high - low;
        }
    }
    std::printf("sweeps: swing in cents at 0/25/50/75/100%%: wow %.0f %.0f %.0f %.0f %.0f, flutter %.0f %.0f %.0f %.0f %.0f, "
                "drift %.0f %.0f %.0f %.0f %.0f\n", wow[0], wow[1], wow[2], wow[3], wow[4], flutter[0], flutter[1],
                flutter[2], flutter[3], flutter[4], drift[0], drift[1], drift[2], drift[3], drift[4]);
    const double first[3] = {20, 10, 6};   // the least swing a quarter turn must make
    const double* swings[3] = {wow, flutter, drift};
    const char* names[3] = {"wow", "flutter", "drift"};
    for (unsigned k = 0; k < 3; ++k) {
        char what[96];
        std::snprintf(what, sizeof what, "sweeps: %s at zero moves the pitch", names[k]);
        check(swings[k][0] == 0, what);
        std::snprintf(what, sizeof what, "sweeps: a quarter of %s swings %.1f cents, under %.0f", names[k], swings[k][1], first[k]);
        check(swings[k][1] >= first[k], what);
        for (unsigned q = 2; q < 5; ++q) {
            std::snprintf(what, sizeof what, "sweeps: %s grows too little from %u%% to %u%%", names[k], 25 * (q - 1), 25 * q);
            check(swings[k][q] > 1.4 * swings[k][q - 1], what);
        }
    }
    // Warmth and age: the twentieth harmonic of a 220 Hz saw against the
    // fundamental, falling at every quarter; age's hiss rising.
    double warm[5], aged[5], hiss[5];
    for (unsigned q = 0; q < 5; ++q) {
        Controls c;
        c.set(wane::controls::DETUNE, 0).set(wane::controls::WARMTH, quarters[q]);
        unsigned n = capture(c, 220.5, 1024, true);
        warm[q] = 20 * std::log10(amplitude(sound, n, 4410) / amplitude(sound, n, 220.5));
        Controls a;
        a.set(wane::controls::DETUNE, 0).set(wane::controls::WARMTH, 0).set(wane::controls::AGE, quarters[q]);
        n = capture(a, 220.5, 1024, true);
        aged[q] = 20 * std::log10(amplitude(sound, n, 4410) / amplitude(sound, n, 220.5));
        hiss[q] = 20 * std::log10(amplitude(sound, n, 15104) / rms(sound, n) + 1e-12);
    }
    std::printf("sweeps: 20th harmonic re the fundamental, dB: warmth %.1f %.1f %.1f %.1f %.1f, age %.1f %.1f %.1f %.1f %.1f; "
                "hiss at 15 kHz %.0f %.0f %.0f %.0f %.0f dB\n", warm[0], warm[1], warm[2], warm[3], warm[4], aged[0], aged[1],
                aged[2], aged[3], aged[4], hiss[0], hiss[1], hiss[2], hiss[3], hiss[4]);
    for (unsigned q = 1; q < 5; ++q) {
        char what[96];
        std::snprintf(what, sizeof what, "sweeps: warmth darkens only %.1f dB from %u%% to %u%%", warm[q - 1] - warm[q], 25 * (q - 1), 25 * q);
        check(warm[q] < warm[q - 1] - 3, what);
        std::snprintf(what, sizeof what, "sweeps: age dulls only %.1f dB from %u%% to %u%%", aged[q - 1] - aged[q], 25 * (q - 1), 25 * q);
        check(aged[q] < aged[q - 1] - 3, what);
        std::snprintf(what, sizeof what, "sweeps: age's hiss grows only %.1f dB from %u%% to %u%%", hiss[q] - hiss[q - 1], 25 * (q - 1), 25 * q);
        check(hiss[q] > hiss[q - 1] + 3, what);
    }
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
    check(std::abs(rig.voices[0].low[1]) < half && std::abs(rig.voices[0].head[0]) < half && std::abs(rig.voices[0].head[1]) < half, "long run: filter state ran away");
}
}

// The header's captions read in the controls' units.
void captions() {
    char text[opxy::draw::header_capacity];
    const struct { double value; const char* text; } detune[5] = {{0, "0c"}, {0.25, "3.5c"}, {0.4, "8.0c"}, {0.5, "11c"}, {1, "70c"}};
    for (unsigned k = 0; k < 5; ++k) {
        wane::caption_cents(text, wane::tape::along(word(detune[k].value), wane::controls::detune_curve));
        char what[64];
        std::snprintf(what, sizeof what, "caption: detune %.1f reads %s, not %s", detune[k].value, text, detune[k].text);
        check(std::strcmp(text, detune[k].text) == 0, what);
    }
    const wane::tape::Depths most = wane::tape::depths(full, full, full);
    wane::caption_cents(text, most.wow);
    check(std::strcmp(text, "120c") == 0, "caption: full wow is not 120c");
    wane::caption_cents(text, most.flutter);
    check(std::strcmp(text, "50c") == 0, "caption: full flutter is not 50c");
    wane::caption_cents(text, most.sag);
    check(std::strcmp(text, "180c") == 0, "caption: full sag is not 180c");
    wane::caption_cents(text, wane::tape::along(full, wane::controls::drift_curve));
    check(std::strcmp(text, "35c") == 0, "caption: full drift is not 35c");
    wane::caption_cents(text, wane::tape::depths(word(0.25), 0, 0).wow);
    check(std::strcmp(text, "16c") == 0, "caption: a quarter of wow is not 16c");
    check(std::strcmp(wane::shape_name(0), "sine") == 0 && std::strcmp(wane::shape_name(word(0.125)), "s/tri") == 0 &&
          std::strcmp(wane::shape_name(word(0.25)), "tri") == 0 && std::strcmp(wane::shape_name(word(0.5)), "saw") == 0 &&
          std::strcmp(wane::shape_name(word(0.8)), "sq") == 0 && std::strcmp(wane::shape_name(full), "pulse") == 0,
          "caption: shape names");
}

// The screen at the extremes: every frame valid, the tape's ribbon within a
// few pixels of its rest and moving more the more the tape wobbles, every
// point distinct, and still with the tape controls at zero.
double ribbon_swing(double tape_controls, bool* valid, bool* distinct) {
    static wane::DisplayState state;
    static wane::Frame frame;
    std::memset(&state, 0, sizeof state);
    std::memset(&frame, 0, sizeof frame);
    wane::display_reset(state);
    opxy::draw::Input input;
    std::memset(&input, 0, sizeof input);
    for (unsigned k = 0; k < 8; ++k) input.slots[k] = word(k >= 4 && k < 7 ? tape_controls : 1);
    input.slots[wane::controls::DRIFT] = word(tape_controls);
    input.track = 1;
    double low = 0, high = 0;
    for (unsigned f = 0; f < 3000; ++f) {
        input.elapsed = 17;   // Q10 seconds: 60 frames a second
        if (f % 20 == 5) { ++input.note_serial; input.held_notes = 1 + (f / 20) % 8; }
        wane::display(state, input, frame);
        *valid = *valid && opxy::draw::valid(frame);
        for (unsigned h = 0; h < 2; ++h) {
            const auto& path = frame.paths[wane::node::tape + h];
            for (unsigned i = 0; i < 32; ++i) {
                low = std::fmin(low, path.points[i].y / 1024.0);
                high = std::fmax(high, path.points[i].y / 1024.0);
            }
            for (unsigned i = 0; i < 64; ++i)
                for (unsigned j = i + 1; j < 64; ++j)
                    *distinct = *distinct && (path.points[i].x != path.points[j].x || path.points[i].y != path.points[j].y);
        }
    }
    return high - low;
}
// Age's wear at one age: how many pieces show, and whether every piece stays
// put while notes start, are held and the tape runs.
unsigned wear_shown(double age, bool* still) {
    static wane::DisplayState state;
    static wane::Frame frame;
    std::memset(&state, 0, sizeof state);
    std::memset(&frame, 0, sizeof frame);
    wane::display_reset(state);
    opxy::draw::Input input;
    std::memset(&input, 0, sizeof input);
    for (unsigned k = 4; k < 7; ++k) input.slots[k] = word(1);
    input.slots[wane::controls::AGE] = word(age);
    input.track = 1;
    i32 first[2 * wane::node::wears];
    unsigned shown = 0;
    for (unsigned f = 0; f < 600; ++f) {
        input.elapsed = 17;
        if (f % 20 == 5) { ++input.note_serial; input.held_notes = 1 + (f / 20) % 8; }
        wane::display(state, input, frame);
        for (unsigned k = 0; k < wane::node::wears; ++k) {
            const opxy::draw::Node& n = frame.nodes[wane::node::wear + k];
            if (!f) {
                first[2 * k] = n.x;
                first[2 * k + 1] = n.y;
                shown += n.x != opxy::draw::hidden_x * opxy::draw::unit;
            }
            *still = *still && n.x == first[2 * k] && n.y == first[2 * k + 1] && n.scale == opxy::draw::unit &&
                     n.cos == opxy::draw::unit && n.sin == 0;
        }
    }
    return shown;
}
void screen() {
    // Age: no wear when new, more at every quarter, and all of it still.
    bool resting = true, more = true;
    unsigned counts[5];
    for (unsigned q = 0; q < 5; ++q) {
        counts[q] = wear_shown(0.25 * q, &resting);
        more = more && (q ? counts[q] > counts[q - 1] : counts[q] == 0);
    }
    std::printf("screen: age shows %u, %u, %u, %u and %u pieces of wear at 0, 25, 50, 75 and 100%%\n",
                counts[0], counts[1], counts[2], counts[3], counts[4]);
    check(more, "screen: age's wear does not grow from none");
    check(resting, "screen: age's wear moves");
    bool valid = true, distinct = true;
    const double still = ribbon_swing(0, &valid, &distinct), half_way = ribbon_swing(0.5, &valid, &distinct);
    const double most = ribbon_swing(1, &valid, &distinct);
    std::printf("screen: the tape's ribbon swings %.1f, %.1f and %.1f px with the tape controls at 0, 50 and 100%%\n",
                still, half_way, most);
    check(valid, "screen: a frame is invalid");
    check(distinct, "screen: the tape's ribbon repeats a point");
    check(still < 1.2 && half_way > still + 2 && most > half_way + 2 && most < 18, "screen: the ribbon's swing is wrong");
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
    sweeps();
    long_run();
    screen();
    if (!failures) std::puts("PASS wane sound checks");
    return failures ? 1 : 0;
}
