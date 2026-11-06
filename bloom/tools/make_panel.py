#!/usr/bin/env python3
"""Writes panel.svg.fragment, Bloom's screen artwork, with the node numbers
display.h names: run it after changing a shape here, and commit both.

The scene, in the stock look: a low sun, a silhouette. One flat light disc
sits on the horizon with four stripes knocked out of it in black; the meadow
of partials stands in front of it as dark stems and blossoms (each with its
haze twin and, with blur, a dark halo), and the vowel's two hills are black
mounds cut into it. Four status dots under the horizon carry the grey code:
hollow, dark, mid and light, left to right, each at its encoder's travel.

Stems are fixed 150 px ribbons hung from their tips, and the hills are fixed
mounds that rise out of the ground: a black band below the horizon hides what
reaches past it (it also sets the sun). The stripes are fixed black bars over
the sun, behind the meadow, so they never move. Every node is one closed
outline of distinct points, so every outline encloses area, and every solid
is authored with a fill. Each stroke width is the palette's; shapes are
authored half a stroke smaller so they draw at the board's sizes."""
import math
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
ID = sys.argv[1] if len(sys.argv) > 1 else "bloom"   # another prefix for a preview copy
FONT = 'font-family="UniversNextPro-Light"'
BASE = 200                    # the horizon (display.cpp: base)
PARTIALS = 24
HEADER, SUN, HILL, PARTIAL, DOT = 0, 8, 9, 11, 107   # display.h's first nodes
HILLS, DOTS = 2, 4
SUN_X, SUN_R = 240, 62        # display.cpp: sun_x
HILL_HIGH, HILL_HALF = 34, 40 # a hill at full depth, px: 0.45 octave either side at 88 px an octave
TRACK_X, TRACK_W, TRACK_Y = 30, 60, 212   # each dot's track: from 30 + 120 i, 60 px long (display.cpp)
STROKE = 0.5
G1, G2, G3, G4 = "#191919", "#4B4B4B", "#969696", "#CDCDCD"   # the grey code, encoders 1..4
EDGE, BASE_GREY = "#5A5A5A", "#262626"                         # no encoder

# Palette (display.cpp names them): fill, stroke, width.
PALETTE = [("#000000", "#000000", STROKE),   # 0 black
           (G4, G4, STROKE),                 # 1 the sun
           ("#000000", EDGE, 0.8),           # 2 a hill: a black mound, its edge dark
           (EDGE, EDGE, STROKE),             # 3 a stem and its blossoms
           (BASE_GREY, BASE_GREY, STROKE),   # 4 a halo
           ("#000000", G4, 1.0),             # 5 encoder 1's dot, hollow
           (G2, G2, STROKE),                 # 6 encoder 2's
           (G3, G3, STROKE),                 # 7 encoder 3's
           (G4, G4, STROKE)]                 # 8 encoder 4's
PALETTE += [(G4, G4, STROKE)] * (16 - len(PALETTE))


def num(v):
    return f"{v:.4f}".rstrip("0").rstrip(".")


def outline(points):
    """A closed outline, started where its longest edge closes it."""
    n = len(points)
    longest = max(range(n), key=lambda i: math.hypot(points[i][0] - points[i - 1][0], points[i][1] - points[i - 1][1]))
    points = points[longest:] + points[:longest]
    return "M" + " L".join(f"{num(x)},{num(y)}" for x, y in points) + " Z"


def rect(x0, y0, x1, y1):
    return outline([(x0, y0), (x1, y0), (x1, y1), (x0, y1)])


def node(i, d, fill, stroke, width=STROKE):
    return (f'<path id="{ID}_node_{i}" transform="matrix(1 0 0 1 640 320)" d="{d}" '
            f'fill="{fill}" stroke="{stroke}" stroke-width="{num(width)}"/>')


def disc(i, r, fill, stroke, width=STROKE):
    return (f'<circle id="{ID}_node_{i}" transform="matrix(1 0 0 1 640 320)" cx="0" cy="0" r="{num(r)}" '
            f'fill="{fill}" stroke="{stroke}" stroke-width="{num(width)}"/>')


def solid(d, grey):
    return f'<path d="{d}" fill="{grey}" stroke="none"/>'


out = [f'<g id="{ID}_panel">',
       "<!-- SDK two-page header: keep this block before machine artwork below y=38. -->",
       '<path d="M0,0 L480,0 L480,38 L0,38 Z" fill="#000000"/>']
