// Orbit's cost on the OP-XY's screen, run by sdk.py test. Every control through
// 0..127 with notes, the eight presets and the settings that load it most, at
// 1, 4 and 8 notes struck together or one after another, at 60 fps. Every frame
// valid and every outline one the device can draw, with no turn within 2.5
// degrees of straight back; on average at most 20 nodes
// changed a frame while notes sound, a peak of at most 24, none at rest; eight
// notes costing little more than one. A held note keeps something moving to the
// end, and every control moves the drawing.
#include "../display.cpp"
#include <cstdio>
#include <cstring>

namespace {
using namespace opxy::draw;
const int mean_limit = 20, peak_limit = 24;
int failures = 0;
orbit::DisplayState state;
orbit::Frame frame, last;

opxy::i32 q31(double v) { return v >= 1 ? 0x7fffffff : static_cast<opxy::i32>(v * 2147483647.0); }
bool same(const Node& a, const Node& b) {
    return a.x == b.x && a.y == b.y && a.scale == b.scale && a.cos == b.cos && a.sin == b.sin &&
           a.shade == b.shade && a.paint == b.paint;
}
bool shown(const Node& n) { return n.x < hidden_x * unit; }
int changes(const orbit::Frame& a, const orbit::Frame& b) {
    int changed = 0;
    for (unsigned i = 0; i < orbit::Frame::node_count; ++i) {
        const bool moved = !same(a.nodes[i], b.nodes[i]);
        const bool outline = i < orbit::Frame::path_count && a.paths[i].key != b.paths[i].key;
        if ((shown(a.nodes[i]) || shown(b.nodes[i])) && (moved || outline)) ++changed;
    }
    return changed;
}
// The device's outline rules (docs/display.md), each outline when it changes.
const char* outline_fault(const orbit::Frame& f, unsigned p) {
    const auto& q = f.paths[p].points;
    const int n = orbit::Frame::point_count;
    long long twice = 0;
    for (int i = 0; i < n; ++i) {
        const auto& a = q[i];
        const auto& b = q[(i + 1) % n];
        const auto& c = q[(i + 2) % n];
        const long long ux = b.x - a.x, uy = b.y - a.y, vx = c.x - b.x, vy = c.y - b.y;
        twice += static_cast<long long>(a.x) * b.y - static_cast<long long>(b.x) * a.y;
        if (ux > -128 && ux < 128 && uy > -128 && uy < 128) return "points within 1/8 px";
        // Under about 2.5 degrees from straight back the device's stroke mitres
        // into a long spike. Wane's 2.57 and Coin's 3.45 draw fine.
        const long long cross = ux * vy - uy * vx, dot = ux * vx + uy * vy;
        if (dot < 0 && (cross < 0 ? -cross : cross) * 23 < -dot) return "a turn within 2.5 degrees of straight back";
    }
    const long long cx = q[0].x - q[n - 1].x, cy = q[0].y - q[n - 1].y;
    if (cx * cx + cy * cy < 256 * 256) return "a closing edge under 1/4 px";
    if ((twice < 0 ? -twice : twice) < 2 * 52429) return "no area";   // 0.05 px^2
    return nullptr;
}
void prepare(Input& input, const double* c) {
    std::memset(&input, 0, sizeof input);
    input.track = 1;
    input.elapsed = 17;
    for (int k = 0; k < 8; ++k) input.slots[k] = q31(c[k]);
    for (int k = 0; k < 4; ++k) { input.parameters[k] = input.slots[k]; input.shown[k] = static_cast<opxy::u8>(k); }
}
struct Result { double mean; int peak; };
enum Strike { together, one_by_one };
Result play(const double* c, int notes, Strike strike, const char* name, int hold = 180) {
    Input input;
    prepare(input, c);
    std::memset(&state, 0, sizeof state);
    std::memset(&frame, 0, sizeof frame);
    orbit::display_reset(state);
    orbit::display(state, input, frame);
    for (int t = 0; t < 30; ++t) orbit::display(state, input, frame);
    Result r = {0, 0};
    long sum = 0;
    int held = 0;
    const int tail = 90;
    for (int t = 0; t < hold + tail; ++t) {
        if (strike == together && !t) { input.note_serial += notes; held = notes; }
        if (strike == one_by_one && t % 12 == 0 && t / 12 < notes && t < hold) { ++input.note_serial; ++held; }
        if (t == hold) { input.note_off_serial += held; held = 0; }
        input.held_notes = static_cast<opxy::u32>(held);
        last = frame;
        orbit::display(state, input, frame);
        if (!valid(frame)) { std::printf("FAIL %s, frame %d: not valid\n", name, t); ++failures; return r; }
        for (unsigned p = 0; p < orbit::Frame::path_count; ++p) {
            if (frame.paths[p].key == last.paths[p].key) continue;
            const char* fault = outline_fault(frame, p);
            if (fault) { std::printf("FAIL %s, frame %d: outline %u has %s\n", name, t, p, fault); ++failures; return r; }
        }
        const int changed = changes(frame, last);
        if (t < hold) sum += changed;
        if (changed > r.peak) r.peak = changed;
    }
    r.mean = static_cast<double>(sum) / hold;
    if (r.mean > mean_limit) { std::printf("FAIL %s: %.1f nodes a frame\n", name, r.mean); ++failures; }
    if (r.peak > peak_limit) { std::printf("FAIL %s: a peak of %d nodes\n", name, r.peak); ++failures; }
    // At rest: it has all landed, and nothing changes.
    for (int t = 0; t < 60; ++t) {
        last = frame;
        orbit::display(state, input, frame);
        if (t >= 30 && std::memcmp(&frame, &last, sizeof frame)) { std::printf("FAIL %s: changes at rest\n", name); ++failures; break; }
    }
    return r;
}
double worst = 0;
int peak = 0;
void note(const Result& r) {
    if (r.mean > worst) worst = r.mean;
    if (r.peak > peak) peak = r.peak;
}
}

