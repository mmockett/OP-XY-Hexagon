// Orbit's sound checks, compiled for the host and run by
// python sdk.py test --machine <folder>. Each check prints FAIL with a reason;
// the program fails if any does. The measurements print as they go: each
// control at 0, 25, 50, 75 and 100 (README, "Controls").
#include "../engine.cpp"
#include "../display.cpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
using orbit::i32;
using orbit::u32;
const unsigned frames = opxy::block_frames;
const i32 full = 0x7fffffff;
const i32 declared = 436000000;   // definition.json outputPeakQ31
const double quarters[5] = {0, 0.25, 0.5, 0.75, 1};
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
        const double defaults[8] = {0.0625, 0.5, 0.5, 0, 0.5, 0, 0, 0};
        for (unsigned i = 0; i < 8; ++i) p[i] = word(defaults[i]);
    }
    Controls& set(unsigned slot, double value) { p[slot] = word(value); return *this; }
    Controls& bank(unsigned b) { return set(orbit::BANK, (b + 0.5) / 8); }
};

// Up to eight voices, rendered as the device does: each sounding voice once a block.
struct Rig {
    orbit::Shared shared;
    orbit::Voice voices[8];
    i32 out[8][frames];
    u32 pitch[8][frames];
    Rig() { std::memset(this, 0, sizeof *this); }
    void play(unsigned v, double hz) {
        for (unsigned i = 0; i < frames; ++i) pitch[v][i] = increment(hz);
        orbit::initialize(voices[v], shared, v + 1);
        orbit::start(voices[v], shared, v + 1);
    }
    void render(unsigned v, const Controls& c) { orbit::render(voices[v], shared, v + 1, out[v], c.p, pitch[v]); }
};

// One voice's sound, as doubles (1 is full scale), after `settle` blocks.
const unsigned most = 1 << 22;
double sound[most];
Rig rig;
unsigned capture(const Controls& c, double hz, unsigned blocks, unsigned settle = 16) {
    rig = Rig();
    rig.play(0, hz);
    for (unsigned b = 0; b < settle; ++b) rig.render(0, c);
    for (unsigned b = 0; b < blocks; ++b) {
        rig.render(0, c);
        for (unsigned i = 0; i < frames; ++i) sound[b * frames + i] = rig.out[0][i] / 2147483648.0;
    }
    return blocks * frames;
}

// A frequency's amplitude in x[at .. at + n) (a plain DFT bin; n spans whole cycles).
double amplitude(const double* x, unsigned n, double hz) {
    const double w = 2 * M_PI * hz / 44100.0;
    double re = 0, im = 0;
    for (unsigned i = 0; i < n; ++i) { re += x[i] * std::cos(w * i); im += x[i] * std::sin(w * i); }
    return 2 * std::sqrt(re * re + im * im) / n;
}
// Harmonics 1..31 of a 220.5 Hz note (200 samples a cycle) over n samples.
const double f0 = 220.5;
struct Spectrum { double a[31]; };
Spectrum spectrum(const double* x, unsigned n) {
    Spectrum s;
    for (unsigned k = 0; k < 31; ++k) s.a[k] = amplitude(x, n, f0 * (k + 1));
    return s;
}
// How far apart two timbres are: the distance between their harmonic
// amplitudes, each set scaled to unit length (0 the same, 1.41 nothing shared).
double apart(const Spectrum& p, const Spectrum& q) {
    double np = 0, nq = 0, d = 0;
    for (unsigned k = 0; k < 31; ++k) { np += p.a[k] * p.a[k]; nq += q.a[k] * q.a[k]; }
    np = std::sqrt(np) + 1e-12;
    nq = std::sqrt(nq) + 1e-12;
    for (unsigned k = 0; k < 31; ++k) d += (p.a[k] / np - q.a[k] / nq) * (p.a[k] / np - q.a[k] / nq);
    return std::sqrt(d);
}
// The spectral centroid, in harmonics.
double centroid(const Spectrum& s) {
    double sum = 0, weight = 0;
    for (unsigned k = 0; k < 31; ++k) { sum += s.a[k] * s.a[k] * (k + 1); weight += s.a[k] * s.a[k]; }
    return sum / (weight + 1e-30);
}
// The share of a sound's power away from the note's harmonics, dB (the
// inharmonic energy: grit's aliasing).
double inharmonic(const double* x, unsigned n) {
    double power = 0, harmonic = 0;
    for (unsigned i = 0; i < n; ++i) power += x[i] * x[i];
    power /= n;
    for (unsigned k = 1; f0 * k < 22050; ++k) { const double a = amplitude(x, n, f0 * k); harmonic += a * a / 2; }
    return 10 * std::log10(std::fmax(power - harmonic, 1e-14) / power);
}

