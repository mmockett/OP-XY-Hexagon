// Bloom's cost on the OP-XY's screen, run by sdk.py test. Every control through
// 0..127 with a note's animation at 60 fps, the rest at the Bloom preset's and
// at the defaults, then every preset and the settings that load it most, at 1
// and 8 notes: each frame valid, the outlines within the device's rules, no
// large fill, on average at most 20 nodes changed a frame and a low peak, the
// animation lasting as long as the note, none changing at rest. Also turning
// each control, whether each control moves the drawing, and every caption's width.
#include "../display.cpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
using namespace opxy::draw;
using Picture = bloom::Frame;
int failures = 0;
bloom::DisplayState state;
Picture frame, last;
const double mean_budget = 20;
const int peak_budget = 22;

opxy::i32 q31(double v) { return v >= 1 ? 0x7fffffff : static_cast<opxy::i32>(v * 2147483647.0 + 0.5); }
bool same(const Node& a, const Node& b) {
    return a.x == b.x && a.y == b.y && a.scale == b.scale && a.cos == b.cos && a.sin == b.sin &&
           a.shade == b.shade && a.paint == b.paint;
}
void prepare(Input& input, const double* c) {
    std::memset(&input, 0, sizeof input);
    for (int k = 0; k < 8; ++k) input.slots[k] = q31(c[k]);
    for (int k = 0; k < 4; ++k) { input.parameters[k] = input.slots[k]; input.shown[k] = static_cast<opxy::u8>(k); }
    input.track = 1;
    input.elapsed = 17;
}
int changed_nodes() {
    int changed = 0;
    for (unsigned i = 0; i < Picture::node_count; ++i) {
        bool c = !same(frame.nodes[i], last.nodes[i]);
        if (i < Picture::path_count && frame.paths[i].key != last.paths[i].key) c = true;
        if (c) ++changed;
    }
    return changed;
}
// The device's rules for each reshaping outline: neighbours at least 1/8 px
// apart, a closing edge of at least 1/4 px, some area, no turn straight back.
bool outlines_ok(const char* name, int t) {
    for (unsigned p = 0; p < Picture::path_count; ++p) {
        const auto& path = frame.paths[p];
        const unsigned n = Picture::point_count;
        long long twice = 0;
        for (unsigned i = 0; i < n; ++i) {
            const Point& a = path.points[i];
            const Point& b = path.points[(i + 1) % n];
            const Point& c = path.points[(i + 2) % n];
            const long long ux = b.x - a.x, uy = b.y - a.y, vx = c.x - b.x, vy = c.y - b.y;
            twice += static_cast<long long>(a.x) * b.y - static_cast<long long>(b.x) * a.y;
            const char* fault = nullptr;
            if (i + 1 < n && std::llabs(ux) < unit / 8 && std::llabs(uy) < unit / 8) fault = "points under 1/8 px apart";
            if (i + 1 == n && ux * ux + uy * uy < unit * unit / 16) fault = "closing edge under 1/4 px";
            if (ux * vy - uy * vx == 0 && ux * vx + uy * vy < 0) fault = "a turn straight back";
            if (fault) { std::printf("FAIL %s, frame %d, path %u: %s\n", name, t, p, fault); ++failures; return false; }
        }
        if (std::llabs(twice) < unit * unit / 10) { std::printf("FAIL %s, frame %d, path %u: no area\n", name, t, p); ++failures; return false; }
    }
    return true;
}
// Nothing of size that moves is filled: blossoms, authored 4 px with half a
// pixel of stroke, are the largest solid moving parts. Scale stays within 2.
bool fills_ok(const char* name, int t) {
    for (unsigned k = 0; k < bloom::partial_count; ++k)
        for (unsigned part = 2; part < 4; ++part) {
            const Node& n = frame.nodes[bloom::partial_first + bloom::partial_nodes * k + part];
            if (n.x >= hidden_x * unit) continue;
            const double r = 4.25 * n.scale / 1024.0;
            if (3.1416 * r * r >= 100) { std::printf("FAIL %s, frame %d: a blossom of %.0f px^2\n", name, t, 3.1416 * r * r); ++failures; return false; }
        }
    return true;
}
struct Result { double mean; int peak, rest, late; };
Result play(const double* c, int notes, int hold, const char* name) {
    Input input;
    prepare(input, c);
    std::memset(&frame, 0, sizeof frame);
    bloom::display_reset(state);
    bloom::display(state, input, frame);
    if (!valid(frame)) { std::printf("FAIL %s, first frame: not valid\n", name); ++failures; }
    for (int t = 0; t < 12; ++t) bloom::display(state, input, frame);
    input.note_serial += notes;
    input.held_notes = notes;
    Result r = {0, 0, 0, 0};
    long sum = 0;
    for (int t = 0; t < hold + 90; ++t) {
        if (t == hold) { input.note_off_serial += notes; input.held_notes = 0; }
        last = frame;
        bloom::display(state, input, frame);
        if (!valid(frame)) { std::printf("FAIL %s, frame %d: not valid\n", name, t); ++failures; return r; }
        if (!outlines_ok(name, t) || !fills_ok(name, t)) return r;
        const int changed = changed_nodes();
        if (t < hold) sum += changed;
        if (t >= hold * 2 / 3 && t < hold) r.late += changed;
        if (changed > r.peak) r.peak = changed;
    }
    r.mean = static_cast<double>(sum) / hold;
    for (int t = 0; t < 60; ++t) {
        last = frame;
        bloom::display(state, input, frame);
        r.rest += changed_nodes();
    }
    if (r.mean > mean_budget) { std::printf("FAIL %s: %.1f nodes a frame\n", name, r.mean); ++failures; }
    if (r.peak > peak_budget) { std::printf("FAIL %s: a peak of %d nodes\n", name, r.peak); ++failures; }
    if (r.rest) { std::printf("FAIL %s: %d nodes changed at rest\n", name, r.rest); ++failures; }
    return r;
}
double worst = 0;
int peak = 0;
void tally(const Result& r) {
    if (r.mean > worst) worst = r.mean;
    if (r.peak > peak) peak = r.peak;
}
// Everything's offset from the last frame, past the header: how far the drawing moved, in px.
double travel(const Picture& a, const Picture& b) {
    double most = 0;
    for (unsigned i = bloom::sun_node; i < Picture::node_count; ++i) {
        const Node& n = a.nodes[i];
        const Node& m = b.nodes[i];
        const bool shown_n = n.x < hidden_x * unit, shown_m = m.x < hidden_x * unit;
        if (!shown_n && !shown_m) continue;
        double d = 0;
        const bool ring = i >= bloom::partial_first && i < bloom::mote_first && (i - bloom::partial_first) % bloom::partial_nodes == 1;
        if (shown_n != shown_m) d = ring ? (shown_n ? n : m).scale / 1024.0 * 10 : 4;
        else {
            d = (std::abs(n.x - m.x) + std::abs(n.y - m.y)) / 1024.0;
            const double sized = std::abs(n.scale - m.scale) / 1024.0 * (ring ? 10 : 4);
            if (sized > d) d = sized;
            if (n.shade != m.shade && d < 1) d = 1;
        }
        if (d > most) most = d;
    }
    for (unsigned p = 0; p < Picture::path_count; ++p)
        if (a.paths[p].key != b.paths[p].key && most < 1) most = 1;
    return most;
}
// The widths of Univers Next Pro Light's characters, per 1000 (the font's
// advance, without kerning). A value at 20 px must fit 53 px, a title at 10 px 56 px.
int advance(char c) {
    static const struct { char c; int width; } table[] = {
        {'+', 676}, {'-', 329}, {'.', 280}, {'0', 560}, {'1', 560}, {'2', 560}, {'3', 560}, {'4', 560}, {'5', 560},
        {'6', 560}, {'7', 560}, {'8', 560}, {'9', 560}, {'B', 656}, {'a', 536}, {'b', 581}, {'c', 522}, {'d', 581},
        {'e', 532}, {'f', 318}, {'g', 589}, {'h', 574}, {'i', 235}, {'k', 513}, {'l', 235}, {'m', 894}, {'n', 574},
        {'o', 571}, {'p', 581}, {'q', 581}, {'r', 345}, {'s', 480}, {'t', 325}, {'u', 574}, {'v', 512}, {'w', 820},
        {'x', 506}, {'y', 507}, {'z', 445}};
    for (const auto& a : table) if (a.c == c) return a.width;
    return -100000;
}
double width(const char* text, int px) {
    int total = 0;
    for (; *text; ++text) total += advance(*text);
    return total * px / 1000.0;
}
}

