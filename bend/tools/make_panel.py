#!/usr/bin/env python3
"""Writes panel.svg.fragment, Bend's screen artwork, from its design board's
geometry (480 x 220 px). Run it after changing a shape here and commit both.

The screen (README, "Screen"): the owner's-manual phase figure on the left
with the cosine it is read through, the dot-matrix scope of both lines on the
right, and below a dial for each note's bend and the eight waves. Node numbers
are the ones display.h names; everything else is static artwork. Every node is
a closed outline (or a circle) of distinct points, and the lines the board
strokes are drawn as thin filled ribbons, so every outline encloses area."""
import math
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
ID = "bend"
FONT = 'font-family="UniversNextPro-Light"'

# Nodes (display.h).
MAP, FADING, HEADER = 0, 1, 2
DOT, DOTS = 10, 160
TWIN, TWINS = 170, 69
NEEDLE, ICON, KEY = 239, 247, 255
NODES = 256

# The board's geometry.
FX, FY, FS = 20, 54, 124                 # the phase figure
CX, CY = 166, 116                        # the cosine glyph
X0, W, SY, A = 198, 258, 116, 51         # the scope's trace
DIAL_X, DIAL_STEP, DIAL_Y = 27, 15.8, 202
KEY_X, KEY_STEP = 190, 34.25


def num(v, places=2):
    text = f"{round(v, places):.{places}f}".rstrip("0").rstrip(".")
    return "0" if text in ("", "-0") else text


def outline(points):
    return "M" + " L".join(f"{num(x)},{num(y)}" for x, y in points) + " Z"


def rect(x, y, w, h):
    return [(x, y), (x + w, y), (x + w, y + h), (x, y + h)]


def path(points, fill, stroke="none", width=None, node=None, at=None):
    ident = f' id="{ID}_node_{node}"' if node is not None else ""
    move = f' transform="matrix(1 0 0 1 {num(at[0])} {num(at[1])})"' if at else ""
    w = f' stroke-width="{num(width)}"' if width is not None else ""
    return f'<path{ident}{move} d="{outline(points)}" fill="{fill}" stroke="{stroke}"{w}/>'


def circle(cx, cy, r, fill, stroke="none", width=None, node=None, at=None):
    ident = f' id="{ID}_node_{node}"' if node is not None else ""
    move = f' transform="matrix(1 0 0 1 {num(at[0])} {num(at[1])})"' if at else ""
    w = f' stroke-width="{num(width)}"' if width is not None else ""
    return f'<circle{ident}{move} cx="{num(cx)}" cy="{num(cy)}" r="{num(r)}" fill="{fill}" stroke="{stroke}"{w}/>'


def label(x, y, words):
    return f'<text transform="matrix(1 0 0 1 {num(x)} {num(y)})" fill="#555555" {FONT} font-size="8px">{words}</text>'


def ribbon(points, hw):
    """The board's ribbon: a thin line as a closed outline, each point offset
    both ways along its mitred normal, the mitre held at sharp turns."""
    n, left, right = len(points), [], []

    def unit(x, y):
        length = math.hypot(x, y)
        return (x / length, y / length) if length else None

    for i in range(n):
        a, b, c = points[max(0, i - 1)], points[i], points[min(n - 1, i + 1)]
        t1, t2 = unit(b[0] - a[0], b[1] - a[1]), unit(c[0] - b[0], c[1] - b[1])
        p, q = t1 or t2, t2 or t1
        m = unit(-p[1] - q[1], p[0] + q[0]) or (-p[1], p[0])
        cos = max(0.5, m[0] * -p[1] + m[1] * p[0])
        length = hw / cos
        left.append((b[0] + m[0] * length, b[1] + m[1] * length))
        right.append((b[0] - m[0] * length, b[1] - m[1] * length))
    return left + right[::-1]


def rounded(x, y, w, h, r, steps=3):
    pts = []
    for cx, cy, a0 in ((x + w - r, y + r, -90), (x + w - r, y + h - r, 0), (x + r, y + h - r, 90), (x + r, y + r, 180)):
        for i in range(steps + 1):
            a = math.radians(a0 + 90 * i / steps)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


# The board's maps, for the wave icons (bent 0.85, no warp).
def pw(u, pts):
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        if u <= x1:
            return y0 + (y1 - y0) * (u - x0) / (x1 - x0)
    return pts[-1][1]


