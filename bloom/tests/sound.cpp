// Bloom's sound checks, compiled for the host and run by
// python sdk.py test --machine <folder>. Each check prints FAIL with a reason.
// Every control is measured at 0, 25, 50, 75 and 100 and must move what it
// names by an audible amount at each step.
#include "../engine.cpp"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
using bloom::i32;
using bloom::u32;

const double rate = 44100.0, pi = 3.14159265358979323846;
const i32 full = 0x7fffffff;
const i32 declared_peak = 450600000;   // definition.json outputPeakQ31
int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}

u32 increment(double hz) { return static_cast<u32>(hz / rate * 4294967296.0 + 0.5); }
i32 word(double normalized) { return static_cast<i32>(normalized * 2147483647.0 + 0.5); }

// Parameters: tilt, shape, stretch, vowel, breath, blur, haze, time.
enum { tilt, shape, stretch, vowel, breath, blur, haze, time_ };
struct Setting {
    double v[8];
};
Setting defaults() {
    Setting s = {{0.5, 0, 0, 0, 0, 0, 0, 0.6}};
    return s;
}
Setting with(int control, double value, Setting s = defaults()) {
    s.v[control] = value;
    return s;
}
const double sweep[5] = {0, 0.25, 0.5, 0.75, 1};

struct Note {
    bloom::Voice voice;
    bloom::Shared shared;
    i32 parameters[8];
    u32 pitch[opxy::block_frames];
    Note(const Setting& s, double hz) {
        std::memset(&voice, 0, sizeof voice);
        std::memset(&shared, 0, sizeof shared);
        set(s);
        for (unsigned i = 0; i < opxy::block_frames; ++i) pitch[i] = increment(hz);
        bloom::initialize(voice, shared, 1);
        bloom::start(voice, shared, 1);
    }
    void set(const Setting& s) {
        for (int i = 0; i < 8; ++i) parameters[i] = word(s.v[i]);
    }
    // Renders blocks into out (if given); returns the largest magnitude seen.
    i32 run(unsigned blocks, i32* out = 0) {
        i32 peak = 0, block[opxy::block_frames];
        for (unsigned b = 0; b < blocks; ++b) {
            bloom::render(voice, shared, 1, block, parameters, pitch);
            for (unsigned i = 0; i < opxy::block_frames; ++i) {
                const i32 m = block[i] < 0 ? -block[i] : block[i];
                if (m > peak) peak = m;
                if (out) out[b * opxy::block_frames + i] = block[i];
            }
        }
        return peak;
    }
};

// Amplitude of the component at hz in x[0, n), as a fraction of full scale
// (Goertzel through a Hann window, so neighbours barely leak in).
double amplitude(const i32* x, unsigned n, double hz) {
    const double w = 2 * pi * hz / rate, c = 2 * std::cos(w);
    double s1 = 0, s2 = 0, weight = 0;
    for (unsigned i = 0; i < n; ++i) {
        const double hann = 0.5 - 0.5 * std::cos(2 * pi * i / n);
        const double s0 = hann * x[i] / 2147483648.0 + c * s1 - s2;
        s2 = s1;
        s1 = s0;
        weight += hann;
    }
    const double re = s1 - s2 * std::cos(w), im = s2 * std::sin(w);
    return 2 * std::sqrt(re * re + im * im) / weight;
}
double db(double a) { return 20 * std::log10(a + 1e-12); }

// 441 Hz repeats every 100 samples, so a 4400-sample window holds whole
// cycles of every harmonic and they do not leak into each other.
const double f0 = 441.0;
const unsigned window = 4400, window_blocks = 69;

void harmonics(Note& note, double (&out)[25], double hz = f0) {
    static i32 x[window_blocks * opxy::block_frames];
    note.run(window_blocks, x);
    for (int k = 1; k <= 24; ++k) out[k] = amplitude(x, window, hz * k);
}
void harmonics_of(const Setting& s, double (&out)[25], double hz = f0) {
    Note note(s, hz);
    note.run(8);
    harmonics(note, out, hz);
}

// Centroid of the first 24 harmonics, in harmonic numbers.
double centroid(const Setting& s) {
    double h[25], sum = 0, weighted = 0;
    harmonics_of(s, h);
    for (int k = 1; k <= 24; ++k) { sum += h[k]; weighted += k * h[k]; }
    return weighted / sum;
}

