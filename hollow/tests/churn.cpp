// Hollow's cost on the OP-XY's screen, run by sdk.py test. Every control through
// 0..127 with a note at 60 fps, every preset and the heaviest settings at 1 and
// 8 notes: each frame valid, at most two outlines reloaded, on average at most
// 20 nodes changed a frame and a low peak while it plays, none at rest. Turning
// Body swaps a whole figure, so that frame's peak is held down too. It checks that
// each control moves the drawing, Sympathy included with one note and with none,
// and that every artwork path is one subpath of at most 64 points.
#include "../display.cpp"
#include <cstdio>
#include <cstring>

namespace {
using namespace opxy::draw;
int failures = 0;
hollow::DisplayState state;
hollow::Frame frame, last, other;

const double kDefaults[8] = {0.35, 0, 0.45, 0.52, 0, 0.5, 0, 0};
const char* const kNames[8] = {"breath", "body", "bright", "ring", "air", "vowel", "spread", "sympathy"};
// Breath, body, bright, ring, air, vowel, spread, sympathy, as in definition.json.
const double kPresets[7][8] = {{0.35, 0, 0.45, 0.52, 0, 0.5, 0, 0.5},     {0.55, 0, 0.3, 0.61, 0.2, 0.2, 0.5, 0.6},
                               {0.15, 1, 0.6, 0.75, 0, 0.5, 0, 0.7},      {0.85, 0.667, 0.35, 0.44, 0.3, 0.25, 0, 0.3},
                               {0.2, 0.333, 0.6, 0.67, 0, 0.5, 0.65, 0.5}, {0.25, 0, 0.55, 0.58, 0.1, 0.6, 1, 0.45},
                               {1, 0, 0.7, 0.3, 0.5, 0.8, 0.3, 0.2}};
const char* const kPresetNames[7] = {"Choir", "Dusk", "Bowed glass", "Bottle", "Strings", "Ensemble", "Whisper"};

opxy::i32 q31(double v) { return v >= 1 ? 0x7fffffff : static_cast<opxy::i32>(v * 2147483647.0 + 0.5); }
bool same(const Node& a, const Node& b) {
    return a.x == b.x && a.y == b.y && a.scale == b.scale && a.cos == b.cos && a.sin == b.sin && a.shade == b.shade &&
           a.paint == b.paint;
}
void prepare(Input& input, const double* c) {
    std::memset(&input, 0, sizeof input);
    input.track = 1;
    input.elapsed = 17;
    for (int k = 0; k < 8; ++k) input.slots[k] = q31(c[k]);
    for (int k = 0; k < 4; ++k) { input.parameters[k] = input.slots[k]; input.shown[k] = static_cast<opxy::u8>(k); }
}
void start(Input& input, const double* c) {
    prepare(input, c);
    std::memset(&frame, 0, sizeof frame);
    hollow::display_reset(state);
    hollow::display(state, input, frame);
}
// Runs one frame and counts what it changed.
int step(Input& input, int* outlines, const char* name, int t) {
    last = frame;
    hollow::display(state, input, frame);
    if (!valid(frame)) { std::printf("FAIL %s, frame %d: not valid\n", name, t); ++failures; }
    int changed = 0, reloads = 0;
    for (unsigned i = 0; i < hollow::Frame::node_count; ++i) {
        bool moved = !same(frame.nodes[i], last.nodes[i]);
        if (i < hollow::Frame::path_count && frame.paths[i].key != last.paths[i].key) {
            moved = true;
            ++reloads;
            if (!frame.paths[i].key) { std::printf("FAIL %s, frame %d: key 0\n", name, t); ++failures; }
        }
        if (moved) ++changed;
    }
    if (outlines) *outlines = reloads;
    return changed;
}

double worst = 0;
int top = 0, most_reloads = 0;
// A note held for `hold` frames at these settings, then its ring-out as far as `tail` frames.
void play(const double* c, int notes, int hold, int tail, bool to_rest, const char* name) {
    Input input;
    start(input, c);
    for (int t = 0; t < 8; ++t)
        if (step(input, 0, name, t)) { std::printf("FAIL %s: changes before a note\n", name); ++failures; break; }
    input.note_serial += notes;
    input.held_notes = notes;
    double sum = 0;
    int peak = 0, reloads = 0;
    for (int t = 0; t < hold; ++t) {
        int outlines = 0;
        const int changed = step(input, &outlines, name, t);
        sum += changed;
        peak = changed > peak ? changed : peak;
        reloads = outlines > reloads ? outlines : reloads;
    }
    const double mean = sum / hold;
    input.note_off_serial += notes;
    input.held_notes = 0;
    for (int t = 0; t < tail; ++t) {
        int outlines = 0;
        const int changed = step(input, &outlines, name, t);
        peak = changed > peak ? changed : peak;
        reloads = outlines > reloads ? outlines : reloads;
    }
    if (mean > 20) { std::printf("FAIL %s: %.1f nodes a frame\n", name, mean); ++failures; }
    if (peak > 24) { std::printf("FAIL %s: peak %d nodes\n", name, peak); ++failures; }
    if (reloads > 2) { std::printf("FAIL %s: %d outlines reloaded in a frame\n", name, reloads); ++failures; }
    worst = mean > worst ? mean : worst;
    top = peak > top ? peak : top;
    most_reloads = reloads > most_reloads ? reloads : most_reloads;
    if (to_rest) {
        for (int t = 0; t < 3600; ++t) hollow::display(state, input, frame);
        int moved = 0;
        for (int t = 0; t < 30; ++t) moved += step(input, 0, name, t);
        if (moved) { std::printf("FAIL %s: %d nodes changed at rest\n", name, moved); ++failures; }
    }
}

// How far two frames differ, in pixels on screen; a part shown in one only counts 8.
double apart(const hollow::Frame& a, const hollow::Frame& b) {
    double far = 0;
    for (unsigned i = 12; i < hollow::Frame::node_count; ++i) {
        const Node& n = a.nodes[i];
        const Node& m = b.nodes[i];
        const bool sn = n.x < hidden_x * unit, sm = m.x < hidden_x * unit;
        if (sn != sm) { far = far > 8 ? far : 8; continue; }
        if (!sn) continue;
        const double dx = (n.x - m.x) / 1024.0, dy = (n.y - m.y) / 1024.0, ds = (n.scale - m.scale) / 1024.0 * 10;
        const double d = dx * dx + dy * dy + ds * ds;
        far = d > far * far ? __builtin_sqrt(d) : far;
    }
    for (unsigned p = 0; p < hollow::Frame::path_count; ++p) {
        if (a.nodes[p].x >= hidden_x * unit && b.nodes[p].x >= hidden_x * unit) continue;
        if ((a.nodes[p].x < hidden_x * unit) != (b.nodes[p].x < hidden_x * unit)) { far = far > 8 ? far : 8; continue; }
        for (unsigned i = 0; i < hollow::Frame::point_count; ++i) {
            const double dx = (a.paths[p].points[i].x - b.paths[p].points[i].x) / 512.0;
            const double dy = (a.paths[p].points[i].y - b.paths[p].points[i].y) / 512.0;
            const double d = dx * dx + dy * dy;
            far = d > far * far ? __builtin_sqrt(d) : far;
        }
    }
    return far;
}
// The drawing `frames` frames into `notes` notes at these settings.
void pose(const double* c, int notes, int frames, hollow::Frame& out) {
    Input input;
    start(input, c);
    for (int t = 0; t < 30; ++t) hollow::display(state, input, frame);
    input.note_serial += notes;
    input.held_notes = notes;
    for (int t = 0; t < frames; ++t) hollow::display(state, input, frame);
    out = frame;
}
// Compares a control at 0 and 1, and in steps of 8 for dead stretches.
double moves(const double* base, int k, int notes, int frames, int* dead) {
    double c[8];
    std::memcpy(c, base, sizeof c);
    c[k] = 0;
    pose(c, notes, frames, other);
    static hollow::Frame low, high;
    low = other;
    c[k] = 1;
    pose(c, notes, frames, high);
    const double far = apart(low, high);
    if (dead) {
        *dead = 0;
        for (int v = 8; v < 128; v += 8) {
            c[k] = v / 127.0;
            pose(c, notes, frames, high);
            c[k] = (v - 8) / 127.0;
            pose(c, notes, frames, low);
            if (apart(low, high) == 0) ++*dead;
        }
    }
    return far;
}

// Every path in the artwork: one subpath, at most 64 points.
void check_artwork() {
    char path[512];
    std::snprintf(path, sizeof path, "%s", __FILE__);
    char* slash = std::strrchr(path, '/');
    if (slash) *slash = 0;
    else std::snprintf(path, sizeof path, ".");
    std::strncat(path, "/../panel.svg.fragment", sizeof path - std::strlen(path) - 1);
    std::FILE* file = std::fopen(path, "rb");
    if (!file) { std::printf("FAIL can't read %s\n", path); ++failures; return; }
    static char text[70000];
    const std::size_t size = std::fread(text, 1, sizeof text - 1, file);
    std::fclose(file);
    text[size] = 0;
    int paths = 0, longest = 0;
    for (const char* at = std::strstr(text, " d=\""); at; at = std::strstr(at, " d=\"")) {
        at += 4;
        int moves_to = 0, points = 0;
        for (; *at && *at != '"'; ++at) {
            if (*at == 'M') ++moves_to;
            if (*at == ',') ++points;
        }
        ++paths;
        longest = points > longest ? points : longest;
        if (moves_to != 1 || points > 64) {
            std::printf("FAIL artwork path %d: %d subpaths, %d points\n", paths, moves_to, points);
            ++failures;
        }
    }
    std::printf("churn: %d artwork paths, one subpath each, at most %d points, %zu bytes\n", paths, longest, size);
}
}