def bent(wave, d, u):
    dd = min(d, 0.985)
    if wave == 0:
        m = 0.5 * (1 - dd)
        return 0.5 * u / m if u < m else 0.5 + 0.5 * (u - m) / (1 - m)
    if wave == 1:
        q, h = 0.5 * (1 - dd), (0 if u < 0.5 else 0.5)
        return h + min(0.5, 0.5 * (u - h) / q)
    if wave == 2:
        w = 1 - 0.9 * dd
        return max(0, (u - (1 - w)) / w)
    if wave == 3:
        return (1 - d) * u + d * pw(u, [(0, 0), (0.25, 0.5), (0.5, 0.25), (0.75, 0.5), (1, 1)])
    if wave == 4:
        return (1 - d) * u + d * pw(u, [(0, 0), (0.04, 0.5), (0.5, 1), (1, 1)])
    return (1 + 5 * d) * u


def window(wave, u):
    return {5: 1 - u, 6: 1 - abs(2 * u - 1), 7: 1 if u < 0.5 else 2 * (1 - u)}.get(wave, 1)


def out(wave, d, u):
    return -math.cos(2 * math.pi * bent(wave, d, u)) * window(wave, u)


out_svg = [f'<g id="{ID}_panel">',
           "<!-- SDK two-page header: keep this block before machine artwork below y=38. -->",
           '<path d="M0,0 L480,0 L480,38 L0,38 Z" fill="#000000"/>']
bar = ["#000000", "#323232", "#4B4B4B", "#646464", "#7D7D7D", "#969696", "#AFAFAF", "#CDCDCD"]
for i, colour in enumerate(bar):
    out_svg.append(f'<path id="{ID}_node_{HEADER + i}" transform="matrix(1 0 0 1 {60 * i} 0)" '
                   f'd="M0,0 L60,0 L60,20 L0,20 Z" fill="{colour}" stroke="{colour}" stroke-width="0.5"/>')
texts = [(5 + 120 * c, 10, "#CBCBCB" if c < 2 else "#000000", 10) for c in range(4)]
texts += [(65 + 120 * c, 17.4, "#CBCBCB" if c < 2 else "#000000", 20) for c in range(4)]
texts += [(5 + 120 * c, 33, "#5A5A5A", 10) for c in range(4)] + [(65 + 120 * c, 33, "#707070", 13) for c in range(4)]
for i, (x, y, ink, size) in enumerate(texts):
    out_svg.append(f'<text id="{ID}_text_{i}" transform="matrix(1 0 0 1 {num(x)} {num(y)})" fill="{ink}" {FONT} '
                   f'font-size="{size}px">-</text>')
o = out_svg.append

# Left: the owner's-manual figure. Input phase across, output phase up; graph
# paper at the quarters, its frame and ticks, the straight map dashed, then
# the bent map (nodes 1, fading, and 0: 64-point ribbons reshaped by display.cpp).
px = lambda f: FX + f * FS
py = lambda f: FY + FS - f * FS
o(label(20, 49, "phase"))
for f in (0.25, 0.5, 0.75):
    o(path(rect(px(f) - 0.3, FY, 0.6, FS), "#111111"))
    o(path(rect(FX, py(f) - 0.3, FS, 0.6), "#111111"))
o(path(rect(FX, FY + FS - 0.3, FS, 0.6), "#333333"))
o(path(rect(FX - 0.3, FY, 0.6, FS), "#333333"))
o(path(rect(FX, FY - 0.3, FS, 0.6), "#222222"))
o(path(rect(FX + FS - 0.3, FY, 0.6, FS), "#222222"))
for f in (0.25, 0.5, 0.75, 1):
    o(path(rect(px(f) - 0.3, FY + FS, 0.6, 3), "#444444"))
    o(path(rect(FX - 3, py(f) - 0.3, 3, 0.6), "#444444"))
for i in range(16):
    a, b = (i + 0.15) / 16, (i + 0.65) / 16
    o(path(ribbon([(px(a), py(a)), (px(b), py(b))], 0.4), "#333333"))
diagonal = [(-FS / 2 + FS * i / 31, FS / 2 - FS * i / 31) for i in range(32)]
for node, ink in ((FADING, "#222222"), (MAP, "#eeeeee")):
    o(path(ribbon(diagonal, 0.3), "none", ink, 0.6, node, (FX + FS / 2, FY + FS / 2)))

# The cosine the map is read through, between the figure and the scope.
o(path(rect(147, CY - 0.3, 6, 0.6), "#444444"))
o(path(rect(180, CY - 0.3, 6, 0.6), "#444444"))
for x in (153, 186):
    o(path([(x + 2.4, CY), (x, CY + 1.8), (x, CY - 1.8)], "#444444"))
o(circle(CX, CY, 11, "none", "#444444", 0.6))
o(path(ribbon([(CX - 7 + i * 14 / 16, CY + 4.5 * math.cos(2 * math.pi * i / 16)) for i in range(17)], 0.4), "#888888"))
o(label(160.2, CY + 22, "cos"))

