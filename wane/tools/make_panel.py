#!/usr/bin/env python3
"""Writes panel.svg.fragment, Wane's screen artwork, from the cassette's real
geometry in millimetres. Run it after changing a shape here and commit both;
`--tables` prints the constants display.cpp keeps in step with it.

The screen (README, "Screen"): a compact cassette, 100.4 x 63.8 mm drawn at
2.72 px/mm, between the oscillator ("osc", left) and a needle per held note
("notes", right). Node numbers are the ones display.h names. Every node is one
closed outline (or a circle) of distinct points, so every outline encloses
area; the cassette's shell and label are drawn over the reels with the window
cut out of them, which is what clips the tape packs to the window. Age's wear
(nodes from 63) is still artwork drawn in its layer (on the label, the
window, the shell, then dust over everything), each piece shown from an age
on; `--tables` prints those ages for display.cpp."""
import math
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
ID = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith('-') else "wane"
FONT = 'font-family="UniversNextPro-Light"'

# The cassette in millimetres, placed as the board places it.
S = 2.72
X0, Y0 = 240 - 50.2 * S, 42.6
HUB_Y, HUBS, HUB_R, FULL_R = 27, (28.95, 71.45), 10.7, 24.4
PACK_AUTHORED = 13                          # packs are circles of 13 mm, scaled by the frame
RING_STEP, RINGS = 1.6, 8                   # winding rings, 12.3 to 23.5 mm
WINDOW = (22.4, 19.5, 78.0, 34.5, 1.6)
SHELL = (0, 0, 100.4, 63.8, 2.4)
LABEL = (6.4, 5.4, 94.0, 48.2, 1.6)
TAPE_LEFT, TAPE_RIGHT, TAPE_Y = 15.4, 85.0, 61.2
NEEDLE_X0, NEEDLE_STEP, NEEDLE_BASE, NEEDLE_LENGTH = 393, 10.3, 196, 118
WAVE_X, WAVE_Y = 51, 134                    # the oscillator's centre: 12..90 across, +-28 up


def X(mm):
    return X0 + mm * S


def Y(mm):
    return Y0 + mm * S


def num(v):
    text = f"{round(v, 4):.4f}".rstrip("0").rstrip(".")
    return "0" if text in ("", "-0") else text


def outline(points):
    return "M" + " L".join(f"{num(x)},{num(y)}" for x, y in points) + " Z"


def rect_points(x0, y0, x1, y1):
    return [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]


def arc(cx, cy, r, a0, a1, steps=4):
    """Points on an arc in degrees (screen y down), both ends included."""
    return [(cx + r * math.cos(math.radians(a0 + (a1 - a0) * i / steps)),
             cy + r * math.sin(math.radians(a0 + (a1 - a0) * i / steps))) for i in range(steps + 1)]


def rounded(x0, y0, x1, y1, r):
    """A rounded rectangle in mm, clockwise from its top-left corner."""
    return (arc(x0 + r, y0 + r, r, 180, 270) + arc(x1 - r, y0 + r, r, 270, 360) +
            arc(x1 - r, y1 - r, r, 0, 90) + arc(x0 + r, y1 - r, r, 90, 180))


def mm(points):
    return [(X(x), Y(y)) for x, y in points]


def holed(outer, hole, split=HUB_Y, overlap=0.25):
    """An outer rounded rectangle less a rounded hole, as two notched halves
    that meet (overlapping slightly) at `split`: no outline doubles back."""
    x0, y0, x1, y1, r = outer
    wx0, wy0, wx1, wy1, wr = hole
    up, down = split + overlap, split - overlap
    upper = ([(x0, up)] + arc(x0 + r, y0 + r, r, 180, 270) + arc(x1 - r, y0 + r, r, 270, 360) +
             [(x1, up), (wx1, up)] + arc(wx1 - wr, wy0 + wr, wr, 0, -90) + arc(wx0 + wr, wy0 + wr, wr, 270, 180) +
             [(wx0, up)])
    lower = ([(x0, down), (wx0, down)] + arc(wx0 + wr, wy1 - wr, wr, 180, 90) + arc(wx1 - wr, wy1 - wr, wr, 90, 0) +
             [(wx1, down), (x1, down)] + arc(x1 - r, y1 - r, r, 0, 90) + arc(x0 + r, y1 - r, r, 90, 180))
    return mm(upper), mm(lower)


