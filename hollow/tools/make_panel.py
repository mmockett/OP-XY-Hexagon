#!/usr/bin/env python3
"""Writes panel.svg.fragment, Hollow's screen artwork, with the node numbers
display.h names: run it after changing a shape here, and commit both.

A shelf of eight vessels (one authored set per body family), the air line
along the top (two mutable halves), sympathy arcs between vessels, and pools
of ripples (one pool per vowel's aspect), breath motes and glass glints.
Every node is one closed subpath of distinct points (M, L..., Z), so every
outline encloses area; the panel stays under the 64 KB asset limit."""
import math
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
ID = sys.argv[1] if len(sys.argv) > 1 else "hollow"   # another prefix for a preview copy
FONT = 'font-family="UniversNextPro-Light"'

# Layout (display.h names the same numbers).
SHELF = 196
XS = [51 + 54 * k for k in range(8)]
AIR_Y, AIR_LEFT, AIR_WIDTH = 56, 16, 448
POINTS = 64
HEADER_FIRST, VESSEL_FIRST, MOUTH_FIRST, ARC_FIRST, MOTE_FIRST, GLINT_FIRST, RIPPLE_FIRST = 2, 10, 42, 50, 68, 92, 108
RIPPLE_POOL = 29
ASPECTS = [0.75, 0.6, 0.45, 0.35, 0.26]   # oo, oh, ah, eh, ee
RIPPLE_R = 13                              # authored radius; scale 0.31..2 draws 4..26 px
SPANS = [(i, j) for i in range(8) for j in range(i + 1, min(8, i + 4))]   # 18 pairs within three


def num(v, digits=2):
    s = f"{v:.{digits}f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def outline(points, digits=2):
    pts = [(num(x, digits), num(y, digits)) for x, y in points]
    assert len(set(pts)) == len(pts), "repeated point"
    return "M" + " L".join(f"{x},{y}" for x, y in pts) + " Z"


def rect(x0, y0, x1, y1):
    return outline([(x0, y0), (x1, y0), (x1, y1), (x0, y1)])


def node(i, d, stroke="#ffffff", width=1, fill="none", at=None):
    transform = f' transform="matrix(1 0 0 1 {num(at[0])} {num(at[1])})"' if at else ""
    return f'<path id="{ID}_node_{i}"{transform} d="{d}" fill="{fill}" stroke="{stroke}" stroke-width="{num(width)}"/>'


def cubic(p0, p1, p2, p3, n):
    """Points along a cubic after p0, up to and including p3."""
    out = []
    for s in range(1, n + 1):
        t = s / n
        u = 1 - t
        out.append((u**3 * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t**3 * p3[0],
                    u**3 * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t**3 * p3[1]))
    return out


def mirror(points):
    return [(-x, y) for x, y in points]


# Vessels, relative to their foot on the shelf (x, SHELF), traced from the board.
def vox():
    # An urn with a narrow throat: left side down from the lip, right side up.
    left = [(-5, -64)] + cubic((-5, -64), (-5, -54), (-18, -50), (-18, -30), 6) + \
        cubic((-18, -30), (-18, -12), (-10, -4), (-9, 0), 5)
    right = list(reversed(mirror(left)))
    return left + right


def string():
    # A tall narrow body and its tuning peg, joined by a thin neck.
    # Round the peg (radius 3 at 75 up) from the neck's right edge, over the
    # top, to its left edge, then back down the neck.
    peg = []
    for s in range(15):
        a = math.radians(-80 + s * 340 / 14)
        peg.append((3 * math.cos(a), -75 - 3 * math.sin(a)))
    return [(-4, 0), (4, 0), (4, -70), (peg[0][0], -70)] + peg + [(peg[-1][0], -70), (-4, -70)]


def pipe():
    # An organ pipe; its mouth, cut above the foot, is a node of its own.
    return [(-7, -72), (7, -72), (7, -26), (2, 0), (-2, 0), (-7, -26)]


def glass():
    # A bowl on a stem and foot.
    left = [(-15, -66)] + cubic((-15, -66), (-15, -44), (-6, -36), (-1, -34), 7) + [(-1, -3), (-11, 0)]
    right = list(reversed(mirror(left)))
    return left + right


VESSELS = [vox(), string(), pipe(), glass()]
MOUTHS = [64, 78, 72, 66]   # height of each family's mouth above the shelf


def arc(span):
    """A sympathy arc from (0, 0) to (span, 0): a thin crescent ribbon."""
    n = 10
    top, bottom = [], []
    for s in range(n + 1):
        t = s / n
        x = span * t
        lift = 2 * t * (1 - t) * span * 0.42   # the board's quadratic, its control 0.42 span up
        thick = 0.25 + 0.5 * 4 * t * (1 - t)
        top.append((x, -lift))
        bottom.append((x, -lift + thick))
    return top + list(reversed(bottom))


def ellipse(aspect, n=16):
    return [(RIPPLE_R * math.cos(2 * math.pi * s / n), RIPPLE_R * aspect * math.sin(2 * math.pi * s / n))
            for s in range(n)]