// The vector's point, from the engine's own corner weights (Q14): x right, y down.
struct Spot { double x, y; };
Spot spot(const orbit::Voice& v) {
    const double u = (v.share[1] + v.share[2]) / 16384.0, t = (v.share[2] + v.share[3]) / 16384.0;
    const Spot s = {u - t, u + t - 1};
    return s;
}

// Every setting stays within the declared peak, writes every sample, sounds
// and carries no DC, through note restarts, bank changes and a voice taken
// mid-note, at the lowest and highest notes.
void extremes() {
    const double pitches[4] = {27.5, 220, 1760, 8372};
    double loudest = 0;
    bool bounded = true, written = true, sounding = true, centred = true;
    u32 seed = 12345;
    for (unsigned trial = 0; trial < 600; ++trial) {
        Controls c;
        for (unsigned k = 0; k < 8; ++k) {
            seed = seed * 1664525u + 1013904223u;
            const unsigned pick = seed >> 29;
            c.p[k] = pick < 2 ? 0 : pick < 4 ? full : static_cast<i32>(seed >> 1);
        }
        const double hz = pitches[trial % 4];
        rig = Rig();
        rig.play(0, hz);
        double sum = 0, power = 0;
        for (unsigned b = 0; b < 240; ++b) {
            if (b == 80) orbit::start(rig.voices[0], rig.shared, 1);           // a restrike
            if (b == 120) { rig.play(0, hz * 1.5); }                           // the voice taken by another note
            if (b % 37 == 36) c.p[orbit::BANK] = static_cast<i32>((seed += 0x9e3779b9u) >> 1);   // a bank change mid-note
            for (unsigned i = 0; i < frames; ++i) rig.out[0][i] = full;
            rig.render(0, c);
            for (unsigned i = 0; i < frames; ++i) {
                const i32 x = rig.out[0][i];
                written = written && x != full;
                bounded = bounded && x >= -declared && x <= declared;
                const double y = x / 2147483648.0;
                if (x != full) loudest = std::fmax(loudest, std::fabs(y));
                if (b >= 140) { sum += y; power += y * y; }
            }
        }
        const double n = 100.0 * frames;
        if (std::sqrt(power / n) < 0.004 && sounding) {
            std::printf("quiet: trial %u at %.0f Hz: %.4f\n", trial, hz, std::sqrt(power / n));
            sounding = false;
        }
        centred = centred && std::fabs(sum / n) < 0.01;
    }
    check(bounded, "extremes: a sample exceeds the declared peak");
    check(written, "extremes: render left samples unwritten");
    check(sounding, "extremes: a setting is (nearly) silent");
    check(centred, "extremes: a setting carries DC");
    std::printf("extremes: loudest %.3f of full scale (declared %.3f)\n", loudest, declared / 2147483648.0);
}

// The defaults are clean: a still, periodic tone of the note's harmonics,
// nothing between them.
void defaults_clean() {
    Controls c;
    const unsigned n = capture(c, f0, 400);
    double moved = 0;
    for (unsigned at = 0; at + 8000 <= n; at += 8000) moved = std::fmax(moved, apart(spectrum(sound, 8000), spectrum(sound + at, 8000)));
    const double between = inharmonic(sound, 8000);
    std::printf("defaults: timbre moves %.4f, inharmonic %.0f dB\n", moved, between);
    check(moved < 0.002, "defaults: the timbre moves with orbit at zero");
    check(between < -60, "defaults: energy between the harmonics");
}

