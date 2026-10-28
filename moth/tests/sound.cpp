// Moth's sound checks, compiled for the host and run by
//   python sdk.py test --machine <folder>
// Each check names what the sound promises: bounded at every extreme and
// within the declared peak, a folder that leaves a sine clean at 0, pitch
// that tracks, aliasing kept down, a vactrol that pings, holds and glows,
// and every control moving the sound clearly at 0, 25, 50, 75 and 100.
// With MOTH_REPORT set in the environment it also prints what it measures.
#include "../engine.cpp"
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <vector>

namespace {
using moth::Voice;
using moth::Shared;
using opxy::i32;
using opxy::u32;

int failures = 0;
bool reporting = false;
void check(const char* what, bool ok) {
    if (!ok) { ++failures; std::printf("FAIL %s\n", what); }
}

const double kRate = 44100;
const unsigned kSecond = 689;      // blocks
const double kPeak = 579820584.0;  // definition.json outputPeakQ31
const double kSteps[5] = {0, 0.25, 0.5, 0.75, 1};
enum { kFold, kFm, kRatio, kStrike, kGlow, kBias, kMode, kDrift };

i32 word(double normalized) { return static_cast<i32>(std::lround(normalized * 2147483647.0)); }
u32 increment(double hz) { return static_cast<u32>(hz / kRate * 4294967296.0); }
// A ratio stop's control value: the middle of its sixteenth, as the device stores it.
double stop(unsigned k) { return (k + 0.5) / 16; }

// The defaults (definition.json: the Clean preset).
struct Controls {
    double c[8] = {0.2, 0, stop(1), 0, 0.7, 0, 0.5, 0};
    Controls with(unsigned i, double v) const { Controls o = *this; o.c[i] = v; return o; }
};

// One voice playing one held note; the output in full-scale units.
typedef std::vector<double> Wave;
Wave play(const Controls& k, double hz, double seconds, u32 owner = 1) {
    static Voice voice;
    static Shared shared;
    unsigned char* bytes = reinterpret_cast<unsigned char*>(&voice);
    for (unsigned i = 0; i < sizeof voice; ++i) bytes[i] = 0;
    moth::initialize(voice, shared, owner);
    moth::start(voice, shared, owner);
    i32 p[8];
    for (unsigned i = 0; i < 8; ++i) p[i] = word(k.c[i]);
    u32 pitch[opxy::block_frames];
    for (unsigned i = 0; i < opxy::block_frames; ++i) pitch[i] = increment(hz);
    const unsigned blocks = static_cast<unsigned>(seconds * kSecond);
    Wave out(blocks * opxy::block_frames);
    i32 o[opxy::block_frames];
    for (unsigned b = 0; b < blocks; ++b) {
        moth::render(voice, shared, owner, o, p, pitch);
        for (unsigned i = 0; i < opxy::block_frames; ++i) out[b * opxy::block_frames + i] = o[i] / 2147483648.0;
    }
    return out;
}

double rms(const Wave& x, double from, double seconds) {
    const unsigned a = static_cast<unsigned>(from * kRate), n = static_cast<unsigned>(seconds * kRate);
    double s = 0;
    for (unsigned i = a; i < a + n; ++i) s += x[i] * x[i];
    return std::sqrt(s / n);
}
double peak(const Wave& x, double from = 0, double seconds = 1e9) {
    const unsigned a = static_cast<unsigned>(from * kRate);
    const unsigned b = seconds > 1e8 ? static_cast<unsigned>(x.size()) : a + static_cast<unsigned>(seconds * kRate);
    double p = 0;
    for (unsigned i = a; i < b && i < x.size(); ++i) p = std::fmax(p, std::fabs(x[i]));
    return p;
}
double db(double ratio) { return 20 * std::log10(ratio + 1e-30); }

// The power spectrum: Blackman-Harris frames (16384 unless asked), half
// overlapped, averaged. Bins are kRate / kFrame apart whatever the frame:
// a shorter frame is zero-padded.
const unsigned kFrame = 16384;
std::vector<double> spectrum(const Wave& x, double from, double seconds, unsigned frame = kFrame) {
    std::vector<double> sum(kFrame / 2, 0.0);
    std::vector<std::complex<double> > a(kFrame);
    const unsigned first = static_cast<unsigned>(from * kRate), last = first + static_cast<unsigned>(seconds * kRate);
    for (unsigned start = first; start + frame <= last; start += frame / 2) {
        for (unsigned i = 0; i < kFrame; ++i) {
            const double t = 6.283185307179586 * i / frame;
            a[i] = i < frame ? x[start + i] * (0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t) - 0.01168 * std::cos(3 * t)) : 0;
        }
        for (unsigned i = 1, j = 0; i < kFrame; ++i) {  // an in-place radix-2 FFT
            unsigned bit = kFrame >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap(a[i], a[j]);
        }
        for (unsigned length = 2; length <= kFrame; length <<= 1) {
            const std::complex<double> turn(std::cos(6.283185307179586 / length), -std::sin(6.283185307179586 / length));
            for (unsigned i = 0; i < kFrame; i += length) {
                std::complex<double> w(1);
                for (unsigned j = 0; j < length / 2; ++j, w *= turn) {
                    const std::complex<double> u = a[i + j], v = a[i + j + length / 2] * w;
                    a[i + j] = u + v;
                    a[i + j + length / 2] = u - v;
                }
            }
        }
        for (unsigned i = 0; i < kFrame / 2; ++i) sum[i] += std::norm(a[i]);
    }
    return sum;
}
double bin_hz(unsigned i) { return i * kRate / kFrame; }
// Power within 6 bins of hz.
double at(const std::vector<double>& s, double hz) {
    double sum = 0;
    const int centre = static_cast<int>(std::lround(hz / kRate * kFrame));
    for (int i = centre - 6; i <= centre + 6; ++i)
        if (i > 1 && i < static_cast<int>(s.size())) sum += s[i];
    return sum;
}
double total(const std::vector<double>& s) {
    double sum = 0;
    for (unsigned i = 2; i < s.size(); ++i) sum += s[i];
    return sum;
}
// The share of power (below `below` hertz) more than 6 bins from every multiple of grid hertz.
double off_grid(const std::vector<double>& s, double grid, double below = 1e9) {
    double off = 0;
    for (unsigned i = 2; i < s.size(); ++i) {
        const double k = std::floor(bin_hz(i) / grid + 0.5);
        if (bin_hz(i) < below && std::fabs(bin_hz(i) - k * grid) > 6 * kRate / kFrame) off += s[i];
    }
    return off / total(s);
}
// The power-weighted mean harmonic number, for a note at hz.
double brightness(const std::vector<double>& s, double hz) {
    double m = 0, a = 0;
    for (unsigned i = 2; i < s.size(); ++i) { m += bin_hz(i) / hz * s[i]; a += s[i]; }
    return m / a;
}
// The highest harmonic of hz within 40 dB of the strongest.
double reach(const std::vector<double>& s, double hz) {
    double strongest = 0, highest = 0;
    for (unsigned h = 1; h * hz < 20000; ++h) strongest = std::fmax(strongest, at(s, h * hz));
    for (unsigned h = 1; h * hz < 20000; ++h)
        if (at(s, h * hz) > strongest * 1e-4) highest = h;
    return highest;
}
// The lag within 10% of the expected period where the signal best matches
// itself: one period, interpolated.
double period(const Wave& x, double from, double expected) {
    const unsigned a = static_cast<unsigned>(from * kRate);
    const unsigned low = static_cast<unsigned>(expected * 0.9), high = static_cast<unsigned>(expected * 1.1) + 1;
    std::vector<double> score(high + 2, 0.0);
    unsigned best = low;
    for (unsigned lag = low - 1; lag <= high + 1; ++lag) {
        for (unsigned i = a; i < a + 8192; ++i) score[lag] += x[i] * x[i + lag];
        if (lag >= low && lag <= high && score[lag] > score[best]) best = lag;
    }
    const double p = score[best - 1], q = score[best], r = score[best + 1], d = p - 2 * q + r;
    return best + (d ? 0.5 * (p - r) / d : 0);
}
// A sine's frequency over seconds from `from`, by its rising zero crossings.
double frequency(const Wave& x, double from, double seconds) {
    const unsigned a = static_cast<unsigned>(from * kRate), b = a + static_cast<unsigned>(seconds * kRate);
    double first = -1, last = -1;
    unsigned cycles = 0;
    for (unsigned i = a; i + 1 < b; ++i)
        if (x[i] < 0 && x[i + 1] >= 0) {
            const double t = i + x[i] / (x[i] - x[i + 1]);
            if (first < 0) first = t;
            else ++cycles;
            last = t;
        }
    return cycles * kRate / (last - first);
}
// Seconds from the 10 ms level's peak until it first falls 60 dB under it.
double fall_time(const Wave& x) {
    const unsigned w = 441;
    double top = 0;
    std::vector<double> level;
    for (unsigned s = 0; s + w <= x.size(); s += w) {
        double p = 0;
        for (unsigned i = s; i < s + w; ++i) p += x[i] * x[i];
        level.push_back(std::sqrt(p / w));
        top = std::fmax(top, level.back());
    }
    unsigned crest = 0;
    while (crest < level.size() && level[crest] < top) ++crest;
    for (unsigned j = crest; j < level.size(); ++j)
        if (level[j] < top * 0.001) return (j - crest) * w / kRate;
    return level.size() * w / kRate;
}
// Seconds until the 5 ms level first reaches 90% of its level at 1.5 s.
double rise_time(const Wave& x) {
    const unsigned w = 220;
    const double goal = 0.9 * rms(x, 1.4, 0.2);
    for (unsigned s = 0; s + w <= x.size(); s += w / 4) {
        double p = 0;
        for (unsigned i = s; i < s + w; ++i) p += x[i] * x[i];
        if (std::sqrt(p / w) >= goal) return (s + w / 2) / kRate;
    }
    return 99;
}

