#!/usr/bin/env python3
"""Writes panel.svg.fragment, Orbit's screen artwork, with the node numbers
display.h names: run it after changing a shape here (or a wave in
tools/make_tables.py), and commit both.

The screen is the design board's (Orbit.dc.html), at the OP-XY's 480 x 220:
on the left the vector's field, a diamond gate with a small wave at each
corner, the orbit dotted round its centre and a point for each held note; on
the right a scope with the mixed wave, its detuned twin and the body under
it, and each corner's share as a row of pips. The three outlines (nodes 0-2)
are the scope's, reshaped by display.cpp. They span 204 px, past an
outline's 128, so they are drawn at 1/1.62 and placed at 1.62 times.

Every stroke width is the palette's 0.45 px, so filled shapes are authored
that much smaller (0.225 px each side) and draw at the board's sizes. Every
node is a closed outline of distinct points or a circle, and check() fails the
build on an outline the device would draw badly: more than 64 points, points
within 1/8 px, a closing edge under 1/4 px, a turn straight back, no area.

Nothing that moves has a large fill: the biggest filled part that can move is
a corner wave, a 0.85 px ribbon of 30 px^2 or so, authored at its true size."""
import math
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
ID = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith("-") else "orbit"
FONT = 'font-family="UniversNextPro-Light"'
STROKE = 0.45
HALF = STROKE / 2
# The board's sixteen greys (grey(0) .. grey(15)): the palette.
GREYS = ["#000000", "#0d0d0d", "#1a1a1a", "#262626", "#333333", "#404040", "#4d4d4d", "#5c5c5c",
         "#6b6b6b", "#7a7a7a", "#8c8c8c", "#a6a6a6", "#bdbdbd", "#d4d4d4", "#e6e6e6", "#f5f5f5"]
G = lambda i: GREYS[max(0, min(15, round(i)))]

# The board's geometry (draw() and scope()).
FX, FY, R = 122, 129, 64                       # the field's centre and the gate's reach
SPOTS = [(FX, FY - R - 13), (FX + R + 28, FY), (FX, FY + R + 13), (FX - R - 28, FY)]   # corner waves A, B, C, D
X0, X1, Y, A = 262, 466, 112, 46               # the scope
GLYPH_H = 0.4                                  # the corner waves' ribbon: with the palette's stroke, 0.85 px wide
SCOPE = 1.62                                   # the outlines' scale
PIP_Y = Y + 75

# Nodes (display.h).
COMB, TWIN, WAVE = 0, 1, 2
HEADER = 3
GLYPH = 11                                     # [stepped][bank][corner]: 64
TICK = 75                                      # the gate's corner ticks: 4
DOT = 79                                       # the orbit's dots: 48
DOTS = 48
STATION = 127                                  # steps' stations: 4
CROSS = 131
SPOKE = 132                                    # 6
TRAIL = 138                                    # per note, 8: 64
POINT = 202                                    # 8
RING = 210
PING = 211                                     # 8
HOME = 219
PIP = 220                                      # per corner, 8: 32
BEAM = 252                                     # the scope's beam, while a note sounds
NODES = 253

# Authored sizes display.cpp scales from (display.h keeps the same).
TRAIL_R, POINT_R, RING_R, PING_R, HOME_R, STATION_R, BEAM_R = 1.0, 2.5, 4.0, 7.0, 2.6, 3.2, 1.3


def num(v, places=4):
    text = f"{round(v, places):.{places}f}"
    text = text.rstrip("0").rstrip(".") if places else text
    return "0" if text in ("", "-0") else text


def outline(points, places=4):
    return "M" + " L".join(f"{num(x, places)},{num(y, places)}" for x, y in points) + " Z"


def check(points, name):
    """What the device needs of an outline (docs/screens.md)."""
    assert 3 <= len(points) <= 64, (name, len(points))
    n = len(points)
    twice = 0
    for i in range(n):
        a, b, c = points[i], points[(i + 1) % n], points[(i + 2) % n]
        twice += a[0] * b[1] - b[0] * a[1]
        ux, uy, vx, vy = b[0] - a[0], b[1] - a[1], c[0] - b[0], c[1] - b[1]
        assert max(abs(ux), abs(uy)) >= 0.125, (name, "points within 1/8 px", i, a, b)
        assert not (abs(ux * vy - uy * vx) < 1e-9 and ux * vx + uy * vy < 0), (name, "turns straight back", i)
    assert math.dist(points[0], points[-1]) >= 0.25, (name, "closes under 1/4 px")
    assert abs(twice) / 2 >= 0.05, (name, "no area")