int main() {
    const double defaults[8] = {0.5, 0, 0, 0, 0, 0, 0, 0.6};
    const double bloom_preset[8] = {0.45, 0.2, 0, 0, 0.5, 0.25, 0.4, 0.6};
    const char* names[8] = {"tilt", "shape", "stretch", "vowel", "breath", "blur", "haze", "time"};

    // Every control, 0..127, from the two starting points.
    const double* const bases[2] = {defaults, bloom_preset};
    for (int b = 0; b < 2; ++b)
        for (int k = 0; k < 8; ++k)
            for (int v = 0; v < 128; ++v) {
                double c[8];
                std::memcpy(c, bases[b], sizeof c);
                c[k] = v / 127.0;
                char name[48];
                std::snprintf(name, sizeof name, "%s at %d (%s)", names[k], v, b ? "Bloom" : "defaults");
                tally(play(c, 1, 90, name));
            }
    // Every preset, 1 and 8 notes: three seconds, moving to the end.
    const struct { const char* name; double c[8]; } presets[] = {
        {"Init", {0.5, 0, 0, 0, 0, 0, 0, 0.6}},
        {"Bloom", {0.45, 0.2, 0, 0, 0.5, 0.25, 0.4, 0.6}},
        {"Tape Choir", {0.45, 0.1, 0, 0.45, 0.45, 0.35, 0.45, 0.55}},
        {"Juno Dusk", {0.55, 0.72, 0, 0, 0.7, 0.15, 0.35, 0.5}},
        {"Glass Bells", {0.62, 0, 0.5, 0, 0.55, 0, 0.25, 0.3}},
        {"Frost", {0.7, 0.3, 0.12, 0, 0.8, 0.2, 0.2, 0.08}},
        {"Fog", {0.6, 0.35, 0, 0.15, 0.35, 0.85, 0.3, 0.65}},
        {"Seasick", {0.5, 0.5, 0, 0.8, 0.6, 0.4, 0.85, 0.45}},
        {"Still Life", {0.7, 0.6, 0.08, 0.6, 0.8, 0.2, 0.3, 1}}};
    for (const auto& p : presets) {
        const Result one = play(p.c, 1, 180, p.name), eight = play(p.c, 8, 180, p.name);
        tally(one);
        tally(eight);
        if (!one.late) { std::printf("FAIL %s: the animation stops while the note is held\n", p.name); ++failures; }
        std::printf("churn: %-11s %5.1f a frame, peak %d, at 1 note; %5.1f, peak %d, at 8\n", p.name, one.mean, one.peak, eight.mean, eight.peak);
    }
    // The settings that load it most.
    const double heaviest[][8] = {{1, 1, 1, 1, 1, 1, 1, 0}, {1, 1, 1, 1, 1, 1, 1, 0.9}, {1, 1, 1, 1, 1, 1, 1, 1}, {0, 1, 0, 1, 1, 1, 1, 0},
                                  {1, 0, 0, 0, 1, 1, 1, 0}, {0.5, 0.5, 0.5, 0.5, 1, 1, 1, 0}, {0, 0, 0, 0, 0, 1, 0, 0}, {0, 0, 0, 0, 0, 0, 1, 0}};
    for (const auto& c : heaviest) {
        tally(play(c, 1, 240, "heaviest"));
        tally(play(c, 8, 240, "heaviest"));
    }
    // Retriggering, four notes a second.
    {
        Input input;
        prepare(input, heaviest[0]);
        std::memset(&frame, 0, sizeof frame);
        bloom::display_reset(state);
        for (int t = 0; t < 12; ++t) bloom::display(state, input, frame);
        long sum = 0;
        int most = 0;
        for (int t = 0; t < 240; ++t) {
            if (t % 15 == 0) { input.note_serial++; input.held_notes = 1; }
            last = frame;
            bloom::display(state, input, frame);
            const int changed = changed_nodes();
            sum += changed;
            if (changed > most) most = changed;
        }
        if (sum / 240.0 > mean_budget || most > peak_budget) { std::printf("FAIL retriggering: %.1f a frame, peak %d\n", sum / 240.0, most); ++failures; }
    }
    // Turning each control end to end over a second and a half, with and without a note.
    for (int held = 0; held < 2; ++held)
        for (int k = 0; k < 8; ++k) {
            Input input;
            prepare(input, bloom_preset);
            input.slots[k] = q31(0);
            std::memset(&frame, 0, sizeof frame);
            bloom::display_reset(state);
            for (int t = 0; t < 30; ++t) bloom::display(state, input, frame);
            if (held) { input.note_serial++; input.held_notes = 1; for (int t = 0; t < 30; ++t) bloom::display(state, input, frame); }
            long sum = 0;
            int most = 0;
            for (int t = 0; t < 90; ++t) {
                input.slots[k] = q31(t / 89.0);
                last = frame;
                bloom::display(state, input, frame);
                if (!valid(frame)) { std::printf("FAIL turning %s: not valid\n", names[k]); ++failures; break; }
                const int changed = changed_nodes();
                sum += changed;
                if (changed > most) most = changed;
            }
            if (sum / 90.0 > mean_budget || most > peak_budget) {
                std::printf("FAIL turning %s: %.1f a frame, peak %d\n", names[k], sum / 90.0, most);
                ++failures;
            }
        }
    // Each control moves the drawing, step by step: at rest, from the Bloom
    // preset, with the drawing settled after each step. Time acts on what's moving.
    static Picture first, previous;
    for (int k = 0; k < 7; ++k) {
        Input input;
        prepare(input, bloom_preset);
        std::memset(&frame, 0, sizeof frame);
        bloom::display_reset(state);
        int moved = 0;
        double most = 0;
        for (int v = 0; v < 128; v += 8) {
            input.slots[k] = q31(v / 127.0);
            for (int t = 0; t < 40; ++t) bloom::display(state, input, frame);
            if (v) {
                if (travel(frame, previous) > 0) ++moved;
                const double d = travel(frame, first);
                if (d > most) most = d;
            } else {
                first = frame;
            }
            previous = frame;
        }
        std::printf("churn: %s moves the drawing in %d of 15 steps, up to %.0f px\n", names[k], moved, most);
        if (moved < 12) { std::printf("FAIL %s hardly moves the drawing\n", names[k]); ++failures; }
        if (most < 8) { std::printf("FAIL %s moves the drawing only %.1f px\n", names[k], most); ++failures; }
    }
    {
        // Time, with a note held: the stems move more when it's faster, and not at hold.
        int moves[3] = {0, 0, 0};
        const double times[3] = {0.1, 0.9, 1};
        for (int i = 0; i < 3; ++i) {
            double c[8];
            std::memcpy(c, defaults, sizeof c);
            c[4] = 0.5;
            c[7] = times[i];
            Input input;
            prepare(input, c);
            std::memset(&frame, 0, sizeof frame);
            bloom::display_reset(state);
            for (int t = 0; t < 12; ++t) bloom::display(state, input, frame);
            input.note_serial++;
            input.held_notes = 1;
            for (int t = 0; t < 30; ++t) bloom::display(state, input, frame);
            for (int t = 0; t < 300; ++t) {
                last = frame;
                bloom::display(state, input, frame);
                for (unsigned k = 0; k < bloom::partial_count; ++k) {
                    const unsigned n = bloom::partial_first + bloom::partial_nodes * k;
                    moves[i] += std::abs(frame.nodes[n].y - last.nodes[n].y) / unit;
                }
            }
        }
        std::printf("churn: time moves the stems %d px in 5 s at 10%%, %d at 90%%, %d at hold\n", moves[0], moves[1], moves[2]);
        if (moves[0] < moves[1] * 3 / 2 || moves[1] <= moves[2] || moves[2]) { std::printf("FAIL time doesn't change the pace\n"); ++failures; }
    }
    // Every caption, finely. Tilt's "-24dB" is 53.7 px, but it's the first
    // column, with 60 px before the next title: the last column has 55.
    for (int slot = 0; slot < 8; ++slot)
        for (int v = 0; v <= 4096; ++v) {
            char out[header_capacity];
            bloom::caption(out, slot, q31(v / 4096.0));
            if (width(out, 20) > (slot == 0 ? 54 : 53) || width(out, 20) < 0) { std::printf("FAIL %s caption '%s': %.1f px\n", names[slot], out, width(out, 20)); ++failures; break; }
        }
    for (const char* name : names)
        if (width(name, 10) > 56) { std::printf("FAIL title %s\n", name); ++failures; }

    if (failures) { std::printf("churn: %d faults\n", failures); return 1; }
    std::printf("churn: at most %.1f nodes a frame on average, peak %d, none at rest, every frame valid\n", worst, peak);
    return 0;
}