// Notes struck together move together, from the top of their path; a note
// struck later starts from the top too.
void together() {
    Controls c;
    c.set(orbit::ORBIT, 0.8).set(orbit::RATE, 0.7).set(orbit::PATH, 0.4);
    rig = Rig();
    rig.play(0, 220);
    rig.play(1, 330);
    bool same = true;
    for (unsigned b = 0; b < 2000; ++b) {
        rig.render(0, c);
        rig.render(1, c);
        for (unsigned k = 0; k < 4; ++k) same = same && rig.voices[0].share[k] == rig.voices[1].share[k];
    }
    check(same, "together: two notes struck together move apart");
    orbit::start(rig.voices[1], rig.shared, 2);
    rig.render(1, c);
    const orbit::Point top = orbit::point_at(orbit::centre_of(c.p[orbit::X], c.p[orbit::Y]), 0,
                                             orbit::radius_of(c.p[orbit::ORBIT]), orbit::q15(c.p[orbit::PATH]), true);
    i32 w[4];
    orbit::weights(top, w);
    bool started = true;
    for (unsigned k = 0; k < 4; ++k) started = started && rig.voices[1].share[k] == w[k];
    check(started, "together: a new note does not start at the top of the path");
}

// Bank: eight distinct sets of waves.
void banks() {
    double bright[8];
    Spectrum s[8];
    for (unsigned b = 0; b < 8; ++b) {
        Controls c;
        c.bank(b);
        capture(c, f0, 140);
        s[b] = spectrum(sound, 8000);
        bright[b] = centroid(s[b]);
    }
    std::printf("bank: centroid in harmonics, glass to dusk:");
    for (unsigned b = 0; b < 8; ++b) std::printf(" %.2f", bright[b]);
    double least = 9;
    for (unsigned a = 0; a < 8; ++a)
        for (unsigned b = a + 1; b < 8; ++b) least = std::fmin(least, apart(s[a], s[b]));
    std::printf("; the closest two banks %.2f apart\n", least);
    check(least > 0.15, "bank: two banks sound alike");
    // The enum: each eighth of the turn is one bank.
    bool steps = true;
    for (unsigned k = 0; k < 64; ++k) steps = steps && orbit::bank_of(word((k + 0.5) / 64)) == k / 8;
    check(steps, "bank: the turn does not step through the banks evenly");
}

// x and y move the mix's centre across the diamond: each quarter turn moves
// the timbre plainly (glass, orbit at zero).
void centre() {
    for (unsigned axis = 0; axis < 2; ++axis) {
        Spectrum s[5];
        for (unsigned q = 0; q < 5; ++q) {
            Controls c;
            c.set(axis ? orbit::Y : orbit::X, quarters[q]);
            capture(c, f0, 140);
            s[q] = spectrum(sound, 8000);
        }
        std::printf("%s: centroid %.2f %.2f %.2f %.2f %.2f; each quarter moves the timbre %.2f %.2f %.2f %.2f\n",
                    axis ? "y" : "x", centroid(s[0]), centroid(s[1]), centroid(s[2]), centroid(s[3]), centroid(s[4]),
                    apart(s[0], s[1]), apart(s[1], s[2]), apart(s[2], s[3]), apart(s[3], s[4]));
        for (unsigned q = 1; q < 5; ++q) {
            char what[80];
            std::snprintf(what, sizeof what, "%s: %u%% to %u%% barely moves the timbre", axis ? "y" : "x", 25 * (q - 1), 25 * q);
            check(apart(s[q - 1], s[q]) > 0.06, what);
        }
    }
}

// The vector's track over `seconds`, block by block.
const unsigned track_most = 60 * 690;
Spot track[track_most];
unsigned trace(const Controls& c, double seconds) {
    rig = Rig();
    rig.play(0, 220);
    const unsigned blocks = static_cast<unsigned>(seconds * 689.0625);
    for (unsigned b = 0; b < blocks; ++b) {
        rig.render(0, c);
        track[b] = spot(rig.voices[0]);
    }
    return blocks;
}