def rect_points(x0, y0, x1, y1):
    return [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]


def path(d, fill, stroke="none", width=None, node=None, at=None, lent=False):
    if node is not None and fill != "none" and not lent:
        width = None   # the palette lends a filled node its stroke and width
    ident = f' id="{ID}_node_{node}"' if node is not None else ""
    move = f' transform="matrix(1 0 0 1 {num(at[0])} {num(at[1])})"' if at else ""
    w = f' stroke-width="{num(width)}"' if width is not None else ""
    edge = f' stroke="{stroke}"' if node is not None or stroke != "none" else ""   # left out, none: it saves 2 KB
    return f'<path{ident}{move} d="{d}" fill="{fill}"{edge}{w}/>'


def circle(cx, cy, r, fill, stroke="none", width=None, node=None, at=None):
    if node is not None and fill != "none":
        width = None
    ident = f' id="{ID}_node_{node}"' if node is not None else ""
    move = f' transform="matrix(1 0 0 1 {num(at[0])} {num(at[1])})"' if at else ""
    w = f' stroke-width="{num(width)}"' if width is not None else ""
    edge = f' stroke="{stroke}"' if node is not None or stroke != "none" else ""
    return f'<circle{ident}{move} cx="{num(cx)}" cy="{num(cy)}" r="{num(r)}" fill="{fill}"{edge}{w}/>'


def text(x, y, size, fill, words):
    return f'<text transform="matrix(1 0 0 1 {num(x)} {num(y)})" fill="{fill}" {FONT} font-size="{num(size)}px">{words}</text>'


def ribbon(points, h):
    """The board's thin line: out along the points, back offset along each
    point's normal by h."""
    back = []
    n = len(points)
    for i in range(n - 1, -1, -1):
        a, b = points[max(0, i - 1)], points[min(n - 1, i + 1)]
        tx, ty = b[0] - a[0], b[1] - a[1]
        length = math.hypot(tx, ty) or 1
        back.append((points[i][0] - ty / length * h, points[i][1] + tx / length * h))
    return points + back


# The waves' 64 samples, as waves.h holds them for the screen.
source = (ROOT / "waves.h").read_text()
body = source[source.index("face[8][4][64]"):source.index("};", source.index("face[8][4][64]"))]
faces = [[int(v) / 127 for v in row.split(",") if v.strip()] for row in re.findall(r"\{([-\d, ]+)\}", body)]
assert len(faces) == 32 and all(len(f) == 64 for f in faces)


def read(table, p):
    p = (p - math.floor(p)) * 64
    i = int(p)
    f = p - i
    return table[i % 64] * (1 - f) + table[(i + 1) % 64] * f


out = [f'<g id="{ID}_panel">',
       "<!-- SDK two-page header: keep this block before machine artwork below y=38. -->",
       '<path d="M0,0 L480,0 L480,38 L0,38 Z" fill="#000000"/>']
bar = ["#000000", "#323232", "#4B4B4B", "#646464", "#7D7D7D", "#969696", "#AFAFAF", "#CDCDCD"]
for i, colour in enumerate(bar):
    out.append(f'<path id="{ID}_node_{HEADER + i}" transform="matrix(1 0 0 1 {60 * i} 0)" '
               f'd="M0,0 L60,0 L60,20 L0,20 Z" fill="{colour}" stroke="{colour}" stroke-width="0.5"/>')
texts = [(5 + 120 * c, 10, "#CBCBCB" if c < 2 else "#000000", 10) for c in range(4)]
texts += [(65 + 120 * c, 17.4, "#CBCBCB" if c < 2 else "#000000", 20) for c in range(4)]
texts += [(5 + 120 * c, 33, "#5A5A5A", 10) for c in range(4)] + [(65 + 120 * c, 33, "#707070", 13) for c in range(4)]
for i, (x, y, ink, size) in enumerate(texts):
    out.append(f'<text id="{ID}_text_{i}" transform="matrix(1 0 0 1 {num(x)} {num(y)})" fill="{ink}" {FONT} '
               f'font-size="{size}px">-</text>')