def path(d, fill, stroke="none", width=None, node=None, at=None):
    ident = f' id="{ID}_node_{node}"' if node is not None else ""
    move = f' transform="matrix(1 0 0 1 {num(at[0])} {num(at[1])})"' if at else ""
    w = f' stroke-width="{num(width)}"' if width is not None else ""
    return f'<path{ident}{move} d="{d}" fill="{fill}" stroke="{stroke}"{w}/>'


def circle(cx, cy, r, fill, stroke="none", width=None, node=None, at=None):
    ident = f' id="{ID}_node_{node}"' if node is not None else ""
    move = f' transform="matrix(1 0 0 1 {num(at[0])} {num(at[1])})"' if at else ""
    w = f' stroke-width="{num(width)}"' if width is not None else ""
    return f'<circle{ident}{move} cx="{num(cx)}" cy="{num(cy)}" r="{num(r)}" fill="{fill}" stroke="{stroke}"{w}/>'


def text(x, y, size, fill, words):
    return f'<text transform="matrix(1 0 0 1 {num(x)} {num(y)})" fill="{fill}" {FONT} font-size="{num(size)}px">{words}</text>'


def hash01(n):
    """The board's hash, so the dust lies where the board puts it."""
    x = math.sin(n * 12.9898 + 78.233) * 43758.5453
    return x - math.floor(x)


# Age's wear: still artwork, each piece shown from an age (percent) on, the
# label's peel in stages that replace one another. Shapes are in mm, drawn in px.
def distinct(points):
    """Points as the artwork writes them, each one different from the last."""
    kept = []
    for x, y in points:
        p = (float(num(x)), float(num(y)))
        if not kept or p != kept[-1]:
            kept.append(p)
    while len(kept) > 1 and kept[0] == kept[-1]:
        kept.pop()
    return kept


def scratch(x0, y0, x1, y1, bow, width, steps=10):
    """A scratch from (x0, y0) to (x1, y1) mm bowing `bow` mm aside at its
    middle: a ribbon `width` px wide at most, tapering to a point at each end."""
    (ax, ay), (bx, by) = (X(x0), Y(y0)), (X(x1), Y(y1))
    length = math.hypot(bx - ax, by - ay)
    nx, ny = (by - ay) / length, -(bx - ax) / length
    centre, half = [], []
    for i in range(steps + 1):
        t = i / steps
        b = 4 * t * (1 - t) * bow * S
        centre.append((ax + (bx - ax) * t + nx * b, ay + (by - ay) * t + ny * b))
        half.append(math.sin(math.pi * t) ** 0.6 * width / 2)
    left = [(x + nx * w, y + ny * w) for (x, y), w in zip(centre, half)]
    right = [(x - nx * w, y - ny * w) for (x, y), w in zip(centre[1:-1], half[1:-1])]
    return distinct(left + list(reversed(right)))


def blob(cx, cy, rx, ry, turn, seed, corners=9, rough=0.35):
    """An irregular blot about (cx, cy) mm, `rx` by `ry` mm, turned `turn` degrees."""
    c, s = math.cos(math.radians(turn)), math.sin(math.radians(turn))
    points = []
    for i in range(corners):
        a = 2 * math.pi * (i + 0.3 * (hash01(seed + 3 * i) - 0.5)) / corners
        r = 1 + rough * (hash01(seed + 7 * i + 1) - 0.5)
        u, v = rx * r * math.cos(a), ry * r * math.sin(a)
        points.append((X(cx + u * c - v * s), Y(cy + u * s + v * c)))
    return distinct(points)


def clip(polygon, a, b, inside):
    """The part of a convex polygon on the side of the line a-b where `inside` lies."""
    side = lambda p: (b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0])
    sign = 1 if side(inside) > 0 else -1
    kept = []
    for i, p in enumerate(polygon):
        q = polygon[(i + 1) % len(polygon)]
        sp, sq = side(p) * sign, side(q) * sign
        if sp >= 0:
            kept.append(p)
        if (sp < 0) != (sq < 0):
            t = sp / (sp - sq)
            kept.append((p[0] + (q[0] - p[0]) * t, p[1] + (q[1] - p[1]) * t))
    return kept