// The biggest relative change of any of the first 12 harmonics between now
// and `later` blocks on.
double spectral_change(Note& note, unsigned later) {
    double a[25], b[25];
    harmonics(note, a);
    note.run(later);
    harmonics(note, b);
    double worst = 0;
    for (int k = 1; k <= 12; ++k) {
        const double change = std::fabs(a[k] - b[k]) / (a[1] + 1e-12);
        if (change > worst) worst = change;
    }
    return worst;
}

// A long render for spectra: 2^15 samples after a second's settling.
const unsigned long_n = 32768, long_blocks = long_n / opxy::block_frames;
void render_long(const Setting& s, double hz, i32* x) {
    Note note(s, hz);
    note.run(689);
    note.run(long_blocks, x);
}
// Power spectrum by direct DFT bins near given frequencies: the fraction of a
// note's power (summed over bins up to 6 kHz) within 6 Hz of its harmonics (the window's main lobe).
double tonal_fraction(const Setting& s) {
    static i32 x[32768];
    render_long(s, f0, x);
    // Goertzel at every 2.69 Hz bin (rate / 16384) from 20 Hz to 6 kHz, over the first 16384 samples.
    const unsigned n = 16384;
    double total = 0, near = 0;
    for (unsigned bin = 8; bin < 2229; ++bin) {
        const double hz = bin * rate / n, a = amplitude(x, n, hz), p = a * a;
        total += p;
        const double k = std::floor(hz / f0 + 0.5);
        if (k >= 1 && std::fabs(hz - k * f0) <= 6.0) near += p;
    }
    return near / total;
}

// Envelope statistics of harmonic k over 2 s: depth (std / mean) and the
// rate its mean is crossed, per second, halved (cycles per second).
struct Movement { double depth, rate_hz, db_spread; };
Movement envelope(const Setting& s, int k, double seconds = 2, unsigned frame = 1024, unsigned hop = 128) {
    static i32 x[441 * 1024];
    const unsigned blocks = static_cast<unsigned>(seconds * rate / 64);
    Note note(s, f0);
    note.run(689);
    note.run(blocks, x);
    const unsigned n = blocks * 64;
    double e[4000];
    unsigned count = 0;
    for (unsigned start = 0; start + frame <= n && count < 4000; start += hop) e[count++] = amplitude(x + start, frame, f0 * k);
    double mean = 0, logs = 0;
    for (unsigned i = 0; i < count; ++i) { mean += e[i]; logs += db(e[i]); }
    mean /= count;
    logs /= count;
    double var = 0, spread = 0;
    int crossings = 0;
    for (unsigned i = 0; i < count; ++i) {
        var += (e[i] - mean) * (e[i] - mean);
        spread += (db(e[i]) - logs) * (db(e[i]) - logs);
        if (i && (e[i] > mean) != (e[i - 1] > mean)) ++crossings;
    }
    Movement m;
    m.depth = std::sqrt(var / count) / (mean + 1e-12);
    m.rate_hz = crossings / 2.0 / (count * hop / rate);
    m.db_spread = std::sqrt(spread / count);
    return m;
}
// Mean over harmonics 2..10.
Movement envelopes(const Setting& s, double seconds = 2, unsigned frame = 1024, unsigned hop = 128) {
    Movement sum = {0, 0, 0};
    for (int k = 2; k <= 10; ++k) {
        const Movement m = envelope(s, k, seconds, frame, hop);
        sum.depth += m.depth / 9;
        sum.rate_hz += m.rate_hz / 9;
        sum.db_spread += m.db_spread / 9;
    }
    return sum;
}

// Frequency of a near-sine tone by its rising zero crossings over two seconds.
double measured(double hz, const Setting& s) {
    Note note(s, hz);
    note.run(4);
    static i32 x[1378 * opxy::block_frames];
    note.run(1378, x);
    const unsigned n = 1378 * opxy::block_frames;
    int first = -1, last = -1, crossings = 0;
    for (unsigned i = 1; i < n; ++i)
        if (x[i - 1] < 0 && x[i] >= 0) {
            if (first < 0) first = static_cast<int>(i);
            last = static_cast<int>(i);
            ++crossings;
        }
    if (crossings < 2) return 0;
    return (crossings - 1) * rate / (last - first);
}

