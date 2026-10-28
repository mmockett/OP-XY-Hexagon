#!/usr/bin/env python3
"""Writes panel.svg.fragment, Moth's screen artwork, and wing.h, the wing's
geometry display.cpp draws from: run it after changing a shape here, and
commit all three.

A moth, wings spread, centred at (240, 124). Each wing's margin is the folded
waveform (display.cpp reshapes the four outlines: the dark edge and the lit
edge of each wing; the dark edge's 31 vertices joined by straight chords).
The OP-XY's outlines cannot be filled, so the membrane is built under each
chord from two pieces: a wedge from the root out to the chord, scaled to the
nearer of its two vertices (so it never passes the edge), and a tooth
authored on the chord at the margin's full reach, which display.cpp turns and
scales so its base lies exactly on the chord as it moves, covering what the
wedge leaves between it and the edge. So the membrane's boundary is the edge
at every fold. The lit wing is twenty wedges a wing, and its edge runs along
their tops. The body
is a vactrol: the LED's leads bent up as antennae, the LED as head and
thorax, the photocell's serpentine track as the abdomen. Behind it all, the
lamp's halo; over it, the scales a strike knocks loose. A specimen label sits
in the corner as static artwork. Node numbers are the ones display.h names.
Every node is one closed outline (or a circle) of distinct points, so every
outline encloses area; lines are thin closed ribbons. The halo comes before
the header block, so the header covers its top, as on the board."""
import math
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
ID = sys.argv[1] if len(sys.argv) > 1 else "moth"   # another prefix for a preview copy
FONT = 'font-family="UniversNextPro-Light"'

CX, CY = 240, 124
MARGIN = 62               # margin points per wing; with the two root points, an outline's 64
POINTS = 64
VERTICES = 31             # the dark edge's vertices along the margin; a wedge and a tooth under each chord
VERTEX_INDEX = [round(i * (MARGIN - 1) / (VERTICES - 1)) for i in range(VERTICES)]
CHORDS_N = VERTICES - 1
LIT_N = 20                # the lit membrane's wedges a wing; the lit edge runs along their tops
LIT_INDEX = [round(i * (MARGIN - 1) / LIT_N) for i in range(LIT_N + 1)]
TAPER = 75                # degrees: how steeply a tooth's sides lean in from its chord
FAN = (-3, 0)             # the wedges' common corner, at the root, from the centre: it scales with them, so it barely moves
VEINS = [0.05, 0.15, 0.26, 0.37, 0.5, 0.62, 0.74, 0.86, 0.95]
SPOTS = [(0.28, 0.6), (0.47, 0.64), (0.72, 0.6), (0.87, 0.52)]
REST = 0.89               # the margin's mean reach, where the veins are authored
DUST = 20

# Nodes (display.h names the same numbers).
PATHS = 0                 # 0, 1: dark edges (left, right); 2, 3: lit edges
HEADER = 4                # eight bar nodes
HALO = 12                 # seven lamp rings, outermost first
LED_GLOW = 19
DARK = 20                 # the dark membrane's wedges (thirty a wing, left first), then its teeth
DARK_TEETH = 80
LIT = 140                 # the lit membrane's wedges (twenty a wing)
VEIN = 180                # nine a wing, left then right
EYE = 198                 # eight eyes, three nodes each: ring, inside, pupil
BODY = 222                # fourteen parts of the vactrol
DUST_FIRST = 236

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


angles = [math.atan2(y - FAN[1], -(x - FAN[0])) for x, y in WING]
assert all(b > a for a, b in zip(angles, angles[1:-1])), "the margin must wind once round the fan's corner"


def inside(polygon, x, y):
    hit = False
    for (x0, y0), (x1, y1) in zip(polygon, polygon[1:] + polygon[:1]):
        if (y0 > y) != (y1 > y) and x < x0 + (y - y0) * (x1 - x0) / (y1 - y0):
            hit = not hit
    return hit


