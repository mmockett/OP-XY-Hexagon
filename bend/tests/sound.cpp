// Bend's sound and screen checks, compiled for the host and run by
// python sdk.py test --machine <folder>. Each check prints FAIL with a reason;
// the program fails if any does. Measurements print as they are taken.
#include "../engine.cpp"
#include "../display.cpp"
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
using bend::i32;
using bend::u32;
const unsigned frames = opxy::block_frames;
const i32 full = 0x7fffffff;
const i32 peak = 0x18000000;   // definition.json outputPeakQ31
int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}
i32 word(double value) { return static_cast<i32>(value * 2147483647.0 + 0.5); }
u32 increment(double hz) { return static_cast<u32>(hz / 44100.0 * 4294967296.0); }
// An enum choice at the middle of its share, as the device stores it.
double choice(unsigned index) { return (index + 0.5) / 8; }

// The defaults, in definition.json's order.
struct Controls {
    i32 p[8];
    Controls() {
        const double defaults[8] = {choice(bend::SAW), 0.25, 0, 0, choice(1), 0, 0, 0};
        for (unsigned i = 0; i < 8; ++i) p[i] = word(defaults[i]);
    }
    Controls& set(unsigned slot, double value) { p[slot] = word(value); return *this; }
};

// One voice's sound, as doubles (1 is full scale).
const unsigned max_samples = 1 << 19;
std::vector<double> sound(max_samples);
bend::Voice voice;
bend::Shared shared;
unsigned capture(const Controls& c, double hz, double seconds) {
    bend::initialize(voice, shared, 1);
    bend::start(voice, shared, 1);
    u32 pitch[frames];
    for (unsigned i = 0; i < frames; ++i) pitch[i] = increment(hz);
    i32 out[frames];
    const unsigned blocks = static_cast<unsigned>(seconds * 44100 / frames);
    unsigned b = 0;
    for (; b < blocks && (b + 1) * frames <= max_samples; ++b) {
        bend::render(voice, shared, 1, out, c.p, pitch);
        for (unsigned i = 0; i < frames; ++i) sound[b * frames + i] = out[i] / 2147483648.0;
    }
    return b * frames;
}

// A power spectrum of n (a power of two) samples, Blackman-Harris windowed.
std::vector<double> spectrum(const double* x, unsigned n) {
    std::vector<std::complex<double> > a(n);
    for (unsigned i = 0; i < n; ++i) {
        const double t = 2 * M_PI * i / n;
        const double w = 0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t) - 0.01168 * std::cos(3 * t);
        a[i] = x[i] * w;
    }
    for (unsigned i = 1, j = 0; i < n; ++i) {
        unsigned bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (unsigned len = 2; len <= n; len <<= 1) {
        const std::complex<double> step = std::polar(1.0, -2 * M_PI / len);
        for (unsigned i = 0; i < n; i += len) {
            std::complex<double> w = 1;
            for (unsigned k = 0; k < len / 2; ++k, w *= step) {
                const std::complex<double> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
            }
        }
    }
    std::vector<double> power(n / 2);
    for (unsigned i = 0; i < n / 2; ++i) power[i] = std::norm(a[i]);
    return power;
}
double bin_hz(unsigned n) { return 44100.0 / n; }
// The spectral centroid, Hz.
double centroid(const std::vector<double>& s, unsigned n) {
    double sum = 0, weighted = 0;
    for (unsigned i = 1; i < s.size(); ++i) { sum += s[i]; weighted += s[i] * i * bin_hz(n); }
    return weighted / sum;
}
// The power within a few bins of a frequency.
double near(const std::vector<double>& s, unsigned n, double hz) {
    const int c = static_cast<int>(hz / bin_hz(n) + 0.5);
    double sum = 0;
    for (int i = c - 4; i <= c + 4; ++i) if (i > 0 && i < static_cast<int>(s.size())) sum += s[i];
    return sum;
}
// The share of power, dB, away from every harmonic of f (counted between lo and hi Hz).
double off_harmonic(const std::vector<double>& s, unsigned n, double f, double lo = 30, double hi = 22050) {
    double total = 0, off = 0;
    for (unsigned i = 1; i < s.size(); ++i) {
        const double hz = i * bin_hz(n);
        total += s[i];
        if (hz < lo || hz > hi) continue;
        const double h = hz / f;
        if (std::fabs(h - std::floor(h + 0.5)) * f > 8 * bin_hz(n)) off += s[i];
    }
    return 10 * std::log10(off / total + 1e-30);
}
// The rms difference of two log spectra, dB, over 50 Hz to 12 kHz.
double distance(const std::vector<double>& a, const std::vector<double>& b, unsigned n) {
    double sum = 0;
    unsigned count = 0;
    for (unsigned i = 1; i < a.size(); ++i) {
        const double hz = i * bin_hz(n);
        if (hz < 50 || hz > 12000) continue;
        const double d = 10 * std::log10((a[i] + 1e-14) / (b[i] + 1e-14));
        sum += d * d;
        ++count;
    }
    return std::sqrt(sum / count);
}
std::vector<double> tail_spectrum(const Controls& c, double hz, double seconds = 1.5, unsigned n = 32768) {
    const unsigned got = capture(c, hz, seconds);
    return spectrum(&sound[got - n], n);
}
// The change from a reference sound, dB: the power of the difference over the reference's.
double change(const std::vector<double>& reference, unsigned end) {
    double diff = 0, power = 0;
    for (unsigned i = 0; i < reference.size(); ++i) {
        const double d = sound[end - reference.size() + i] - reference[i];
        diff += d * d;
        power += reference[i] * reference[i];
    }
    return 10 * std::log10(diff / power + 1e-30);
}

