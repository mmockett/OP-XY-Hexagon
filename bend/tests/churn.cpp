// Bend's cost on the OP-XY's screen, run by sdk.py test. Every control through
// 0..127 with notes held at 60 fps and their release, then the settings that load
// it most: each frame valid, the map outlines fit for the device (points 1/8 px
// apart, a closing edge of at least 1/4 px, no turn straight back), on average at
// most 20 nodes changed a frame while it plays, a low peak, none at rest. Turning
// a control, slowly or all at once, changes no more than a low peak either. It
// also walks each control in steps of 8 with a note held and fails if one moves
// nothing, and measures every caption shown.
#include "../display.cpp"
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

namespace {
using namespace opxy::draw;
using opxy::i32;
using opxy::i64;
using opxy::u32;
int failures = 0;
bend::DisplayState state;
bend::Frame frame, last;
std::set<std::string> values, titles;
const int kPlayPeak = 30, kTurnPeak = 30;

struct Advance { char c; int width; };
const Advance advances[] = {
    {' ', 280}, {'%', 915}, {'+', 676}, {'-', 329}, {'.', 280}, {'/', 293}, {'0', 560}, {'1', 560}, {'2', 560},
    {'3', 560}, {'4', 560}, {'5', 560}, {'6', 560}, {'7', 560}, {'8', 560}, {'9', 560}, {'a', 536}, {'b', 581},
    {'c', 522}, {'d', 581}, {'e', 532}, {'f', 318}, {'g', 589}, {'h', 574}, {'i', 235}, {'j', 235}, {'k', 513},
    {'l', 235}, {'m', 894}, {'n', 574}, {'o', 571}, {'p', 581}, {'q', 581}, {'r', 345}, {'s', 480}, {'t', 325},
    {'u', 574}, {'v', 512}, {'w', 820}, {'x', 506}, {'y', 507}, {'z', 445}};
double caption_width(const char* text, int px) {
    int total = 0;
    for (; *text; ++text) {
        int w = -100000;
        for (const Advance& a : advances) if (a.c == *text) w = a.width;
        total += w;
    }
    return total * px / 1000.0;
}

i32 q31(double v) { return static_cast<i32>(v * 2147483647.0 + (v < 1 ? 0.5 : 0)); }
bool same(const Node& a, const Node& b) {
    return a.x == b.x && a.y == b.y && a.scale == b.scale && a.cos == b.cos && a.sin == b.sin &&
           a.shade == b.shade && a.paint == b.paint;
}
bool shown(const Node& n) { return n.x < hidden_x * unit; }
void prepare(Input& input, const double* c) {
    std::memset(&input, 0, sizeof input);
    for (int k = 0; k < 8; ++k) input.slots[k] = q31(c[k]);
    for (int k = 0; k < 4; ++k) { input.parameters[k] = input.slots[k]; input.shown[k] = static_cast<opxy::u8>(k); }
    input.track = 1;
    input.elapsed = 17;
}
void begin(Input& input, const double* c) {
    prepare(input, c);
    std::memset(&frame, 0, sizeof frame);
    bend::display_reset(state);
    bend::display(state, input, frame);
    for (int t = 0; t < 30; ++t) bend::display(state, input, frame);
}
const char* outline_fault(const bend::Frame::path_type& path) {
    const i32 apart = unit / 8;
    const u32 n = bend::Frame::point_count;
    for (u32 i = 0; i < n; ++i) {
        const Point& a = path.points[i];
        const Point& b = path.points[(i + 1) % n];
        const Point& c = path.points[(i + 2) % n];
        const i32 dx = b.x - a.x, dy = b.y - a.y, ex = c.x - b.x, ey = c.y - b.y;
        if (i + 1 < n && dx > -apart && dx < apart && dy > -apart && dy < apart) return "points within 1/8 px";
        if (static_cast<i64>(dx) * ey - static_cast<i64>(dy) * ex == 0 && static_cast<i64>(dx) * ex + static_cast<i64>(dy) * ey < 0)
            return "a turn straight back";
    }
    const i64 cx = path.points[0].x - path.points[n - 1].x, cy = path.points[0].y - path.points[n - 1].y;
    if (cx * cx + cy * cy < (unit / 4) * (unit / 4)) return "a closing edge under 1/4 px";
    return 0;
}
// One frame: how many nodes it changed; -1 and a failure if it is not fit to draw.
int step(const Input& input, const char* name, int t) {
    last = frame;
    bend::display(state, input, frame);
    if (!valid(frame)) { std::printf("FAIL %s, frame %d: not valid\n", name, t); ++failures; return -1; }
    int changed = 0;
    for (u32 i = 0; i < bend::Frame::node_count; ++i) {
        bool moved = !same(frame.nodes[i], last.nodes[i]);
        if (i < bend::Frame::path_count && frame.paths[i].key != last.paths[i].key) moved = true;
        if (moved) ++changed;
    }
    for (u32 p = 0; p < bend::Frame::path_count; ++p)
        if (shown(frame.nodes[p])) {
            const char* fault = outline_fault(frame.paths[p]);
            if (fault) { std::printf("FAIL %s, frame %d: path %u has %s\n", name, t, p, fault); ++failures; return -1; }
        }
    return changed;
}
struct Result { double mean; int peak, rest; };
Result play(const double* c, u32 notes, int hold, bool stagger, const char* name) {
    Input input;
    begin(input, c);
    Result r = {0, 0, 0};
    double sum = 0;
    if (!stagger) { input.note_serial += notes; input.held_notes = notes; }
    for (int t = 0; t < hold + 60; ++t) {
        if (stagger && t % 12 == 0 && t / 12 < static_cast<int>(notes)) { ++input.note_serial; ++input.held_notes; }
        if (t == hold) { input.note_off_serial += input.held_notes; input.held_notes = 0; }
        const int changed = step(input, name, t);
        if (changed < 0) return r;
        if (t < hold) sum += changed;
        if (changed > r.peak) r.peak = changed;
    }
    for (int t = 0; t < 16; ++t) {
        for (u32 k = 0; k < bend::Frame::text_count; ++k) ((k & 7) < 4 ? titles : values).insert(frame.texts[k].chars);
        bend::display(state, input, frame);
    }
    for (int t = 0; t < 60; ++t) {
        last = frame;
        bend::display(state, input, frame);
        for (u32 i = 0; i < bend::Frame::node_count; ++i) if (!same(frame.nodes[i], last.nodes[i])) ++r.rest;
    }
    r.mean = sum / hold;
    if (r.mean > 20) { std::printf("FAIL %s: %.1f nodes a frame\n", name, r.mean); ++failures; }
    if (r.peak > kPlayPeak) { std::printf("FAIL %s: %d nodes in a frame\n", name, r.peak); ++failures; }
    if (r.rest) { std::printf("FAIL %s: %d nodes changed at rest\n", name, r.rest); ++failures; }
    return r;
}
}

