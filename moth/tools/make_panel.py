#!/usr/bin/env python3
"""Writes panel.svg.fragment, Moth's screen artwork, and wing.h, the wing's
geometry display.cpp draws from: run it after changing a shape here, and
commit all three.

A moth, wings spread, centred at (240, 124), drawn in line. Each wing is its
margin, the folded waveform, as one outline of the root's two points and 62
margin points (display.cpp reshapes the four outlines: each wing's edge, and
inside it the lit edge, the gate's light, which grows toward the edge and
brightens as the vactrol lights). Fine static veins and an eyespot a voice
(a ring and a pupil) sit inside the wings, short of the deepest scallop, so
they stay inside the edge at every fold. The body is a vactrol: the LED's
leads bent up as antennae, the LED as head and thorax, the photocell's
serpentine track as the abdomen. Over it all, the scales a strike knocks
loose; in the corner, a specimen label (static artwork). Node numbers are the
ones display.h names. Every node is one closed outline of distinct points (or
a circle), so every outline encloses area; lines are thin closed ribbons."""
import math
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
ID = sys.argv[1] if len(sys.argv) > 1 else "moth"   # another prefix for a preview copy
FONT = 'font-family="UniversNextPro-Light"'

CX, CY = 240, 124
MARGIN = 62               # margin points per wing; with the two root points, an outline's 64
POINTS = 64
VEINS = [0.06, 0.2, 0.35, 0.5, 0.65, 0.8, 0.94]
VEIN_REACH = 0.76         # where the veins end: inside the deepest scallop (the margin's reach is 0.78 to 1)
SPOTS = [(0.28, 0.6), (0.47, 0.64), (0.72, 0.6), (0.87, 0.52)]   # along the margin, and the share of its reach
REST = 0.89               # the margin's mean reach, where the eyespots sit
DUST = 8

# Nodes (display.h names the same numbers).
PATHS = 0                 # 0, 1: dark edges (left, right); 2, 3: lit edges
HEADER = 4                # eight bar nodes
VEIN = 12                 # seven a wing, left then right
EYE = 26                  # eight eyespots, two nodes each: ring, pupil (odd voices left, even right)
BODY = 42                 # fourteen parts of the vactrol
DUST_FIRST = 56

GREYS = ['#000000', '#0d0d0d', '#141414', '#1c1c1c', '#262626', '#333333', '#404040', '#4d4d4d',
         '#5c5c5c', '#6e6e6e', '#808080', '#999999', '#b3b3b3', '#cccccc', '#e6e6e6', '#ffffff']


def num(v, digits=2):
    s = f"{v:.{digits}f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def outline(points, digits=2):
    pts = [(num(x, digits), num(y, digits)) for x, y in points]
    assert len(set(pts)) == len(pts), "repeated point"
    return "M" + " L".join(f"{x},{y}" for x, y in pts) + " Z"


def node(i, d, fill, stroke=None, width=0.25, at=None):
    transform = f' transform="matrix(1 0 0 1 {num(at[0])} {num(at[1])})"' if at else ""
    return (f'<path id="{ID}_node_{i}"{transform} d="{d}" fill="{fill}" stroke="{stroke or fill}" '
            f'stroke-width="{num(width)}"/>')


def ribbon(points, w0, w1=None):
    """A closed ribbon along a polyline, as the board draws a line."""
    n, left, right = len(points), [], []
    for i in range(n):
        a, b = points[max(0, i - 1)], points[min(n - 1, i + 1)]
        dx, dy = b[0] - a[0], b[1] - a[1]
        length = math.hypot(dx, dy) or 1
        w = (w0 if w1 is None else w0 + (w1 - w0) * i / max(1, n - 1)) / 2
        left.append((points[i][0] - dy / length * w, points[i][1] + dx / length * w))
        right.append((points[i][0] + dy / length * w, points[i][1] - dx / length * w))
    return left + right[::-1]


def arc(cx, cy, r, a0, a1, n):
    return [(cx + r * math.cos(a0 + (a1 - a0) * s / n), cy + r * math.sin(a0 + (a1 - a0) * s / n)) for s in range(n + 1)]