// ---- Captions -------------------------------------------------------------
void captions() {
    char text[opxy::draw::header_capacity];
    const struct { double value; const char* text; } detune[6] = {
        {0, "0.0c"}, {0.25, "2.5c"}, {0.3, "3.6c"}, {0.5, "10c"}, {0.75, "23c"}, {1, "40c"}};
    for (unsigned k = 0; k < 6; ++k) {
        bend::screen::caption_cents(text, bend::pd::detune_tenths(word(detune[k].value)));
        char what[80];
        std::snprintf(what, sizeof what, "caption: detune %.2f reads %s, not %s", detune[k].value, text, detune[k].text);
        check(std::strcmp(text, detune[k].text) == 0, what);
    }
    const struct { double value; const char* text; } lofi[5] = {{0, "48k"}, {0.3, "23k"}, {0.5, "14k"}, {0.85, "5.8k"}, {1, "4.0k"}};
    for (unsigned k = 0; k < 5; ++k) {
        bend::screen::caption_rate(text, bend::pd::lofi_rate_tens(word(lofi[k].value)));
        char what[80];
        std::snprintf(what, sizeof what, "caption: lofi %.2f reads %s, not %s", lofi[k].value, text, lofi[k].text);
        check(std::strcmp(text, lofi[k].text) == 0, what);
    }
    check(bend::pd::lofi_bits(0) == 16 && bend::pd::lofi_bits(word(0.5)) == 10 && bend::pd::lofi_bits(full) == 4,
          "lofi: its depth is not 16, 10 and 4 bits at 0, 50 and 100%");
    const char* waves[8] = {"saw", "sq", "pulse", "dsine", "sawpl", "reso1", "reso2", "reso3"};
    const char* intervals[8] = {"-oct", "uni", "4th", "5th", "m7", "oct", "oct+5", "2oct"};
    for (unsigned k = 0; k < 8; ++k) {
        check(std::strcmp(bend::pd::wave_names[bend::pd::choice(word(choice(k)), 8)], waves[k]) == 0, "caption: wave names");
        check(std::strcmp(bend::pd::interval_names[bend::pd::choice(word(choice(k)), 8)], intervals[k]) == 0,
              "caption: interval names");
        check(bend::pd::choice(word(k / 8.0 + 0.001), 8) == k && bend::pd::choice(word((k + 1) / 8.0 - 0.001), 8) == k,
              "enum: a choice does not span an eighth of the travel");
    }
}