# Right: the scope. Its window, the dotted zero line, ticks at full height,
# line 1's one cycle marked, then line 2's dots (nodes 170-238, the fainter
# twin) under line 1's (nodes 10-169).
o(label(190, 49, "wave"))
o(label(450.4, 49, "1+2'"))
o(path(rounded(190, 54, 274, 124, 3), "#111111", "#333333", 0.6))
for i in range(43):
    o(path(rect(X0 + i * W / 42 - 0.5, SY - 0.3, 1.2, 0.6), "#222222"))
for y in (SY - A, SY + A):
    for i in range(9):
        o(path(rect(X0 + i * W / 8 - 0.3, y - 1, 0.6, 2), "#222222"))
for j in range(22):
    o(path(rect(X0 + W / 2 - 0.3, 60 + j * 5.4, 0.6, 2.4), "#333333"))
# Each dot is authored on the zero line at its place across; display.cpp moves
# it up and down only.
for k in range(TWINS):
    o(circle(X0 + k * W / (TWINS - 1), SY, 0.75, "#666666", "#666666", 0.2, TWIN + k))
for k in range(DOTS):
    o(circle(X0 + k * W / (DOTS - 1), SY, 0.9, "#eeeeee", "#eeeeee", 0.2, DOT + k))

# Below: the strip's rule, a dial for each note (its arc, needle 239-246 and
# hub) and "dcw"; then the chosen wave's key (255) and the eight waves (247-254)
# as the CZ numbers them.
o(path(rect(20, 186 - 0.3, 444, 0.6), "#222222"))
for k in range(8):
    x = DIAL_X + k * DIAL_STEP
    steps = 12
    outer = [(x + 5.7 * math.cos(math.pi * (0.75 + 1.5 * i / steps)), DIAL_Y + 5.7 * math.sin(math.pi * (0.75 + 1.5 * i / steps)))
             for i in range(steps + 1)]
    inner = [(x + 5.1 * math.cos(math.pi * (0.75 + 1.5 * i / steps)), DIAL_Y + 5.1 * math.sin(math.pi * (0.75 + 1.5 * i / steps)))
             for i in range(steps, -1, -1)]
    o(path(outer + inner, "#333333"))
for k in range(8):
    x = DIAL_X + k * DIAL_STEP
    o(path(rect(-0.45, -4.8, 0.9, 4.65), "#555555", "#555555", 0.2, NEEDLE + k, (x, DIAL_Y)))
for k in range(8):
    o(circle(DIAL_X + k * DIAL_STEP, DIAL_Y, 1.1, "#555555"))
o(label(152, 205, "dcw"))
o(path(rounded(0, 0, 31, 20, 2), "#222222", "#222222", 0.1, KEY, (KEY_X, 192)))
for k in range(8):
    x0 = KEY_X + k * KEY_STEP + 4
    pts = [(x0 + (i / 24) * 23, DIAL_Y - 5.5 * out(k, 0.85, min(i / 24, 0.9999))) for i in range(25)]
    o(path(ribbon(pts, 0.4), "#555555", "#555555", 0.1, ICON + k))

# Palette (display.h): the map's greys dim to bright (1-7, stroked 0.6), the
# dots, the needles and the icons lit and dim. Parked off screen.
palette = [("#000000", 0.3), ("#222222", 0.6), ("#444444", 0.6), ("#666666", 0.6), ("#888888", 0.6),
           ("#aaaaaa", 0.6), ("#cccccc", 0.6), ("#eeeeee", 0.6), ("#eeeeee", 0.2), ("#666666", 0.2),
           ("#888888", 0.2), ("#dddddd", 0.2), ("#555555", 0.2), ("#eeeeee", 0.1), ("#555555", 0.1),
           ("#222222", 0.1)]
for i, (grey, width) in enumerate(palette):
    o(f'<circle id="{ID}_palette_{i}" cx="640" cy="0" r="1" fill="{grey}" stroke="{grey}" stroke-width="{width}"/>')
o("</g>")

text = "\n".join(out_svg) + "\n"


def check(artwork):
    """What the device's loader needs of the artwork, and what it can read."""
    for d in re.findall(r' d="([^"]*)"', artwork):
        assert d.count("M") == 1, "more than one subpath"
        assert d.count(",") <= 64, "more than 64 points"
    for size in re.findall(r'font-size="([0-9.]+)px"', artwork):
        assert float(size) >= 8, f"text at {size} px"
    assert len(artwork.encode()) <= 64 * 1024, "artwork over 64 KB"


check(text)
target = ROOT / "panel.svg.fragment" if len(sys.argv) < 2 else pathlib.Path(sys.argv[1])
target.write_text(text)
print("wrote", target, "nodes", NODES)