// Orbit: how far the timbre swings over a cycle (the windowed spectrum's mean
// distance from the cycle's average), at a 1 Hz circle round glass.
double swing(const Controls& c, double seconds) {
    const unsigned n = capture(c, f0, static_cast<unsigned>(seconds * 689.0625), 0);
    const unsigned windows = n / 2000;
    static Spectrum s[4000];
    Spectrum mean = {};
    for (unsigned w = 0; w < windows; ++w) {
        s[w] = spectrum(sound + w * 2000, 2000);
        for (unsigned k = 0; k < 31; ++k) mean.a[k] += s[w].a[k] / windows;
    }
    double d = 0;
    for (unsigned w = 0; w < windows; ++w) d += apart(s[w], mean) / windows;
    return d;
}
void orbits() {
    double depth[5], reach[5];
    for (unsigned q = 0; q < 5; ++q) {
        Controls c;
        c.set(orbit::ORBIT, quarters[q]).set(orbit::RATE, std::log(1 / 0.02) / std::log(400.0));   // 1 Hz
        depth[q] = swing(c, 2);
        const unsigned blocks = trace(c, 1);
        reach[q] = 0;
        for (unsigned b = 0; b < blocks; ++b) reach[q] = std::fmax(reach[q], std::hypot(track[b].x, track[b].y));
    }
    std::printf("orbit: the point's reach %.2f %.2f %.2f %.2f %.2f; the timbre swings %.3f %.3f %.3f %.3f %.3f\n",
                reach[0], reach[1], reach[2], reach[3], reach[4], depth[0], depth[1], depth[2], depth[3], depth[4]);
    check(depth[0] < 0.002 && reach[0] < 0.001, "orbit: zero moves the vector");
    for (unsigned q = 1; q < 5; ++q) {
        char what[80];
        std::snprintf(what, sizeof what, "orbit: %u%% to %u%% barely deepens the swing", 25 * (q - 1), 25 * q);
        check(depth[q] > depth[q - 1] * 1.2 && depth[q] > 0.03, what);
        check(std::fabs(reach[q] - 0.85 * quarters[q]) < 0.01, "orbit: the path's radius is not 0.85 of the turn");
    }
}

// Rate: the vector goes round at 0.02 to 8 Hz (each quarter 4.5 times faster).
void rates() {
    const double expected[5] = {0.02, 0.0894, 0.4, 1.789, 8};
    double measured[5];
    for (unsigned q = 0; q < 5; ++q) {
        Controls c;
        c.set(orbit::ORBIT, 0.6).set(orbit::RATE, quarters[q]);
        const double seconds = q == 0 ? 55 : q == 1 ? 25 : 6;
        const unsigned blocks = trace(c, seconds);
        // Each cycle of a circle crosses y = 0 upward once.
        unsigned crossings = 0, first = 0, last = 0;
        for (unsigned b = 1; b < blocks; ++b)
            if (track[b - 1].y < 0 && track[b].y >= 0) { if (!crossings) first = b; last = b; ++crossings; }
        measured[q] = crossings > 1 ? (crossings - 1) * 689.0625 / (last - first) : 1 / seconds * crossings;
        // The slowest takes 50 s a cycle: one crossing, a quarter cycle in.
        if (q == 0) measured[q] = crossings == 1 ? 0.25 * 689.0625 / first : 0;
    }
    std::printf("rate: the vector cycles at %.3f %.3f %.3f %.3f %.2f Hz\n", measured[0], measured[1], measured[2], measured[3], measured[4]);
    for (unsigned q = 0; q < 5; ++q) {
        char what[80];
        std::snprintf(what, sizeof what, "rate: %u%% cycles at %.3f Hz, not %.3f", 25 * q, measured[q], expected[q]);
        check(std::fabs(measured[q] / expected[q] - 1) < 0.03, what);
    }
}