int main() {
    check_artwork();
    // Every control through its travel with a note.
    for (int k = 0; k < 8; ++k)
        for (int v = 0; v < 128; ++v) {
            double c[8];
            std::memcpy(c, kDefaults, sizeof c);
            c[k] = v / 127.0;
            char name[48];
            std::snprintf(name, sizeof name, "%s at %d", kNames[k], v);
            play(c, 1, 60, 30, v % 42 == 0, name);
        }
    // The presets, and the heaviest settings on each body.
    const int counts[2] = {1, 8};
    for (int n = 0; n < 2; ++n) {
        for (int i = 0; i < 7; ++i) play(kPresets[i], counts[n], 300, 120, true, kPresetNames[i]);
        for (int b = 0; b < 4; ++b) {
            const double heaviest[8] = {1, b / 3.0, 1, 1, 1, 1, 1, 1};
            play(heaviest, counts[n], 300, 120, true, "heaviest");
            const double loud[8] = {1, b / 3.0, 1, 0.3, 1, 0.5, 0.5, 0.5};
            play(loud, counts[n], 300, 120, true, "heavy, short ring");
        }
    }
    // Turning each control end to end, at rest and playing: Body swaps whole figures.
    int turning = 0;
    for (int notes = 0; notes < 2; ++notes)
        for (int k = 0; k < 8; ++k) {
            Input input;
            double c[8];
            std::memcpy(c, kDefaults, sizeof c);
            c[k] = 0;
            start(input, c);
            input.note_serial += notes;
            input.held_notes = notes;
            for (int t = 0; t < 30; ++t) hollow::display(state, input, frame);
            for (int pass = 0; pass < 2; ++pass)
                for (int v = 0; v < 128; ++v) {
                    input.slots[k] = q31((pass ? 127 - v : v) / 127.0);
                    if (k < 4) input.parameters[k] = input.slots[k];
                    const int changed = step(input, 0, kNames[k], v);
                    turning = changed > turning ? changed : turning;
                }
        }
    if (turning > 32) { std::printf("FAIL turning a control: peak %d nodes\n", turning); ++failures; }
    // However Body was turned, the drawing settles exactly as it would have drawn from fresh.
    {
        const double stops[6] = {1, 0, 0.5, 0.3, 0.7, 0.34};
        Input input;
        double c[8];
        std::memcpy(c, kDefaults, sizeof c);
        c[7] = 0.6;
        start(input, c);
        for (int i = 0; i < 6; ++i) {
            input.slots[1] = q31(stops[i]);
            input.parameters[1] = input.slots[1];
            for (int t = 0; t < 4; ++t) hollow::display(state, input, frame);     // caught mid swap by the next stop
            if (i % 2) for (int t = 0; t < 60; ++t) hollow::display(state, input, frame);
            if (i % 2 == 0) continue;
            other = frame;
            Input fresh;
            c[1] = stops[i];
            start(fresh, c);
            int differs = 0;
            for (unsigned n = 12; n < hollow::Frame::node_count; ++n) differs += !same(frame.nodes[n], other.nodes[n]);
            for (unsigned k = 0; k < hollow::Frame::path_count; ++k)
                if (frame.nodes[k].x < hidden_x * unit && std::memcmp(frame.paths[k].points, other.paths[k].points, sizeof frame.paths[k].points)) ++differs;
            if (differs) { std::printf("FAIL Body at %.2f: %d parts differ from a fresh drawing\n", stops[i], differs); ++failures; }
        }
    }
    // Reduced motion: still valid, and still at rest.
    {
        const double heaviest[8] = {1, 0.5, 1, 1, 1, 1, 1, 1};
        Input input;
        start(input, heaviest);
        input.reduced_motion = 1;
        input.note_serial++;
        input.held_notes = 1;
        hollow::display(state, input, frame);
        hollow::display(state, input, frame);
        int moved = 0;
        for (int t = 0; t < 60; ++t) moved += step(input, 0, "reduced motion", t);
        if (moved) { std::printf("FAIL reduced motion: %d nodes changed\n", moved); ++failures; }
    }
    // Each control moves the drawing with a note held; Sympathy and the shape
    // controls with none, too. A step that moves nothing is a dead stretch.
    for (int k = 0; k < 8; ++k) {
        int dead = 0;
        const double far = moves(kDefaults, k, 1, 60, &dead);
        if (far < 3) { std::printf("FAIL %s moves the drawing only %.1f px\n", kNames[k], far); ++failures; }
        if (dead > 2) { std::printf("FAIL %s: %d of 15 steps move nothing\n", kNames[k], dead); ++failures; }
        std::printf("churn: %s moves the drawing %.1f px with a note, %d of 15 steps dead\n", kNames[k], far, dead);
    }
    const int at_rest[3] = {1, 5, 7};
    for (int i = 0; i < 3; ++i) {
        const double far = moves(kDefaults, at_rest[i], 0, 30, 0);
        if (far < 2) { std::printf("FAIL %s moves the drawing only %.1f px at rest\n", kNames[at_rest[i]], far); ++failures; }
        std::printf("churn: %s moves the drawing %.1f px at rest\n", kNames[at_rest[i]], far);
    }
    if (failures) { std::printf("churn: %d faults\n", failures); return 1; }
    std::printf("churn: at most %.1f nodes a frame on average, peak %d, %d outlines a frame, none at rest; turning a control peaks at %d\n",
                worst, top, most_reloads, turning);
    return 0;
}