// Each value over the one before by the factor `by` (or, for by below 1, under it).
bool rising(const double* v, unsigned n, double by) {
    for (unsigned i = 1; i < n; ++i)
        if (!(v[i] > v[i - 1] * by)) return false;
    return true;
}
void row(const char* what, const double* v, const char* unit) {
    if (reporting) std::printf("%-44s %9.3g %9.3g %9.3g %9.3g %9.3g %s\n", what, v[0], v[1], v[2], v[3], v[4], unit);
}

void extremes() {
    // Every corner of the eight controls, at a low, a middle and a high note,
    // the note struck again half way.
    bool bounded = true, sounding = true;
    for (double hz : {27.5, 220.0, 3520.0}) {
        for (u32 corner = 0; corner < 256; ++corner) {
            static Voice voice;
            static Shared shared;
            moth::initialize(voice, shared, 1);
            moth::start(voice, shared, 1);
            i32 p[8];
            for (unsigned c = 0; c < 8; ++c) p[c] = corner & (1u << c) ? 0x7fffffff : 0;
            u32 pitch[opxy::block_frames];
            for (unsigned i = 0; i < opxy::block_frames; ++i) pitch[i] = increment(hz);
            i32 out[opxy::block_frames];
            double energy = 0;
            for (unsigned b = 0; b < kSecond / 2; ++b) {
                if (b == kSecond / 4) moth::start(voice, shared, 1);
                moth::render(voice, shared, 1, out, p, pitch);
                for (unsigned i = 0; i < opxy::block_frames; ++i) {
                    bounded = bounded && std::fabs(static_cast<double>(out[i])) <= kPeak;
                    energy += static_cast<double>(out[i]) * out[i];
                }
            }
            sounding = sounding && energy > 0;
        }
    }
    check("within the declared peak at every corner of the controls", bounded);
    check("sounds at every corner of the controls", sounding);
    // The keyboard's ends and a six-octave bend, every control at full.
    Controls full;
    for (unsigned c = 0; c < 8; ++c) full.c[c] = 1;
    for (double hz : {4.0, 8.2, 12543.0, 20000.0})
        check("within the declared peak at the keyboard's ends", peak(play(full, hz, 0.5)) * 2147483648.0 <= kPeak);
    static Voice bend;
    static Shared shared;
    moth::initialize(bend, shared, 3);
    moth::start(bend, shared, 3);
    i32 p[8];
    for (unsigned c = 0; c < 8; ++c) p[c] = 0x7fffffff;
    bool bent = true;
    for (unsigned b = 0; b < 2 * kSecond; ++b) {
        u32 pitch[opxy::block_frames];
        for (unsigned i = 0; i < opxy::block_frames; ++i)
            pitch[i] = increment(100.0 * std::pow(2.0, 6.0 * (b * 64.0 + i) / (2 * kSecond * 64.0)));
        i32 out[opxy::block_frames];
        moth::render(bend, shared, 3, out, p, pitch);
        for (unsigned i = 0; i < opxy::block_frames; ++i) bent = bent && std::fabs(static_cast<double>(out[i])) <= kPeak;
    }
    check("within the declared peak through a six-octave bend", bent);
    // A random search of settings and notes stays under 0.25 of full scale,
    // so the clamp at the declared peak (0.27) never has to act.
    u32 seed = 7;
    double worst = 0;
    for (unsigned n = 0; n < 1500; ++n) {
        Controls k;
        for (unsigned c = 0; c < 8; ++c) {
            seed = seed * 1664525u + 1013904223u;
            const double r = (seed >> 8) / 16777216.0;
            k.c[c] = r < 0.15 ? 0 : r > 0.85 ? 1 : (r - 0.15) / 0.7;
        }
        seed = seed * 1664525u + 1013904223u;
        worst = std::fmax(worst, peak(play(k, 27.5 * std::pow(2.0, (seed >> 8) / 16777216.0 * 8.5), 0.25)));
    }
    if (reporting) std::printf("worst single-voice peak over 1500 random settings: %.3f of full scale\n", worst);
    check("a voice peaks under 0.25 of full scale, short of the clamp", worst < 0.25);
}