// ---- Bounds ---------------------------------------------------------------
// Every corner of the controls, on every wave, low to high, with a note played
// again mid-way: within the declared peak, every sample written, sounding, no
// DC; the output clamp is a guard that almost never engages.
void extremes() {
    const double pitches[4] = {0, 27.5, 261.6, 4186};   // 0: a voice given no pitch
    double loudest = 0;
    long clamped = 0, samples = 0;
    bool bounded = true, written = true, sounding = true, centred = true;
    for (unsigned w = 0; w < 8; ++w)
        for (unsigned corner = 0; corner < 128; ++corner)
            for (unsigned h = 0; h < 4; ++h) {
                Controls c;
                c.set(bend::WAVE, choice(w)).set(bend::BEND, (corner & 1) ? 1 : 0.5);
                c.set(bend::DETUNE, (corner >> 1) & 1).set(bend::RING, (corner >> 2) & 1);
                c.set(bend::INTERVAL, choice((corner >> 3) & 1 ? 7 : 0)).set(bend::WARP, (corner >> 4) & 1);
                c.set(bend::NOISE, (corner >> 5) & 1).set(bend::LOFI, (corner >> 6) & 1);
                bend::initialize(voice, shared, 1);
                bend::start(voice, shared, 1);
                u32 pitch[frames];
                for (unsigned i = 0; i < frames; ++i) pitch[i] = increment(pitches[h]);
                double sum = 0, power = 0;
                i32 out[frames];
                for (unsigned b = 0; b < 240; ++b) {
                    if (b == 120) bend::start(voice, shared, 1);
                    for (unsigned i = 0; i < frames; ++i) out[i] = full;   // never produced
                    bend::render(voice, shared, 1, out, c.p, pitch);
                    for (unsigned i = 0; i < frames; ++i) {
                        const i32 x = out[i];
                        written = written && x != full;
                        bounded = bounded && x >= -peak && x <= peak;
                        clamped += x == peak || x == -peak;
                        ++samples;
                        const double y = x / 2147483648.0;
                        if (x != full && std::fabs(y) > loudest) loudest = std::fabs(y);
                        if (b >= 40) { sum += y; power += y * y; }
                    }
                }
                const double n = 200.0 * frames;
                if (h && std::sqrt(power / n) < 0.004 && sounding) {
                    std::printf("quiet: wave %u corner %u at %.1f Hz: %.4f\n", w, corner, pitches[h], std::sqrt(power / n));
                    sounding = false;
                }
                centred = centred && (!h || std::fabs(sum / n) < 0.01);   // no pitch holds still: its DC fades
            }
    check(bounded, "an extreme setting exceeds the declared peak");
    check(written, "render left samples unwritten");
    check(sounding, "an extreme setting is (nearly) silent");
    check(centred, "an extreme setting carries DC");
    check(clamped * 100000 < samples, "the output clamp engages too often");
    std::printf("extremes: loudest %.3f of full scale (declared %.4f), clamped %ld of %ld samples\n", loudest,
                peak / 2147483648.0, clamped, samples);
}

// ---- Bend and the contour -------------------------------------------------
// bend 0 is a still, pure sine on every bent wave; the reso waves are still too.
void straight() {
    for (unsigned w = 0; w < 8; ++w) {
        Controls c;
        c.set(bend::WAVE, choice(w)).set(bend::BEND, 0);
        const unsigned n = capture(c, 441, 3);
        const std::vector<double> early = spectrum(&sound[4096], 16384), late = spectrum(&sound[n - 16384], 16384);
        double moved = 0, top = 0;
        for (unsigned i = 0; i < early.size(); ++i) {
            moved = std::fmax(moved, std::fabs(early[i] - late[i]));
            top = std::fmax(top, early[i]);
        }
        char what[96];
        std::snprintf(what, sizeof what, "bend 0: wave %u changes while held", w);
        check(moved < 1e-4 * top, what);
        if (w < bend::RESO1) {
            const double h1 = near(late, 16384, 441), h2 = near(late, 16384, 882), h3 = near(late, 16384, 1323);
            std::snprintf(what, sizeof what, "bend 0: wave %u is not a pure sine (h2 %.0f dB, h3 %.0f dB)", w,
                          10 * std::log10(h2 / h1), 10 * std::log10(h3 / h1));
            check(h2 < 1e-8 * h1 && h3 < 1e-8 * h1, what);
        }
    }
}
// The contour: each note starts brighter than the knob within tens of
// milliseconds, decays over the next second to a little above it and breathes
// while held; a note played again restarts it without a jump.
void contour() {
    Controls c;
    c.set(bend::WAVE, choice(bend::RESO1)).set(bend::BEND, 0.35);
    capture(c, 220, 6);
    double at[6];
    const double when[6] = {0, 0.09, 0.6, 1.5, 3.0, 4.5};
    for (unsigned k = 0; k < 6; ++k) at[k] = centroid(spectrum(&sound[static_cast<unsigned>(when[k] * 44100)], 1024), 1024);
    std::printf("contour: reso1 at bend 35, centroid at 0 %.0f, 0.1 s %.0f, 0.6 s %.0f, 1.5 s %.0f, 3 s %.0f, 4.5 s %.0f Hz\n",
                at[0], at[1], at[2], at[3], at[4], at[5]);
    check(at[1] > 1.2 * at[0], "contour: the note does not brighten as it starts");
    check(at[1] > at[2] && at[2] > 1.05 * at[3], "contour: the attack does not decay");
    const double low = std::fmin(at[3], std::fmin(at[4], at[5])), high = std::fmax(at[3], std::fmax(at[4], at[5]));
    check(high > 1.02 * low && high < 1.5 * low, "contour: the held note does not breathe gently");
    check(bend::pd::contour(0, 0, bend::pd::breath_slowest) == 0, "contour: does not start from where it was left");
    check(bend::pd::contour(48, 0, bend::pd::breath_slowest) > 12000, "contour: does not near its peak within 70 ms");
    const i32 settled = bend::pd::contour(20000, 0, bend::pd::breath_slowest);
    check(settled > 4588 - 2400 && settled < 4588 + 2400, "contour: does not settle near 0.14");
    // bend 0 holds the contour off entirely; full bend has nothing left to add.
    check(bend::pd::bent(0, 20000) == 0 && bend::pd::bent(32767, 20000) == 32767, "contour: reaches past bend's ends");
    // Played again mid-note: no jump in the output.
    bend::initialize(voice, shared, 1);
    bend::start(voice, shared, 1);
    u32 pitch[frames];
    for (unsigned i = 0; i < frames; ++i) pitch[i] = increment(220);
    i32 out[frames];
    double last = 0, biggest = 0, typical = 0;
    for (unsigned b = 0; b < 400; ++b) {
        if (b == 300) bend::start(voice, shared, 1);
        bend::render(voice, shared, 1, out, c.p, pitch);
        for (unsigned i = 0; i < frames; ++i) {
            const double y = out[i] / 2147483648.0, step = std::fabs(y - last);
            if (b >= 299 && b <= 301) biggest = std::fmax(biggest, step);
            else if (b > 100) typical = std::fmax(typical, step);
            last = y;
        }
    }
    check(biggest < 1.2 * typical, "contour: a note played again jumps");
}

