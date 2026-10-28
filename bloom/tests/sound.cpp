// Bloom's sound checks, compiled for the host and run by
// python sdk.py test --machine <folder>. Each check prints FAIL with a reason.
#include "../engine.cpp"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
using bloom::i32;
using bloom::u32;

const double rate = 44100.0, pi = 3.14159265358979323846;
const i32 full = 0x7fffffff;
const i32 half_scale = 335544320;    // definition.json outputPeakQ31
int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}

u32 increment(double hz) { return static_cast<u32>(hz / rate * 4294967296.0 + 0.5); }
i32 word(double normalized) { return static_cast<i32>(normalized * 2147483647.0 + 0.5); }

// Parameters: tilt, odd, stretch, breath, partials, blur, haze, time.
struct Setting {
    double v[8];
};
Setting defaults() {
    Setting s = {{0.5, 0, 0, 0, 0.6522, 0, 0, 0.68}};
    return s;
}

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

// Amplitude of the component at hz in the first n samples, as a fraction of
// full scale (Goertzel through a Hann window, so neighbours barely leak in).
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

// 441 Hz repeats every 100 samples, so a 4400-sample window holds whole
// cycles of every harmonic and they do not leak into each other.
const double f0 = 441.0;
const unsigned window = 4400, window_blocks = 69;

void harmonics(Note& note, double (&out)[25]) {
    static i32 x[window_blocks * opxy::block_frames];
    note.run(window_blocks, x);
    for (int k = 1; k <= 24; ++k) out[k] = amplitude(x, window, f0 * k);
}