void clean() {
    // At fold 0 and no FM the folder passes the sine: every harmonic 60 dB down.
    const Wave x = play(Controls().with(kFold, 0), 220, 2);
    const std::vector<double> s = spectrum(x, 1, 1);
    double harmonics = 0;
    for (unsigned h = 2; h <= 40; ++h) harmonics += at(s, 220.0 * h);
    if (reporting) std::printf("fold 0: harmonics %.1f dB under the fundamental\n", -10 * std::log10(harmonics / at(s, 220)));
    check("fold 0 passes a clean sine: harmonics 60 dB down", harmonics < 1e-6 * at(s, 220));
    // The offset is removed: the mean of a held note is near 0 at full bias.
    const Wave y = play(Controls().with(kBias, 1).with(kFold, 0.6), 110, 2);
    double mean = 0;
    for (unsigned i = 44100; i < 88200; ++i) mean += y[i];
    mean /= 44100;
    check("bias's offset is removed: the mean under 1% of the level", std::fabs(mean) < 0.01 * rms(y, 1, 1));
    // The folder's offset is predicted: a biased note is struck without a
    // thump. A thump is what passes two one-poles at 8 Hz (a struck sine
    // passes a little of its own start); full bias passes no more than none.
    for (double fold : {0.1, 0.3, 0.6}) {
        double thump[2], top[2];
        for (unsigned b = 0; b < 2; ++b) {
            const Wave z = play(Controls().with(kBias, b).with(kFold, fold).with(kStrike, 1).with(kGlow, 0.4), 110, 0.5);
            double low = 0, lower = 0;
            thump[b] = 0;
            for (unsigned i = 0; i < z.size(); ++i) {
                low += (z[i] - low) * 0.00114;
                lower += (low - lower) * 0.00114;
                thump[b] = std::fmax(thump[b], std::fabs(lower));
            }
            top[b] = peak(z);
        }
        if (reporting) std::printf("fold %.1f: a struck note's thump %.1f dB under its peak at bias 0, %.1f at bias 100\n", fold, -db(thump[0] / top[0]), -db(thump[1] / top[1]));
        check("a biased pluck starts without a thump: no more than an unbiased one, within 3 dB", thump[1] / top[1] < 1.41 * thump[0] / top[0]);
    }
}