# Under each chord between vertices: a wedge from FAN out to the chord, and a
# tooth on the chord reaching inward by half its length (less where the wing
# is thinner), authored with its first corner at its origin.
OUTLINE = [(-3, -13)] + WING + [(-3, 13)]
# The interior lies on one side of every chord: the side the outline turns to.
AREA = sum(x0 * y1 - x1 * y0 for (x0, y0), (x1, y1) in zip(OUTLINE, OUTLINE[1:] + OUTLINE[:1]))
TURN = 1 if AREA > 0 else -1
TEETH, CHORDS, WEDGES, MIDDLES = [], [], [], []
# The edge as drawn: the root's points and the vertices, joined by chords.
EDGE = [(-3, -13)] + [WING[i] for i in VERTEX_INDEX] + [(-3, 13)]
for j in range(CHORDS_N):
    a, b = WING[VERTEX_INDEX[j]], WING[VERTEX_INDEX[j + 1]]
    ux, uy = b[0] - a[0], b[1] - a[1]
    length = math.hypot(ux, uy)
    nx, ny = -uy / length * TURN, ux / length * TURN
    # Deep enough to cover what the wedge leaves: the wedge stops at the
    # nearer vertex's reach, leaving a triangle between the chord and the
    # point at that reach on the farther vertex's ray. Its depth, as a share
    # of the chord, is found over every pair of reaches the edge can take
    # (0.78 to 1): half where the margin crosses the rays square, more where
    # it runs along them, as at the tornus.
    need = 0.5
    steps = [0.78 + 0.22 * k / 10 for k in range(11)]
    for ra in steps:
        for rb in steps:
            A, B = (a[0] * ra, a[1] * ra), (b[0] * rb, b[1] * rb)
            D = (b[0] * ra, b[1] * ra) if rb > ra else (a[0] * rb, a[1] * rb)
            chord = math.hypot(B[0] - A[0], B[1] - A[1])
            if chord < length * 0.3:
                continue
            gap = abs((B[0] - A[0]) * (D[1] - A[1]) - (B[1] - A[1]) * (D[0] - A[0])) / chord
            need = max(need, gap / chord)
    depth = min(need, 1.2) * length
    while not all(inside(EDGE, a[0] + t * ux + nx * depth * w, a[1] + t * uy + ny * depth * w)
                  for t in (0.05, 0.5, 0.95) for w in (0.25, 1)):
        depth *= 0.9
        assert depth > length / 20, "a tooth faces out"
    # Its sides lean in at TAPER from the chord, so at a sharp crest it does
    # not reach past the next chord.
    lean = depth / math.tan(math.radians(TAPER)) / length
    TEETH.append([(0, 0), (ux, uy), (ux + nx * depth - ux * lean, uy + ny * depth - uy * lean),
                  (nx * depth + ux * lean, ny * depth + uy * lean)])
    CHORDS.append((ux, uy))
    # The wedge reaches a little past each vertex's ray (5% of the chord), so
    # neighbours overlap even as a beat turns each its own way.
    wa = (a[0] - ux * 0.05, a[1] - uy * 0.05)
    wb = (b[0] + ux * 0.05, b[1] + uy * 0.05)
    WEDGES.append([FAN, wa, wb])
    MIDDLES.append(((a[0] + b[0]) / 2, (a[1] + b[1]) / 2))
LIT_WEDGES, LIT_MIDDLES = [], []
for j in range(LIT_N):
    a, b = WING[LIT_INDEX[j]], WING[LIT_INDEX[j + 1]]
    ux, uy = b[0] - a[0], b[1] - a[1]
    LIT_WEDGES.append([FAN, (a[0] - ux * 0.05, a[1] - uy * 0.05), (b[0] + ux * 0.05, b[1] + uy * 0.05)])
    LIT_MIDDLES.append(((a[0] + b[0]) / 2, (a[1] + b[1]) / 2))


# Veins: from the root to the margin, authored at REST along +x from their
# root; display.cpp turns and scales each to the margin as it moves.
def vein_geometry(u, reach):
    tip = at([(x * reach, y * reach) for x, y in WING], u)
    base = (-3, (u - 0.5) * 22)
    root = (base[0] + (tip[0] - base[0]) * 0.16, base[1] + (tip[1] - base[1]) * 0.16)
    return root, math.hypot(tip[0] - root[0], tip[1] - root[1]) * 0.97


VEIN_REST = [vein_geometry(u, REST)[1] for u in VEINS]


def vein(length):
    return [(0, -0.6), (length, -0.2), (length, 0.2), (0, 0.6)]


out = [f'<g id="{ID}_panel">']
# The lamp's halo: rings of light behind the moth, drawn at full and scaled.
for k in range(1, 8):
    out.append(f'<circle id="{ID}_node_{HALO + k - 1}" cx="0" cy="0" r="{176 - 22 * k}" fill="#000000" '
               f'stroke="#000000" stroke-width="0.25" transform="matrix(1 0 0 1 {CX} {CY - 16})"/>')
out.append(f'<circle id="{ID}_node_{LED_GLOW}" cx="0" cy="0" r="22" fill="#000000" stroke="#000000" '
           f'stroke-width="0.25" transform="matrix(1 0 0 1 {CX} {CY - 16})"/>')

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