// Path: circle, ellipse, figure 8, steps. The four shapes (at 0, 1/3, 2/3
// and 1): their width and height, how often they cross the centre and how
// long they dwell at the corners; and each quarter turn moves the trace.
struct Shape { double wide, tall, dwell; unsigned through; };
Shape shape_at(double path, Spot* keep) {
    Controls c;
    c.set(orbit::ORBIT, 1).set(orbit::RATE, std::log(1 / 0.02) / std::log(400.0)).set(orbit::PATH, path);
    const unsigned blocks = trace(c, 4);
    double x0 = 9, x1 = -9, y0 = 9, y1 = -9;
    unsigned still = 0;
    Shape s = {0, 0, 0, 0};
    for (unsigned b = 0; b < blocks; ++b) {
        x0 = std::fmin(x0, track[b].x); x1 = std::fmax(x1, track[b].x);
        y0 = std::fmin(y0, track[b].y); y1 = std::fmax(y1, track[b].y);
        if (b && std::hypot(track[b].x - track[b - 1].x, track[b].y - track[b - 1].y) < 0.0005) ++still;
        if (b && (track[b - 1].x < 0) != (track[b].x < 0) && std::fabs(track[b].y) < 0.1) ++s.through;
        if (keep && b < 689) keep[b] = track[b];
    }
    s.wide = x1 - x0;
    s.tall = y1 - y0;
    s.dwell = static_cast<double>(still) / blocks;
    return s;
}
void paths() {
    const char* names[4] = {"circle", "ellipse", "figure 8", "steps"};
    Shape pure[4];
    for (unsigned k = 0; k < 4; ++k) pure[k] = shape_at(k / 3.0, 0);
    std::printf("path:");
    for (unsigned k = 0; k < 4; ++k)
        std::printf(" %s %.2f x %.2f, %u centre crossings in 4 s, dwelling %.0f%%;", names[k], pure[k].wide, pure[k].tall,
                    pure[k].through, 100 * pure[k].dwell);
    check(std::fabs(pure[0].wide - 1.7) < 0.02 && std::fabs(pure[0].tall - 1.7) < 0.02 && pure[0].dwell < 0.01 && !pure[0].through,
          "path: 0 is not a circle");
    check(pure[1].wide < 0.85 * pure[0].wide && pure[1].tall < 0.85 * pure[0].tall && !pure[1].through, "path: 1/3 is not a narrow ellipse");
    check(pure[2].through >= 7 && pure[2].tall < 0.55 * pure[2].wide, "path: 2/3 is not a figure 8 through the centre");
    check(pure[3].dwell > 0.75, "path: 1 does not dwell at the corners (steps)");
    // Each quarter turn moves the trace: the mean distance between the two
    // traces at the same moments, over a second.
    static Spot traces[5][689];
    for (unsigned q = 0; q < 5; ++q) shape_at(quarters[q], traces[q]);
    std::printf(" each quarter moves the trace");
    for (unsigned q = 1; q < 5; ++q) {
        double d = 0;
        for (unsigned b = 0; b < 689; ++b) d += std::hypot(traces[q][b].x - traces[q - 1][b].x, traces[q][b].y - traces[q - 1][b].y) / 689;
        std::printf(" %.2f", d);
        char what[80];
        std::snprintf(what, sizeof what, "path: %u%% to %u%% barely moves the trace", 25 * (q - 1), 25 * q);
        check(d > 0.1, what);
    }
    std::printf("\n");
}

// Grit: the DAC's rate falls and its bits coarsen: the aliasing between the
// harmonics rises at every quarter turn.
void grit() {
    double alias[5];
    for (unsigned q = 0; q < 5; ++q) {
        Controls c;
        c.set(orbit::GRIT, quarters[q]);
        capture(c, f0, 140);
        alias[q] = inharmonic(sound, 8000);
    }
    std::printf("grit: aliasing %.0f %.1f %.1f %.1f %.1f dB of the sound\n", alias[0], alias[1], alias[2], alias[3], alias[4]);
    check(alias[0] < -60, "grit: zero is not clean");
    check(alias[1] > -40, "grit: a quarter turn adds too little aliasing");
    for (unsigned q = 2; q < 5; ++q) {
        char what[80];
        std::snprintf(what, sizeof what, "grit: %u%% to %u%% adds too little aliasing", 25 * (q - 1), 25 * q);
        check(alias[q] > alias[q - 1] + 2, what);
    }
}