void pitch() {
    for (double fm : {0.0, 0.5})
        for (double hz : {55.0, 110.0, 220.0, 440.0, 880.0}) {
            const Wave x = play(Controls().with(kFold, 0.55).with(kFm, fm).with(kRatio, stop(5)), hz, 1);
            check("in tune within 0.1%, folded and with FM at 2.00", std::fabs(period(x, 0.5, kRate / hz) / (kRate / hz) - 1) < 0.001);
        }
}

// Aliasing: power off the harmonics, below 15 kHz (where it can be heard),
// and in all, at C3 to C8.
void aliasing() {
    struct Case { const char* name; Controls k; double grid; };
    const Controls base;
    const Case cases[] = {
        {"fold 100", base.with(kFold, 1), 1},
        {"fold 100, bias 100", base.with(kFold, 1).with(kBias, 1), 1},
        {"fm 100, ratio 1.00", base.with(kFm, 1), 1},
        {"fm 100, ratio 7.00", base.with(kFm, 1).with(kRatio, stop(15)), 1},
        {"fold 100, fm 100, ratio 7.00", base.with(kFold, 1).with(kFm, 1).with(kRatio, stop(15)), 1},
        {"Ping held (fold 55, fm 30, ratio 3.50)", base.with(kFold, 0.55).with(kFm, 0.3).with(kRatio, stop(10)).with(kBias, 0.2), 0.5},
    };
    for (const Case& c : cases) {
        double v[5], all[5], heard = -1000;
        const double notes[5] = {130.81, 523.25, 1046.5, 2093.0, 4186.0};
        for (unsigned n = 0; n < 5; ++n) {
            const std::vector<double> s = spectrum(play(c.k, notes[n], 2.6), 1, 1.5);
            v[n] = 10 * std::log10(off_grid(s, notes[n] * c.grid, 15000) + 1e-30);
            all[n] = 10 * std::log10(off_grid(s, notes[n] * c.grid) + 1e-30);
            heard = std::fmax(heard, v[n]);
        }
        if (reporting) {
            std::printf("aliasing, %s (C3 C5 C6 C7 C8):\n", c.name);
            row("  below 15 kHz", v, "dB");
            row("  in all", all, "dB");
        }
        check("aliasing below 15 kHz stays 50 dB under the note, C3 to C8", heard < -50);
    }
}