def fold(points, a, b, depth, lean):
    """Points folded over the line a-b, their distance from it scaled by `depth`
    (the lifted paper foreshortened as it curls) and carried `lean` times that
    distance along it, toward b."""
    dx, dy = b[0] - a[0], b[1] - a[1]
    length = math.hypot(dx, dy)
    dx, dy = dx / length, dy / length
    folded = []
    for x, y in points:
        along = (x - a[0]) * dx + (y - a[1]) * dy
        across = -((x - a[0]) * -dy + (y - a[1]) * dx) * depth
        along += abs(across) * lean
        folded.append((a[0] + along * dx - across * dy, a[1] + along * dy + across * dx))
    return folded


def peel(corner, along, up, depth, lean):
    """A corner of the label lifted along a fold from `along` mm in from the
    corner on its long edge to `up` mm along its short edge: the bare shell
    where it was, the shadow it throws and its paler underside, curled back
    over the label. `corner` is 'top right' or 'bottom left'."""
    x0, y0, x1, y1, r = LABEL
    if corner == "top right":
        tip, a, b = (x1, y0), (x1 - along, y0), (x1, y0 + up)
    else:
        tip, a, b = (x0, y1), (x0 + along, y1), (x0, y1 - up)
    tip, a, b = (X(tip[0]), Y(tip[1])), (X(a[0]), Y(a[1])), (X(b[0]), Y(b[1]))
    # Bare: the label's outline grown a little, so its edge line goes too.
    bare = clip(mm(rounded(x0 - 0.3, y0 - 0.3, x1 + 0.3, y1 + 0.3, r + 0.3)), a, b, tip)
    lip = clip(mm(rounded(x0, y0, x1, y1, r)), a, b, tip)
    flap = fold(lip, a, b, depth, lean)
    shadow = [(x - 0.4, y + 0.8) for x, y in fold(lip, a, b, depth * 1.15, lean * 1.1)]
    # The curl catches the light toward its tip: the far half of the flap, paler.
    mid = lambda p: ((p[0] + tip[0]) / 2, (p[1] + tip[1]) / 2)
    curl = fold(clip(lip, mid(a), mid(b), tip), a, b, depth, lean)
    return distinct(bare), distinct(shadow), distinct(flap), distinct(curl)


# Nodes (display.h).
TWIN, WAVE, TAPE_A, TAPE_B = 0, 1, 2, 3
HEADER = 4
PACK = 12
RING = 14              # 8 per hub
TOOTH = 30             # per hub: 6 teeth then the clamp
STRIPE = 44
NEEDLE = 47
PIVOT = 55
WEAR = 63             # age's wear, in the order the artwork draws it

# The wear, by layer: (from, until, points or circle, fill, stroke, stroke width).
# Each piece shows while from <= age (percent, as the header shows it) < until.
SHELL_INK, LABEL_INK = "#0d0d0d", "#1a1a1a"
FOREVER = 101
wear = {"label": [], "window": [], "shell": [], "dust": []}


def add(layer, start, shape, fill, stroke="none", width=None, until=FOREVER):
    wear[layer].append((start, until, shape, fill, stroke, width))


# The label's print rubbed through to the paper: flecks out of "wane" and the
# side letter, gaps in the title lines, the stripes' ends.
for cx, cy, rx, ry, turn, start in ((21.9, 10.9, 0.35, 0.22, 15, 46), (25.6, 8.4, 0.3, 0.2, -20, 58),
                                    (29.2, 11.1, 0.45, 0.25, 5, 68), (32.4, 9.6, 0.35, 0.3, 40, 76),
                                    (23.6, 8.3, 0.3, 0.22, 0, 84), (27.0, 10.6, 0.4, 0.28, -30, 90),
                                    (30.1, 8.6, 0.32, 0.2, 10, 95), (33.0, 11.3, 0.5, 0.25, 0, 99),
                                    (11.1, 14.1, 0.35, 0.22, 60, 52), (15.4, 16.2, 0.3, 0.2, -60, 82)):
    add("label", start, blob(cx, cy, rx, ry, turn, int(cx * 10 + cy), 7, 0.45), LABEL_INK)
for cx, cy, rx, ry, turn, start in ((44.0, 12.75, 1.5, 0.35, 0, 52), (61.0, 12.75, 1.1, 0.3, 0, 76),
                                    (35.0, 17.75, 1.3, 0.35, 0, 68), (72.0, 17.75, 1.7, 0.3, 0, 88),
                                    (7.3, 39.2, 0.9, 0.8, 30, 74), (93.2, 43.6, 0.8, 0.9, -20, 86)):
    add("label", start, blob(cx, cy, rx, ry, turn, int(cx * 10 + cy)), LABEL_INK)
