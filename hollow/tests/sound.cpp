// Hollow's sound checks, compiled for the host and run by
//   python sdk.py test --machine <folder>
// Each check names what the sound promises: bounded and stable at every
// extreme, sustaining while held, a body that changes the spectrum, pitch that
// tracks, and sympathy that comes only from notes still held.
#include "../engine.cpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

namespace {
using hollow::Voice;
using hollow::Shared;
using opxy::i32;
using opxy::u32;

int failures = 0;
void check(const char* what, bool ok) {
    if (!ok) { ++failures; std::printf("FAIL %s\n", what); }
}

const i32 kFull = 0x7fffffff, kPeak = 1073741824;
const unsigned kRate = 44100, kSecond = 689;  // blocks
enum { kBreath, kBody, kBright, kRing, kAir, kVowel, kSpread, kSympathy };

u32 increment(double hz) { return static_cast<u32>(hz / kRate * 4294967296.0); }
i32 word(double normalized) { return static_cast<i32>(std::lround(normalized * 2147483647.0)); }

// The defaults (definition.json), as control words.
void defaults(i32* p) {
    const double d[8] = {0.35, 0, 0.45, 0.5, 0, 0.5, 0, 0};
    for (unsigned i = 0; i < 8; ++i) p[i] = word(d[i]);
}

// A few voices sharing one Shared, as the device runs them: each owner keeps
// its voice; rendering is serial.
struct Choir {
    Voice voices[4];
    Shared shared;
    u32 owners[4];
    u32 pitch[4][opxy::block_frames];
    bool on[4];
    Choir() {
        unsigned char* bytes = reinterpret_cast<unsigned char*>(this);
        for (unsigned i = 0; i < sizeof(*this); ++i) bytes[i] = 0;
    }
    void play(unsigned v, u32 owner, double hz) {
        owners[v] = owner;
        unsigned char* bytes = reinterpret_cast<unsigned char*>(&voices[v]);
        for (unsigned i = 0; i < sizeof(Voice); ++i) bytes[i] = 0;
        hollow::initialize(voices[v], shared, owner);
        hollow::start(voices[v], shared, owner);
        tune(v, hz);
        on[v] = true;
    }
    void tune(unsigned v, double hz) {
        for (unsigned i = 0; i < opxy::block_frames; ++i) pitch[v][i] = increment(hz);
    }
    void end(unsigned v) {
        hollow::destroy(voices[v], shared, owners[v]);
        on[v] = false;
    }
    // One block of every sounding voice; out[v] gets voice v's block.
    void render(const i32* parameters, i32 (*out)[opxy::block_frames]) {
        for (unsigned v = 0; v < 4; ++v)
            if (on[v]) hollow::render(voices[v], shared, owners[v], out[v], parameters, pitch[v]);
    }
};

// A long recording of voice 0 (and the others rendered alongside).
const unsigned kMaxBlocks = 40 * kSecond;
i32 recording[kMaxBlocks * opxy::block_frames];

// Plays `blocks` blocks of the choir, recording voice 0 from `at` (in blocks).
// Returns whether every voice stayed within the declared peak.
bool record(Choir& choir, const i32* parameters, unsigned blocks, unsigned at = 0) {
    static i32 out[4][opxy::block_frames];
    bool bounded = true;
    for (unsigned b = 0; b < blocks; ++b) {
        choir.render(parameters, out);
        for (unsigned v = 0; v < 4; ++v)
            if (choir.on[v])
                for (unsigned i = 0; i < opxy::block_frames; ++i)
                    bounded = bounded && out[v][i] >= -kPeak && out[v][i] <= kPeak;
        if (at + b < kMaxBlocks)
            for (unsigned i = 0; i < opxy::block_frames; ++i)
                recording[(at + b) * opxy::block_frames + i] = choir.on[0] ? out[0][i] : 0;
    }
    return bounded;
}

double power(unsigned from, unsigned count) {
    double sum = 0;
    for (unsigned i = from; i < from + count; ++i) sum += static_cast<double>(recording[i]) * recording[i];
    return sum / count / 4.611686e18;  // relative to full scale squared
}

// Energy at hz, Hann-windowed over count samples from `from`.
double energy_at(unsigned from, unsigned count, double hz) {
    double re = 0, im = 0;
    for (unsigned i = 0; i < count; ++i) {
        const double window = 0.5 - 0.5 * std::cos(6.283185307179586 * i / count);
        const double phase = 6.283185307179586 * hz * i / kRate;
        const double x = recording[from + i] / 2147483648.0 * window;
        re += x * std::cos(phase);
        im += x * std::sin(phase);
    }
    return (re * re + im * im) / (count * count);
}

// The frequency within 3% of hz carrying most energy, by a fine scan.
double peak_near(unsigned from, unsigned count, double hz) {
    double best = 0, at = hz;
    for (double f = hz * 0.97; f <= hz * 1.03; f += hz * 0.0005) {
        const double e = energy_at(from, count, f);
        if (e > best) { best = e; at = f; }
    }
    return at;
}

// The shortest lag where the signal matches itself nearly as well as anywhere:
// one period, interpolated.
double period(unsigned from) {
    static double score[2400];
    double best = 0;
    for (unsigned lag = 20; lag < 2400; ++lag) {
        double sum = 0;
        for (unsigned i = from; i < from + 8192; ++i) sum += static_cast<double>(recording[i]) * recording[i + lag];
        score[lag] = sum;
        if (sum > best) best = sum;
    }
    for (unsigned lag = 21; lag + 1 < 2400; ++lag)
        if (score[lag] >= 0.9 * best && score[lag] >= score[lag - 1] && score[lag] >= score[lag + 1]) {
            const double a = score[lag - 1], b = score[lag], c = score[lag + 1], d = a - 2 * b + c;
            return lag + (d ? 0.5 * (a - c) / d : 0);
        }
    return 0;
}

// Every mode's amplitude stays inside the limit it is kept to.
bool states_bounded(const Voice& voice) {
    const i32 limit = 4095 << 16;
    for (unsigned k = 0; k < hollow::kModes; ++k)
        if (std::abs(voice.modes[k].a) > limit || std::abs(voice.modes[k].b) > limit) return false;
    for (unsigned j = 0; j < hollow::kEchoes; ++j)
        if (std::abs(voice.echoes[j].mode.a) > limit || std::abs(voice.echoes[j].mode.b) > limit) return false;
    return true;
}

bool registered(const Shared& shared, u32 owner) {
    for (unsigned j = 0; j < hollow::kNotes; ++j)
        if (shared.notes[j].owner == owner) return true;
    return false;
}

void extremes() {
    // Every corner of the eight controls, at a low, a middle and a high note,
    // with a second voice a fifth up so Sympathy is live.
    bool bounded = true, stable = true;
    for (double hz : {27.5, 220.0, 2093.0}) {
        for (u32 corner = 0; corner < 256; ++corner) {
            i32 p[8];
            for (unsigned c = 0; c < 8; ++c) p[c] = corner & (1u << c) ? kFull : 0;
            static Choir choir;
            choir = Choir();
            choir.play(0, 1, hz);
            choir.play(1, 2, hz * 1.5);
            bounded = record(choir, p, kSecond / 4) && bounded;
            stable = stable && states_bounded(choir.voices[0]) && states_bounded(choir.voices[1]);
        }
    }
    check("within half scale at every corner of the controls", bounded);
    check("resonator states inside their limit at every corner", stable);
    // The keyboard's ends, and a pitch bend through the top of the modes.
    i32 p[8];
    for (unsigned c = 0; c < 8; ++c) p[c] = kFull;
    for (double hz : {4.0, 8.2, 12543.0, 20000.0}) {
        static Choir choir;
        choir = Choir();
        choir.play(0, 1, hz);
        choir.play(1, 2, 440.0);
        check("within half scale at the keyboard's ends", record(choir, p, kSecond / 2));
    }
    static Choir bend;
    bend = Choir();
    bend.play(0, 1, 100.0);
    bool bent = true;
    for (unsigned b = 0; b < 2 * kSecond; ++b) {
        bend.tune(0, 100.0 * std::pow(2.0, 6.0 * b / (2 * kSecond)));  // six octaves up
        bent = record(bend, p, 1) && bent;
    }
    check("within half scale through a six-octave bend", bent);
}

void long_ring() {
    // Thirty seconds at the longest ring, every other control at its most,
    // breath and bow, with four voices ringing into each other.
    for (double breath : {0.0, 1.0}) {
        i32 p[8];
        for (unsigned c = 0; c < 8; ++c) p[c] = kFull;
        p[kBreath] = word(breath);
        p[kBody] = kFull;  // glass rings longest
        static Choir choir;
        choir = Choir();
        choir.play(0, 1, 110.0);
        choir.play(1, 2, 164.81);
        choir.play(2, 3, 220.0);
        choir.play(3, 4, 277.18);
        const bool bounded = record(choir, p, 30 * kSecond);
        const unsigned s = kSecond * opxy::block_frames;
        const double middle = power(10 * s, 5 * s), late = power(25 * s, 5 * s);
        bool stable = true;
        for (unsigned v = 0; v < 4; ++v) stable = stable && states_bounded(choir.voices[v]);
        check("within half scale for 30 s at the longest ring", bounded);
        check("resonators stay inside their limit at the longest ring", stable);
        check("no growth at the longest ring: the last 5 s no louder than 10-15 s", late < 2.0 * middle);
        check("still sounding after 30 s at the longest ring", late > 1e-6);
    }
}

void sustain() {
    // The bow holds a steady level; a breath through narrow resonators drifts
    // slowly (its noise beats against the bow), but never dies away.
    i32 p[8];
    defaults(p);
    for (double breath : {0.0, 0.35, 1.0})
        for (double ring : {0.0, 0.5, 1.0}) {
            p[kBreath] = word(breath);
            p[kRing] = word(ring);
            static Choir choir;
            choir = Choir();
            choir.play(0, 1, 220.0);
            record(choir, p, 20 * kSecond);
            const unsigned s = kSecond * opxy::block_frames;
            const double early = power(2 * s, 4 * s), late = power(14 * s, 4 * s);
            if (breath == 0) check("sustains while bowed: seconds 14-18 within 1.5 dB of 2-6", late > 0.7 * early && late < 1.4 * early);
            check("sustains while held: seconds 14-18 within 6 dB of 2-6", late > 0.25 * early && late < 4.0 * early);
            check("sustains while held: clearly audible", late > 1e-4);  // above -40 dBFS
        }
}

void body() {
    // The bow alone at 110 Hz, so the spectrum is the body's.
    i32 p[8];
    defaults(p);
    p[kBreath] = 0;
    p[kRing] = word(0.6);
    auto play = [&](double body, double breath) {
        p[kBody] = word(body);
        p[kBreath] = word(breath);
        static Choir choir;
        choir = Choir();
        choir.play(0, 1, 110.0);
        record(choir, p, 4 * kSecond);
    };
    const unsigned from = 2 * kSecond * opxy::block_frames, count = 16384;
    auto even_to_odd = [&]() {
        double even = 0, odd = 0;
        for (unsigned k = 1; k <= 8; ++k) (k % 2 ? odd : even) += energy_at(from, count, 110.0 * k);
        return even / odd;
    };
    play(2.0 / 3, 0);
    const double pipe = even_to_odd();
    play(1.0 / 3, 0);
    const double string = even_to_odd();
    const double string_glass = energy_at(from, count, 110.0 * 2.32) / energy_at(from, count, 110.0 * 2);
    play(1.0, 0.5);
    const double glass = energy_at(from, count, 110.0 * 2.32) / energy_at(from, count, 110.0 * 2);
    check("the pipe is hollow: its even harmonics 30 dB under its odd", pipe < 0.001);
    check("the string keeps its even harmonics, within 10 dB of its odd", string > 0.1);
    check("glass rings at its own partial, 2.32x, above the second harmonic", glass > 3.0);
    check("a string has no 2.32x partial", string_glass < 0.1);
    // The vowel moves the voice's formants: at 220 Hz "ah" (first formant
    // 730 Hz) lifts the third harmonic over the fundamental; "oo" (320 Hz) does not.
    auto vowel = [&](double v) {
        p[kBody] = 0;
        p[kBreath] = 0;
        p[kVowel] = word(v);
        static Choir choir;
        choir = Choir();
        choir.play(0, 1, 220.0);
        record(choir, p, 3 * kSecond);
        return energy_at(from, count, 220.0 * 3) / energy_at(from, count, 220.0);
    };
    check("the vowel moves the formants", vowel(0.5) > 4.0 * vowel(0.0));
}

void pitch() {
    i32 p[8];
    defaults(p);
    p[kBreath] = 0;
    for (double body : {0.0, 1.0 / 3}) {
        p[kBody] = word(body);
        for (double hz : {55.0, 110.0, 220.0, 440.0, 880.0}) {
            static Choir choir;
            choir = Choir();
            choir.play(0, 1, hz);
            record(choir, p, kSecond);
            const double found = period(kSecond / 2 * opxy::block_frames), expected = kRate / hz;
            check("in tune within 0.5% (vox and string, by period)", std::fabs(found / expected - 1) < 0.005);
        }
    }
    // Glass, and Spread on the voice: the fundamental still sits on the note.
    for (double spread : {0.0, 1.0}) {
        p[kBody] = spread ? 0 : kFull;
        p[kSpread] = word(spread);
        p[kBreath] = word(0.35);
        // (Spread's flat pulse sits 15 cents below: far enough to resolve from 440 Hz.)
        for (double hz : {spread ? 440.0 : 110.0, spread ? 880.0 : 440.0}) {
            static Choir choir;
            choir = Choir();
            choir.play(0, 1, hz);
            record(choir, p, 2 * kSecond);
            const double found = peak_near(kSecond * opxy::block_frames, 32768, hz);
            check("glass, and Spread, keep the fundamental within 0.2%", std::fabs(found / hz - 1) < 0.002);
        }
    }
}

void sympathy() {
    // Voice A at 220 Hz, voice B a major third up. With Sympathy, A carries
    // energy at B's pitch, which none of A's own modes sit near.
    const double a = 220.0, b = 277.18;
    const unsigned window = 16384;
    i32 p[8];
    defaults(p);
    const unsigned from = 2 * kSecond * opxy::block_frames;
    double alone = 0;  // A's energy at B's pitch without Sympathy
    auto at_b = [&](double amount) {
        p[kSympathy] = word(amount);
        static Choir choir;
        choir = Choir();
        choir.play(0, 1, a);
        choir.play(1, 2, b);
        record(choir, p, 3 * kSecond);
        if (amount == 0) alone = energy_at(from, window, b);
        return energy_at(from, window, b) / energy_at(from, window, a);
    };
    const double off = at_b(0.0), on = at_b(1.0);
    check("sympathy: A rings at B's pitch only with Sympathy up (10 dB more)", on > 10.0 * off);
    check("sympathy: still quiet beside A's own note", on < 1.0);

    // Destroy: B ends; within a block A no longer rings at B's pitch, and B's
    // registration is gone.
    p[kSympathy] = kFull;
    static Choir choir;
    choir = Choir();
    choir.play(0, 1, a);
    choir.play(1, 2, b);
    record(choir, p, 3 * kSecond);
    const double held = energy_at(from, window, b);
    choir.end(1);
    check("destroy clears the note's registration", !registered(choir.shared, 2));
    record(choir, p, kSecond, 3 * kSecond);
    const double after = energy_at(3 * kSecond * opxy::block_frames + 512, window, b);
    check("no sympathetic ringing at B's pitch after B's destroy", after < 0.1 * held && after < 3.0 * alone);
    bool idle = true;
    for (unsigned j = 0; j < hollow::kEchoes; ++j) idle = idle && !choir.voices[0].echoes[j].owner;
    check("A's sympathetic resonators all idle after B ends", idle);

    // Stale: B stops rendering without destroy (never expected on the device);
    // A drops it after kFresh renders.
    choir = Choir();
    choir.play(0, 1, a);
    choir.play(1, 2, b);
    record(choir, p, 3 * kSecond);
    choir.on[1] = false;  // silent, but still registered
    record(choir, p, kSecond, 3 * kSecond);
    const double stale = energy_at(3 * kSecond * opxy::block_frames + hollow::kFresh * 64 + 512, window, b);
    check("a note that stops rendering stops ringing in others", stale < 0.1 * held);

    // Stolen: B's voice goes to a new owner at another pitch. A follows the
    // new note, not the old.
    choir = Choir();
    choir.play(0, 1, a);
    choir.play(1, 2, b);
    record(choir, p, 3 * kSecond);
    hollow::destroy(choir.voices[1], choir.shared, 2);  // the adapter destroys before reassigning
    choir.play(1, 3, 329.63);
    record(choir, p, 2 * kSecond, 3 * kSecond);
    const unsigned later = 4 * kSecond * opxy::block_frames;
    check("a stolen voice's old pitch no longer rings in others",
          energy_at(later, window, b) < 0.1 * held);
    check("the new note rings in others", energy_at(later, window, 329.63) > 0.3 * held);

    // More owners than entries: the registry stays bounded, one entry per owner.
    static Choir crowd;
    crowd = Choir();
    for (u32 owner = 1; owner <= 3 * hollow::kNotes; ++owner) {
        crowd.play(owner % 4, owner, 100.0 + owner * 37.0);
        record(crowd, p, 4);
    }
    bool unique = true;
    for (unsigned i = 0; i < hollow::kNotes; ++i)
        for (unsigned j = i + 1; j < hollow::kNotes; ++j)
            unique = unique && !(crowd.shared.notes[i].owner && crowd.shared.notes[i].owner == crowd.shared.notes[j].owner);
    check("the registry keeps one entry per owner when crowded", unique);
    for (unsigned v = 0; v < 4; ++v) check("every sounding voice is registered", registered(crowd.shared, crowd.owners[v]));
}

void captions() {
    char text[8];
    hollow::body_caption(text, sizeof text, 0);
    check("body caption at 0: vox", text[0] == 'v' && text[1] == 'o' && text[2] == 'x' && !text[3]);
    hollow::body_caption(text, sizeof text, 32767);
    check("body caption at 1: glass", text[0] == 'g' && text[4] == 's' && !text[5]);
    hollow::ring_caption(text, sizeof text, 16384, 0);
    check("ring caption at the default: 1.0s", text[0] == '1' && text[1] == '.' && text[2] == '0' && text[3] == 's');
    hollow::ring_caption(text, sizeof text, 32767, 32767);
    check("ring caption at the longest: 30s", text[0] == '3' && text[1] == '0' && text[2] == 's');
    hollow::vowel_caption(text, sizeof text, 16384);
    check("vowel caption in the middle: ah", text[0] == 'a' && text[1] == 'h');
    hollow::spread_caption(text, sizeof text, 32767);
    check("spread caption at full: 15c", text[0] == '1' && text[1] == '5' && text[2] == 'c');
}
}

int main() {
    extremes();
    long_ring();
    sustain();
    body();
    pitch();
    sympathy();
    captions();
    if (failures) std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