// ---- Sweeps: every control at 0, 25, 50, 75 and 100% ---------------------
const double quarters[5] = {0, 0.25, 0.5, 0.75, 1};
void sweep_wave() {
    std::vector<double> last;
    double d[8] = {0};
    for (unsigned w = 0; w < 8; ++w) {
        Controls c;
        c.set(bend::WAVE, choice(w)).set(bend::BEND, 0.6);
        std::vector<double> s = tail_spectrum(c, 220);
        if (w) {
            d[w] = distance(s, last, 32768);
            char what[80];
            std::snprintf(what, sizeof what, "sweep wave: wave %u sounds like wave %u (%.1f dB apart)", w, w - 1, d[w]);
            check(d[w] > 6, what);
        }
        last = s;
    }
    std::printf("sweep wave (bend 60): each wave from the last %.1f %.1f %.1f %.1f %.1f %.1f %.1f dB\n", d[1], d[2], d[3], d[4],
                d[5], d[6], d[7]);
}
void sweep_bend() {
    const unsigned waves[4] = {bend::SAW, bend::PULSE, bend::DSINE, bend::RESO2};
    const char* names[4] = {"saw", "pulse", "dsine", "reso2"};
    for (unsigned w = 0; w < 4; ++w) {
        double brightness[5];
        for (unsigned q = 0; q < 5; ++q) {
            Controls c;
            c.set(bend::WAVE, choice(waves[w])).set(bend::BEND, quarters[q]);
            brightness[q] = centroid(tail_spectrum(c, 220, 2.5), 32768);
        }
        std::printf("sweep bend (%s, 220 Hz, held 2.5 s): centroid %.0f %.0f %.0f %.0f %.0f Hz\n", names[w], brightness[0],
                    brightness[1], brightness[2], brightness[3], brightness[4]);
        for (unsigned q = 1; q < 5; ++q) {
            char what[96];
            std::snprintf(what, sizeof what, "sweep bend: %s brightens too little from %u%% to %u%%", names[w], 25 * (q - 1), 25 * q);
            check(brightness[q] > 1.05 * brightness[q - 1], what);
        }
    }
}
// Detune: at unison the lines beat at the cents between them (40 x^2).
void sweep_detune() {
    double beat[5];
    for (unsigned q = 0; q < 5; ++q) {
        Controls c;
        c.set(bend::BEND, 0).set(bend::DETUNE, quarters[q]);
        const unsigned n = capture(c, 900, 8);   // 9 cycles in each 10 ms
        // The envelope every 10 ms, and its strongest slow rate.
        const unsigned hop = 441, count = n / hop;
        std::vector<double> envelope(count);
        for (unsigned k = 0; k < count; ++k) {
            double s = 0;
            for (unsigned i = 0; i < hop; ++i) s += sound[k * hop + i] * sound[k * hop + i];
            envelope[k] = std::sqrt(s / hop);
        }
        double mean = 0;
        const unsigned first = 20;   // past the voice's first 0.2 s
        for (unsigned k = first; k < count; ++k) mean += envelope[k] / (count - first);
        double best = 0, rate = 0, depth = 0;
        for (double f = 0.2; f < 30; f += 0.01) {
            double re = 0, im = 0;
            for (unsigned k = first; k < count; ++k) {
                re += (envelope[k] - mean) * std::cos(2 * M_PI * f * k / 100);
                im += (envelope[k] - mean) * std::sin(2 * M_PI * f * k / 100);
            }
            if (re * re + im * im > best) { best = re * re + im * im; rate = f; }
        }
        for (unsigned k = first; k < count; ++k) depth = std::fmax(depth, std::fabs(envelope[k] - mean));
        beat[q] = depth > 0.05 * mean ? rate : 0;
        const double expected = 900 * (std::pow(2, 40 * quarters[q] * quarters[q] / 1200) - 1);
        char what[96];
        std::snprintf(what, sizeof what, "sweep detune: %.0f%% beats at %.2f Hz, not %.2f", 100 * quarters[q], beat[q], expected);
        check(q == 0 ? beat[q] == 0 : std::fabs(beat[q] - expected) < 0.06 * expected + 0.05, what);
    }
    std::printf("sweep detune (900 Hz at unison): the lines beat %.2f %.2f %.2f %.2f %.2f Hz\n", beat[0], beat[1], beat[2],
                beat[3], beat[4]);
}
// Ring: line 2 becomes its product with line 1 (at the 5th), ever further from ring 0.
void sweep_ring() {
    Controls base;
    base.set(bend::INTERVAL, choice(3)).set(bend::BEND, 0.4);
    const unsigned n = capture(base, 220, 1.5);
    const std::vector<double> clean(sound.begin() + n - 32768, sound.begin() + n);
    double moved[5];
    for (unsigned q = 0; q < 5; ++q) {
        Controls c(base);
        c.set(bend::RING, quarters[q]);
        moved[q] = change(clean, capture(c, 220, 1.5));
    }
    std::printf("sweep ring (at the 5th): change from ring 0 %.1f %.1f %.1f %.1f %.1f dB\n", moved[0], moved[1], moved[2],
                moved[3], moved[4]);
    check(moved[0] < -100, "sweep ring: ring 0 is not the plain mix");
    for (unsigned q = 1; q < 5; ++q) {
        char what[80];
        std::snprintf(what, sizeof what, "sweep ring: grows too little from %u%% to %u%%", 25 * (q - 1), 25 * q);
        check(moved[q] > (q == 1 ? -20 : moved[q - 1] + 2), what);
    }
}
// Interval: line 2 sounds at its interval, as loud as line 1.
void sweep_interval() {
    const double ratio[8] = {0.5, 1, 1.33484, 1.49831, 1.78180, 2, 2.99661, 4};
    for (unsigned k = 0; k < 8; ++k) {
        if (k == 1) continue;
        Controls c;
        c.set(bend::BEND, 0).set(bend::INTERVAL, choice(k));
        const std::vector<double> s = tail_spectrum(c, 110);
        const double line1 = near(s, 32768, 110), line2 = near(s, 32768, 110 * ratio[k]);
        char what[96];
        std::snprintf(what, sizeof what, "sweep interval: %s does not sound line 2 at %.3f (%.1f dB)", bend::pd::interval_names[k],
                      ratio[k], 10 * std::log10(line2 / line1));
        check(line2 > 0.5 * line1 && line2 < 2 * line1, what);
    }
}
// Warp: every quarter turn is a new sync-like timbre.
void sweep_warp() {
    const unsigned waves[2] = {bend::SAW, bend::RESO2};
    for (unsigned w = 0; w < 2; ++w) {
        std::vector<double> s[5];
        for (unsigned q = 0; q < 5; ++q) {
            Controls c;
            c.set(bend::WAVE, choice(waves[w])).set(bend::BEND, 0.5).set(bend::WARP, quarters[q]);
            s[q] = tail_spectrum(c, 220);
        }
        double d[4];
        for (unsigned q = 1; q < 5; ++q) d[q - 1] = distance(s[q], s[q - 1], 32768);
        std::printf("sweep warp (%s): change from the last quarter %.1f %.1f %.1f %.1f dB\n", w ? "reso2" : "saw", d[0], d[1],
                    d[2], d[3]);
        for (unsigned q = 0; q < 4; ++q) {
            char what[80];
            std::snprintf(what, sizeof what, "sweep warp: changes too little from %u%% to %u%%", 25 * q, 25 * (q + 1));
            check(d[q] > 4, what);
        }
    }
}
// Noise: ever more of the sound is noise, coloured by the note.
void sweep_noise() {
    double share[5];
    for (unsigned q = 0; q < 5; ++q) {
        Controls c;
        c.set(bend::BEND, 0.5).set(bend::NOISE, quarters[q]);
        share[q] = off_harmonic(tail_spectrum(c, 220.5), 32768, 220.5);
    }
    std::printf("sweep noise: power off the harmonics %.1f %.1f %.1f %.1f %.1f dB\n", share[0], share[1], share[2], share[3],
                share[4]);
    check(share[0] < -50, "sweep noise: noise 0 is not clean");
    for (unsigned q = 1; q < 5; ++q) {
        char what[80];
        std::snprintf(what, sizeof what, "sweep noise: grows too little from %u%% to %u%%", 25 * (q - 1), 25 * q);
        check(share[q] > (q == 1 ? -30 : share[q - 1] + 2.5), what);
    }
}
// lofi: the DAC's error grows at every quarter; at the top it aliases plainly.
void sweep_lofi() {
    Controls base;
    base.set(bend::BEND, 0.5);
    const unsigned n = capture(base, 220.5, 1.5);
    const std::vector<double> clean(sound.begin() + n - 32768, sound.begin() + n);
    double error[5], alias[5];
    for (unsigned q = 0; q < 5; ++q) {
        Controls c(base);
        c.set(bend::LOFI, quarters[q]);
        error[q] = change(clean, capture(c, 220.5, 1.5));
        alias[q] = off_harmonic(spectrum(&sound[n - 32768], 32768), 32768, 220.5);
    }
    std::printf("sweep lofi: error %.1f %.1f %.1f %.1f %.1f dB, off the harmonics %.1f %.1f %.1f %.1f %.1f dB\n", error[0],
                error[1], error[2], error[3], error[4], alias[0], alias[1], alias[2], alias[3], alias[4]);
    check(error[0] < -100, "sweep lofi: lofi 0 is not clean");
    for (unsigned q = 1; q < 5; ++q) {
        char what[80];
        std::snprintf(what, sizeof what, "sweep lofi: grows too little from %u%% to %u%%", 25 * (q - 1), 25 * q);
        check(error[q] > (q == 1 ? -40 : error[q - 1] + 3), what);
    }
    check(alias[4] > -25, "sweep lofi: the top does not alias");
}