def ellipse(cx, cy, rx, ry, n):
    return [(cx + rx * math.cos(2 * math.pi * s / n), cy + ry * math.sin(2 * math.pi * s / n)) for s in range(n)]


# The left wing's resting silhouette, round its root: a long forewing with a
# near-straight costa and a squared apex, the hindwing tucked under it. A
# Catmull-Rom curve through these points (the board's), sampled evenly along
# the margin so the folds fall evenly along the edge.
P = [(-4, -13), (-26, -36), (-70, -60), (-118, -74), (-158, -79), (-176, -74), (-176, -58), (-160, -30),
     (-136, -4), (-122, 10), (-130, 30), (-126, 52), (-106, 70), (-76, 79), (-46, 74), (-22, 56), (-8, 32), (-4, 13)]


def margin():
    fine = []
    for i in range(len(P) - 1):
        p0, p1, p2, p3 = P[max(0, i - 1)], P[i], P[i + 1], P[min(len(P) - 1, i + 2)]
        for j in range(24):
            t = j / 24
            t2, t3 = t * t, t * t * t
            fine.append(tuple(0.5 * (2 * p1[k] + (-p0[k] + p2[k]) * t + (2 * p0[k] - 5 * p1[k] + 4 * p2[k] - p3[k]) * t2 +
                                     (-p0[k] + 3 * p1[k] - 3 * p2[k] + p3[k]) * t3) for k in (0, 1)))
    fine.append(P[-1])
    length = [0]
    for i in range(1, len(fine)):
        length.append(length[-1] + math.hypot(fine[i][0] - fine[i - 1][0], fine[i][1] - fine[i - 1][1]))
    total = length[-1]
    joint = length[9 * 24] / total   # the tornus, where hindwing meets forewing
    out, j = [], 0
    for i in range(MARGIN):
        want = total * i / (MARGIN - 1)
        while j < len(fine) - 2 and length[j + 1] < want:
            j += 1
        w = min(1, (want - length[j]) / ((length[j + 1] - length[j]) or 1))
        out.append((fine[j][0] + (fine[j + 1][0] - fine[j][0]) * w, fine[j][1] + (fine[j + 1][1] - fine[j][1]) * w))
    return out, joint


WING, JOINT = margin()


def at(points, u):
    f = u * (len(points) - 1)
    i = min(len(points) - 2, int(f))
    w = f - i
    return (points[i][0] + (points[i + 1][0] - points[i][0]) * w, points[i][1] + (points[i + 1][1] - points[i][1]) * w)


def mirror(points):
    return [(-x, y) for x, y in points]


# Veins: from the root toward the margin, authored in place and ending at
# VEIN_REACH of it, so no fold brings the edge in past them.
def vein_points(u):
    tip = at([(x * VEIN_REACH, y * VEIN_REACH) for x, y in WING], u)
    base = (-3, (u - 0.5) * 22)
    root = (base[0] + (tip[0] - base[0]) * 0.16, base[1] + (tip[1] - base[1]) * 0.16)
    dx, dy = tip[0] - root[0], tip[1] - root[1]
    length = math.hypot(dx, dy)
    nx, ny = -dy / length, dx / length
    end = (root[0] + dx * 0.97, root[1] + dy * 0.97)
    return [(root[0] + nx * 0.6, root[1] + ny * 0.6), (end[0] + nx * 0.2, end[1] + ny * 0.2),
            (end[0] - nx * 0.2, end[1] - ny * 0.2), (root[0] - nx * 0.6, root[1] - ny * 0.6)]


# Eyespots: each at its margin point, at its share of the margin's mean reach.
EYES = []
for u, f in SPOTS:
    i = round(u * (MARGIN - 1))
    EYES.append((WING[i][0] * REST * f, WING[i][1] * REST * f))


out = [f'<g id="{ID}_panel">']
out += ["<!-- SDK two-page header: keep this block before machine artwork below y=38. -->",
       '<path d="M0,0 L480,0 L480,38 L0,38 Z" fill="#000000"/>']
bar = ["#000000", "#323232", "#4B4B4B", "#646464", "#7D7D7D", "#969696", "#AFAFAF", "#CDCDCD"]
for i, colour in enumerate(bar):
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