# The dark membrane (fill, then teeth), the dark edges, then the lit membrane and lit edges.
def membrane(first, teeth, wedges=WEDGES):
    for side in range(2):
        for j, wedge in enumerate(wedges):
            out.append(node(first + len(wedges) * side + j, outline(mirror(wedge) if side else wedge), GREYS[4], width=0.3, at=(CX, CY)))
    if teeth is None:
        return
    for side in range(2):
        for j, tooth in enumerate(TEETH):
            a = WING[VERTEX_INDEX[j]]
            out.append(node(teeth + CHORDS_N * side + j, outline(mirror(tooth) if side else tooth), GREYS[4], width=0.3,
                            at=(CX - a[0] if side else CX + a[0], CY + a[1])))


membrane(DARK, DARK_TEETH)
# The edges: the wing's outline at rest, the root's two points and the margin
# (display.cpp places them at scale 2, from the wing's middle).
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
               f'stroke="{GREYS[6]}" stroke-width="0.5" stroke-linejoin="round"/>')
    if p == 1:
        membrane(LIT, None, LIT_WEDGES)

# Veins.
for side in range(2):
    for v, length in enumerate(VEIN_REST):
        out.append(node(VEIN + 9 * side + v, outline(vein(length)), GREYS[7], width=0.1, at=(CX, CY)))

# Eyespots: a ring, the wing showing inside it, and the pupil.
for e in range(8):
    out.append(f'<circle id="{ID}_node_{EYE + 3 * e}" cx="0" cy="0" r="4.4" fill="{GREYS[6]}" stroke="{GREYS[6]}" stroke-width="0.25"/>')
    out.append(f'<circle id="{ID}_node_{EYE + 3 * e + 1}" cx="0" cy="0" r="3.3" fill="{GREYS[4]}" stroke="{GREYS[4]}" stroke-width="0.25"/>')
    out.append(f'<circle id="{ID}_node_{EYE + 3 * e + 2}" cx="0" cy="0" r="3" fill="{GREYS[3]}" stroke="{GREYS[3]}" stroke-width="0.25"/>')


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

# The specimen label, pinned in the corner: static artwork, not a readout.
out.append(f'<path d="{outline([(398, 200), (468, 200), (468, 214), (398, 214)])}" fill="none" stroke="{GREYS[5]}" stroke-width="0.6"/>')
out.append(f'<text transform="matrix(1 0 0 1 402 209.4)" fill="{GREYS[8]}" {FONT} font-size="6.5px">Phalaena vactrolis</text>')

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
          f"// The edge's vertices: their margin points.",
          f"const opxy::u32 kVertices = {VERTICES};",
          f"const opxy::u8 kVertexIndex[{VERTICES}] = {{{', '.join(str(i) for i in VERTEX_INDEX)}}};",
          f"// Each tooth's chord as authored, from its first vertex to its next (the tooth's origin is the first).",
          f"const opxy::i32 kChordX[{VERTICES - 1}] = {{{', '.join(str(q10(x)) for x, _ in CHORDS)}}};",
          f"const opxy::i32 kChordY[{VERTICES - 1}] = {{{', '.join(str(q10(v)) for _, v in CHORDS)}}};",
          f"// Each tooth's two inner corners as authored, from its origin.",
          f"const opxy::i32 kToothInner[{VERTICES - 1}][4] = {{{', '.join('{' + ', '.join(str(q10(v)) for v in (t[3][0], t[3][1], t[2][0], t[2][1])) + '}' for t in TEETH)}}};",
          f"// Each chord's middle as authored, where a beat turns its wedge about the centre.",
          f"const opxy::i32 kMiddleX[{CHORDS_N}] = {{{', '.join(str(q10(x)) for x, _ in MIDDLES)}}};",
          f"const opxy::i32 kMiddleY[{CHORDS_N}] = {{{', '.join(str(q10(v)) for _, v in MIDDLES)}}};",
          f"// The lit membrane's wedges: the margin points each spans, and each one's middle.",
          f"const opxy::u32 kLitWedges = {LIT_N};",
          f"const opxy::u8 kLitIndex[{LIT_N + 1}] = {{{', '.join(str(i) for i in LIT_INDEX)}}};",
          f"const opxy::i32 kLitMiddleX[{LIT_N}] = {{{', '.join(str(q10(x)) for x, _ in LIT_MIDDLES)}}};",
          f"const opxy::i32 kLitMiddleY[{LIT_N}] = {{{', '.join(str(q10(v)) for _, v in LIT_MIDDLES)}}};",
          f"// Each vein's authored length (it is drawn with the margin at {REST} of its reach).",
          f"const opxy::i32 kVeinLength[9] = {{{', '.join(str(q10(v)) for v in VEIN_REST)}}};",
          "}",
          ""]
if ID == "moth":
    (ROOT / "wing.h").write_text("\n".join(header))
nodes = DUST_FIRST + DUST
print("wrote", target, len(text.encode()), "bytes,", nodes, "nodes")