// ---- Band limits ------------------------------------------------------------
// Without lofi, high notes stay clean: little power folds back below 12 kHz.
// Bend 0.5 is measured once its contour has settled (4.5 s), as its motion
// would also spread power between the harmonics.
void aliasing() {
    const double pitches[3] = {261.3, 1046.7, 2093.1};
    const double most[3] = {-48, -40, -34};
    double worst[3] = {-200, -200, -200};
    for (unsigned w = 0; w < 8; ++w)
        for (unsigned h = 0; h < 3; ++h)
            for (unsigned b = 0; b < 3; ++b)
                for (unsigned warp = 0; warp < 2; ++warp) {
                    Controls c;
                    c.set(bend::WAVE, choice(w)).set(bend::BEND, b * 0.5).set(bend::WARP, warp);
                    const double a = off_harmonic(tail_spectrum(c, pitches[h], b == 1 ? 4.5 : 1), 32768, pitches[h], 30, 12000);
                    worst[h] = std::fmax(worst[h], a);
                    if (a > most[h]) {
                        char what[96];
                        std::snprintf(what, sizeof what, "aliasing: wave %u bend %.1f warp %u at %.0f Hz folds %.1f dB", w,
                                      b * 0.5, warp, pitches[h], a);
                        check(false, what);
                    }
                }
    std::printf("aliasing below 12 kHz, worst over waves, bend and warp: %.1f dB at C4, %.1f at C6, %.1f at C7\n", worst[0],
                worst[1], worst[2]);
}