# Scuffs across the paper.
for x0, y0, x1, y1, bow, w, start in ((40, 6.8, 64, 15.4, 0.6, 0.5, 54), (80, 22, 92.5, 34, -0.4, 0.5, 72),
                                      (9, 22, 20, 36, 0.3, 0.45, 88), (30, 46.6, 70, 36.2, 0.8, 0.5, 96)):
    add("label", start, scratch(x0, y0, x1, y1, bow, w), "#2e2e2e")
# Tears: a bite from its foot with a split running up into the stripes, and a nick in its side.
add("label", 78, distinct(mm([(57.6, 48.6), (58.2, 47.9), (58.9, 47.6), (59.4, 46.9), (60.1, 47.2), (60.6, 46.6),
                             (61.3, 47.3), (62.0, 47.5), (62.6, 48.1), (63.0, 48.6)])), SHELL_INK)
add("label", 78, scratch(60.5, 46.9, 61.6, 43.9, 0.15, 0.6), SHELL_INK)
add("label", 92, distinct(mm([(6.0, 25.8), (7.2, 26.4), (7.7, 27.2), (7.0, 27.7), (7.4, 28.3), (6.0, 28.8)])), SHELL_INK)
# The label peeling: its top-right corner in four stages, its bottom-left in two;
# each stage the bare shell, the shadow and the curled underside.
for corner, stages in (("top right", ((16, 38, 3.4, 2.6), (38, 60, 6.6, 4.8), (60, 82, 10.4, 7.4), (82, FOREVER, 15.0, 10.6))),
                       ("bottom left", ((66, 88, 4.2, 3.0), (88, FOREVER, 7.4, 5.2)))):
    for start, until, along, up in stages:
        bare, shadow, flap, curl = peel(corner, along, up, 0.8, 0.15)
        add("label", start, bare, SHELL_INK, until=until)
        add("label", start, shadow, "#070707", until=until)
        add("label", start, flap, "#4f4f4f", "#6a6a6a", 0.4, until=until)
        add("label", start, curl, "#616161", until=until)

# Scratches in the window's clear plastic, over the reels.
for x0, y0, x1, y1, bow, w, start in ((30, 33.2, 47, 21.5, 0.5, 0.55, 24), (52, 31.5, 74.5, 26.0, -0.4, 0.5, 44),
                                      (25, 25.0, 38, 20.6, 0.3, 0.45, 63), (58, 21.0, 76.8, 33.4, 0.8, 0.5, 80),
                                      (36, 33.6, 64, 30.5, -0.5, 0.45, 94)):
    add("window", start, scratch(x0, y0, x1, y1, bow, w), "#6a6a6a")

# The shell: scratches, its edges rubbed pale and chipped.
for x0, y0, x1, y1, bow, w, start in ((18, 3.6, 46, 1.6, 0.3, 0.5, 12), (1.6, 20, 4.6, 44, 0.4, 0.45, 34),
                                      (2, 50.5, 13, 58, -0.6, 0.5, 50), (24, 61.6, 42, 53.8, 0.5, 0.5, 58),
                                      (96.8, 12, 98.6, 38, -0.3, 0.45, 70), (62, 62.6, 84, 52.6, -0.4, 0.45, 76),
                                      (55, 3.8, 82, 1.9, 0.2, 0.45, 86), (86, 50.0, 99, 56.5, 0.3, 0.45, 92)):
    add("shell", start, scratch(x0, y0, x1, y1, bow, w), "#3c3c3c")
for x0, y0, x1, y1, start in ((0.6, 9, 0.7, 14, 42), (4, 63.2, 9, 63.25, 64), (66, 0.55, 72, 0.6, 74),
                              (99.8, 44, 99.75, 50, 82)):
    add("shell", start, scratch(x0, y0, x1, y1, 0.05, 0.7), "#4e4e4e")
for cx, cy, rx, ry, turn, start in ((0.7, 63.1, 1.0, 0.6, 45, 56), (33, 0.05, 0.9, 0.45, 0, 88),
                                    (100.35, 30, 0.4, 0.8, 0, 97)):
    add("shell", start, blob(cx, cy, rx, ry, turn, int(cx * 10 + cy), 8, 0.4), "#000000")