// Each control at 0, 25, 50, 75 and 100, from the defaults (or, where it acts
// through another, with that one up: ratio with fm, mode and glow with strike).
void sweeps() {
    double v[5];
    {  // fold: the folded wave reaches higher harmonics at every step, and brightens.
        double mean[5];
        for (unsigned i = 0; i < 5; ++i) {
            const std::vector<double> s = spectrum(play(Controls().with(kFold, kSteps[i]), 220, 2), 1, 1);
            v[i] = reach(s, 220);
            mean[i] = brightness(s, 220);
        }
        row("fold: highest harmonic within 40 dB", v, "");
        row("fold: mean harmonic", mean, "");
        check("fold reaches higher harmonics at every step", rising(v, 5, 1.2));
        check("fold brightens overall, to a mean harmonic over 4 at 100", mean[2] > mean[0] * 2 && mean[4] > 4);
    }
    {  // fm (ratio 2.00): the FM reaches higher sidebands at every step.
        double mean[5];
        for (unsigned i = 0; i < 5; ++i) {
            const std::vector<double> s = spectrum(play(Controls().with(kFm, kSteps[i]).with(kFold, 0).with(kRatio, stop(5)), 220, 2), 1, 1);
            v[i] = reach(s, 220);
            mean[i] = brightness(s, 220);
        }
        row("fm (ratio 2.00): highest harmonic within 40 dB", v, "");
        row("fm (ratio 2.00): mean harmonic", mean, "");
        check("fm 0 is a pure sine", v[0] == 1);
        check("fm reaches higher sidebands at every step, and brightens", rising(v, 5, 1.2) && rising(mean + 1, 4, 1.3));
    }
    {  // ratio (with fm 50): each stop puts the first sidebands where the ratio says.
        const unsigned stops[5] = {0, 4, 8, 12, 15};
        const double ratios[5] = {0.5, 1.5, 2.76, 4.7, 7.0};
        bool placed = true;
        for (unsigned i = 0; i < 5; ++i) {
            const std::vector<double> s =
                spectrum(play(Controls().with(kFm, 0.5).with(kFold, 0).with(kRatio, stop(stops[i])), 220, 2), 1, 1);
            v[i] = (at(s, 220 * (1 + ratios[i])) + at(s, 220 * std::fabs(1 - ratios[i]))) / total(s);
            placed = placed && v[i] > 0.05;
        }
        row("ratio 0.50 1.50 2.76 4.70 7.00 (fm 50): 1 +- r share", v, "");
        check("ratio puts the first sidebands at 1 +- r of the note, at every stop", placed);
        const std::vector<double> bell = spectrum(play(Controls().with(kFm, 0.5).with(kFold, 0).with(kRatio, stop(8)), 220, 2), 1, 1);
        const std::vector<double> harmonic = spectrum(play(Controls().with(kFm, 0.5).with(kFold, 0).with(kRatio, stop(5)), 220, 2), 1, 1);
        if (reporting) std::printf("ratio 2.76: %.2f of the power off the harmonics; ratio 2.00: %.6f\n", off_grid(bell, 220), off_grid(harmonic, 220));
        check("ratio 2.76 is inharmonic: much of its power off the harmonics", off_grid(bell, 220) > 0.2);
        check("ratio 2.00 is harmonic", off_grid(harmonic, 220) < 1e-4);
    }
    {  // strike (combo): how far the ping stands over the held level.
        for (unsigned i = 0; i < 5; ++i) {
            const Wave x = play(Controls().with(kStrike, kSteps[i]).with(kGlow, 0.3), 220, 2);
            v[i] = db(peak(x, 0, 0.05) / (peak(x, 1.5, 0.3) + 1e-9));
        }
        row("strike: ping over the held level", v, "dB");
        check("strike 0 holds the gate open: no ping", std::fabs(v[0]) < 0.5);
        check("strike pings further over a lower held level at every step", v[1] > 1.5 && rising(v + 1, 4, 1.5));
        check("strike 100 is a pluck: 60 dB down by 1.5 s", v[4] > 60);
    }
    {  // glow (strike 100, vca): the pluck's fall to -60 dB follows the caption.
        bool honest = true;
        for (unsigned i = 0; i < 5; ++i) {
            const double caption = 0.04 * std::pow(150.0, kSteps[i]);
            v[i] = fall_time(play(Controls().with(kStrike, 1).with(kMode, 1).with(kGlow, kSteps[i]), 220, caption * 1.6 + 0.5));
            honest = honest && v[i] > caption * 0.75 && v[i] < caption * 1.33;
        }
        row("glow: a pluck's fall to -60 dB (vca)", v, "s");
        check("glow's pluck falls 60 dB in its caption's time, within a third", honest);
        // Unstruck, the cell opens at a twentieth of glow's pace: the swell.
        for (unsigned i = 0; i < 5; ++i) v[i] = rise_time(play(Controls().with(kGlow, kSteps[i]), 220, 2));
        row("glow: an unstruck note's rise to 90%", v, "s");
        check("glow slows an unstruck note's swell at every step", v[0] < 0.02 && rising(v + 1, 4, 1.5) && v[4] > 0.3);
    }
    {  // bias: the even harmonics (2 and 4) against the odd (1 and 3), at the default fold.
        for (unsigned i = 0; i < 5; ++i) {
            const std::vector<double> s = spectrum(play(Controls().with(kBias, kSteps[i]), 220, 2), 1, 1);
            v[i] = (at(s, 440) + at(s, 880)) / (at(s, 220) + at(s, 660));
        }
        double shown[5];
        for (unsigned i = 0; i < 5; ++i) shown[i] = 10 * std::log10(v[i] + 1e-30);
        row("bias: even harmonics against odd", shown, "dB");
        check("bias 0 is symmetric: no even harmonics", v[0] < 1e-7);
        check("bias adds even harmonics at every step", v[1] > 1e-3 && rising(v + 1, 4, 1.5));
    }
    {  // mode (strike 100, fold 30): a pluck's level and brightness 200 ms in.
        double level[5], light[5];
        auto bright = [&](const std::vector<double>& s) {
            double high = 0;
            for (unsigned i = 2; i < s.size(); ++i)
                if (bin_hz(i) > 500) high += s[i];
            return high / total(s);
        };
        const double open = bright(spectrum(play(Controls().with(kFold, 0.3), 220, 1), 0.5, 0.1, 4096));
        for (unsigned i = 0; i < 5; ++i) {
            const Wave x = play(Controls().with(kMode, kSteps[i]).with(kStrike, 1).with(kGlow, 0.5).with(kFold, 0.3), 220, 0.6);
            level[i] = db(rms(x, 0.2, 0.1) / rms(x, 0.005, 0.03));
            light[i] = bright(spectrum(x, 0.15, 0.1, 4096)) / open;
        }
        row("mode: level 200 ms into a pluck", level, "dB");
        row("mode: power over 500 Hz at 150-240 ms, of open", light, "");
        check("mode lp closes the filter: its overtones a quarter of the open tone's", light[0] < 0.25);
        check("mode vca keeps the tone's overtones", light[4] > 0.9);
        check("mode fades more from lp to combo at every step", level[1] < level[0] - 1 && level[2] < level[1] - 1);
        check("mode keeps more overtones from combo to vca at every step", light[3] > light[2] * 1.2 && light[4] > light[3] * 1.05);
    }
    {  // drift: the pitch's wander over 12 s, in cents (rms, from the note).
        for (unsigned i = 0; i < 5; ++i) {
            const Wave x = play(Controls().with(kDrift, kSteps[i]).with(kFold, 0), 440, 12.5);
            double squares = 0;
            unsigned n = 0;
            for (double t = 0.5; t < 12; t += 0.25, ++n) {
                const double cents = 1200 * std::log2(frequency(x, t, 0.25) / 440);
                squares += cents * cents;
            }
            v[i] = std::sqrt(squares / n);
        }
        row("drift: the pitch's wander (rms)", v, "cents");
        check("drift 0 holds the pitch: under 0.1 cent", v[0] < 0.1);
        check("drift wanders further at every step", v[1] > 1 && rising(v + 1, 4, 1.4));
    }
}