# The field: the gate, its half-size inner diamond, the two axes dotted and
# quarter ticks along them.
gate = [(FX, FY - R), (FX + R, FY), (FX, FY + R), (FX - R, FY)]
out.append(path(outline(gate), G(1), G(9), 0.8))
out.append(path(outline([(FX + (x - FX) / 2, FY + (y - FY) / 2) for x, y in gate]), "none", G(3), 0.6))
for i in range(-15, 16):
    if i:
        out.append(circle(FX + R * i / 16, FY, 0.45, G(4)))
        out.append(circle(FX, FY + R * i / 16, 0.45, G(4)))
for q in (-0.75, -0.25, 0.25, 0.75):
    out.append(path(outline(rect_points(FX + R * q - 0.3, FY - 2, FX + R * q + 0.3, FY + 2)), G(5)))
    out.append(path(outline(rect_points(FX - 2, FY + R * q - 0.3, FX + 2, FY + R * q + 0.3)), G(5)))

# The corner waves (nodes 11-74): each bank's four, smooth and stepped (grit
# past half), as the board's thin ribbons (20 points, or eight steps), about
# the origin at their true size: display.cpp places them at their spots.
for stepped in (0, 1):
    for bank in range(8):
        for corner in range(4):
            table = faces[bank * 4 + corner]
            if stepped:   # eight held steps, each riser a 0.2 px slant
                points = []
                for k in range(8):
                    y = -5 * read(table, k / 8)
                    points += [((k / 8 - 0.5) * 26 + (0.1 if k else 0), y), (((k + 1) / 8 - 0.5) * 26 - (0.1 if k < 7 else 0), y)]
            else:
                points = [((i / 19 - 0.5) * 26, -5 * read(table, i / 19)) for i in range(20)]
            node = GLYPH + stepped * 32 + bank * 4 + corner
            shape = [(round(x, 1), round(y, 1)) for x, y in ribbon(points, GLYPH_H)]
            half = len(shape) // 2
            for i in range(1, half):   # both edges run strictly left to right
                shape[i] = (max(shape[i][0], round(shape[i - 1][0] + 0.2, 1)), shape[i][1])
                j = 2 * half - 1 - i
                shape[j] = (max(shape[j][0], round(shape[j + 1][0] + 0.2, 1)), shape[j][1])
            check(shape, f"corner wave {node}")
            # The device lends these the palette's width anyway; written out, a preview that swaps only colours agrees.
            out.append(path(outline(shape, 1), "#5c5c5c", "#5c5c5c", STROKE, node, lent=True))
# The gate's corner ticks (75-78), brightening with each corner's share.
for k, (vx, vy) in enumerate(gate):
    ox, oy = (vx - FX) / R, (vy - FY) / R
    x, y = vx + ox * 2, vy + oy * 2
    out.append(path(outline(rect_points(x - 1 + HALF, y - 1 + HALF, x + 1 - HALF, y + 1 - HALF)), G(4), G(4), STROKE, TICK + k))
for name, (x, y) in zip("ABCD", ((FX + 17, FY - R - 10), (FX + R + 15, FY - 9), (FX + 17, FY + R + 16), (FX - R - 41, FY - 9))):
    out.append(text(x, y, 8, G(6), name))

# The orbit (79-126): its path dotted; steps' four stations (127-130); the
# centre's cross (131); the spoke to the newest point (132-137).
for i in range(DOTS):
    out.append(circle(0, 0, 0.6 - HALF, G(7), G(7), STROKE, DOT + i))
for k in range(4):
    out.append(circle(0, 0, STATION_R, "none", G(3), STROKE, STATION + k))
arm, thick = 3 - HALF, 0.35 - HALF
cross = [(-arm, -thick), (-thick, -thick), (-thick, -arm), (thick, -arm), (thick, -thick), (arm, -thick),
         (arm, thick), (thick, thick), (thick, arm), (-thick, arm), (-thick, thick), (-arm, thick)]
out.append(path(outline(cross), G(10), G(10), STROKE, CROSS))
for i in range(6):
    out.append(circle(0, 0, 0.5 - HALF, G(3), G(3), STROKE, SPOKE + i))