// Detune: the twin a few cents apart beats against the note: slow at a
// quarter turn, quick at the top (220.5 Hz's fundamental).
void detune() {
    double rate[5], depth[5];
    for (unsigned q = 0; q < 5; ++q) {
        Controls c;
        c.set(orbit::DETUNE, quarters[q]);
        const unsigned n = capture(c, f0, 689 * 12, 64);
        const unsigned windows = n / 2000;
        static double level[4000];
        double mean = 0, low = 9, high = 0;
        for (unsigned w = 0; w < windows; ++w) {
            level[w] = amplitude(sound + w * 2000, 2000, f0);
            mean += level[w] / windows;
            low = std::fmin(low, level[w]);
            high = std::fmax(high, level[w]);
        }
        unsigned crossings = 0, first = 0, last = 0;
        for (unsigned w = 1; w < windows; ++w)
            if ((level[w - 1] < mean) != (level[w] < mean)) { if (!crossings) first = w; last = w; ++crossings; }
        rate[q] = crossings > 2 ? (crossings - 1) / 2.0 / ((last - first) * 2000 / 44100.0) : 0;
        depth[q] = (high - low) / (high + low);
    }
    std::printf("detune: the fundamental beats at %.2f %.2f %.2f %.2f %.2f Hz, %.0f%% %.0f%% %.0f%% %.0f%% %.0f%% deep\n",
                rate[0], rate[1], rate[2], rate[3], rate[4], 100 * depth[0], 100 * depth[1], 100 * depth[2], 100 * depth[3], 100 * depth[4]);
    const double expected[5] = {0, 0.239, 0.955, 2.15, 3.84};
    check(depth[0] < 0.01, "detune: zero beats");
    for (unsigned q = 1; q < 5; ++q) {
        char what[80];
        std::snprintf(what, sizeof what, "detune: %u%% beats at %.2f Hz, not %.2f", 25 * q, rate[q], expected[q]);
        check(std::fabs(rate[q] / expected[q] - 1) < 0.1 && depth[q] > 0.3, what);
    }
}

// The fade: a moving vector, a bank change or a restart never steps the
// output by more than the wave itself moves.
void smooth() {
    Controls c;
    c.set(orbit::ORBIT, 1).set(orbit::RATE, 1).set(orbit::PATH, 1).bank(6);
    rig = Rig();
    rig.play(0, 55);
    double biggest = 0, wave = 0;
    i32 last = 0;
    for (unsigned b = 0; b < 4000; ++b) {
        if (b % 500 == 250) c.bank((b / 500) % 8);
        if (b % 700 == 350) orbit::start(rig.voices[0], rig.shared, 1);
        rig.render(0, c);
        for (unsigned i = 0; i < frames; ++i) {
            const double step = std::fabs((rig.out[0][i] - static_cast<double>(last)) / 2147483648.0);
            if (i == 0) biggest = std::fmax(biggest, step);
            else wave = std::fmax(wave, step);
            last = rig.out[0][i];
        }
    }
    std::printf("smooth: the largest step at a block's edge %.4f, within one %.4f\n", biggest, wave);
    check(biggest <= wave * 1.05, "smooth: a block's edge clicks");
}

// A minute at the harshest settings keeps sounding and never pins.
void long_run() {
    Controls c;
    for (unsigned k = 0; k < 8; ++k) c.p[k] = full;
    rig = Rig();
    rig.play(0, 55);
    bool sane = true;
    double power = 0;
    for (unsigned b = 0; b < 60 * 689; ++b) {
        rig.render(0, c);
        for (unsigned i = 0; i < frames; ++i) {
            sane = sane && rig.out[0][i] <= declared && rig.out[0][i] >= -declared;
            if (b >= 59 * 689) power += (rig.out[0][i] / 2147483648.0) * (rig.out[0][i] / 2147483648.0);
        }
    }
    const double level = std::sqrt(power / (689.0 * frames));
    check(sane && level > 0.03 && level < 0.3, "long run: the voice decays, explodes or pins");
}