# Dust settling, finer first, and grime gathering in corners and along edges.
SPECKS = 36
for k in range(SPECKS):
    x, y = 1.5 + hash01(k + 50) * 97.4, 1.5 + hash01(k + 80) * 60.8
    r = 0.17 + (0.08 + 0.2 * hash01(k + 7)) * (0.4 + k / SPECKS)
    grey = "#%02x%02x%02x" % ((0x50 + round(0x48 * hash01(k + 21)),) * 3)
    add("dust", 4 + round(94 * k / SPECKS), ("circle", X(x), Y(y), r * S), grey)
for cx, cy, rx, ry, turn, start in ((1.4, 30, 0.35, 1.2, 4, 40), (77.0, 33.9, 0.9, 0.35, -10, 55),
                                    (40, 34.05, 1.4, 0.3, 0, 68), (98.6, 8, 0.4, 1.0, 10, 78),
                                    (50.2, 63.0, 1.6, 0.35, 0, 86), (30, 48.75, 1.2, 0.25, 0, 95)):
    add("dust", start, blob(cx, cy, rx, ry, turn, int(cx * 10 + cy), 9, 0.5), "#3a3a3a")

WEARS = sum(len(pieces) for pieces in wear.values())
NODES = WEAR + WEARS
wear_ids = {}
for layer in ("label", "window", "shell", "dust"):
    wear_ids[layer] = range(WEAR + sum(len(wear[l]) for l in wear_ids), WEAR + sum(len(wear[l]) for l in wear_ids) + len(wear[layer]))


def draw_wear(layer):
    """Appends a layer of wear, each piece a node display.cpp shows by age."""
    for node, (start, until, shape, fill, stroke, width) in zip(wear_ids[layer], wear[layer]):
        if shape[0] == "circle":
            out.append(circle(shape[1], shape[2], shape[3], fill, stroke, width, node))
        else:
            out.append(path(outline(shape), fill, stroke, width, node))