// ---- Level ------------------------------------------------------------------
// A new voice starts without a click or a thump: its first samples rise from
// silence and its DC blocker is already settled at the waves' offset.
void onset() {
    Controls c;
    c.set(bend::WAVE, choice(bend::PULSE)).set(bend::BEND, 0.8);
    capture(c, 110, 0.5);
    check(std::fabs(sound[0]) < 0.01, "onset: the first sample clicks");
    double mean = 0;
    for (unsigned i = 0; i < 4410; ++i) mean += sound[i] / 4410;
    char what[80];
    std::snprintf(what, sizeof what, "onset: the bent pulse's offset thumps (%.4f in the first 0.1 s)", mean);
    check(std::fabs(mean) < 0.01, what);
}
// Every preset's eight-note chord (definition.json's presets; C3 G3 C4 E4 G4
// B4 D5 G5 struck at once) peaks between 0.4 and 0.8 of full scale.
void chords() {
    const struct { const char* name; double c[8]; } presets[8] = {
        {"Clean", {choice(0), 0.25, 0, 0, choice(1), 0, 0, 0}},
        {"Haze", {choice(6), 0.35, 0.5, 0.2, choice(5), 0.1, 0.15, 0.3}},
        {"Brass 101", {choice(0), 0.45, 0.35, 0, choice(1), 0, 0, 0}},
        {"Wah Bass", {choice(5), 0.2, 0.15, 0, choice(0), 0, 0, 0}},
        {"Glass Ring", {choice(3), 0.55, 0.2, 0.6, choice(6), 0, 0, 0}},
        {"Sync Lead", {choice(1), 0.5, 0.25, 0, choice(3), 0.6, 0, 0}},
        {"Breath", {choice(2), 0.3, 0.4, 0, choice(5), 0, 0.45, 0}},
        {"Toy DAC", {choice(4), 0.6, 0.1, 0.3, choice(5), 0.2, 0, 0.8}}};
    const int notes[8] = {48, 55, 60, 64, 67, 71, 74, 79};
    static bend::Voice voices[8];
    double peaks[8];
    for (unsigned k = 0; k < 8; ++k) {
        Controls c;
        for (unsigned i = 0; i < 8; ++i) c.set(i, presets[k].c[i]);
        for (unsigned v = 0; v < 8; ++v) { bend::initialize(voices[v], shared, v + 1); bend::start(voices[v], shared, v + 1); }
        double loudest = 0;
        for (unsigned b = 0; b < 3 * 689; ++b) {
            double mix[frames] = {0};
            for (unsigned v = 0; v < 8; ++v) {
                u32 pitch[frames];
                for (unsigned i = 0; i < frames; ++i) pitch[i] = increment(440 * std::pow(2, (notes[v] - 69) / 12.0));
                i32 out[frames];
                bend::render(voices[v], shared, v + 1, out, c.p, pitch);
                for (unsigned i = 0; i < frames; ++i) mix[i] += out[i] / 2147483648.0;
            }
            for (unsigned i = 0; i < frames; ++i) loudest = std::fmax(loudest, std::fabs(mix[i]));
        }
        peaks[k] = loudest;
    }
    std::printf("chords: eight-note peaks");
    for (unsigned k = 0; k < 8; ++k) std::printf(" %s %.2f%s", presets[k].name, peaks[k], k < 7 ? "," : "\n");
    for (unsigned k = 0; k < 8; ++k) {
        char what[80];
        std::snprintf(what, sizeof what, "chords: %s's eight-note chord peaks at %.2f", presets[k].name, peaks[k]);
        check(peaks[k] > 0.4 && peaks[k] < 0.8, what);
    }
}
// No integer equivalent of NaN: a minute at the harshest settings keeps
// sounding, never pins at the rail, and the DC blocker stays settled.
void long_run() {
    Controls c;
    for (unsigned k = 0; k < 8; ++k) c.p[k] = full;
    bend::initialize(voice, shared, 1);
    bend::start(voice, shared, 1);
    u32 pitch[frames];
    for (unsigned i = 0; i < frames; ++i) pitch[i] = increment(55);
    i32 out[frames];
    double power = 0;
    for (unsigned b = 0; b < 60 * 689; ++b) {
        bend::render(voice, shared, 1, out, c.p, pitch);
        if (b >= 59 * 689)
            for (unsigned i = 0; i < frames; ++i) power += (out[i] / 2147483648.0) * (out[i] / 2147483648.0);
    }
    const double level = std::sqrt(power / (689.0 * frames));
    check(level > 0.01 && level < 0.15, "long run: the voice decays, explodes or pins");
    check(std::abs(voice.dc_out) < (1 << 27), "long run: the DC blocker ran away");
}