// The header's captions read as the board writes them.
void captions() {
    char text[opxy::draw::header_capacity];
    struct Case { void (*write)(char*, i32); double value; const char* text; };
    const Case cases[] = {
        {orbit::caption_axis, 0, "-100"}, {orbit::caption_axis, 0.4, "-20"}, {orbit::caption_axis, 0.5, "0"},
        {orbit::caption_axis, 0.58, "16"}, {orbit::caption_axis, 1, "100"},
        {orbit::caption_rate, 0, ".02Hz"}, {orbit::caption_rate, 0.5, ".40Hz"}, {orbit::caption_rate, 0.62, ".82Hz"},
        {orbit::caption_rate, 0.75, "1.8Hz"}, {orbit::caption_rate, 1, "8.0Hz"},
        {orbit::caption_grit, 0, "clean"}, {orbit::caption_grit, 0.25, "32k"}, {orbit::caption_grit, 0.55, "20k"},
        {orbit::caption_grit, 0.75, "14.8k"}, {orbit::caption_grit, 1, "10.0k"},
        {orbit::caption_detune, 0, "0.0c"}, {orbit::caption_detune, 0.55, "9.1c"}, {orbit::caption_detune, 1, "30.0c"},
    };
    for (const Case& k : cases) {
        k.write(text, word(k.value));
        char what[80];
        std::snprintf(what, sizeof what, "caption: %.2f reads %s, not %s", k.value, text, k.text);
        check(std::strcmp(text, k.text) == 0, what);
    }
    const struct { double value; const char* name; } path_names[] = {
        {0, "circ"}, {0.1, "c/el"}, {0.33, "ellip"}, {0.5, "el/f8"}, {0.67, "fig8"}, {0.85, "f8/st"}, {1, "steps"}};
    for (const auto& k : path_names) {
        char what[80];
        std::snprintf(what, sizeof what, "caption: path %.2f reads %s, not %s", k.value, orbit::path_name(word(k.value)), k.name);
        check(std::strcmp(orbit::path_name(word(k.value)), k.name) == 0, what);
    }
    check(std::strcmp(orbit::bank_name(orbit::bank_of(word(0.0625))), "glass") == 0 &&
          std::strcmp(orbit::bank_name(orbit::bank_of(word(0.1875))), "vox") == 0 &&
          std::strcmp(orbit::bank_name(orbit::bank_of(full)), "dusk") == 0, "caption: bank names");
}
}