// The spectrum's own levels in dB, from spectrum.h, at note time t.
void levels_db(const Setting& s, u32 t, double (&out)[24]) {
    i32 p[8];
    for (int i = 0; i < 8; ++i) p[i] = word(s.v[i]);
    bloom::Spectrum sp;
    bloom::shape(bloom::decode(p), 3, t, 0, bloom::log2_of(increment(220)), sp);
    for (int k = 0; k < 24; ++k) out[k] = db(sp.level[k] / 32768.0);
}
bool increasing(const double (&v)[5], double by) {
    for (int i = 1; i < 5; ++i) if (!(v[i] > v[i - 1] + by)) return false;
    return true;
}
bool decreasing(const double (&v)[5], double by) {
    for (int i = 1; i < 5; ++i) if (!(v[i] < v[i - 1] - by)) return false;
    return true;
}
void row(const char* name, const double (&v)[5], const char* unit) {
    std::printf("%-8s", name);
    for (int i = 0; i < 5; ++i) std::printf(" %8.3g", v[i]);
    std::printf("  %s\n", unit);
}
}  // namespace

int main() {
    std::printf("control at 0, 25, 50, 75, 100:\n");

    // 1. Within the declared peak, and sounding, at every corner of the
    //    controls, from the lowest to the highest notes, with restarts.
    {
        const double notes[] = {8.18, 55, 441, 3000, 12000, 21000};
        i32 worst = 0;
        bool sounding = true;
        for (unsigned corner = 0; corner < 256; ++corner)
            for (unsigned n = 0; n < sizeof notes / sizeof notes[0]; ++n) {
                Setting s;
                for (int i = 0; i < 8; ++i) s.v[i] = (corner >> i) & 1 ? 1.0 : 0.0;
                Note note(s, notes[n]);
                i32 peak = note.run(60);
                bloom::start(note.voice, note.shared, 1);
                const i32 again = note.run(60);
                if (again > peak) peak = again;
                if (peak > worst) worst = peak;
                if (notes[n] < 20000 && peak == 0) sounding = false;
            }
        // Random settings changing every few blocks, over long notes.
        unsigned seed = 1;
        for (unsigned trial = 0; trial < 40; ++trial) {
            Note note(defaults(), 30 + trial * 97.0);
            for (unsigned b = 0; b < 700; ++b) {
                if (b % 7 == 0)
                    for (int i = 0; i < 8; ++i) {
                        seed = seed * 1664525u + 1013904223u;
                        note.parameters[i] = static_cast<i32>(seed >> 1);
                    }
                const i32 peak = note.run(1);
                if (peak > worst) worst = peak;
            }
        }
        check(worst <= declared_peak, "output above its declared peak");
        check(worst > declared_peak / 2, "output much quieter than its declared peak");
        check(sounding, "silent at some setting");
        std::printf("peak %.3f of full scale (declared %.3f)\n", worst / 2147483648.0, declared_peak / 2147483648.0);
    }

    // 2. The defaults are clean: harmonic, strongest at the fundamental, still.
    {
        double h[25];
        harmonics_of(defaults(), h);
        bool fundamental_first = true;
        for (int k = 2; k <= 24; ++k) fundamental_first = fundamental_first && h[k] < h[1];
        check(fundamental_first, "default spectrum not strongest at its fundamental");
        check(h[2] > h[1] * 0.2, "default spectrum has no even partials");
        Note still(defaults(), f0);
        still.run(8);
        const double change = spectral_change(still, 1378 * 5);
        check(change < 1e-3, "the default sound moves");
        const double tonal = tonal_fraction(defaults());
        check(tonal > 0.999, "the default sound is not purely harmonic");
    }

    // 3. Tilt moves the spectral centroid up, from a near-sine to bright.
    {
        double c[5];
        for (int i = 0; i < 5; ++i) c[i] = centroid(with(tilt, sweep[i]));
        row("tilt", c, "centroid, harmonic number");
        check(c[0] < 1.2 && increasing(c, 0.05) && c[4] > 8, "tilt does not move the spectral centroid");
    }

    // 4. Shape: saw to square (the even partials fade), then a narrowing pulse
    //    (notches every 1/width partials).
    {
        double evens[5], h[25];
        for (int i = 0; i < 5; ++i) {
            harmonics_of(with(shape, sweep[i], with(tilt, 0.6)), h);
            double even = 0, odd = 0;
            for (int k = 2; k <= 12; k += 2) even += h[k] * h[k];
            for (int k = 3; k <= 13; k += 2) odd += h[k] * h[k];
            evens[i] = 10 * std::log10(even / odd + 1e-20);
        }
        row("shape", evens, "even/odd partials, dB");
        check(evens[0] > -1 && evens[1] < evens[0] - 6 && evens[2] < -80 && evens[3] > -6, "shape does not fade the even partials");
        // At 75% the pulse is 17.7% wide: partial 6 falls near the notch at 5.66.
        harmonics_of(with(shape, 0.75, with(tilt, 0.6)), h);
        check(h[6] < h[3] * 0.25 && h[11] < h[9] * 0.3, "shape at 75 does not notch near partials 6 and 11");
        // At 100 the pulse is 6.25% wide: partial 16 is notched out, the rest nearly flat below it.
        harmonics_of(with(shape, 1, with(tilt, 0.6)), h);
        check(h[16] < h[1] * 1e-3 && h[8] > h[1] * 0.4, "shape at 100 is not a narrow pulse");
        check(bloom::shape_width_percent(word(0.75)) == 18 && bloom::shape_width_percent(full) == 6, "pulse width caption");
    }

    // 5. Stretch moves partials off the harmonic series, to k^1.585 at full
    //    (partial 2 at a twelfth).
    {
        double ratio[5];
        for (int i = 0; i < 5; ++i) {
            i32 p[8] = {word(0.5), 0, word(sweep[i]), 0, 0, 0, 0, word(0.6)};
            bloom::Spectrum sp;
            bloom::shape(bloom::decode(p), 1, 0, 0, bloom::log2_of(increment(f0)), sp);
            ratio[i] = std::pow(2.0, sp.pitch[1] / 65536.0);
        }
        row("stretch", ratio, "partial 2's ratio");
        check(std::fabs(ratio[0] - 2) < 0.001 && std::fabs(ratio[4] - 3) < 0.005 && increasing(ratio, 0.15),
              "stretch does not move partial 2 from 2 to 3");
        Setting s = with(stretch, 1);
        Note bell(s, f0);
        bell.run(8);
        static i32 x[window_blocks * opxy::block_frames];
        bell.run(window_blocks, x);
        const double fundamental = amplitude(x, window, f0);
        const double stretched2 = amplitude(x, window, f0 * 3.0);
        const double stretched3 = amplitude(x, window, f0 * std::pow(3.0, 1.585));
        double harmonic = 0;
        for (int k = 2; k <= 8; ++k) {
            if (k == 3) continue;   // 2^1.585 is the third harmonic
            const double a = amplitude(x, window, f0 * k);
            harmonic = a > harmonic ? a : harmonic;
        }
        check(stretched2 > fundamental * 0.2 && stretched3 > fundamental * 0.1 && harmonic < fundamental * 0.03,
              "stretch does not make the partials inharmonic");
        check(bloom::stretch_x100(0) == 200 && bloom::stretch_x100(full) == 300, "stretch caption");
    }

    // 6. Vowel: off at 0; then the strongest harmonic of C3 follows the first
    //    formant (oo 300, oh 480, ah 730, eh 530, ee 270 Hz) and ee lifts the
    //    second formant (2290 Hz) far above oo.
    {
        const double c3 = 130.8128;
        double peak_hz[5];
        const double at[5] = {0.1, 0.325, 0.55, 0.775, 1.0};   // oo, oh, ah, eh, ee
        const double f1[5] = {300, 480, 730, 530, 270};
        bool follows = true;
        double h[25], oo_f2 = 0, ee_f2 = 0;
        for (int i = 0; i < 5; ++i) {
            harmonics_of(with(vowel, at[i]), h, c3);
            int best = 1;
            for (int k = 2; k <= 24; ++k) if (h[k] > h[best]) best = k;
            peak_hz[i] = best * c3;
            follows = follows && std::fabs(peak_hz[i] - f1[i]) < c3 * 0.75;
            const double f2 = db(h[17]) + db(h[18]) - 2 * db(h[1]);   // 2224 and 2355 Hz
            if (i == 0) oo_f2 = f2;
            if (i == 4) ee_f2 = f2;
        }
        row("vowel", peak_hz, "strongest harmonic of C3, Hz (oo oh ah eh ee)");
        check(follows, "vowel does not move the first formant");
        check(ee_f2 > oo_f2 + 24, "vowel ee does not lift the second formant");
        // Off is exactly off.
        double off[25], none[25];
        harmonics_of(with(vowel, 0), off, c3);
        harmonics_of(defaults(), none, c3);
        bool same = true;
        for (int k = 1; k <= 24; ++k) same = same && off[k] == none[k];
        check(same, "vowel at 0 changes the sound");
        std::printf("vowel ee lifts partials 17-18 by %.1f dB over oo\n", (ee_f2 - oo_f2) / 2);
    }

    // 7. Breath: how far the partials' levels drift, 0 to 18 dB.
    {
        double spread[5];
        for (int i = 0; i < 5; ++i) {
            // Each partial's level against its level without breath, over 40
            // drift periods, from spectrum.h (less the common shift).
            double squares = 0;
            int count = 0;
            for (u32 t = 0; t < 40u << 20; t += 1u << 17) {
                double l[24], still[24], mean = 0;
                levels_db(with(breath, sweep[i]), t, l);
                levels_db(defaults(), t, still);
                for (int k = 0; k < 10; ++k) mean += (l[k] - still[k]) / 10;
                for (int k = 0; k < 10; ++k) { squares += (l[k] - still[k] - mean) * (l[k] - still[k] - mean); ++count; }
            }
            spread[i] = std::sqrt(squares / count);
        }
        row("breath", spread, "level drift of partials 1..10, dB rms");
        check(spread[0] < 0.01 && increasing(spread, 1) && spread[4] > 6, "breath does not move the partials' levels");
        // Heard: the harmonics' levels move over time.
        const Movement m0 = envelopes(with(breath, 0), 4, 2048, 1024), m1 = envelopes(with(breath, 1), 4, 2048, 1024);
        check(m0.db_spread < 0.05 && m1.db_spread > 3, "breath's drift is not heard");
    }

    // 8. Blur: each partial a band of noise; the share of power on the
    //    harmonics falls, the fundamental stays clear.
    {
        double tonal[5];
        for (int i = 0; i < 5; ++i) tonal[i] = tonal_fraction(with(blur, sweep[i], with(tilt, 0.75)));
        row("blur", tonal, "share of power within 6 Hz of a harmonic");
        check(tonal[0] > 0.999 && decreasing(tonal, 0.04) && tonal[2] < 0.7 && tonal[4] < 0.45, "blur does not smear the partials");
        const Movement fundamental = envelope(with(blur, 1), 1, 2, 4096, 512);
        check(fundamental.depth < 0.25, "blur smears the fundamental too");
        check(bloom::blur_cents(full) == 120 && bloom::blur_cents(word(0.5)) == 42, "blur caption");
    }

    // 9. Haze: each partial a detuned pair. Beating sets in by 25 and gets
    //    faster across the travel; the note stays in tune.
    {
        double depth[5], rate_hz[5];
        for (int i = 0; i < 5; ++i) {
            const Movement m = envelopes(with(haze, sweep[i], with(tilt, 0.75)));
            depth[i] = m.depth;
            rate_hz[i] = m.rate_hz;
        }
        row("haze", depth, "beat depth, harmonics 2..10");
        row("haze", rate_hz, "beat rate, Hz");
        bool deep = depth[0] < 0.01;
        for (int i = 1; i < 5; ++i) deep = deep && depth[i] > 0.35;
        check(deep, "haze does not make the partials beat");
        check(rate_hz[1] > 1 && rate_hz[2] > rate_hz[1] * 2 && rate_hz[3] > rate_hz[2] * 1.5 && rate_hz[4] > rate_hz[3] * 1.05,
              "haze's beats do not get faster");
        const double tuned = measured(220, with(haze, 1, with(tilt, 0)));
        check(std::fabs(tuned / 220 - 1) < 2e-4, "haze detunes the note");
        check(bloom::haze_cents(full) == 100 && bloom::haze_cents(word(0.5)) == 25, "haze caption");
    }

    // 10. Time: the drift's period, 30 ms to 30 s, then hold. The levels move
    //     faster the shorter it is; at hold they stand still.
    {
        double change[5];
        for (int i = 0; i < 5; ++i) {
            // Mean level change of partials 1..10 per second, from spectrum.h, block by block.
            i32 p[8];
            const Setting s = with(time_, sweep[i], with(breath, 1));
            for (int j = 0; j < 8; ++j) p[j] = word(s.v[j]);
            const bloom::Controls c = bloom::decode(p);
            bloom::Spectrum a, b;
            double sum = 0;
            u32 t = 12345;
            bloom::shape(c, 5, t, 0, bloom::log2_of(increment(220)), a);
            for (int block = 0; block < 2756; ++block) {   // 4 s
                t += c.step;
                bloom::shape(c, 5, t, 0, bloom::log2_of(increment(220)), b);
                for (int k = 0; k < 10; ++k) sum += std::fabs(db(b.level[k] / 32768.0) - db(a.level[k] / 32768.0));
                a = b;
            }
            change[i] = sum / 10 / 4;
        }
        row("time", change, "level change, dB per second");
        check(change[0] > 100 && decreasing(change, 0) && change[0] > change[1] * 3 && change[1] > change[2] * 3 &&
                  change[2] > change[3] * 3 && change[4] == 0,
              "time does not set the drift's speed");
        // Held: with breath up, nothing changes over ten seconds; running, it moves.
        Setting s = with(breath, 1, with(time_, 1));
        Note frozen(s, f0);
        frozen.run(8);
        const double still = spectral_change(frozen, 1378 * 5);
        s.v[time_] = 0.6;
        Note moving(s, f0);
        moving.run(8);
        const double moved = spectral_change(moving, 1378 * 5);
        check(still < 1e-3 && moved > 0.05, "time at hold does not hold the drift");
        std::printf("spectral change over 10 s: held %.6f, running %.3f\n", still, moved);
        check(bloom::time_ms(0) == 30 && bloom::time_ms(full) == 0, "time caption");
        const u32 slowest = bloom::time_ms(word(0.984));
        check(slowest > 25000 && slowest < 30000, "time caption below hold");
    }

    // 11. Nothing at or above Nyquist: on high notes the partials that would
    //     fold back are dropped, so no aliases sound below.
    {
        Setting s = with(tilt, 1);   // bright: the upper partials as loud as they get
        const double hz = 7 * f0;    // whole cycles in the window, aliases on no harmonic
        Note high(s, hz);
        high.run(8);
        static i32 x[window_blocks * opxy::block_frames];
        high.run(window_blocks, x);
        const double fundamental = amplitude(x, window, hz);
        double worst = 0;
        for (int k = 8; k <= 24; ++k) {
            double alias = std::fmod(k * hz, rate);
            if (alias > rate / 2) alias = rate - alias;
            const double a = amplitude(x, window, alias);
            worst = a > worst ? a : worst;
        }
        const double kept = amplitude(x, window, 5 * hz);
        check(fundamental > 0.003 && kept > fundamental * 0.5 && worst < fundamental * 1e-3, "aliasing partials above Nyquist");
        std::printf("3 kHz note, bright: worst alias %.1f dB below the fundamental\n", db(fundamental) - db(worst));
        // The spectrum itself never gives a level to a partial at or above the
        // cutoff, whatever the stretch, on any note.
        bool clean = true;
        for (int st = 0; st <= 4; ++st)
            for (double note_hz = 1000; note_hz < 22000; note_hz *= 1.19) {
                i32 p[8] = {full, 0, word(st / 4.0), full, full, 0, 0, word(0.3)};
                bloom::Spectrum sp;
                const i32 log_note = bloom::log2_of(increment(note_hz));
                for (u32 t = 0; t < 40u << 20; t += 3u << 18) {
                    bloom::shape(bloom::decode(p), 7, t, 0, log_note, sp);
                    for (u32 k = 0; k < bloom::partial_count; ++k)
                        if (sp.level[k] && log_note + sp.pitch[k] >= bloom::log2_cutoff) clean = false;
                }
            }
        check(clean, "a partial at or above the cutoff has a level");
    }

    // 12. Pitch tracking: a near-sine sounds at the played frequency.
    {
        const double notes[] = {32.7, 110, 441, 1000, 3520, 8000};
        double worst = 0;
        for (unsigned n = 0; n < sizeof notes / sizeof notes[0]; ++n) {
            const double error = std::fabs(measured(notes[n], with(tilt, 0)) / notes[n] - 1);
            worst = error > worst ? error : worst;
        }
        check(worst < 2e-4, "pitch does not track the note");
        std::printf("pitch error at most %.5f%%\n", worst * 100);
    }

    // 13. Deterministic: the same notes give the same sound; another note's
    //     seed sounds different.
    {
        Setting s = defaults();
        s.v[breath] = 0.8;
        s.v[haze] = 0.6;
        s.v[blur] = 0.5;
        static i32 a[400 * opxy::block_frames], b[400 * opxy::block_frames];
        Note one(s, 220), two(s, 220);
        one.run(400, a);
        two.run(400, b);
        check(std::memcmp(a, b, sizeof a) == 0, "the same note does not repeat exactly");
        bloom::start(two.voice, two.shared, 1);   // the track's next note: a new seed
        bloom::start(one.voice, one.shared, 1);
        bloom::start(one.voice, one.shared, 1);
        one.run(400, a);
        two.run(400, b);
        check(std::memcmp(a, b, sizeof a) != 0, "different notes share one seed");
    }

    // 14. The weather moves once per block, however many voices render.
    {
        Setting s = with(breath, 1, with(time_, 0));
        Note chord(s, 220);
        bloom::Voice other;
        std::memset(&other, 0, sizeof other);
        bloom::initialize(other, chord.shared, 2);
        i32 out[opxy::block_frames];
        chord.run(1);
        bloom::render(other, chord.shared, 2, out, chord.parameters, chord.pitch);
        const u32 cycle = chord.shared.cycle, clock = chord.shared.clock;
        for (int b = 0; b < 100; ++b) {
            chord.run(1);
            bloom::render(other, chord.shared, 2, out, chord.parameters, chord.pitch);
        }
        check(chord.shared.cycle == cycle + 100 && chord.shared.clock == clock + 100 * (bloom::time_step(0) >> 2),
              "the shared weather does not advance once per block");
    }

    // 15. The levels never sum above peak_level, at any setting and time.
    {
        unsigned seed = 7;
        bool bounded = true;
        for (int trial = 0; trial < 20000; ++trial) {
            i32 p[8];
            for (int i = 0; i < 8; ++i) {
                seed = seed * 1664525u + 1013904223u;
                p[i] = static_cast<i32>(seed >> 1);
            }
            seed = seed * 1664525u + 1013904223u;
            bloom::Spectrum sp;
            bloom::shape(bloom::decode(p), seed, seed * 2654435761u, static_cast<i32>(seed >> 16) - 32768,
                         bloom::log2_of(seed >> 1 | 1u << 20), sp);
            i32 sum = 0;
            for (u32 k = 0; k < bloom::partial_count; ++k) sum += sp.level[k];
            bounded = bounded && sum <= bloom::peak_level;
        }
        check(bounded, "levels sum above peak_level");
    }

    // 16. Captions follow the sound's laws.
    {
        check(bloom::tilt_db(0) == -24 && bloom::tilt_db(word(0.5)) == -9 && bloom::tilt_db(full) == 6, "tilt caption");
        check(bloom::shape_square_percent(0) == 0 && bloom::shape_square_percent(word(0.25)) == 50 &&
                  bloom::shape_square_percent(word(0.5)) == 100 && bloom::shape_width_percent(word(0.5)) == 50,
              "shape caption");
        check(bloom::vowel_shown(word(0.1)) == 0 && bloom::vowel_shown(word(0.55)) == 2 && bloom::vowel_shown(full) == 4,
              "vowel caption");
    }

    if (!failures) std::puts("bloom: all sound checks passed");
    return failures ? 1 : 0;
}
