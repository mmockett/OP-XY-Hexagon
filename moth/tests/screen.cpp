// Moth's screen checks, compiled for the host and run by
//   python sdk.py test --machine <folder>
// Every frame is valid and every outline has distinct points; the screen is
// still when nothing sounds; a strike lights the wings and they fade at
// glow's pace; reduced motion, a reset and a track change show the end
// state at once; drift sways the moth while it sounds, a group of pieces a
// frame, and no frame redraws much of the moth.
#include "../display.cpp"
#include <cstdio>
#include <cstring>

namespace {
using opxy::i32;
using opxy::u32;

int failures = 0;
void check(const char* what, bool ok) {
    if (!ok) { ++failures; std::printf("FAIL %s\n", what); }
}

moth::Frame frame;
moth::DisplayState state;

struct Player {
    opxy::draw::Input input;
    Player(const double* controls, u32 page) {
        std::memset(&input, 0, sizeof input);
        for (u32 i = 0; i < 8; ++i) input.slots[i] = static_cast<i32>(controls[i] * 2147483647.0);
        for (u32 i = 0; i < 4; ++i) {
            input.shown[i] = static_cast<opxy::u8>(i + 4 * page);
            input.parameters[i] = input.slots[i + 4 * page];
        }
        input.track = 1;
    }
    void hold(u32 notes) {
        if (notes > input.held_notes) input.note_serial += notes - input.held_notes;
        else input.note_off_serial += input.held_notes - notes;
        input.held_notes = notes;
    }
    // One frame, 17 ms after the last; whether it is valid with distinct outline points.
    bool tick(u32 elapsed = 17) {
        input.elapsed = static_cast<i32>(elapsed);
        moth::display(state, input, frame);
        bool ok = opxy::draw::valid(frame);
        for (u32 p = 0; p < moth::Frame::path_count; ++p)
            for (u32 i = 0; i < moth::Frame::point_count; ++i)
                for (u32 j = i + 1; j < moth::Frame::point_count; ++j)
                    ok = ok && (frame.paths[p].points[i].x != frame.paths[p].points[j].x ||
                                frame.paths[p].points[i].y != frame.paths[p].points[j].y);
        return ok;
    }
};

bool same(const moth::Frame& a, const moth::Frame& b) { return std::memcmp(&a, &b, sizeof a) == 0; }
bool shown(u32 node) { return frame.nodes[node].x < opxy::draw::hidden_x * opxy::draw::unit; }

void frames() {
    // Each preset's controls, struck and held, then released, on both pages.
    const double presets[4][8] = {{0.2, 0, 0.09, 0, 0.7, 0, 0.5, 0},
                                  {0.55, 0.3, 0.67, 0.85, 0.3, 0.2, 0.5, 0.25},
                                  {1, 1, 1, 1, 0, 1, 1, 1},
                                  {0, 0, 0, 0, 1, 0, 0, 0}};
    bool valid = true;
    for (u32 p = 0; p < 4; ++p)
        for (u32 page = 0; page < 2; ++page) {
            moth::display_reset(state);
            Player player(presets[p], page);
            for (u32 f = 0; f < 600; ++f) {
                if (f == 10) player.hold(3);
                if (f == 60) player.hold(8);
                if (f == 200) player.hold(0);
                valid = valid && player.tick();
            }
        }
    check("every frame is valid and every outline's points distinct", valid);
}

void stillness() {
    const double ping[8] = {0.55, 0.3, 0.67, 0.85, 0.3, 0.2, 0.5, 0.25};
    moth::display_reset(state);
    Player player(ping, 0);
    for (u32 f = 0; f < 30; ++f) player.tick();
    static moth::Frame before;
    before = frame;
    player.tick();
    check("idle: nothing moves while nothing sounds", same(before, frame));
    check("idle: the lit edges and the light crossing to the cell are dark",
          !shown(2) && !shown(3) && !shown(moth::kBodyFirst + 8));
    // A strike lights the wings and the voice's eyespot and sheds scales; then it all fades.
    player.hold(1);
    player.tick();
    player.tick();
    bool scales = false;
    for (u32 s = 0; s < moth::kScaleCount; ++s) scales = scales || shown(moth::kScaleFirst + s);
    check("a strike lights the wings and its eyespot", shown(2) && shown(3) && frame.nodes[moth::kEyeFirst + 1].shade > 4);
    check("a strike knocks scales loose", scales);
    player.hold(0);
    for (u32 f = 0; f < 240; ++f) player.tick();
    before = frame;
    player.tick();
    check("after the release fades, the screen is still again", same(before, frame) && !shown(2));
}

void snapping() {
    // Reduced motion shows a held note's end state at once, and stays still.
    const double pad[8] = {0.45, 0.15, 0.41, 0, 0.95, 0.3, 0, 0.4};
    moth::display_reset(state);
    Player player(pad, 0);
    player.input.reduced_motion = 1;
    player.tick();
    player.hold(2);
    player.tick();
    check("reduced motion: a held note lights the wings at once", shown(2) && frame.nodes[2].shade >= 8);
    static moth::Frame before;
    before = frame;
    player.tick();
    check("reduced motion: still while held", same(before, frame));
    bool scales = false;
    for (u32 s = 0; s < moth::kScaleCount; ++s) scales = scales || shown(moth::kScaleFirst + s);
    check("reduced motion: no scales", !scales);
    // A reset while a note is held shows it lit at once, without a strike.
    player.input.reduced_motion = 0;
    moth::display_reset(state);
    player.tick();
    check("after a reset, a held note is lit at once", shown(2) && frame.nodes[2].shade >= 8);
    before = frame;
    player.tick();
    bool moved = false;
    for (u32 s = 0; s < moth::kScaleCount; ++s) moved = moved || shown(moth::kScaleFirst + s);
    check("a reset is not a strike: no scales", !moved);
    // A track change does the same.
    player.input.track = 2;
    player.tick();
    check("a track change shows the end state", shown(2));
}

// Nodes that differ between two frames.
u32 changed(const moth::Frame& a, const moth::Frame& b) {
    u32 n = 0;
    for (u32 k = 0; k < moth::Frame::node_count; ++k) n += std::memcmp(&a.nodes[k], &b.nodes[k], sizeof a.nodes[k]) != 0;
    return n;
}

void swaying() {
    // A held pad at full drift: the moth sways, a step at a time, and each
    // frame redraws only a few pieces.
    const double pad[8] = {0.45, 0.15, 0.41, 0, 0.95, 0.3, 0, 1};
    moth::display_reset(state);
    Player player(pad, 0);
    player.tick();
    player.hold(2);
    for (u32 f = 0; f < 30; ++f) player.tick();
    static moth::Frame before;
    u32 peak = 0, total = 0, steps = 0;
    i32 low = 1 << 30, high = -(1 << 30);
    for (u32 f = 0; f < 600; ++f) {
        before = frame;
        player.tick();
        const u32 n = changed(before, frame);
        peak = n > peak ? n : peak;
        total += n;
        steps += frame.nodes[moth::kBodyFirst + 9].y != before.nodes[moth::kBodyFirst + 9].y ||
                 frame.nodes[moth::kBodyFirst + 9].sin != before.nodes[moth::kBodyFirst + 9].sin;
        low = frame.nodes[moth::kBodyFirst + 9].sin < low ? frame.nodes[moth::kBodyFirst + 9].sin : low;
        high = frame.nodes[moth::kBodyFirst + 9].sin > high ? frame.nodes[moth::kBodyFirst + 9].sin : high;
    }
    check("drift sways the moth while it sounds", steps >= 4 && high > low);
    check("the sway moves in steps, a few a second", steps <= 10 * 60);
    check("no frame redraws more than a group of pieces", peak <= 16);
    check("on average a held, swaying pad redraws under six pieces a frame", total < 6 * 600);
    // Without drift, a held note is still.
    const double still[8] = {0.45, 0.15, 0.41, 0, 0.95, 0.3, 0, 0};
    moth::display_reset(state);
    Player quiet(still, 0);
    quiet.tick();
    quiet.hold(2);
    for (u32 f = 0; f < 120; ++f) quiet.tick();
    before = frame;
    quiet.tick();
    check("without drift, a held harmonic note is still", same(before, frame));
}

void header() {
    const double ping[8] = {0.55, 0.3, 0.67, 0.85, 0.3, 0.2, 0.5, 0.25};
    moth::display_reset(state);
    Player player(ping, 0);
    player.tick();
    auto is = [](const char* text, const char* want) { return std::strcmp(text, want) == 0; };
    check("header: fold 55", is(frame.texts[4].chars, "55"));
    check("header: fm 0.36", is(frame.texts[5].chars, "0.36"));
    check("header: ratio 3.50", is(frame.texts[6].chars, "3.50"));
    check("header: strike 85", is(frame.texts[7].chars, "85"));
    check("header: glow 180ms", is(frame.texts[12].chars, "180ms"));
    check("header: mode both", is(frame.texts[14].chars, "both"));
    check("header: titles", is(frame.texts[0].chars, "fold") && is(frame.texts[8].chars, "glow"));
}
}

int main() {
    frames();
    stillness();
    snapping();
    swaying();
    header();
    if (failures) std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