# The edges: the wing's outline at rest, the root's two points and the margin
# (display.cpp places them at scale 2, from the wing's middle, so the 0.5 px
# stroke draws a 1 px line).
for p in range(4):
    side = p & 1
    mid = 90 * (1 if side else -1)
    pts = [(-3, -13)] + [(x, y) for x, y in WING] + [(-3, 13)]
    if side:
        pts = mirror(pts)
    rel = [((x - mid) / 2, y / 2) for x, y in pts]
    d = "M" + " L".join(f"{num(x, 4)},{num(y, 4)}" for x, y in rel) + " Z"
    assert len(rel) == POINTS
    out.append(f'<path id="{ID}_node_{PATHS + p}" transform="matrix(1 0 0 1 {CX + mid} {CY})" d="{d}" fill="none" '
               f'stroke="{GREYS[9] if p < 2 else GREYS[6]}" stroke-width="0.5" stroke-linejoin="round"/>')

# Veins, fine lines in place.
for side in range(2):
    for v, u in enumerate(VEINS):
        pts = vein_points(u)
        out.append(node(VEIN + len(VEINS) * side + v, outline(mirror(pts) if side else pts), GREYS[6], width=0.1, at=(CX, CY)))

# Eyespots: a ring (a circle drawn at scale 2, stroked: a 1 px line) and the pupil.
for e in range(8):
    j, side = e // 2, e % 2
    x, y = EYES[j]
    at_x, at_y = num(CX - x if side else CX + x), num(CY + y)
    out.append(f'<circle id="{ID}_node_{EYE + 2 * e}" transform="matrix(1 0 0 1 {at_x} {at_y})" cx="0" cy="0" r="2.2" '
               f'fill="none" stroke="{GREYS[7]}" stroke-width="0.5"/>')
    out.append(f'<circle id="{ID}_node_{EYE + 2 * e + 1}" transform="matrix(1 0 0 1 {at_x} {at_y})" cx="0" cy="0" r="3" '
               f'fill="{GREYS[4]}" stroke="{GREYS[4]}" stroke-width="0.25"/>')


# The body: the vactrol, authored round the moth's centre.
def y(v):
    return v - CY


body = []
# 0, 1: the LED's leads as antennae, the anode longer, as LEDs come.
body.append((ribbon([(-1.8, y(96)), (-2.6, y(84)), (-9, y(70)), (-21, y(56)), (-27, y(47))], 0.9), GREYS[10]))
body.append((ribbon([(1.8, y(96)), (2.6, y(86)), (9, y(74)), (19, y(62)), (24, y(55))], 0.9), GREYS[10]))


def led(top, ledge, flange, outer):
    """The LED's outline: its flange, then the case down to the dome."""
    return ([(-flange, y(top)), (flange, y(top)), (flange, y(ledge)), (outer, y(ledge))] +
            arc(0, y(111.5), outer, 0, math.pi, 14) + [(-outer, y(ledge)), (-flange, y(ledge))])


body.append((led(96, 99, 6.5, 5.5), GREYS[9]))              # 2: flange and case
body.append((led(96.7, 98.3, 5.7, 4.7), GREYS[2]))          # 3: the epoxy, lit by the LED
body.append(([(-2.4, y(96.7)), (-1.4, y(96.7)), (-1.4, y(105.7)), (-2.4, y(105.7))], GREYS[7]))  # 4: the post
body.append(([(1, y(96.7)), (2.2, y(96.7)), (2.2, y(103)), (3.6, y(105)), (3.6, y(108.6)), (-0.4, y(108.6)),
              (-0.4, y(105)), (1, y(103))], GREYS[7]))       # 5: the anvil and its cup