void vactrol() {
    // The photocell remembers: a note struck again while it still glows opens from there.
    static Voice voice;
    static Shared shared;
    moth::initialize(voice, shared, 1);
    moth::start(voice, shared, 1);
    i32 p[8];
    const Controls k = Controls().with(kStrike, 1).with(kGlow, 0.6);
    for (unsigned c = 0; c < 8; ++c) p[c] = word(k.c[c]);
    u32 pitch[opxy::block_frames];
    for (unsigned i = 0; i < opxy::block_frames; ++i) pitch[i] = increment(220);
    i32 out[opxy::block_frames];
    for (unsigned b = 0; b < kSecond / 10; ++b) moth::render(voice, shared, 1, out, p, pitch);
    const i32 glowing = voice.cell;
    moth::start(voice, shared, 1);
    check("a note struck again keeps the cell's glow", voice.cell == glowing && glowing > 0);
    // The strike's pulse opens the cell nearly fully within 3 ms.
    moth::initialize(voice, shared, 2);
    moth::start(voice, shared, 2);
    for (unsigned b = 0; b < 2; ++b) moth::render(voice, shared, 2, out, p, pitch);
    if (reporting) std::printf("the cell 2.9 ms after a strike at 100: %.3f\n", voice.cell / 1073741824.0);
    check("strike 100 opens the cell past 0.9 within 3 ms", voice.cell > 0.9 * 1073741824.0);
    // Held at strike 0, the gate holds a steady level for seconds.
    const Wave x = play(Controls(), 220, 6);
    check("strike 0 sustains: seconds 4-5 within 0.5 dB of 1-2", std::fabs(db(rms(x, 4, 1) / rms(x, 1, 1))) < 0.5);
}

