// Moth's cost on the OP-XY's screen, run by sdk.py test. Every control through
// 0..127 with a note held 3 s at 60 fps and its release, then the settings that
// load it most: each frame valid, every shown outline fit for the device (points
// 1/8 px apart, a closing edge of at least 1/4 px, no turn under 2.5 degrees), on
// average at most 20 nodes changed a frame while it plays, a low peak, and none
// at rest. A held note keeps the screen moving for as long as it is held, while
// the moth still sounds. It also walks each control in steps of 8 with a note
// held and fails if one moves nothing, and measures every caption shown.
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
moth::DisplayState state;
moth::Frame frame, last;
std::set<std::string> values, titles;

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
// The device's rules on an outline; 0 when it keeps them.
const char* outline_fault(const moth::Frame::path_type& path) {
    const i32 apart = unit / 8;
    for (u32 i = 0; i < moth::Frame::point_count; ++i) {
        const Point& a = path.points[i];
        const Point& b = path.points[(i + 1) % moth::Frame::point_count];
        const Point& c = path.points[(i + 2) % moth::Frame::point_count];
        const i32 dx = b.x - a.x, dy = b.y - a.y, ex = c.x - b.x, ey = c.y - b.y;
        if (dx > -apart && dx < apart && dy > -apart && dy < apart) return "points within 1/8 px";
        // Under 2.5 degrees of straight back: a stroke's corner there can run far past the point.
        const i64 cross = static_cast<i64>(dx) * ey - static_cast<i64>(dy) * ex, dot = static_cast<i64>(dx) * ex + static_cast<i64>(dy) * ey;
        if (dot < 0 && (cross < 0 ? -cross : cross) * 23 < -dot) return "a turn under 2.5 degrees";
    }
    const i64 cx = path.points[0].x - path.points[moth::Frame::point_count - 1].x, cy = path.points[0].y - path.points[moth::Frame::point_count - 1].y;
    if (cx * cx + cy * cy < (unit / 4) * (unit / 4)) return "a closing edge under 1/4 px";
    return 0;
}
struct Result { double mean; int peak, rest, late; };
// One chord of notes held for hold frames, then let go and left to go dark.
Result play(const double* c, u32 notes, int hold, bool retrigger, const char* name) {
    Input input;
    prepare(input, c);
    std::memset(&frame, 0, sizeof frame);
    moth::display_reset(state);
    moth::display(state, input, frame);
    for (int t = 0; t < 12; ++t) moth::display(state, input, frame);
    Result r = {0, 0, 0, 0};
    double sum = 0;
    input.note_serial += notes;
    input.held_notes = notes;
    for (int t = 0; t < hold + 480; ++t) {
        if (t == hold) { input.note_off_serial += input.held_notes; input.held_notes = 0; }
        if (retrigger && t < hold && t && t % 15 == 0) { ++input.note_serial; ++input.note_off_serial; }
        last = frame;
        moth::display(state, input, frame);
        if (!valid(frame)) { std::printf("FAIL %s, frame %d: not valid\n", name, t); ++failures; return r; }
        int changed = 0;
        for (u32 i = 0; i < moth::Frame::node_count; ++i) {
            bool moved = !same(frame.nodes[i], last.nodes[i]);
            if (i < moth::Frame::path_count && frame.paths[i].key != last.paths[i].key) moved = true;
            if (moved) ++changed;
        }
        for (u32 p = 0; p < moth::Frame::path_count; ++p)
            if (shown(frame.nodes[p])) {
                const char* fault = outline_fault(frame.paths[p]);
                if (fault) { std::printf("FAIL %s, frame %d: path %u has %s\n", name, t, p, fault); ++failures; return r; }
            }
        if (t < hold) sum += changed;
        if (changed > r.peak) r.peak = changed;
        if (t < hold && t >= hold * 2 / 3) r.late += changed;
    }
    for (int t = 0; t < 16; ++t) {
        for (u32 k = 0; k < moth::Frame::text_count; ++k) ((k & 7) < 4 ? titles : values).insert(frame.texts[k].chars);
        moth::display(state, input, frame);
    }
    // Long after the release, nothing moves.
    for (int t = 0; t < 600; ++t) moth::display(state, input, frame);
    last = frame;
    for (int t = 0; t < 60; ++t) {
        moth::display(state, input, frame);
        for (u32 i = 0; i < moth::Frame::node_count; ++i) if (!same(frame.nodes[i], last.nodes[i])) ++r.rest;
        last = frame;
    }
    r.mean = sum / hold;
    if (r.mean > 20) { std::printf("FAIL %s: %.1f nodes a frame\n", name, r.mean); ++failures; }
    if (r.peak > 40) { std::printf("FAIL %s: %d nodes in a frame\n", name, r.peak); ++failures; }
    if (r.rest) { std::printf("FAIL %s: %d nodes changed at rest\n", name, r.rest); ++failures; }
    return r;
}
}