double centroid(double tilt) {
    Setting s = defaults();
    s.v[0] = tilt;
    s.v[4] = 1;
    Note note(s, f0);
    note.run(8);
    double h[25], sum = 0, weighted = 0;
    harmonics(note, h);
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

// Frequency of a pure tone by its rising zero crossings over two seconds.
double measured(double hz) {
    Setting s = defaults();
    s.v[4] = 0;   // one partial: a pure sine
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
}  // namespace

int main() {
    // 1. Within half scale, and sounding, at every corner of the controls,
    //    from the lowest to the highest notes, with restarts.
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
        // Random settings changing every block, over long notes.
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
        check(worst <= half_scale, "output above its declared peak");
        check(worst > half_scale / 2, "output much quieter than its declared peak");
        check(sounding, "silent at some setting");
        std::printf("peak %.3f of full scale\n", worst / 2147483648.0);
    }

    // 2. Tilt moves the spectral centroid up, from dark to bright.
    {
        const double dark = centroid(0), middle = centroid(0.5), bright = centroid(1);
        check(dark < 1.3 && middle > dark + 0.5 && bright > middle + 3, "tilt does not move the spectral centroid");
        std::printf("centroid (harmonic number): tilt 0 %.2f, 0.5 %.2f, 1 %.2f\n", dark, middle, bright);
    }

    // 3. Odd at full removes the even partials; at zero they sound.
    {
        Setting s = defaults();
        s.v[1] = 1;
        Note hollow(s, f0);
        hollow.run(8);
        double h[25];
        harmonics(hollow, h);
        double even = 0;
        for (int k = 2; k <= 16; k += 2) even = h[k] > even ? h[k] : even;
        check(h[1] > 0.03 && h[3] > h[1] * 0.05 && even < h[1] * 1e-3, "odd does not remove the even partials");
        s.v[1] = 0;
        Note full_note(s, f0);
        full_note.run(8);
        harmonics(full_note, h);
        check(h[2] > h[1] * 0.2, "even partials missing with odd at zero");
    }

    // 4. Stretch moves partials off the harmonic series, to k^1.5 at full.
    {
        Setting s = defaults();
        s.v[2] = 1;
        Note bell(s, f0);
        bell.run(8);
        static i32 x[window_blocks * opxy::block_frames];
        bell.run(window_blocks, x);
        const double fundamental = amplitude(x, window, f0);
        const double stretched2 = amplitude(x, window, f0 * std::pow(2.0, 1.5));
        const double stretched3 = amplitude(x, window, f0 * std::pow(3.0, 1.5));
        double harmonic = 0;
        for (int k = 2; k <= 7; ++k) {   // 4^1.5 = 8 is harmonic again
            const double a = amplitude(x, window, f0 * k);
            harmonic = a > harmonic ? a : harmonic;
        }
        std::printf("stretch: fundamental %.4f, at 2^1.5 %.4f, at 3^1.5 %.4f, harmonics at most %.5f\n", fundamental, stretched2, stretched3, harmonic);
        check(stretched2 > fundamental * 0.2 && stretched3 > fundamental * 0.1 && harmonic < fundamental * 0.03,
              "stretch does not make the partials inharmonic");
        bloom::Spectrum sp;
        i32 p[8] = {word(0.5), 0, full, 0, full, 0, 0, word(0.68)};
        bloom::shape(bloom::decode(p), 1, 0, 0, bloom::log2_of(increment(f0)), sp);
        check(std::fabs(sp.pitch[1] / 65536.0 - 1.5) < 0.001 && std::fabs(sp.pitch[23] / 65536.0 - 1.5 * std::log2(24.0)) < 0.001,
              "partial k is not at k^1.5 with stretch at full");
        p[2] = 0;
        bloom::shape(bloom::decode(p), 1, 0, 0, bloom::log2_of(increment(f0)), sp);
        check(sp.pitch[0] == 0 && std::fabs(sp.pitch[2] / 65536.0 - std::log2(3.0)) < 0.0001,
              "partials not harmonic with stretch at zero");
    }

    // 5. Time at its top holds the spectrum: with breath, haze and blur all up,
    //    nothing changes over twenty seconds; running, the same setting moves.
    {
        Setting s = defaults();
        s.v[3] = 1;
        s.v[5] = 0.5;
        s.v[7] = 1;
        Note held(s, f0);
        held.run(8);
        const double frozen = spectral_change(held, 1378 * 10);
        check(frozen < 1e-3, "time at hold does not freeze the spectrum");
        s.v[7] = 0.5;
        Note moving(s, f0);
        moving.run(8);
        const double moved = spectral_change(moving, 1378 * 10);
        check(moved > 0.05, "breath does not move the spectrum");
        std::printf("spectral change over 20 s: held %.6f, running %.3f\n", frozen, moved);
        // With haze too, held: every partial's level and the note time stay put.
        s.v[6] = 1;
        s.v[7] = 1;
        Note hazy(s, f0);
        hazy.run(8);
        bloom::Voice before = hazy.voice;
        hazy.run(2000);
        bool same = hazy.voice.time == before.time;
        for (u32 k = 0; k < bloom::partial_count; ++k)
            same = same && hazy.voice.partials[k].amplitude == before.partials[k].amplitude;
        check(same, "time at hold does not hold the levels with haze");
    }

    // 6. Breath at zero: a static spectrum, even with time at its fastest and
    //    blur up; with haze, the levels still never move.
    {
        Setting s = defaults();
        s.v[5] = 1;
        s.v[7] = 0;
        Note still(s, f0);
        still.run(8);
        const double change = spectral_change(still, 1378 * 10);
        std::printf("breath zero change %.6f\n", change);
        check(change < 1e-3, "breath at zero still moves the spectrum");
        s.v[6] = 1;
        Note hazy(s, f0);
        hazy.run(8);
        bloom::Voice before = hazy.voice;
        hazy.run(2000);
        bool same = true;
        for (u32 k = 0; k < bloom::partial_count; ++k)
            same = same && hazy.voice.partials[k].amplitude == before.partials[k].amplitude;
        check(same, "breath at zero still moves the levels when haze is up");
    }

    // 7. Nothing at or above Nyquist: on high notes the partials that would
    //    fold back are dropped, so no aliases sound below.
    {
        Setting s = defaults();
        s.v[0] = 1;   // tilt bright: the upper partials as loud as they get
        s.v[4] = 1;   // all 24
        const double hz = 7 * f0;   // whole cycles in the window, aliases on no harmonic
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
        double kept = amplitude(x, window, 5 * hz);
        check(fundamental > 0.003 && kept > fundamental * 0.5 && worst < fundamental * 1e-3, "aliasing partials above Nyquist");
        std::printf("3 kHz note, all partials, bright: worst alias %.1f dB below the fundamental\n",
                    20 * std::log10(fundamental / (worst + 1e-12)));
        // The spectrum itself never gives a level to a partial at or above the cutoff,
        // whatever the stretch and haze, on any note.
        bool clean = true;
        for (int st = 0; st <= 4; ++st)
            for (double note_hz = 1000; note_hz < 22000; note_hz *= 1.19) {
                i32 p[8] = {full, 0, word(st / 4.0), full, full, 0, full, word(0.3)};
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

    // 8. Pitch tracking: a single partial sounds at the played frequency.
    {
        const double notes[] = {32.7, 110, 441, 1000, 3520, 8000};
        double worst = 0;
        for (unsigned n = 0; n < sizeof notes / sizeof notes[0]; ++n) {
            const double error = std::fabs(measured(notes[n]) / notes[n] - 1);
            worst = error > worst ? error : worst;
        }
        check(worst < 2e-4, "pitch does not track the note");
        // And the full spectrum is strongest at its fundamental.
        Setting s = defaults();
        Note note(s, f0);
        note.run(8);
        double h[25];
        harmonics(note, h);
        bool fundamental_first = true;
        for (int k = 2; k <= 24; ++k) fundamental_first = fundamental_first && h[k] < h[1];
        check(fundamental_first, "default spectrum not strongest at its fundamental");
        std::printf("pitch error at most %.5f%%\n", worst * 100);
    }

    // 9. Deterministic: the same notes give the same sound; another note's seed
    //    breathes differently.
    {
        Setting s = defaults();
        s.v[3] = 0.8;
        s.v[6] = 0.8;
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

    // 10. The weather moves once per block, however many voices render.
    {
        Setting s = defaults();
        s.v[3] = 1;
        s.v[7] = 0;
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
        check(chord.shared.cycle == cycle + 100 && chord.shared.clock == clock + 100 * (bloom::time_step(0) >> 1),
              "the shared weather does not advance once per block");
    }

    // 11. The levels never sum above peak_level, at any setting and time.
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

    // 12. Captions follow the sound's laws.
    {
        check(bloom::tilt_db(0) == -24 && bloom::tilt_db(word(0.5)) == -9 && bloom::tilt_db(full) == 6, "tilt caption");
        check(bloom::stretch_x100(0) == 200 && bloom::stretch_x100(full) == 283, "stretch caption");
        check(bloom::partials_shown(0) == 1 && bloom::partials_shown(word(0.6522)) == 16 && bloom::partials_shown(full) == 24,
              "partials caption");
        check(bloom::haze_tenths(full) == 300 && bloom::time_hundredths(0) == 50 && bloom::time_hundredths(full) == 0,
              "haze or time caption");
        const u32 slowest = bloom::time_hundredths(word(0.984));
        check(slowest > 2800 && slowest < 3000, "time caption below hold");
    }

    if (!failures) std::puts("bloom: all sound checks passed");
    return failures ? 1 : 0;
}