void captions() {
    char text[8];
    auto is = [&](const char* want) {
        for (unsigned i = 0; i < 8; ++i) {
            if (text[i] != want[i]) return false;
            if (!want[i]) return true;
        }
        return false;
    };
    moth::index_caption(text, sizeof text, 9830);  // fm 30
    check("fm caption at 30: 0.36", is("0.36"));
    moth::index_caption(text, sizeof text, 32767);
    check("fm caption at 100: 4.00", is("4.00"));
    moth::ratio_caption(text, sizeof text, 21954);  // 67, as the board's Ping
    check("ratio caption at 67: 3.50", is("3.50"));
    moth::ratio_caption(text, sizeof text, 0);
    check("ratio caption at 0: 0.50", is("0.50"));
    moth::glow_caption(text, sizeof text, 9830);  // 30
    check("glow caption at 30: 180ms", is("180ms"));
    moth::glow_caption(text, sizeof text, 22937);  // 70
    check("glow caption at 70: 1.3s", is("1.3s"));
    moth::glow_caption(text, sizeof text, 0);
    check("glow caption at 0: 40ms", is("40ms"));
    moth::glow_caption(text, sizeof text, 32767);
    check("glow caption at 100: 6.0s", is("6.0s"));
    moth::mode_caption(text, sizeof text, 1638);
    check("mode caption at 5: lp", is("lp"));
    moth::mode_caption(text, sizeof text, 16384);
    check("mode caption at 50: combo", is("combo"));
    moth::mode_caption(text, sizeof text, 32767);
    check("mode caption at 100: vca", is("vca"));
    moth::drift_caption(text, sizeof text, 8192);
    check("drift caption at 25: 3.9c", is("3.9c"));
    moth::drift_caption(text, sizeof text, 32767);
    check("drift caption at 100: 25.0c", is("25.0c"));
    moth::drift_caption(text, sizeof text, 0);
    check("drift caption at 0: 0.0c", is("0.0c"));
}
}

int main() {
    reporting = std::getenv("MOTH_REPORT") != 0;
    extremes();
    clean();
    pitch();
    aliasing();
    sweeps();
    vactrol();
    captions();
    if (failures) std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