// ---- Screen -------------------------------------------------------------------
// The SDK's outline check (sdk/browser/display.mjs outlineFaults), stricter:
// every point distinct and no point between two equal neighbours.
bool outline_ok(const bend::Frame::path_type& path) {
    for (unsigned i = 0; i < 64; ++i)
        for (unsigned j = i + 1; j < 64; ++j)
            if (path.points[i].x == path.points[j].x && path.points[i].y == path.points[j].y) return false;
    for (unsigned i = 0; i < 64; ++i) {
        const opxy::draw::Point& a = path.points[(i + 63) % 64];
        const opxy::draw::Point& b = path.points[(i + 1) % 64];
        if (a.x == b.x && a.y == b.y) return false;
    }
    return true;
}
struct Screen {
    bend::DisplayState state;
    bend::Frame frame;
    opxy::draw::Input input;
    void clear() {
        std::memset(&state, 0, sizeof state);
        std::memset(&frame, 0, sizeof frame);
        std::memset(&input, 0, sizeof input);
        bend::display_reset(state);
        input.track = 1;
        for (unsigned k = 0; k < 4; ++k) input.shown[k] = static_cast<opxy::u8>(k);
    }
    void set(const double (&v)[8]) { for (unsigned k = 0; k < 8; ++k) input.slots[k] = word(v[k]); }
    void tick() { input.elapsed = 17; bend::display(state, input, frame); }   // 60 frames a second
};
Screen s;
bend::Frame before;
bool same() { return std::memcmp(&before, &s.frame, sizeof before) == 0; }
void screen() {
    bool valid = true, outlines = true;
    // Every wave, bent and warped, noisy and coarse, with notes coming and going.
    for (unsigned w = 0; w < 8; ++w)
        for (unsigned corner = 0; corner < 16; ++corner) {
            s.clear();
            const double v[8] = {choice(w), corner & 1 ? 1.0 : 0.4, 0.5, corner & 2 ? 1.0 : 0.0, choice(corner & 4 ? 7 : 0),
                                 corner & 4 ? 1.0 : 0.0, corner & 8 ? 1.0 : 0.0, corner & 8 ? 1.0 : 0.0};
            s.set(v);
            for (unsigned f = 0; f < 300; ++f) {
                if (f % 50 == 10) { ++s.input.note_serial; s.input.held_notes = 1 + (f / 50) % 8; }
                if (f % 50 == 40) { s.input.held_notes = 0; ++s.input.note_off_serial; }
                if (f == 150) s.input.slots[bend::WAVE] = word(choice((w + 3) % 8));   // a new wave fades in
                s.tick();
                valid = valid && opxy::draw::valid(s.frame);
                outlines = outlines && outline_ok(s.frame.paths[bend::node::map]) && outline_ok(s.frame.paths[bend::node::fading]);
            }
        }
    check(valid, "screen: a frame is invalid");
    check(outlines, "screen: a map outline repeats a point or turns straight back");

    // Still when idle; a turn settles within 0.2 s; held notes move it; reduced motion jumps.
    s.clear();
    const double haze[8] = {choice(bend::RESO2), 0.35, 0.5, 0.2, choice(5), 0.1, 0.15, 0.3};
    s.set(haze);
    s.tick();
    const char* expected[4] = {"reso2", "35", "10c", "20"};
    for (unsigned k = 0; k < 4; ++k) {
        char what[80];
        std::snprintf(what, sizeof what, "screen: header %u reads %s, not %s", k, s.frame.header[k], expected[k]);
        check(std::strcmp(s.frame.header[k], expected[k]) == 0, what);
    }
    const opxy::draw::Node rest = s.frame.nodes[bend::node::needle];
    s.input.held_notes = 2;
    ++s.input.note_serial;
    for (unsigned f = 0; f < 4; ++f) s.tick();
    const opxy::draw::Node up = s.frame.nodes[bend::node::needle];
    check(up.sin > rest.sin + 100, "screen: a held note's dial does not swing with its contour");
    check(up.shade == bend::shade::needle_lit && s.frame.nodes[bend::node::needle + 2].shade == bend::shade::needle_dim,
          "screen: the dials do not light for held notes");
    before = s.frame;
    s.tick();
    check(!same(), "screen: held notes do not move it");
    for (unsigned f = 0; f < 60; ++f) s.tick();
    s.input.held_notes = 0;
    ++s.input.note_off_serial;
    for (unsigned f = 0; f < 30; ++f) s.tick();   // half a second to settle
    before = s.frame;
    bool still = true;
    for (unsigned f = 0; f < 120; ++f) { s.tick(); still = still && same(); }
    check(still, "screen: the idle screen moves");
    s.input.slots[bend::BEND] = word(0.9);
    for (unsigned f = 0; f < 12; ++f) s.tick();   // 0.2 s
    before = s.frame;
    s.tick();
    check(same(), "screen: a turn has not settled after 0.2 s");
    s.input.reduced_motion = 1;
    s.input.held_notes = 3;
    ++s.input.note_serial;
    s.input.slots[bend::BEND] = word(0.2);
    s.tick();
    before = s.frame;
    for (unsigned f = 0; f < 10; ++f) s.tick();
    check(same(), "screen: reduced motion still eases or animates");
}
}

int main() {
    captions();
    extremes();
    straight();
    contour();
    sweep_wave();
    sweep_bend();
    sweep_detune();
    sweep_ring();
    sweep_interval();
    sweep_warp();
    sweep_noise();
    sweep_lofi();
    aliasing();
    onset();
    chords();
    long_run();
    screen();
    if (!failures) std::puts("PASS bend sound checks");
    return failures ? 1 : 0;
}