def glint():
    a, w = 2, 0.3   # arms 4 px long, 0.6 px thick
    return [(-w, -a), (w, -a), (w, -w), (a, -w), (a, w), (w, w), (w, a), (-w, a), (-w, w), (-a, w), (-a, -w), (-w, -w)]


out = [f'<g id="{ID}_panel">',
       "<!-- SDK two-page header: keep this block before machine artwork below y=38. -->",
       '<path d="M0,0 L480,0 L480,38 L0,38 Z" fill="#000000"/>']
bar = ["#000000", "#323232", "#4B4B4B", "#646464", "#7D7D7D", "#969696", "#AFAFAF", "#CDCDCD"]
for i, colour in enumerate(bar):
    out.append(f'<path id="{ID}_node_{HEADER_FIRST + i}" transform="matrix(1 0 0 1 {60 * i} 0)" '
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

# Nodes 0 and 1: the air line's halves, 32 samples out and 32 back; display.cpp
# places them at scale 2 and reshapes them (flat here).
for h in range(2):
    top = [(-56 + i * 112 / 31, 0) for i in range(32)]
    back = [(x, 0.075) for x, _ in reversed(top)]
    pts = top + back
    d = "M" + " L".join(f"{num(x, 4)},{num(y, 4)}" for x, y in pts) + " Z"
    out.append(f'<path id="{ID}_node_{h}" d="{d}" fill="none" stroke="#4d4d4d" stroke-width="0.45"/>')

# Static: the shelf.
out.append(f'<path d="{rect(18, SHELF, 462, SHELF + 1)}" fill="#333333"/>')

# Sympathy arcs (behind the vessels), one per pair within three, at vessel i.
arcs = {span: outline(arc(54 * span), 1) for span in (1, 2, 3)}
for n, (i, j) in enumerate(SPANS):
    out.append(node(ARC_FIRST + n, arcs[j - i], "#474747", 0.3, "#474747"))

# Vessels: eight of each family, filled so the glow can change.
for f, shape in enumerate(VESSELS):
    d = outline(shape, 1)
    for k in range(8):
        out.append(node(VESSEL_FIRST + 8 * f + k, d, "#3d3d3d", 1, "#000000"))
# The pipes' mouths, dark openings drawn over them.
for k in range(8):
    out.append(node(MOUTH_FIRST + k, rect(-4, -36, 4, -30), "#3d3d3d", 1, "#000000"))

# Ripples: a pool per vowel's aspect.
for a, aspect in enumerate(ASPECTS):
    d = outline(ellipse(aspect), 1)
    for s in range(RIPPLE_POOL):
        out.append(node(RIPPLE_FIRST + RIPPLE_POOL * a + s, d, "#5c5c5c", 1))

# Breath motes: three per vessel.
for m in range(24):
    out.append(f'<circle id="{ID}_node_{MOTE_FIRST + m}" cx="0" cy="0" r="0.9" fill="#6e6e6e" stroke="#6e6e6e" stroke-width="0.2"/>')
# Glints on glass: two per vessel.
gd = outline(glint())
for g in range(16):
    out.append(node(GLINT_FIRST + g, gd, "#ffffff", 0.2, "#ffffff"))

# Palette: paint pairs (fill, stroke, width), parked off screen.
PALETTE = [
    ("#000000", "#3d3d3d", 1),     # 0 vessel at rest
    ("#000000", "#d9d9d9", 1),     # 1..5 vessel ringing, glowing as it fills
    ("#111111", "#d9d9d9", 1),
    ("#1c1c1c", "#d9d9d9", 1),
    ("#2a2a2a", "#d9d9d9", 1),
    ("#3b3b3b", "#d9d9d9", 1),
    # 6..9 ripples, faint to bright: wider strokes for the brighter ones,
    # which are the young, small (scaled-down) rings, so lines stay about 0.9 px.
    ("#2a2a2a", "#2a2a2a", 0.7),
    ("#5c5c5c", "#5c5c5c", 1),
    ("#a6a6a6", "#a6a6a6", 1.6),
    ("#f0f0f0", "#f0f0f0", 1.9),
    ("#474747", "#474747", 0.3),   # 10..12 sympathy arcs
    ("#8a8a8a", "#8a8a8a", 0.3),
    ("#cfcfcf", "#cfcfcf", 0.3),
    ("#4d4d4d", "#4d4d4d", 0.45),  # 13 air (at scale 2)
    ("#6e6e6e", "#6e6e6e", 0.2),   # 14 breath motes
    ("#ffffff", "#ffffff", 0.2),   # 15 glints
]
for i, (fill, stroke, width) in enumerate(PALETTE):
    out.append(f'<circle id="{ID}_palette_{i}" cx="640" cy="0" r="1" fill="{fill}" stroke="{stroke}" stroke-width="{num(width)}"/>')
out.append("</g>")

text = "\n".join(out) + "\n"
target = ROOT / "panel.svg.fragment" if ID == "hollow" else pathlib.Path(sys.argv[2])
target.write_text(text)
nodes = RIPPLE_FIRST + RIPPLE_POOL * len(ASPECTS)
print("wrote", target, len(text.encode()), "bytes,", nodes, "nodes")