int main() {
    const char* names[8] = {"Bank", "X", "Y", "Orbit", "Rate", "Path", "Grit", "Detune"};
    const double base[8] = {0.1875, 0.4, 0.58, 0.7, 0.62, 0.67, 0.55, 0.55};   // Dusty
    // Every control, every value.
    for (int k = 0; k < 8; ++k)
        for (int v = 0; v < 128; ++v) {
            double c[8];
            std::memcpy(c, base, sizeof c);
            c[k] = v / 127.0;
            char name[48];
            const int notes = v % 3 == 0 ? 1 : v % 3 == 1 ? 4 : 8;
            std::snprintf(name, sizeof name, "%s at %d, %d notes", names[k], v, notes);
            note(play(c, notes, v & 1 ? one_by_one : together, name, 110));
        }
    // The eight presets (definition.json) and the heaviest settings.
    const double settings[][8] = {
        {0.0625, 0.5, 0.5, 0, 0.5, 0, 0, 0},
        {0.0625, 0.5, 0.5, 0.45, 0.5, 0, 0, 0},
        {0.1875, 0.4, 0.58, 0.7, 0.62, 0.67, 0.55, 0.55},
        {0.3125, 0.55, 0.45, 0.35, 0.35, 0.33, 0, 0.45},
        {0.5625, 0.5, 0.6, 0.6, 0.42, 0, 0.2, 0.3},
        {0.6875, 0.5, 0.5, 1, 0.78, 1, 0.35, 0.2},
        {0.8125, 0.35, 0.6, 0.55, 0.3, 0.2, 0.9, 0.35},
        {0.9375, 0.5, 0.5, 0.8, 0.2, 0.67, 0, 0.7},
        {0.9, 0.5, 0.5, 1, 1, 0, 1, 1},
        {0.9, 0.5, 0.5, 1, 1, 0.5, 1, 1},
        {0.9, 0.5, 0.5, 1, 1, 1, 1, 1},
        {1, 1, 1, 1, 1, 1, 1, 1},
        {0, 0.5, 0.75, 0.45, 1, 0.55, 0.01, 0.8},   // the heaviest the design audit found
    };
    const int counts[3] = {1, 4, 8};
    for (const auto& c : settings)
        for (int n : counts)
            for (int s = 0; s < 2; ++s) note(play(c, n, s ? one_by_one : together, "settings"));
    // Eight notes cost little more than one.
    const Result one = play(settings[1], 1, together, "Clean, one note");
    const Result eight = play(settings[1], 8, one_by_one, "Clean, eight notes");
    std::printf("churn: Clean, one note %.1f a frame, eight one by one %.1f\n", one.mean, eight.mean);
    if (eight.mean > 3 * one.mean) { std::printf("FAIL eight notes cost %.1f times one\n", eight.mean / one.mean); ++failures; }
    // A held note: at the defaults, with no orbit to travel, something still moves at its end.
    {
        Input input;
        prepare(input, settings[0]);
        std::memset(&state, 0, sizeof state);
        std::memset(&frame, 0, sizeof frame);
        orbit::display_reset(state);
        for (int t = 0; t < 30; ++t) orbit::display(state, input, frame);
        ++input.note_serial;
        input.held_notes = 1;
        for (int t = 0; t < 300; ++t) orbit::display(state, input, frame);
        int moved = 0;
        for (int t = 0; t < 60; ++t) {
            last = frame;
            orbit::display(state, input, frame);
            moved += changes(frame, last);
        }
        std::printf("churn: five seconds into a held note at the defaults, %d nodes change in a second\n", moved);
        if (!moved) { std::printf("FAIL a held note's animation has stopped\n"); ++failures; }
    }
    // Each control moves the drawing, the header apart, half a second into a note.
    for (int k = 0; k < 8; ++k) {
        int moved = 0;
        for (int v = 0; v < 128; v += 8) {
            Input input;
            double c[8];
            std::memcpy(c, base, sizeof c);
            c[k] = v / 127.0;
            prepare(input, c);
            std::memset(&state, 0, sizeof state);
            std::memset(&frame, 0, sizeof frame);
            orbit::display_reset(state);
            for (int t = 0; t < 10; ++t) orbit::display(state, input, frame);
            ++input.note_serial;
            input.held_notes = 1;
            for (int t = 0; t < 30; ++t) orbit::display(state, input, frame);
            if (v) {
                bool any = false;
                for (unsigned i = 0; i < orbit::Frame::node_count; ++i) {
                    if (i >= orbit::node::header && i < orbit::node::glyph) continue;
                    any = any || !same(frame.nodes[i], last.nodes[i]);
                }
                for (unsigned p = 0; p < orbit::Frame::path_count; ++p)
                    any = any || std::memcmp(frame.paths[p].points, last.paths[p].points, sizeof frame.paths[p].points) != 0;
                if (any) ++moved;
            }
            last = frame;
        }
        std::printf("churn: %s moves the drawing in %d of 15 steps\n", names[k], moved);
        if (moved < (k ? 8 : 3)) { std::printf("FAIL %s hardly moves the drawing\n", names[k]); ++failures; }
    }
    if (failures) { std::printf("churn: %d faults\n", failures); return 1; }
    std::printf("churn: at most %.1f nodes a frame on average, peak %d, none at rest\n", worst, peak);
    return 0;
}