// The screen: every frame valid and its outlines enclosing area; still when
// nothing plays; a note's point starting at the top of the path and moving as
// the audio's vector does; a reset and reduced motion showing the end state.
orbit::DisplayState state;
orbit::Frame frame, before;
opxy::draw::Input input;
void tick(int elapsed = 17) {
    input.elapsed = elapsed;
    orbit::display(state, input, frame);
}
bool encloses(const orbit::Frame& f) {
    for (unsigned p = 0; p < orbit::Frame::path_count; ++p) {
        // Not traced out and back: some edge is not walked the other way.
        const auto& pts = f.paths[p].points;
        bool retraced = true;
        for (unsigned i = 0; i < 64 && retraced; ++i) {
            const auto& a = pts[i];
            const auto& b = pts[(i + 1) % 64];
            bool back = false;
            for (unsigned j = 0; j < 64 && !back; ++j)
                back = pts[j].x == b.x && pts[j].y == b.y && pts[(j + 1) % 64].x == a.x && pts[(j + 1) % 64].y == a.y;
            retraced = back || (a.x == b.x && a.y == b.y);
        }
        if (retraced) return false;
    }
    return true;
}
void screen() {
    std::memset(&state, 0, sizeof state);
    std::memset(&input, 0, sizeof input);
    input.track = 1;
    orbit::display_reset(state);
    bool valid = true, enclosed = true;
    u32 seed = 99;
    for (unsigned f = 0; f < 6000; ++f) {
        if (f % 40 == 0)
            for (unsigned k = 0; k < 8; ++k) {
                seed = seed * 1664525u + 1013904223u;
                input.slots[k] = (seed >> 30) == 0 ? 0 : (seed >> 30) == 1 ? full : static_cast<i32>(seed >> 1);
            }
        for (unsigned k = 0; k < 4; ++k) input.shown[k] = static_cast<opxy::u8>(k + ((f / 90) & 1) * 4);
        if (f % 23 == 3) { ++input.note_serial; input.held_notes = (f / 23) % 11; }
        input.reduced_motion = (f / 1000) % 3 == 2;
        if (f % 1700 == 900) orbit::display_reset(state);
        tick(f ? 17 : 0);
        valid = valid && opxy::draw::valid(frame);
        enclosed = enclosed && encloses(frame);
    }
    check(valid, "screen: a frame is invalid");
    check(enclosed, "screen: an outline encloses no area");

    // Still when nothing plays: two seconds after the last turn, nothing moves.
    Controls c;
    c.set(orbit::ORBIT, 0.7).set(orbit::DETUNE, 0.5).set(orbit::GRIT, 0.3);
    for (unsigned k = 0; k < 8; ++k) input.slots[k] = c.p[k];
    input.held_notes = 0;
    input.reduced_motion = 0;
    for (unsigned f = 0; f < 60; ++f) tick();
    before = frame;
    for (unsigned f = 0; f < 120; ++f) tick();
    check(std::memcmp(&before, &frame, sizeof frame) == 0, "screen: the idle screen moves");

    // A note: its point starts at the top of the path and travels it at the
    // rate, as the audio's vector does.
    c.set(orbit::RATE, 0.75).set(orbit::PATH, 0);
    for (unsigned k = 0; k < 8; ++k) input.slots[k] = c.p[k];
    for (unsigned f = 0; f < 30; ++f) tick();
    ++input.note_serial;
    input.held_notes = 1;
    tick();
    const double first = state.voices[0].phase / 4294967296.0;
    const orbit::Point top = orbit::point_at(orbit::centre_of(c.p[orbit::X], c.p[orbit::Y]), state.voices[0].phase,
                                             orbit::radius_of(c.p[orbit::ORBIT]), 0, true);
    const auto& start = frame.nodes[orbit::node::point];
    const double off = std::hypot(start.x / 1024.0 - (122 + top.x / 256.0), start.y / 1024.0 - (129 + top.y / 256.0));
    std::printf("screen: a new point shows %.3f of a turn from the top of the path, %.1f px from where the path puts it; ", first, off);
    check(first < 0.02 && off < 0.5, "screen: a new note's point does not start at the top of the path");
    // A second later (1.79 Hz) the point has gone round 1.79 times: 0.79 of a turn on.
    for (unsigned f = 0; f < 60; ++f) tick();
    const double turns = state.voices[0].phase / 4294967296.0;
    std::printf("a second later at 1.79 Hz it is %.2f of a turn on (0.79, less its 0.15 s ease in)\n", turns);
    check(std::fabs(turns - 0.76) < 0.04, "screen: the point does not keep the audio's rate");
    // Reduced motion: the points stay at the top; a reset shows the end state at once.
    input.reduced_motion = 1;
    const u32 held = state.voices[0].phase;
    for (unsigned f = 0; f < 30; ++f) tick();
    check(state.voices[0].phase == held, "screen: reduced motion still moves the points");
    input.reduced_motion = 0;
    c.set(orbit::ORBIT, 0.1);
    for (unsigned k = 0; k < 8; ++k) input.slots[k] = c.p[k];
    orbit::display_reset(state);
    tick(0);
    check(state.eased[orbit::ORBIT] == orbit::q15(c.p[orbit::ORBIT]), "screen: a reset eases instead of showing the end state");
}

int main() {
    captions();
    extremes();
    defaults_clean();
    together();
    banks();
    centre();
    orbits();
    rates();
    paths();
    grit();
    detune();
    smooth();
    long_run();
    screen();
    if (!failures) std::puts("PASS orbit sound checks");
    return failures ? 1 : 0;
}