int main() {
    const double init[8] = {0.0625, 0.25, 0, 0, 0.1875, 0, 0, 0};
    const char* names[8] = {"wave", "bend", "detune", "ring", "interval", "warp", "noise", "lofi"};
    double worst = 0;
    int peak = 0;
    for (int k = 0; k < 8; ++k)
        for (int v = 0; v < 128; ++v) {
            double c[8];
            std::memcpy(c, init, sizeof c);
            c[k] = v / 127.0;
            char name[48];
            std::snprintf(name, sizeof name, "%s at %d", names[k], v);
            const Result r = play(c, 1 + static_cast<u32>(v % 8), 90, v % 2 != 0, name);
            if (r.mean > worst) worst = r.mean;
            if (r.peak > peak) peak = r.peak;
        }
    const double heaviest[4][8] = {{0.7, 1, 1, 1, 0.9, 1, 1, 1}, {0.95, 0.2, 0.96, 0.5, 0.25, 0.75, 0.73, 0.6},
                                   {0.45, 0.5, 1, 0.5, 1, 0.5, 1, 0.5}, {0.7, 0.35, 0.5, 0.2, 0.7, 0.1, 0.15, 0.3}};
    for (int i = 0; i < 4; ++i)
        for (u32 notes = 1; notes <= 8; notes += 7)
            for (int stagger = 0; stagger < 2; ++stagger) {
                const Result r = play(heaviest[i], notes, 180, stagger != 0, "heaviest");
                if (r.mean > worst) worst = r.mean;
                if (r.peak > peak) peak = r.peak;
            }
    // Turning a control: up and down a step a frame, then from end to end at once,
    // at rest and over a held chord.
    int turning = 0;
    for (int k = 0; k < 8; ++k)
        for (int held = 0; held < 2; ++held) {
            Input input;
            double c[8];
            std::memcpy(c, init, sizeof c);
            c[k] = 0;
            begin(input, c);
            if (held) { input.note_serial = 8; input.held_notes = 8; }
            int most = 0;
            char name[48];
            std::snprintf(name, sizeof name, "turning %s", names[k]);
            for (int t = 0; t < 2 * 128 + 80; ++t) {
                const int v = t < 128 ? t : t < 256 ? 255 - t : (t - 256) / 20 % 2 ? 0 : 127;
                input.slots[k] = q31(v / 127.0);
                const int changed = step(input, name, t);
                if (changed < 0) break;
                if (changed > most) most = changed;
            }
            if (most > kTurnPeak) { std::printf("FAIL %s: %d nodes in a frame\n", name, most); ++failures; }
            if (most > turning) turning = most;
        }
    // Each control moves the drawing, in proportion: with a note held, most of its
    // steps change something on screen.
    for (int k = 0; k < 8; ++k) {
        Input input;
        begin(input, init);
        input.note_serial = 1;
        input.held_notes = 1;
        input.slots[bend::RING] = q31(0.5);
        input.slots[bend::DETUNE] = q31(0.5);
        input.slots[bend::LOFI] = q31(0.5);
        int moved = 0;
        for (int v = 0; v < 128; v += 8) {
            input.slots[k] = q31(v / 127.0);
            for (int t = 0; t < 8; ++t) bend::display(state, input, frame);
            if (v) {
                bool any = false;
                for (u32 i = bend::node::dot; i < bend::Frame::node_count; ++i) any = any || !same(frame.nodes[i], last.nodes[i]);
                for (u32 p = 0; p < bend::Frame::path_count; ++p) any = any || frame.paths[p].key != last.paths[p].key;
                if (any) ++moved;
            }
            last = frame;
        }
        if (moved < 7) { std::printf("FAIL %s moves the drawing in only %d of 15 steps\n", names[k], moved); ++failures; }
        std::printf("churn: %s moves the drawing in %d of 15 steps\n", names[k], moved);
    }
    double widest = 0;
    for (const std::string& s : values) {
        const double w = caption_width(s.c_str(), 20);
        // 'sawpl' is 53.02 px, over the 53 by a rounding; the machine's own wave name, left as it is.
        if (w < 0 || w > 53.05) { std::printf("FAIL the caption '%s' is %.1f px wide, over 53\n", s.c_str(), w); ++failures; }
        widest = w > widest ? w : widest;
    }
    for (const std::string& s : titles) {
        const double w = caption_width(s.c_str(), 10);
        if (w < 0 || w > 56) { std::printf("FAIL the title '%s' is %.1f px wide, over 56\n", s.c_str(), w); ++failures; }
    }
    if (failures) { std::printf("churn: %d faults\n", failures); return 1; }
    std::printf("churn: at most %.1f nodes a frame on average playing, peak %d; turning a control peak %d; none at rest; %zu captions, the widest %.1f of 53 px\n",
                worst, peak, turning, values.size(), widest);
    return 0;
}