body.append(([(0.7, y(106.4)), (2.5, y(106.4)), (2.5, y(108)), (0.7, y(108))], GREYS[6]))  # 6: the die
body.append((ribbon([(-1.9, y(105.7)), (-1.2, y(107.8)), (0.2, y(108.4)), (1.2, y(107.4))], 0.3), GREYS[8]))  # 7: bond wire
body.append(([(-3, y(116.6)), (3, y(116.6)), (6.5, y(122.4)), (-6.5, y(122.4))], GREYS[4]))  # 8: the light crossing
body.append((ellipse(0, y(148), 7.4, 26, 40), GREYS[8]))    # 9: the photocell's case
body.append((ellipse(0, y(148), 6.6, 25.2, 40), GREYS[3]))  # 10: its window
track = []
for r in range(11):
    ty = 128 + r * 4
    w = 5.6 * math.sqrt(max(0, 1 - ((ty - 148) / 25.2) ** 2)) - 1.2
    track += [(w, y(ty)), (-w, y(ty))] if r % 2 else [(-w, y(ty)), (w, y(ty))]
body.append((ribbon(track, 1.1), GREYS[5]))                 # 11: the CdS track, a serpentine
body.append((ribbon([(-2.5, y(173.5)), (-3, y(184)), (-4.5, y(192))], 0.8), GREYS[6]))  # 12, 13: its leads
body.append((ribbon([(2.5, y(173.5)), (3, y(184)), (4.5, y(192))], 0.8), GREYS[6]))
for b, (pts, ink) in enumerate(body):
    out.append(node(BODY + b, outline(pts), ink, width=0.1, at=(CX, CY)))

# The specimen label, pinned in the corner: static artwork, not a readout. The
# box leaves room for the OP-XY's Univers, which sets the name wider than the
# browser's stand-in fonts.
out.append(f'<path d="{outline([(384, 200), (472, 200), (472, 214), (384, 214)])}" fill="none" stroke="{GREYS[5]}" stroke-width="0.6"/>')
out.append(f'<text transform="matrix(1 0 0 1 388 209.4)" fill="{GREYS[8]}" {FONT} font-size="6.5px">Phalaena vactrolis</text>')

# Scales knocked loose: a pool, parked off screen until a strike sheds them.
scale_d = outline(ellipse(0, 0, 1.6, 0.7, 10))
for d in range(DUST):
    out.append(f'<path id="{ID}_node_{DUST_FIRST + d}" transform="matrix(1 0 0 1 640 320)" d="{scale_d}" '
               f'fill="{GREYS[8]}" stroke="{GREYS[8]}" stroke-width="0.25"/>')

# Palette: the board's sixteen greys, each its own fill and stroke. The
# stroke is 0.5 px: a filled shape grows by a quarter pixel, and an edge,
# drawn at scale 2, is a 1 px line.
for i, grey in enumerate(GREYS):
    out.append(f'<circle id="{ID}_palette_{i}" cx="640" cy="0" r="1" fill="{grey}" stroke="{grey}" stroke-width="0.5"/>')
out.append("</g>")

text = "\n".join(out) + "\n"
target = ROOT / "panel.svg.fragment" if ID == "moth" else pathlib.Path(sys.argv[2])
target.write_text(text)


def q10(v):
    return int(round(v * 1024))


header = ["#pragma once",
          "// Written by tools/make_panel.py, with the artwork: the left wing's margin",
          "// and what display.cpp draws from it. Q10 pixels from the moth's centre",
          "// (240, 124); the right wing mirrors x.",
          "#include <opxy/types.hpp>",
          "",
          "namespace moth {",
          f"const opxy::u32 kMargin = {MARGIN};",
          f"const opxy::i32 kWingX[{MARGIN}] = {{{', '.join(str(q10(x)) for x, _ in WING)}}};",
          f"const opxy::i32 kWingY[{MARGIN}] = {{{', '.join(str(q10(v)) for _, v in WING)}}};",
          f"// The tornus, where the hindwing meets the forewing, as a share of the margin (Q16).",
          f"const opxy::i32 kJoint = {round(JOINT * 65536)};",
          f"// The eyespots' places on the left wing (the right mirrors x).",
          f"const opxy::i32 kEyeX[{len(EYES)}] = {{{', '.join(str(q10(x)) for x, _ in EYES)}}};",
          f"const opxy::i32 kEyeY[{len(EYES)}] = {{{', '.join(str(q10(v)) for _, v in EYES)}}};",
          "}",
          ""]
if ID == "moth":
    (ROOT / "wing.h").write_text("\n".join(header))
nodes = DUST_FIRST + DUST
print("wrote", target, len(text.encode()), "bytes,", nodes, "nodes")