# The notes: each one's trail (138-201), its point (202-209), the newest
# one's ring (210), each one's ping as it starts (211-218), and the hollow
# ring where the next note will start (219).
for i in range(64):
    out.append(circle(0, 0, TRAIL_R, G(8), G(8), STROKE, TRAIL + i))
for k in range(8):
    out.append(circle(0, 0, POINT_R, G(15), G(15), STROKE, POINT + k))
out.append(circle(0, 0, RING_R, "none", G(9), STROKE, RING))
for k in range(8):
    out.append(circle(0, 0, PING_R, "none", G(12), STROKE, PING + k))
out.append(circle(0, 0, HOME_R, "none", G(10), STROKE, HOME))

# The scope: its corner brackets, graticule and baseline.
for x, y, dx, dy in ((X0 - 6, Y - 60, 1, 1), (X1 + 6, Y - 60, -1, 1), (X0 - 6, Y + 60, 1, -1), (X1 + 6, Y + 60, -1, -1)):
    x0 = min(x, x + 8 * dx)
    y0 = y - (0.6 if dy < 0 else 0)
    out.append(path(outline(rect_points(x0, y0, x0 + 8, y0 + 0.6)), G(4)))
    x0 = x - (0.6 if dx < 0 else 0)
    y0 = min(y, y + 8 * dy)
    out.append(path(outline(rect_points(x0, y0, x0 + 0.6, y0 + 8)), G(4)))
for i in range(9):
    for j in range(5):
        out.append(circle(X0 + i * 25.5, Y - 50 + j * 25, 0.55, G(5 if j == 2 else 3)))
for i in range(64):
    out.append(circle(X0 + i * 3.2, Y, 0.35, G(3)))
# Its outlines (0-2), at 1/1.6 about the scope's middle: the body under the
# wave as a comb of upright strokes, the detuned twin and the wave, ribbons.
middle = ((X0 + X1) / 2, Y)
s = 1 / SCOPE
wave = [((X0 + (X1 - X0) * i / 31 - middle[0]) * s, -A * s * math.sin(2 * math.pi * i / 31)) for i in range(32)]
comb = []
for i in range(32):
    x = (X0 + (X1 - X0) * (i + 0.5) / 32 - middle[0]) * s
    top = -A * s * math.sin(2 * math.pi * (i + 0.5) / 32) * 0.9
    comb += [(x, 0.2), (x, top)] if i % 2 == 0 else [(x, top), (x, 0.2)]
out.append(path(outline(comb), "none", G(2), STROKE, COMB, middle))
out.append(path(outline(ribbon([(x, y + 1.5) for x, y in wave], 0.175)), "none", G(5), STROKE, TWIN, middle))
out.append(path(outline(ribbon(wave, 0.3)), "none", G(12), STROKE, WAVE, middle))
# The beam: a bright bead that sweeps the wave for as long as a note sounds.
out.append(circle(0, 0, BEAM_R, G(15), G(15), STROKE, BEAM))

# Each corner's share: a row of eight pips (220-251) under the scope.
for k, name in enumerate("ABCD"):
    x = X0 + k * 53
    out.append(text(x, Y + 82, 8, G(6), name))
    for j in range(8):
        px = x + 8 + j * 5
        out.append(path(outline(rect_points(px + HALF, PIP_Y + HALF, px + 3 - HALF, PIP_Y + 7 - HALF)), G(3), G(3), STROKE,
                        PIP + k * 8 + j))

for i, grey in enumerate(GREYS):
    out.append(f'<circle id="{ID}_palette_{i}" cx="640" cy="0" r="1" fill="{grey}" stroke="{grey}" stroke-width="{STROKE}"/>')
out.append("</g>")

ids = [int(m) for m in re.findall(rf'id="{ID}_node_(\d+)"', "\n".join(out))]
assert sorted(ids) == list(range(NODES)), "every node once"
target = ROOT / "panel.svg.fragment" if ID == "orbit" else pathlib.Path(sys.argv[2])
target.write_text("\n".join(out) + "\n")
size = target.stat().st_size
assert size <= 65536, f"the artwork is {size} bytes, past its 64 KiB"
print("wrote", target, NODES, "nodes,", size, "bytes")