int main() {
    const double clean[8] = {0.2, 0, 0.0938, 0, 0.7, 0, 0.5, 0};
    const char* names[8] = {"fold", "fm", "ratio", "strike", "glow", "bias", "mode", "drift"};
    double worst = 0;
    int peak = 0;
    for (int k = 0; k < 8; ++k)
        for (int v = 0; v < 128; ++v) {
            double c[8];
            std::memcpy(c, clean, sizeof c);
            c[k] = v / 127.0;
            char name[48];
            std::snprintf(name, sizeof name, "%s at %d", names[k], v);
            const Result r = play(c, 1 + static_cast<u32>(v % 8), 90, false, name);
            if (r.mean > worst) worst = r.mean;
            if (r.peak > peak) peak = r.peak;
        }
    const double heaviest[4][8] = {{1, 1, 1, 1, 0, 1, 1, 1}, {1, 1, 1, 0.5, 1, 1, 0, 1}, {0.55, 0.3, 0.6562, 0.85, 0.3, 0.2, 0.5, 1}, {1, 0.5, 0.53, 1, 0.02, 0.6, 0.5, 1}};
    for (int i = 0; i < 4; ++i)
        for (u32 notes = 1; notes <= 8; notes += 7)
            for (int again = 0; again < 2; ++again) {
                const Result r = play(heaviest[i], notes, 180, again != 0, "heaviest");
                if (r.mean > worst) worst = r.mean;
                if (r.peak > peak) peak = r.peak;
            }
    // The settings Bend's fault was seen on, and random settings, through the same
    // checks, a turn under 2.5 degrees among them.
    {
        const double seen[8] = {0.3125, 0.30, 0.40, 0, 0.6875, 0.68, 0.79, 0};
        for (u32 notes = 1; notes <= 8; notes += 7) play(seen, notes, 300, false, "the settings Max saw it on");
        u32 seed = 0x1234567;
        for (int n = 0; n < 1000; ++n) {
            double c[8];
            for (int k = 0; k < 8; ++k) {
                seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
                const u32 x = seed % 8;
                c[k] = x == 0 ? 0 : x == 1 ? 1 : ((seed >> 8) % 128) / 127.0;
            }
            char name[48];
            std::snprintf(name, sizeof name, "random %d", n);
            play(c, 1 + static_cast<u32>(n) % 8, 40, n % 2 != 0, name);
        }
    }
    // The animation lasts while a note is held, not a fixed time: at the defaults,
    // with no drift or strike, the last two seconds of a six-second note still
    // move the light; with reduced motion they don't.
    {
        const Result r = play(clean, 1, 360, false, "a held note");
        if (r.late < 20) { std::printf("FAIL a note held 6 s: %d changes in its last 2 s\n", r.late); ++failures; }
        std::printf("churn: a note held 6 s still changes %d nodes in its last 2 s\n", r.late);
    }
    // Each control moves the drawing while a note is held.
    for (int k = 0; k < 8; ++k) {
        Input input;
        prepare(input, clean);
        input.slots[moth::kFm] = q31(0.3);
        std::memset(&frame, 0, sizeof frame);
        moth::display_reset(state);
        moth::display(state, input, frame);
        input.note_serial = 1;
        input.held_notes = 1;
        int moved = 0;
        for (int v = 0; v < 128; v += 8) {
            input.slots[k] = q31(v / 127.0);
            for (int t = 0; t < 40; ++t) moth::display(state, input, frame);
            if (v) {
                bool any = false;
                for (u32 i = 12; i < moth::Frame::node_count; ++i) any = any || !same(frame.nodes[i], last.nodes[i]);
                for (u32 p = 0; p < moth::Frame::path_count; ++p) any = any || frame.paths[p].key != last.paths[p].key;
                if (any) ++moved;
            }
            last = frame;
        }
        if (!moved) { std::printf("FAIL %s moves nothing on screen\n", names[k]); ++failures; }
        std::printf("churn: %s moves the drawing in %d of 15 steps\n", names[k], moved);
    }
    // Every caption the controls can show.
    double widest = 0;
    for (const std::string& s : values) {
        const double w = caption_width(s.c_str(), 20);
        if (w < 0 || w > 53) { std::printf("FAIL the caption '%s' is %.1f px wide, over 53\n", s.c_str(), w); ++failures; }
        widest = w > widest ? w : widest;
    }
    for (const std::string& s : titles) {
        const double w = caption_width(s.c_str(), 10);
        if (w < 0 || w > 56) { std::printf("FAIL the title '%s' is %.1f px wide, over 56\n", s.c_str(), w); ++failures; }
    }
    // glow, finely: every caption it has.
    for (int v = 0; v <= 32767; v += 16) {
        char text[32];
        moth::glow_caption(text, sizeof text, v);
        const double w = caption_width(text, 20);
        if (w < 0 || w > 53) { std::printf("FAIL glow's caption '%s' is %.1f px wide, over 53\n", text, w); ++failures; break; }
    }
    if (failures) { std::printf("churn: %d faults\n", failures); return 1; }
    std::printf("churn: at most %.1f nodes a frame on average, peak %d, none at rest; %zu captions, the widest %.1f of 53 px\n", worst, peak,
                values.size(), widest);
    return 0;
}