out = [f'<g id="{ID}_panel">',
       "<!-- SDK two-page header: keep this block before machine artwork below y=38. -->",
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

# Left: the oscillator. Nodes 0 (the detuned twin, behind) and 1: ribbons of
# 32 samples out along the top and back 0.6 px below, reshaped by display.cpp.
out.append(text(12, 58, 9, "#4d4d4d", "osc"))
out.append(path(outline(rect_points(12, 64, 90, 64.6)), "#262626"))
out.append(path(outline(rect_points(12, 204, 90, 204.6)), "#262626"))
for node, ink in ((TWIN, "#4d4d4d"), (WAVE, "#e6e6e6")):
    top = [(-39 + i * 78 / 31, -24 * math.sin(math.pi * 4 * i / 31)) for i in range(32)]
    out.append(path(outline(top + [(x, y + 0.6) for x, y in reversed(top)]), "none", ink, 0.6, node, (WAVE_X, WAVE_Y)))

# Right: a needle per held note, pivoting at its foot (nodes 47..54, pivots 55..62).
out.append(text(446.5, 58, 9, "#4d4d4d", "notes"))
out.append(path(outline(rect_points(390, 64, 468, 64.6)), "#262626"))
out.append(path(outline(rect_points(390, 204, 468, 204.6)), "#262626"))
for k in range(8):
    x = NEEDLE_X0 + k * NEEDLE_STEP
    out.append(path(outline(rect_points(x - 1.5, 74, x + 1.5, 74.6)), "#333333"))
for k in range(8):
    x = NEEDLE_X0 + k * NEEDLE_STEP
    out.append(path(outline(rect_points(-0.4, -NEEDLE_LENGTH + 0.15, 0.4, -0.15)), "#333333", "#333333", 0.3,
                    NEEDLE + k, (x, NEEDLE_BASE)))
for k in range(8):
    x = NEEDLE_X0 + k * NEEDLE_STEP
    out.append(circle(0, 0, 1.45, "#262626", "#262626", 0.3, PIVOT + k, (x, NEEDLE_BASE)))

# The cassette. First what shows through the window: its dark ground, the two
# tape packs (scaled circles, 12-13), the winding rings inside them (14-29,
# shown while within their pack), the hubs, and their turning splines and
# clamps (30-43). The shell and label, drawn next with the window cut out,
# hide everything outside the window.
wx0, wy0, wx1, wy1, wr = WINDOW
out.append(path(outline(mm(rounded(wx0, wy0, wx1, wy1, wr))), "#050505"))
for h, hx in enumerate(HUBS):
    out.append(circle(0, 0, PACK_AUTHORED * S, "#383838", "#383838", 0.5, PACK + h, (X(hx), Y(HUB_Y))))
for h, hx in enumerate(HUBS):
    for k in range(RINGS):
        r = HUB_R + RING_STEP * (k + 1)
        out.append(circle(X(hx), Y(HUB_Y), r * S, "none", "#4a4a4a", 1, RING + RINGS * h + k))
for h, hx in enumerate(HUBS):
    out.append(circle(X(hx), Y(HUB_Y), HUB_R * S, "#7a7a7a"))
    out.append(circle(X(hx), Y(HUB_Y), 6.6 * S, "#5e5e5e"))
    out.append(circle(X(hx), Y(HUB_Y), 4.2 * S, "#000000"))
for h, hx in enumerate(HUBS):
    for k in range(6):
        a = math.radians(60 * (k + 1))
        c, s = math.cos(a), math.sin(a)
        square = [(u * S * c - v * S * s, u * S * s + v * S * c) for u, v in rect_points(2.9, -0.7, 4.3, 0.7)]
        out.append(path(outline(square), "#7a7a7a", "none", None, TOOTH + 7 * h + k, (X(hx), Y(HUB_Y))))
    clamp = [(u * S, v * S) for u, v in rect_points(7.6, -0.9, 10.0, 0.9)]
    out.append(path(outline(clamp), "#2a2a2a", "none", None, TOOTH + 7 * h + 6, (X(hx), Y(HUB_Y))))

# The shell and its edge, the write-protect notches on its top.
for half in holed(SHELL, WINDOW):
    out.append(path(outline(half), "#0d0d0d"))
out.append(path(outline(mm(rounded(*SHELL))), "none", "#8c8c8c", 1))
for x in (8.6, 85.8):
    out.append(path(outline(mm(rect_points(x, 0, x + 6, 1.3))), "#000000", "#4d4d4d", 1))

# The label: title lines, "wane", "C60", the side letter, and three stripes
# that fade with age (nodes 44-46, repainted by display.cpp).
for half in holed(LABEL, WINDOW):
    out.append(path(outline(half), "#1a1a1a"))
out.append(path(outline(mm(rounded(*LABEL))), "none", "#3d3d3d", 1))
for i, ink in enumerate(("#d9d9d9", "#9a9a9a", "#5c5c5c")):
    y = 37.4 + i * 2.7
    out.append(path(outline(rect_points(X(LABEL[0]) + 0.5, Y(y), X(LABEL[2]) - 0.5, Y(y + 2.7) - 0.3)),
                    ink, ink, 0.3, STRIPE + i))
for y in (12.6, 17.6):
    out.append(path(outline(rect_points(X(20.5), Y(y), X(90), Y(y) + 0.8)), "#3d3d3d"))
out.append(text(X(20.8), Y(11.6), 5.4 * S, "#e6e6e6", "wane"))
out.append(text(X(89.8) - 13, Y(11.4), 2.6 * S, "#808080", "C60"))
out.append(text(X(9.6), Y(17.4), 10 * S, "#d9d9d9", "A"))
draw_wear("label")
out.append(path(outline(mm(rounded(wx0, wy0, wx1, wy1, wr))), "none", "#8c8c8c", 1))
draw_wear("window")

# The foot: its trapezoid, the pad's leaf spring and felt, the tape (nodes 2
# and 3, its two halves, reshaped by display.cpp: 0.7 px ribbons stroked 0.8 px), the head's opening, the
# capstans and the guides.
out.append(path(outline(mm([(20.2, 51.6), (80.2, 51.6), (86.4, 63.8), (14, 63.8)])), "#121212", "#5c5c5c", 1))
out.append(path(outline(rect_points(X(45.6), Y(59.3), X(54.8), Y(59.3) + 1)), "#4d4d4d"))
out.append(path(outline(mm(rect_points(48.1, 59.7, 52.3, 60.8))), "#bdbdbd"))
step = (TAPE_RIGHT - TAPE_LEFT) / 62
for node, first in ((TAPE_A, 0), (TAPE_B, 31)):
    middle = TAPE_LEFT + (first + 15.5) * step
    top = [((TAPE_LEFT + (first + i) * step - middle) * S, 0) for i in range(32)]
    out.append(path(outline(top + [(x, 0.7) for x, _ in reversed(top)]), "none", "#a6a6a6", 0.8, node,
                    (X(middle), Y(TAPE_Y))))
out.append(path(outline(mm(rect_points(44.7, 58.4, 55.7, 63.8))), "none", "#8c8c8c", 1))
for x in (32.4, 68):
    out.append(circle(X(x), Y(58.4), 1.6 * S, "none", "#8c8c8c", 1))
for x in (24.8, 75.6):
    out.append(circle(X(x), Y(56.6), 1.05 * S, "none", "#8c8c8c", 1))

draw_wear("shell")

# Five screws: the corners and the foot's middle.
for x, y in ((3.5, 3.5), (96.9, 3.5), (3.5, 60.3), (96.9, 60.3), (50.2, 54.4)):
    out.append(circle(X(x), Y(y), 1.25 * S, "#262626", "#5c5c5c", 1))
    out.append(path(outline(mm(rect_points(x - 0.8, y - 0.15, x + 0.8, y + 0.15))), "#000000"))
    out.append(path(outline(mm(rect_points(x - 0.15, y - 0.8, x + 0.15, y - 0.15))), "#000000"))
    out.append(path(outline(mm(rect_points(x - 0.15, y + 0.15, x + 0.15, y + 0.8))), "#000000"))

draw_wear("dust")

# Palette (display.h): the stripes' greys by age, the tape (15 is spare),
# the needles and pivots lit and unlit. Parked off screen.
palette = [("#000000", 0.3), ("#d9d9d9", 0.3), ("#9a9a9a", 0.3), ("#5c5c5c", 0.3), ("#a6a6a6", 0.3),
           ("#808080", 0.3), ("#575757", 0.3), ("#6b6b6b", 0.3), ("#5e5e5e", 0.3), ("#4f4f4f", 0.3),
           ("#333333", 0.3), ("#e6e6e6", 0.3), ("#999999", 0.3), ("#262626", 0.3), ("#a6a6a6", 0.8),
           ("#333333", 0.8)]
for i, (grey, width) in enumerate(palette):
    out.append(f'<circle id="{ID}_palette_{i}" cx="640" cy="0" r="1" fill="{grey}" stroke="{grey}" stroke-width="{width}"/>')
out.append("</g>")


def tables():
    """display.cpp's constants that follow this geometry."""
    q10 = lambda v: round(v * 1024)
    print("hub x, y (Q10 px):", [q10(X(h)) for h in HUBS], q10(Y(HUB_Y)))
    print("tape origins (Q10 px):", [q10(X(TAPE_LEFT + (f + 15.5) * step)) for f in (0, 31)], q10(Y(TAPE_Y)))
    print("tape x step, first x (Q10 px):", q10(step * S), q10(-15.5 * step * S))
    print("tape delay per point (Q8 blocks):", round(step / 60 * 689.0625 * 256))
    print("head bump (Q10 px):", [round(S * 0.35 * math.exp(-((d * step) / 4) ** 2) * 1024) for d in range(32)])
    for k in range(8):
        f1, phase, f2 = 0.13 + 0.05 * hash01(k), 6 * hash01(k + 9), 0.37 + 0.1 * hash01(k + 3)
        print("needle", k, round(f1 * 2 ** 32 / 689.0625), round(phase / (2 * math.pi) % 1 * 2 ** 32),
              round(f2 * 2 ** 32 / 689.0625))
    pieces = [piece for layer in ("label", "window", "shell", "dust") for piece in wear[layer]]
    for name, column in (("wear_from", 0), ("wear_until", 1)):
        values = [str(p[column]) for p in pieces]
        rows = [", ".join(values[i:i + 20]) for i in range(0, len(values), 20)]
        print(f"const opxy::u8 {name}[node::wears] = {{\n    " + ",\n    ".join(rows) + "};")
    print("wear:", WEARS, "nodes:", NODES)


if "--tables" in sys.argv:
    tables()
else:
    target = ROOT / "panel.svg.fragment" if ID == "wane" else pathlib.Path(sys.argv[2])
    target.write_text("\n".join(out) + "\n")
    print("wrote", target)