bar = ["#000000", "#323232", "#4B4B4B", "#646464", "#7D7D7D", "#969696", "#AFAFAF", "#CDCDCD"]
for i, colour in enumerate(bar):   # nodes 0..7
    out.append(f'<path id="{ID}_node_{HEADER + i}" transform="matrix(1 0 0 1 {60 * i} 0)" '
               f'd="M0,0 L60,0 L60,20 L0,20 Z" fill="{colour}" stroke="{colour}" stroke-width="0.5"/>')
texts = []
for c in range(4):
    texts.append((5 + 120 * c, 10, "#CBCBCB" if c < 2 else "#000000", 10))
for c in range(4):
    texts.append((65 + 120 * c, 17.4, "#CBCBCB" if c < 2 else "#000000", 20))
texts += [(5 + 120 * c, 33, "#5A5A5A", 10) for c in range(4)] + [(65 + 120 * c, 33, "#707070", 13) for c in range(4)]
for i, (x, y, ink, size) in enumerate(texts):
    out.append(f'<text id="{ID}_text_{i}" transform="matrix(1 0 0 1 {num(x)} {num(y)})" fill="{ink}" {FONT} '
               f'font-size="{size}px">-</text>')

h = STROKE / 2
# Node 8: the sun, one flat light disc.
out.append(disc(SUN, SUN_R - h, G4, G4))
# Static: its four stripes, knocked out in black at fixed heights over the
# horizon, thickening toward it; the meadow stands in front of them.
for dy, height in ((36, 2), (27, 3), (17, 4), (7, 5)):
    out.append(solid(rect(SUN_X - SUN_R - 2, BASE - dy, SUN_X + SUN_R + 2, BASE - dy + height), "#000000"))
# Nodes 9 and 10: the vowel's hills, black mounds with a dark edge, one for each
# formant: the formant's lift (spectrum.h: bump) at full depth, 34 px high and
# 0.45 octave either side, standing on a foot below the horizon.
reach = 0.45 * 88
top = []
for j in range(41):
    x = -HILL_HALF + 2 * j
    d = min(1.0, abs(x) / reach)
    top.append((x, -(HILL_HIGH - h) * (1 - d * d) ** 2))
hill = top + [(HILL_HALF, 8), (-HILL_HALF, 8)]
for i in range(HILLS):
    out.append(node(HILL + i, outline(hill), "#000000", EDGE, 0.8))
# Nodes 11 + 4k .. 14 + 4k: partial k's stem (from its tip down), halo,
# blossom and twin. Drawn halos first, then stems, then blossoms, so no halo
# hides another partial's stem.
for k in range(PARTIALS):
    out.append(disc(PARTIAL + 4 * k + 1, 12, BASE_GREY, BASE_GREY))
for k in range(PARTIALS):
    out.append(node(PARTIAL + 4 * k, rect(-0.7 + h, 0, 0.7 - h, 150), EDGE, EDGE))
for k in range(PARTIALS):
    out.append(disc(PARTIAL + 4 * k + 2, 4, EDGE, EDGE))
    out.append(disc(PARTIAL + 4 * k + 3, 4, EDGE, EDGE))
# Static: below the horizon, black, over the stems' and hills' feet and the
# setting sun; then the horizon, one light solid line across the screen.
out.append(f'<path d="{rect(0, BASE + 0.5, 480, 222)}" fill="#000000" stroke="#000000" stroke-width="0.5"/>')
out.append(solid(rect(0, BASE, 480, BASE + 1.4), G4))
# Static: the dots' tracks; nodes 107..110: the dots, encoder 1's hollow.
for i in range(DOTS):
    x0 = TRACK_X + 120 * i
    out.append(solid(rect(x0, TRACK_Y - 0.5, x0 + TRACK_W, TRACK_Y + 0.5), BASE_GREY))
out.append(disc(DOT, 3, "#000000", G4, 1.0))
for i in range(1, DOTS):
    grey = (G1, G2, G3, G4)[i]
    out.append(disc(DOT + i, 4 - h, grey, grey))
for i, (fill, stroke, width) in enumerate(PALETTE):
    out.append(f'<circle id="{ID}_palette_{i}" cx="640" cy="0" r="1" fill="{fill}" stroke="{stroke}" '
               f'stroke-width="{num(width)}"/>')
out.append("</g>")
target = ROOT / "panel.svg.fragment" if len(sys.argv) < 3 else pathlib.Path(sys.argv[2])
target.write_text("\n".join(out) + "\n")
print("wrote", target)
