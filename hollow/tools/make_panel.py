#!/usr/bin/env python3
"""Writes panel.svg.fragment, Hollow's screen artwork, the generated block of
display.h that keeps display.cpp in step with it, and the node count in
definition.json: run it after changing a shape here, and commit them all. `--sheet out.svg` also writes a debug sheet
that tags each body's nodes with data-family, to view one figure at a time.

The screen is an engraved plate of acoustics (README, "Screen"). Fig. 1, on
the left, is the current body in section: the organs of voice (vox), a violin
(str), a stopped wooden organ pipe beside a smaller one (pipe), or two wine
glasses (glass). Its standing wave is drawn along the vocal tract, the
string, the air column or the rim, and its sympathetic partner (the nasal
cavity, the open string beside it, the neighbouring pipe, the second glass)
rings beside it. Fig. 2, on the right, is Koenig's analyser: a Helmholtz
resonator for each of the body's eight modes, each as large as its pitch
needs, feeding a manometric flame that burns as high as its mode rings.

Every node is one closed outline of distinct points (or a circle), so every
outline encloses area: thin lines are thin closed ribbons. check() holds every
path to the device's rules: one subpath, at most 64 points, neighbours at least
1/8 px apart, a closing edge of at least 1/4 px and no turn straight back. What
moves is drawn unfilled, and display.cpp moves it in turns (see its top)."""
import math
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
ARGS = sys.argv[1:]
SHEET = None
if '--sheet' in ARGS:
    at = ARGS.index('--sheet')
    SHEET = pathlib.Path(ARGS[at + 1])
    del ARGS[at:at + 2]
ID = ARGS[0] if ARGS else "hollow"   # another prefix writes a preview copy to ARGS[1]
FONT = 'font-family="UniversNextPro-Light"'


# ---------------------------------------------------------------------------
# Geometry. Points are (x, y) screen pixels.

def num(v, digits=2):
    s = f"{round(v, digits):.{digits}f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


LOADER_POINTS = 64   # the device's SVG loader takes one subpath of at most this many points


def thin(points, most):
    """Drops the points that matter least to the shape (Visvalingam) until at most `most` remain."""
    pts = list(points)
    while len(pts) > most:
        def area(i):
            (ax, ay), (bx, by), (cx, cy) = pts[i - 1], pts[i], pts[(i + 1) % len(pts)]
            return abs((bx - ax) * (cy - ay) - (cx - ax) * (by - ay))
        del pts[min(range(len(pts)), key=area)]
    return pts


def outline(points, digits=1):
    """M, L..., Z through distinct points at least 1/8 px apart, at most
    LOADER_POINTS of them, starting on the longest edge so it closes over that."""
    pts = []
    for x, y in thin(points, LOADER_POINTS):
        p = (float(num(x, digits)), float(num(y, digits)))
        if not pts or math.dist(pts[-1], p) >= 0.125:
            pts.append(p)
    while len(pts) > 1 and math.dist(pts[0], pts[-1]) < 0.125:
        pts.pop()
    assert len(set(pts)) == len(pts), "an outline repeats a point"
    longest = max(range(len(pts)), key=lambda i: math.dist(pts[i - 1], pts[i]))
    pts = pts[longest:] + pts[:longest]
    return "M" + " L".join(f"{num(x, digits)},{num(y, digits)}" for x, y in pts) + " Z"


def check(text):
    """Fails on any artwork path the device couldn't draw as written."""
    faults = []
    for m in re.finditer(r'<path [^>]*?\bd="([^"]*)"[^>]*/>', text):
        name = re.search(r'id="([^"]+)"', m.group(0))
        name = name.group(1) if name else m.group(1)[:24]
        d = m.group(1)
        pts = [(float(x), float(y)) for x, y in re.findall(r"(-?[\d.]+),(-?[\d.]+)", d)]
        if d.count("M") != 1 or d.count("Z") != 1:
            faults.append(f"{name}: not one subpath")
        if len(pts) > LOADER_POINTS:
            faults.append(f"{name}: {len(pts)} points")
        if pts and math.dist(pts[0], pts[-1]) < 0.25:
            faults.append(f"{name}: closes over {math.dist(pts[0], pts[-1]):.2f} px")
        for i in range(len(pts)):
            a, b, c = pts[i - 2], pts[i - 1], pts[i]
            if math.dist(b, c) < 0.125:
                faults.append(f"{name}: points {i - 1},{i} {math.dist(b, c):.2f} px apart")
            ux, uy, vx, vy = b[0] - a[0], b[1] - a[1], c[0] - b[0], c[1] - b[1]
            if abs(ux * vy - uy * vx) < 1e-9 and ux * vx + uy * vy < 0:
                faults.append(f"{name}: turns straight back at point {i - 1}")
    assert not faults, "\n".join(faults)


DETAIL = 0.5


def spline(points, n=6, closed=False):
    """A Catmull-Rom curve through points, n steps a span."""
    pts = list(points)
    m = len(pts)
    out = []
    n = max(2, round(n * DETAIL))
    for i in range(m if closed else m - 1):
        p0 = pts[(i - 1) % m] if closed or i > 0 else pts[0]
        p1, p2 = pts[i % m], pts[(i + 1) % m]
        p3 = pts[(i + 2) % m] if closed or i + 2 < m else pts[-1]
        for s in range(n):
            t = s / n
            out.append(tuple(0.5 * (2 * p1[j] + (p2[j] - p0[j]) * t + (2 * p0[j] - 5 * p1[j] + 4 * p2[j] - p3[j]) * t * t +
                                    (3 * p1[j] - p0[j] - 3 * p2[j] + p3[j]) * t ** 3) for j in (0, 1)))
    if not closed:
        out.append(pts[-1])
    return out


def normals(points, closed=False):
    out = []
    m = len(points)
    for i in range(m):
        a = points[(i - 1) % m] if closed or i > 0 else points[i]
        b = points[(i + 1) % m] if closed or i < m - 1 else points[i]
        dx, dy = b[0] - a[0], b[1] - a[1]
        d = math.hypot(dx, dy) or 1
        out.append((dy / d, -dx / d))
    return out


def offset(points, d, closed=True):
    return [(x + nx * d, y + ny * d) for (x, y), (nx, ny) in zip(points, normals(points, closed))]


def ribbon(points, width=0.3):
    """A line as a thin closed ribbon: out along one side, back along the other."""
    return offset(points, width / 2, False) + list(reversed(offset(points, -width / 2, False)))


def inset(points, d):
    """A closed outline moved d px inward, whichever way round it runs."""
    twice = sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(points, points[1:] + points[:1]))
    return offset(points, d if twice > 0 else -d)


def resample(points, n):
    """n points evenly spaced along a polyline, both ends included."""
    lengths = [0.0]
    for a, b in zip(points, points[1:]):
        lengths.append(lengths[-1] + math.dist(a, b))
    out, j = [], 0
    for i in range(n):
        target = lengths[-1] * i / (n - 1)
        while j < len(lengths) - 2 and lengths[j + 1] < target:
            j += 1
        span = lengths[j + 1] - lengths[j] or 1
        t = (target - lengths[j]) / span
        out.append((points[j][0] + (points[j + 1][0] - points[j][0]) * t,
                    points[j][1] + (points[j + 1][1] - points[j][1]) * t))
    return out


def ellipse(cx, cy, rx, ry=None, n=24, start=0.0):
    ry = rx if ry is None else ry
    return [(cx + rx * math.cos(start + 2 * math.pi * i / n), cy + ry * math.sin(start + 2 * math.pi * i / n))
            for i in range(n)]


def arc(cx, cy, rx, ry, a0, a1, n):
    """Points on an elliptical arc in degrees (screen y down), both ends included."""
    return [(cx + rx * math.cos(math.radians(a0 + (a1 - a0) * i / n)),
             cy + ry * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]


def rect(x0, y0, x1, y1):
    return [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]


def hatch(x0, x1, y0, y1, step=2.4, slope=1.0):
    """Section hatching across a vertical strip: parallel strokes at 45 degrees,
    joined along the strip's edges (where its outline covers them)."""
    w = x1 - x0
    pts, y, k = [], y0, 0
    while y + w * slope <= y1 + 0.01:
        a, b = (x0, y), (x1, y + w * slope)
        pts += [a, b] if k % 2 == 0 else [b, a]
        y += step
        k += 1
    if pts and pts[-1][0] != x0:
        pts.append((x0, pts[-1][1]))
    return pts


def hatched(f, name, x0, x1, y0, y1, step=2.4):
    """Section hatching in as many nodes as the loader needs, keeping its rhythm."""
    strokes = int((y1 - y0 - (x1 - x0)) / step) + 1
    each = (LOADER_POINTS - 2) // 2
    for k in range(0, strokes, each):
        top = y0 + k * step
        f.shape(f"{name}{k // each}", hatch(x0, x1, top, min(y1, top + (each - 1) * step + (x1 - x0)), step), "none", HATCH, 0.3)


def hatch_h(x0, x1, y0, y1, step=2.4):
    """Hatching across a horizontal strip, strokes at 45 degrees."""
    h = y1 - y0
    pts, x, k = [], x0, 0
    while x + h <= x1 + 0.01:
        a, b = (x, y1), (x + h, y0)
        pts += [a, b] if k % 2 == 0 else [b, a]
        x += step
        k += 1
    if pts and pts[-1][1] != y1:
        pts.append((pts[-1][0], y1))
    return pts


# ---------------------------------------------------------------------------
# Artwork elements.

def weight(width):
    """Authored line weights, from fine (0.3) to bold (0.75), as the screen shows them: 0.5 to 1.1 px."""
    return width if width < 0.29 else 0.5 + (width - 0.3) * 1.35


def path_svg(d, fill, stroke, width, node=None, at=None):
    width = weight(width)
    ident = f' id="{ID}_node_{node}"' if node is not None else ""
    move = f' transform="matrix(1 0 0 1 {num(at[0], 4)} {num(at[1], 4)})"' if at else ""
    return f'<path{ident}{move} d="{d}" fill="{fill}" stroke="{stroke}" stroke-width="{num(width, 4)}"/>'


def circle_svg(cx, cy, r, fill, stroke, width, node=None, at=None):
    width = weight(width)
    ident = f' id="{ID}_node_{node}"' if node is not None else ""
    move = f' transform="matrix(1 0 0 1 {num(at[0], 4)} {num(at[1], 4)})"' if at else ""
    return (f'<circle{ident}{move} cx="{num(cx)}" cy="{num(cy)}" r="{num(r)}" fill="{fill}" stroke="{stroke}" '
            f'stroke-width="{num(width, 4)}"/>')


def text_svg(x, y, size, fill, words, italic=False):
    return (f'<text transform="matrix(1 0 0 1 {num(x)} {num(y)})" fill="{fill}" {FONT} '
            f'font-size="{num(size)}px">{words}</text>')


class Layer:
    """Artwork in drawing order: each item is (svg maker taking its node number or None, family, name)."""

    def __init__(self, family=None):
        self.family = family
        self.items = []
        self.names = {}

    def shape(self, name, points, fill="none", stroke="#a6a6a6", width=0.6, at=None, node=True):
        small = max(max(x for x, _ in points) - min(x for x, _ in points), max(y for _, y in points) - min(y for _, y in points)) < 8
        d = outline([(x - at[0], y - at[1]) if at else (x, y) for x, y in points], 2 if small else 1)
        self.items.append((lambda n, d=d, at=at: path_svg(d, fill, stroke, width, n, at), name, node))

    def line(self, name, points, stroke="#a6a6a6", width=0.35, at=None, node=True):
        """A stroke of about `width` px: a 0.3 px ribbon outlined to that width,
        in as many pieces as the loader needs (they meet on a shared point)."""
        most = LOADER_POINTS // 2
        pieces = [points[i:i + most] for i in range(0, max(1, len(points) - 1), most - 1)]
        for k, piece in enumerate(pieces):
            self.shape(f"{name}{k}" if k else name, ribbon(piece, 0.3), stroke, stroke, max(0.1, width - 0.3), at, node)

    def circle(self, name, cx, cy, r, fill="none", stroke="#a6a6a6", width=0.6, at=None, node=True):
        self.items.append((lambda n: circle_svg(cx, cy, r, fill, stroke, width, n, at), name, node))

    def text(self, x, y, size, fill, words):
        self.items.append((lambda n: text_svg(x, y, size, fill, words), "text", False))


INK, MID, DIM, FAINT = "#b4b4b4", "#7a7a7a", "#4d4d4d", "#2b2b2b"
WOOD, DARK = "#101010", "#1c1c1c"
HATCH = "#3a3a3a"

STRANDS = {}   # family -> [(points, normals, anchor, loop) for the main strand and its partner]
SPECIAL = {}   # name -> (family, item index) for nodes display.cpp moves
JETS = {}      # pipe -> its jet's origin
FOLLOW = []    # vox: how far each tract sample follows the tongue, Q8
SOURCES = {}   # family -> breath source (x, y) and drift (dx, dy) px a second


def strand(family, which, points, direction, loop=False):
    """A strand's rest line: 32 samples of a ribbon or 64 of a loop, and the
    direction (unit or projected) its displacement moves each sample."""
    STRANDS.setdefault(family, [None, None])[which] = (points, direction, loop)


# ---------------------------------------------------------------------------
# vox: the organs of voice in mid-sagittal section, facing right, at about
# 0.9 px/mm: nasal cavity, palate, tongue (moved by the vowel), pharynx,
# larynx and the cervical spine behind.

def vox():
    f = Layer(0)
    face_points = [(206, 40), (212, 50), (210, 57), (215, 66), (222, 76), (229, 86), (228, 90.5), (221, 93.5),
                   (216, 94.5), (218.5, 100), (221.5, 104.5), (218, 108), (220.5, 112.5), (214, 120), (217.5, 129),
                   (215, 138), (207, 145), (193, 150), (180, 153.5), (180, 163), (184, 172), (181, 182), (178, 196),
                   (177, 214)]
    face = spline(face_points, 4)
    # The cut: everything below the skull's base, from the face back to the
    # nape, in a dark tone; the airways are cut out of it in black.
    region = spline(face_points[2:], 3)[:-1] + [(177, 214), (70, 214)] + spline(
        [(70, 214), (69, 180), (66, 145), (68, 112), (78, 92), (96, 79), (114, 72), (132, 63), (152, 55), (174, 50.5),
         (194, 50), (206, 53), (210, 57)], 3)[1:-1]
    f.shape("tissue", region, "#151515", "#151515", 0.3)
    nose = spline([(222, 92), (214, 89), (209, 74), (202, 61), (190, 53), (174, 51), (160, 57), (146, 60), (133, 68),
                   (127, 82), (126, 97), (130, 101), (139, 97.5), (148, 94), (158, 93.5), (184, 92.5), (200, 93.5),
                   (212, 95), (217, 95)], 3)
    f.shape("nasalair", nose, "#000000", "#000000", 0.3)
    mouth = spline([(219, 109), (209, 100.5), (198, 99), (184, 97.5), (166, 96.5), (158, 98.5), (150, 101), (141, 104),
                    (132, 105), (126, 120), (127, 145), (131, 166), (138, 172), (145, 168), (152, 173), (150, 188),
                    (150.5, 214), (172.5, 214), (173, 190), (176.5, 172), (174, 162), (168, 156), (162, 151),
                    (160, 152), (166, 149), (174, 144), (186, 136), (198, 128), (205, 122), (212, 120), (214, 113),
                    (219, 111)], 3)
    f.shape("airway", mouth, "#000000", "#000000", 0.3)
    # The profile: brow, nose, lips, chin, throat and the front of the neck.
    f.line("profile", face, INK, 0.75)
    # The cervical spine behind the pharynx: the atlas's arch, then C2 to C7.
    f.shape("atlas", ellipse(110, 80, 5.5, 3.6, 12), "#222222", DIM, 0.45)
    for k in range(6):
        y = 89 + 20.5 * k
        f.shape(f"vertebra{k}", spline([(103, y), (115, y - 0.5), (118, y + 2.5), (118, y + 14), (115, y + 16.5),
                                        (103, y + 17), (100, y + 14), (100, y + 2.5)], 2, closed=True), "#222222", DIM, 0.45)
    # The back wall of the pharynx, from the nasopharynx's roof down to the gullet.
    wall = spline([(160, 57), (146, 60), (133, 68), (127, 82), (125, 100), (126, 125), (128, 150), (133, 172),
                   (136, 214)], 4)
    f.line("pharynx", wall, MID, 0.6)
    # The nasal cavity: its roof under the skull, the conchae; its floor is the palate.
    roof = spline([(214, 89), (209, 74), (202, 61), (190, 53), (174, 51), (160, 57)], 4)
    f.line("nasalroof", roof, MID, 0.5)
    for k, (x, y, w) in enumerate(((186, 80, 20), (180, 70, 18), (174, 61, 13))):
        concha = spline([(x + w, y - 2), (x + w * 0.5, y - 4), (x, y - 1), (x - 3, y + 3), (x + 2, y + 5),
                         (x + w * 0.5, y + 3), (x + w * 0.9, y + 2)], 3)
        f.shape(f"concha{k}", concha, DARK, DIM, 0.45)
    # The hard palate (bone) and the velum, raised against the wall for a vowel.
    palate = spline([(212, 95), (200, 93.5), (184, 92.5), (166, 92.5), (158, 93.5), (166, 96.5), (184, 97.5),
                     (198, 99), (209, 100.5)], 4)
    f.shape("palate", palate, "#2a2a2a", MID, 0.5)
    velum = spline([(158, 93.5), (148, 94), (139, 97.5), (130, 101), (132, 105), (141, 104), (150, 101), (158, 98.5)], 4)
    f.shape("velum", velum, WOOD, MID, 0.5)
    # Teeth, and the jaw's symphysis in section.
    f.shape("upperincisor", spline([(209, 98), (213, 99), (215.5, 104), (215, 108.5), (212.5, 108), (210, 103)], 3), WOOD, MID, 0.45)
    f.shape("lowerincisor", spline([(208, 120), (210.5, 112), (213.5, 110.5), (214, 113), (212, 120)], 3), WOOD, MID, 0.45)
    jaw = spline([(205, 121), (212, 120), (213, 128), (212, 137), (205, 142), (196, 140), (196, 132), (200, 124)], 3, closed=True)
    f.shape("symphysis", jaw, DARK, DIM, 0.5)
    # The floor of the mouth down to the hyoid, and the hyoid.
    f.line("floor", spline([(198, 128), (186, 136), (174, 144), (166, 149)], 4), DIM, 0.45)
    f.shape("hyoid", ellipse(166, 152.5, 4.5, 2.6, 12), DARK, MID, 0.5)
    # The epiglottis, leaning back over the larynx.
    f.shape("epiglottis", spline([(160, 151), (154, 143), (148, 131), (146, 124), (149, 127), (153, 137), (159, 146),
                                  (163, 150)], 3), WOOD, MID, 0.5)
    # The larynx: the thyroid's angle, the false and true folds, the arytenoid,
    # the cricoid's arch and plate, and the trachea's rings below.
    f.shape("thyroid", spline([(171, 156), (177, 157), (182, 166), (181.5, 178), (177.5, 180), (176.5, 170),
                               (174, 162)], 3), DARK, MID, 0.5)
    f.shape("falsefold", spline([(176, 169), (166, 167.5), (155, 168), (165, 169.6), (176, 170.8)], 3), WOOD, DIM, 0.4)
    f.shape("fold", spline([(177, 175.5), (165, 173.8), (152, 174.6), (165, 176.8), (177, 177.6)], 3), DARK, INK, 0.45)
    f.shape("arytenoid", spline([(144, 165), (150, 164), (153.5, 170), (151.5, 177), (145, 178)], 3, closed=True), DARK, MID, 0.45)
    f.shape("cricoidarch", spline([(172, 182.5), (177.5, 183.5), (177.5, 188.5), (172, 189)], 2, closed=True), DARK, MID, 0.45)
    f.shape("cricoidplate", spline([(140, 171), (147, 171.5), (148.5, 182), (148, 192), (141, 192), (139.5, 182)], 2,
                                   closed=True), DARK, MID, 0.45)
    for k in range(4):
        f.shape(f"ring{k}", ellipse(175.5, 195.5 + 5.6 * k, 2.4, 1.6, 10), DARK, DIM, 0.4)
    f.line("tracheaback", [(150, 193), (150.5, 214)], DIM, 0.45)
    f.line("tracheafront", [(173, 191), (172.5, 214)], FAINT, 0.4)
    f.line("gullet", [(140, 193), (141, 214)], FAINT, 0.4)
    # The tongue: a node display.cpp moves with the vowel, authored for "ah"
    # (low and back), about its root.
    tongue = spline([(207, 115), (200, 110.5), (190, 108.5), (178, 109.5), (166, 113), (157, 119.5), (152, 131),
                     (153, 142), (159, 148), (167, 147), (181, 140), (196, 129), (205, 122)], 4, closed=True)
    # Its flesh stays put, drawn a little inside it; only its outline moves.
    f.shape("tongueflesh", inset(tongue, 2.5), "#212121", "#212121", 0.3)
    f.shape("tongue", tongue, "none", INK, 0.6, at=(158, 146))
    SPECIAL["tongue"] = (0, len(f.items) - 1)
    # The lips' opening: a dark lens over the profile, scaled by the vowel.
    f.shape("mouth", [(220.5 + 3.2 * math.cos(a), 108 + 1.3 * math.sin(a)) for a in
                      [2 * math.pi * i / 12 for i in range(12)]], "#000000", MID, 0.4, at=(220.5, 108))
    SPECIAL["mouth"] = (0, len(f.items) - 1)
    # The tract's centre line: glottis, pharynx, under the velum, over the
    # tongue, out to the lips; and the nasal passage above it.
    tract = spline([(163, 175), (154, 163), (142, 147), (137, 128), (139, 113), (149, 107), (166, 103.5),
                    (184, 103), (200, 105), (214, 107), (221, 108)], 6)
    strand(0, 0, resample(tract, 32), None)
    # How far each sample of the tract follows the tongue (the oral cavity's
    # floor), from none behind the velum to most over the tongue's back.
    for x, y in resample(tract, 32):
        t = (x - 146) / (214 - 146) if y < 112 else 0
        FOLLOW.append(round(256 * 0.6 * math.sin(math.pi * t)) if 0 < t < 1 else 0)
    nasal = spline([(136, 84), (147, 74), (162, 68), (182, 70), (200, 77), (212, 86), (222, 92)], 6)
    strand(0, 1, resample(nasal, 32), None)
    SOURCES[0] = ((224, 108), (14, -3))
    return f


# ---------------------------------------------------------------------------
# str: a violin seen from the front, scroll to the left, in real proportion
# (593 mm long, its body 356 mm) at 0.47 px/mm. u runs along the instrument
# from the top of the body, w across it.

VS, VX, VY = 0.47, 12 + 237 * 0.47, 126


def V(u, w):
    return (VX + u * VS, VY + w * VS)


def Vs(points):
    return [V(u, w) for u, w in points]


def violin():
    f = Layer(1)
    upper = spline([(0, 0), (3, 22), (12, 45), (28, 64), (50, 78), (75, 84), (98, 81.5), (115, 74), (125, 67.5),
                    (133, 69)], 3)
    cbout = spline([(133, 69), (131.5, 63), (138, 57.5), (152, 55), (168, 54.5), (182, 56), (191, 59.5), (196.5, 65),
                    (195, 72.5)], 3)[1:]
    lower = spline([(195, 72.5), (203, 79), (220, 91), (243, 101), (268, 104), (295, 100), (318, 88), (337, 68),
                    (350, 40), (355, 16), (356, 0)], 3)[1:]
    side = upper + cbout + lower
    body = side + [(u, -w) for u, w in reversed(side[1:-1])]
    edge = Vs(body)
    f.shape("body", edge, WOOD, INK, 0.7)
    f.shape("purfling", offset(edge, 1.9), "none", DIM, 0.35)
    # The arching's contour lines, as a maker draws them.
    for k, d in enumerate((14,)):
        e = 0.9 * d
        level = [(d, 0), (8 + d * 0.6, 40 - e * 0.5), (40, 74 - e), (90, 80 - e), (125, 64 - e * 0.8),
                 (165, 52 - e * 0.75), (200, 68 - e * 0.85), (245, 99 - e), (295, 98 - e), (335, 68 - e * 0.9),
                 (356 - d, 0)]
        half = spline(level, 6)
        f.shape(f"arch{k}", Vs(half + [(u, -w) for u, w in reversed(half[1:-1])]), "none", FAINT, 0.35)
    board = Vs([(-130, -12), (140, -21)] + [(140 + 3 * math.sin(math.pi * i / 6), -21 + 42 * i / 6) for i in range(1, 6)] +
               [(140, 21), (-130, 12)])
    f.shape("fingerboard", board, DARK, MID, 0.5)
    f.shape("nut", Vs(rect(-134, -13, -130, 13)), WOOD, INK, 0.5)
    box = Vs([(-134, -13), (-134, 13), (-200, 11), (-206, 13)] +
             [(-222 + 15 * math.cos(math.radians(a)), 18 * math.sin(math.radians(a))) for a in range(60, 300, 20)] +
             [(-206, -13), (-200, -11)])
    f.shape("pegbox", box, WOOD, INK, 0.6)
    f.shape("pegcavity", Vs([(-137, -8), (-137, 8), (-197, 6.5), (-197, -6.5)]), "#000000", MID, 0.4)
    f.line("fluting", Vs([(-207, 0), (-236, 0)]), DIM)
    for s in (-1, 1):
        f.shape(f"volute{s}", Vs([(-207, s * 9)] + [(-222 + 10 * math.cos(math.radians(a)), s * 12 * math.sin(math.radians(a)))
                                                    for a in range(70, 200, 15)] + [(-232.5, s * 3)]), "none", DIM, 0.35)
    for k, (u, s) in enumerate(((-145, -1), (-153, 1), (-168, -1), (-176, 1))):
        head = [(u - 2.5, s * 13), (u - 3, s * 18), (u - 8, s * 21), (u - 9, s * 31), (u - 6, s * 37), (u, s * 38.5),
                (u + 6, s * 37), (u + 9, s * 31), (u + 8, s * 21), (u + 3, s * 18), (u + 2.5, s * 13), (u + 2, -s * 17),
                (u - 2, -s * 17)]
        f.shape(f"peg{k}", Vs(head), WOOD, INK, 0.5)
    tail = Vs([(232, -20)] + [(228 - 5 * math.sin(math.pi * i / 8), -20 + 40 * i / 8) for i in range(1, 8)] +
              [(232, 20), (334, 9), (338, 5), (338, -5), (334, -9)])
    f.shape("tailpiece", tail, DARK, MID, 0.5)
    f.shape("saddle", Vs(rect(350, -12, 355.5, 12)), WOOD, MID, 0.4)
    for s in (-1, 1):
        # An f-hole: an S-curved stem, widest between its notches at the
        # bridge, ending in a small upper eye and a larger lower one.
        centre = spline([(153, 21.5), (162, 22.5), (176, 25.5), (190, 30), (204, 34.5), (215, 37.5), (221, 38.5)], 3)
        side = normals(centre)
        width = [0.9 + 1.6 * math.sin(math.pi * i / (len(centre) - 1)) for i in range(len(centre))]
        one = [(u + nu * h, w + nw * h) for (u, w), (nu, nw), h in zip(centre, side, width)]
        two = [(u - nu * h, w - nw * h) for (u, w), (nu, nw), h in zip(centre, side, width)]
        f.shape(f"fstem{s}", Vs([(u, s * w) for u, w in one + list(reversed(two))]), "#000000", INK, 0.45)
        f.circle(f"feye{s}a", *V(151.5, s * 21), 1.4, "#000000", INK, 0.45)
        f.circle(f"feye{s}b", *V(223, s * 38.5), 1.9, "#000000", INK, 0.45)
    bridge = Vs([(193.5, -22), (197, -22), (197.5, -8), (197, 8), (197, 22), (193.5, 22), (194, 8), (194, -8)])
    f.shape("bridge", bridge, WOOD, INK, 0.5)
    strings = ((-8.3, -16.5, -15), (-2.8, -5.5, -5), (2.8, 5.5, 5), (8.3, 16.5, 15))
    for k, (a, b, c) in enumerate(strings):
        f.line(f"string{k}", Vs([(-150 - 8 * k, a * 0.6), (-130, a), (195, b), (232, c)]), DIM, 0.45)
    # The bow, across the strings between the fingerboard and the bridge: a
    # node display.cpp slides along its length and fades as Breath takes over.
    bx = V(172, 0)[0]
    hair = [(bx - 1.1, 58), (bx + 1.1, 58.5), (bx + 1.1, 240), (bx - 1.1, 240)]
    f.shape("bowhair", hair, "none", "#c8c8c8", 0.5, at=(bx, VY))
    SPECIAL["bowhair"] = (1, len(f.items) - 1)
    # The stick, cambered toward the hair, ending in the head at the tip.
    stick = [(bx + 2.2 + 1.3 * math.sin(math.pi * (y - 56) / 184), y) for y in range(56, 241, 23)]
    head = [(bx + 3.4, 54), (bx + 3, 50.5), (bx + 0.5, 49.5), (bx - 1.6, 52), (bx - 1.6, 58), (bx + 1.4, 58.5)]
    outline_pts = head + [(x + 0.2, y) for x, y in stick[1:]] + [(x + 1.1, y) for x, y in reversed(stick[1:])]
    f.shape("bowstick", outline_pts, "none", "#c8c8c8", 0.5, at=(bx, VY))
    SPECIAL["bowstick"] = (1, len(f.items) - 1)
    # The D string sounds; the open G beside it rings in sympathy.
    strand(1, 0, resample(Vs([(-130, -2.8), (195, -5.5)]), 32), None)
    strand(1, 1, resample(Vs([(-130, -8.3), (195, -16.5)]), 32), None)
    SOURCES[1] = (V(172, 0), (0, -9))
    return f


# ---------------------------------------------------------------------------
# pipe: a stopped wooden pipe (Gedackt) in section, beside a smaller one a
# fifth above, standing on the wind chest. Walls hatched as cut wood.

def gedackt(f, tag, cx, b, t, top, lip, scale):
    """One pipe in section: bore half-width b, walls t, the mouth from the
    lower lip `lip` up 0.8 b, the stopper and its handle in the top."""
    upper = lip - b * 0.8                   # the upper lip's edge
    block = lip + 18 * scale                # the block's foot
    ink = INK if tag == "main" else MID
    wall = "#1a1a1a"
    # Back wall, front wall above the mouth (its upper lip bevelled), cap, block.
    f.shape(f"{tag}back", rect(cx + b, top, cx + b + t, block), wall, ink, 0.55)
    hatched(f, f"{tag}backhatch", cx + b, cx + b + t, top + 1, block - 1, 3.2)
    front = [(cx - b - t, top), (cx - b, top), (cx - b, upper), (cx - b - t, upper - t * 1.2)]
    f.shape(f"{tag}front", front, wall, ink, 0.55)
    hatched(f, f"{tag}fronthatch", cx - b - t, cx - b, top + 1, upper - t * 1.3, 3.2)
    f.shape(f"{tag}cap", rect(cx - b - t - 1, lip, cx - b - 1.1, block), wall, ink, 0.55)
    f.shape(f"{tag}block", [(cx - b, lip), (cx + b, lip), (cx + b, block), (cx + 2.5, block), (cx + 2.5, block - 4 * scale),
                            (cx - b, block - 4 * scale)], wall, ink, 0.55)
    f.shape(f"{tag}blockhatch", hatch_h(cx - b + 1, cx + b - 1, lip + 1, block - 5 * scale, 3.2), "none", HATCH, 0.3)
    # The foot: a turned toe into the chest, bored through.
    f.shape(f"{tag}foot", [(cx - 8 * scale, block), (cx - 2.5, block), (cx - 2, 203), (cx - 4, 203), (cx - 4.5, 200.5)],
            wall, ink, 0.5)
    f.shape(f"{tag}foot2", [(cx + 8 * scale, block), (cx + 2.5, block), (cx + 2, 203), (cx + 4, 203), (cx + 4.5, 200.5)],
            wall, ink, 0.5)
    # The stopper, leathered, and its handle.
    f.shape(f"{tag}stopper", rect(cx - b, top + 2, cx + b, top + 13 * scale), "#262626", ink, 0.5)
    f.line(f"{tag}leather", [(cx - b + 0.6, top + 10 * scale), (cx + b - 0.6, top + 10 * scale)], DIM, 0.35)
    handle(f, tag, cx, top, scale, ink)
    # The wind's sheet from the flue across the mouth: a node display.cpp sways.
    h = lip - upper
    jet = [(cx - b - 0.5, lip), (cx - b - 1.3, lip - h * 0.5), (cx - b - 1.9, upper + 0.6), (cx - b - 1.0, upper + 0.4),
           (cx - b - 0.3, lip - h * 0.5), (cx - b + 0.4, lip)]
    f.shape(f"{tag}jet", jet, "none", "#9a9a9a", 0.3, at=(cx - b - 0.5, lip))
    SPECIAL[f"{tag}jet"] = (2, len(f.items) - 1)
    JETS[tag] = (cx - b - 0.5, lip)
    return (cx, lip - 2, top + 13 * scale + 1)


def handle(f, tag, cx, top, scale, ink):
    f.shape(f"{tag}handle", [(cx - 3, top + 2), (cx - 3, top - 8 * scale), (cx - 5, top - 10 * scale), (cx - 5, top - 13 * scale),
                             (cx - 2, top - 15 * scale), (cx + 2, top - 15 * scale), (cx + 5, top - 13 * scale),
                             (cx + 5, top - 10 * scale), (cx + 3, top - 8 * scale), (cx + 3, top + 2)], "#1a1a1a", ink, 0.5)


def elevation(f, cx, b, t, top, lip):
    """The same pipe from the front: its board, the mouth cut into it with
    the upper lip's edge, the cap below, the stopper's handle and the toe."""
    w = b + t
    upper = lip - b * 0.8
    block = lip + 18
    f.shape("elevbody", rect(cx - w, top, cx + w, lip), "#161616", INK, 0.55)
    for k, x in enumerate((-0.55, -0.1, 0.4)):
        f.line(f"elevgrain{k}", [(cx + x * w, top + 6 + 9 * k), (cx + x * w + 0.6, lip - 26 + 5 * k)], FAINT, 0.4)
    # The mouth: a dark opening, the upper lip bevelled across its top.
    f.shape("elevmouth", rect(cx - b, upper, cx + b, lip), "#000000", MID, 0.45)
    f.shape("elevlip", [(cx - b, upper), (cx + b, upper), (cx + b - 1.5, upper - 3.5), (cx - b + 1.5, upper - 3.5)],
            "#262626", MID, 0.45)
    f.shape("elevcap", rect(cx - w - 1, lip, cx + w + 1, block), "#161616", INK, 0.55)
    f.line("elevflue", [(cx - b, lip + 1.3), (cx + b, lip + 1.3)], DIM, 0.4)
    f.shape("elevtoe", [(cx - 8, block), (cx + 8, block), (cx + 4.5, 200.5), (cx - 4.5, 200.5)], "#161616", INK, 0.5)
    handle(f, "elev", cx, top, 1.0, INK)


def pipes():
    f = Layer(2)
    # The wind chest: its top board and the chamber under it.
    f.shape("chest", [(34, 203), (246, 203), (246, 214), (34, 214)], "#121212", MID, 0.5)
    f.shape("toeboard", rect(34, 200, 246, 203), "#1f1f1f", MID, 0.5)
    main = gedackt(f, "main", 84, 15, 5, 56, 176, 1.0)
    elevation(f, 148, 15, 5, 56, 176)
    side = gedackt(f, "side", 214, 10, 4, 99, 176, 0.75)
    strand(2, 0, resample([(main[0], main[1]), (main[0], main[2])], 32), None)
    strand(2, 1, resample([(side[0], side[1]), (side[0], side[2])], 32), None)
    SOURCES[2] = ((84 - 15 - 5, 168), (-9, -4))
    return f


# ---------------------------------------------------------------------------
# glass: two wine glasses, the second smaller, each with a little water. The
# rims are the strands: display.cpp draws them, bending in the glass's modes.

def wineglass(f, tag, cx, foot, s, water):
    rim_y = foot - 130 * s
    rx, ry = 31 * s, 8.5 * s
    prof = spline([(31, 0), (32.5, 12), (31.5, 27), (27, 41), (19, 53), (9, 60.5), (4, 63.5), (2.2, 66)], 5)
    prof = [(x * s, y * s) for x, y in prof]
    stem = [(1.6 * s, 72 * s), (2.2 * s, 76 * s), (1.5 * s, 80 * s), (1.5 * s, 118 * s), (2.6 * s, 123 * s),
            (6 * s, 126.5 * s), (14 * s, 128 * s)]
    right = prof + stem
    bowl = [(cx + x, rim_y + y) for x, y in right] + [(cx - x, rim_y + y) for x, y in reversed(right)]
    back = arc(cx, rim_y, rx, ry, 180, 360, 16)[1:-1]
    ink = INK if tag == "main" else MID
    f.shape(f"{tag}bowl", bowl + back, "#080808", ink, 0.6)
    f.shape(f"{tag}foot", ellipse(cx, foot - 1.5 * s, 28 * s, 5.5 * s, 28), "#080808", ink, 0.55)
    f.shape(f"{tag}footrim", arc(cx, foot - 1.5 * s, 22 * s, 3.4 * s, 200, 340, 10), "none", DIM, 0.35)
    # Water: its surface and the body of it, filled faintly.
    depth = water * 60 * s
    yw = rim_y + depth
    # Bowl half-width at the water line.
    hw = None
    for (x0, y0), (x1, y1) in zip(prof, prof[1:]):
        if y0 <= depth <= y1:
            hw = x0 + (x1 - x0) * (depth - y0) / ((y1 - y0) or 1)
    hw = hw or prof[-1][0]
    below = [(x, y) for x, y in prof if y > depth]
    body = [(cx + hw, yw)] + [(cx + x, rim_y + y) for x, y in below] + [(cx - x, rim_y + y) for x, y in reversed(below)] + \
           [(cx - hw, yw)] + arc(cx, yw, hw, hw * ry / rx, 180, 0, 12)[1:-1]
    f.shape(f"{tag}water", body, "#141414", DIM, 0.4)
    f.shape(f"{tag}surface", ellipse(cx, yw, hw, hw * ry / rx, 24), "#1f1f1f", DIM, 0.4)
    # Highlights: two curved streaks on the bowl and one down the stem.
    f.line(f"{tag}glint0", spline([(cx - 26 * s, rim_y + 6 * s), (cx - 27 * s, rim_y + 20 * s), (cx - 22 * s, rim_y + 34 * s)], 4), "#8c8c8c", 0.5)
    f.line(f"{tag}glint1", spline([(cx - 21 * s, rim_y + 9 * s), (cx - 22 * s, rim_y + 17 * s)], 3), "#5c5c5c", 0.4)
    f.line(f"{tag}glint2", [(cx - 0.6 * s, rim_y + 82 * s), (cx - 0.6 * s, rim_y + 112 * s)], "#5c5c5c", 0.35)
    loop = [(cx + rx * math.cos(2 * math.pi * i / 64), rim_y + ry * math.sin(2 * math.pi * i / 64)) for i in range(64)]
    radial = [(math.cos(2 * math.pi * i / 64), ry / rx * math.sin(2 * math.pi * i / 64)) for i in range(64)]
    return loop, radial


def glasses():
    f = Layer(3)
    f.line("table", [(40, 204), (250, 204)], FAINT, 0.5)
    loop, radial = wineglass(f, "main", 112, 202, 1.0, 0.42)
    strand(3, 0, loop, radial, loop=True)
    loop, radial = wineglass(f, "side", 196, 202, 0.78, 0.25)
    strand(3, 1, loop, radial, loop=True)
    SOURCES[3] = ((112, 64), (3, -9))
    return f


FIGURES = [vox(), violin(), pipes(), glasses()]


# ---------------------------------------------------------------------------
# Fig. 2: Koenig's analyser. A resonator per mode hangs from its capsule under
# the rail; each capsule feeds a burner on the rail, whose flame display.cpp
# scales. Resonators are authored at the fundamental's size and scaled.

SLOT_X = [330 + 19 * k for k in range(8)]
RAIL_Y, NECK_Y = 150, 168
RES_R = 9.5
FLAME_H, FLAME_SMALL, PILOT_H = 52, 26, 5.2   # a tall flame, a low one and the pilot, px
FLAME_LOWEST = 8                               # under this only the pilot burns
RES_SIZE = [1.0, 0.7, 0.48, 0.42, 0.34, 0.3, 0.25, 0.2]   # each resonator's usual size, of the fundamental's


def analyser_static():
    a = Layer()
    # The stand: a rail carrying the burners, on two posts and a foot. The left
    # post runs on above the rail as a rod graduated every 10 px, to read the
    # flames against.
    teeth = [(316.4, 96)]
    for y in range(100, 146, 10):
        teeth += [(316.4, y - 0.25), (319.5, y - 0.25), (319.5, y + 0.25), (316.4, y + 0.25)]
    a.shape("rod", teeth + [(316.4, 150), (315.6, 150), (315.6, 96)], "none", DIM, 0.3, node=False)
    for x in (316, 476):
        a.shape(f"post{x}", rect(x - 1, RAIL_Y + 3, x + 1, 205), DARK, DIM, 0.4, node=False)
    a.shape("rail", rect(312, RAIL_Y, 479.5, RAIL_Y + 3), DARK, MID, 0.5, node=False)
    a.shape("foot", rect(310, 205, 479.5, 208), DARK, DIM, 0.45, node=False)
    for k, x in enumerate(SLOT_X):
        # Burner on the rail, its capsule under it, and the tube to the resonator.
        a.shape(f"burner{k}", [(x - 1.5, RAIL_Y), (x - 0.8, RAIL_Y - 4.5), (x + 0.8, RAIL_Y - 4.5), (x + 1.5, RAIL_Y)],
                DARK, MID, 0.4, node=False)
        # The pilot that always burns; display.cpp's flame stands over it.
        a.shape(f"pilot{k}", thin(flame(x, PILOT_H), 8), "#4d4d4d", "#7a7a7a", 0.3, node=False)
        a.shape(f"capsule{k}", [(x - 3.2, RAIL_Y + 3), (x + 3.2, RAIL_Y + 3), (x + 3.2, RAIL_Y + 5.5), (x + 2, RAIL_Y + 7),
                                (x - 2, RAIL_Y + 7), (x - 3.2, RAIL_Y + 5.5)], DARK, MID, 0.4, node=False)
        a.line(f"tube{k}", [(x, RAIL_Y + 7), (x, NECK_Y)], DIM, 0.55, node=False)
    return a


def resonator(x, s=1.0):
    """A Helmholtz resonator hanging from its neck at (x, NECK_Y): the neck,
    the sphere, and the nipple below it that goes to the ear. Authored at about
    the size it's drawn (s of the fundamental's), for a steady line."""
    r, neck, nip = RES_R * s, 1.7 * max(s, 0.5), 1.1 * max(s, 0.5)
    a0 = math.degrees(math.asin(neck / r))
    a1 = math.degrees(math.asin(nip / r))
    cy = NECK_Y + 2.4 * max(s, 0.5) + r * math.cos(math.radians(a0))
    left = [(x - r * math.sin(math.radians(a)), cy - r * math.cos(math.radians(a)))
            for a in [a0 + (180 - a1 - a0) * i / 10 for i in range(11)]]
    right = [(2 * x - px, py) for px, py in reversed(left)]
    foot = cy + r + 2.2 * max(s, 0.5)
    return ([(x - neck, NECK_Y)] + left + [(x - nip, foot), (x + nip, foot)] + right +
            [(x + neck, NECK_Y)])


def shine(x, s=1.0):
    """The light on a resonator's sphere: a crescent at its upper left."""
    r = RES_R * s
    knee = max(s, 0.5)
    cy = NECK_Y + 2.4 * knee + r * math.cos(math.asin(1.7 * knee / r))
    outer = arc(x, cy, r * 0.72, r * 0.72, 200, 265, 6)
    inner = arc(x - 0.6 * knee, cy + 0.6 * knee, r * 0.66, r * 0.66, 262, 203, 6)
    return outer + inner


def flame(x, h=FLAME_H, core=False, detail=2):
    """A flame standing on its burner, as a single outline. With `core` the line
    runs on from the left of its foot up round a slender inner flame, whose two
    close edges read as the flame's bright heart with no fill to redraw."""
    w = h * 0.16
    side = [(w * 0.55, -0.0), (w * 0.95, -h * 0.12), (w, -h * 0.25), (w * 0.8, -h * 0.45), (w * 0.45, -h * 0.7), (0.0, -h)]
    right = spline(side, detail)
    left = [(-px, py) for px, py in reversed(right[:-1])]
    points = right + left
    if core:
        inner = spline([(w * 0.14, -h * 0.03), (w * 0.27, -h * 0.13), (w * 0.26, -h * 0.26), (w * 0.14, -h * 0.42), (0.0, -h * 0.58)], 4)
        points += [(-px, py) for px, py in inner] + [(px, py) for px, py in reversed(inner[:-1])]
    return [(x + px, RAIL_Y - 4.5 + py) for px, py in points]


# ---------------------------------------------------------------------------
# Assemble the fragment.

HEADER_FIRST = 4
PATHS = 4


def build():
    numbered = {}         # node -> svg
    order = []            # (svg maker or text, node or None)
    counter = [PATHS + 8]

    def take():
        n = counter[0]
        counter[0] += 1
        return n

    out = [f'<g id="{ID}_panel">',
           "<!-- SDK two-page header: keep this block before machine artwork below y=38. -->",
           '<path d="M0,0 L480,0 L480,38 L0,38 Z" fill="#000000"/>']
    bar = ["#000000", "#323232", "#4B4B4B", "#646464", "#7D7D7D", "#969696", "#AFAFAF", "#CDCDCD"]
    for i, colour in enumerate(bar):
        out.append(f'<path id="{ID}_node_{HEADER_FIRST + i}" transform="matrix(1 0 0 1 {60 * i} 0)" '
                   f'd="M0,0 L60,0 L60,20 L0,20 Z" fill="{colour}" stroke="{colour}" stroke-width="0.5"/>')
    texts = [(5 + 120 * c, 10, "#CBCBCB" if c < 2 else "#000000", 10) for c in range(4)]
    texts += [(65 + 120 * c, 17.4, "#CBCBCB" if c < 2 else "#000000", 20) for c in range(4)]
    texts += [(5 + 120 * c, 33, "#5A5A5A", 10) for c in range(4)] + [(65 + 120 * c, 33, "#707070", 13) for c in range(4)]
    for i, (x, y, ink, size) in enumerate(texts):
        out.append(f'<text id="{ID}_text_{i}" transform="matrix(1 0 0 1 {num(x)} {num(y)})" fill="{ink}" {FONT} '
                   f'font-size="{size}px">-</text>')

    sheet = []
    tables = {}

    def emit(layer, family=None):
        first = None
        for maker, name, is_node in layer.items:
            n = take() if is_node else None
            if is_node and first is None:
                first = n
            svg = maker(n)
            out.append(svg)
            tagged = svg if family is None else re.sub(r"^<(\w+) ", rf'<\1 data-family="{family}" ', svg)
            sheet.append(tagged)
        return first

    # Plate captions and Fig. 2's frame (static).
    plate = Layer()
    plate.text(10, 216, 8, "#4d4d4d", "fig. 1")
    plate.text(316, 216, 8, "#4d4d4d", "fig. 2")
    emit(plate)
    emit(analyser_static())
    # The bodies, each shown in place by display.cpp.
    for fam, fig in enumerate(FIGURES):
        first = emit(fig, fam)
        count = sum(1 for _, _, node in fig.items if node)
        tables.setdefault("figure", []).append((first, count))
    special = {}
    for name, (fam, index) in SPECIAL.items():
        first = tables["figure"][fam][0]
        special[name] = first + sum(1 for _, _, node in FIGURES[fam].items[:index] if node)
    tables["special"] = special
    # Strands: the four mutable outlines, flat at first (64 distinct points).
    for p in range(PATHS):
        pts = [(-63 + 2 * i, 0.5 * (i % 2)) for i in range(64)]
        out.append(f'<path id="{ID}_node_{p}" d="M' + " L".join(f"{num(x, 1)},{num(y, 1)}" for x, y in pts) +
                   ' Z" fill="none" stroke="#5a5a5a" stroke-width="0.25"/>')
    # Fig. 2's resonators and flames.
    res = Layer()
    for k, x in enumerate(SLOT_X):
        res.shape(f"res{k}", thin(resonator(x, RES_SIZE[k]), 28 if RES_SIZE[k] > 0.5 else 22), "#1a1a1a", "#7a7a7a", 0.5, at=(x, NECK_Y))
    tables["resonator"] = emit(res)
    glint = Layer()
    for k, x in enumerate(SLOT_X):
        glint.shape(f"shine{k}", thin(shine(x, RES_SIZE[k]), 8), "#8c8c8c", "#8c8c8c", 0.2, at=(x, NECK_Y))
    tables["shine"] = emit(glint)
    fl = Layer()
    for k, x in enumerate(SLOT_X):
        fl.shape(f"flame{k}", thin(flame(x, FLAME_SMALL), 15), "none", "#8c8c8c", 0.5, at=(x, RAIL_Y - 4.5))
    tables["flame"] = emit(fl)
    core = Layer()
    for k, x in enumerate(SLOT_X):
        core.shape(f"core{k}", thin(flame(x, FLAME_H, core=True, detail=5), 32), "none", "#d9d9d9", 0.5, at=(x, RAIL_Y - 4.5))
    tables["core"] = emit(core)
    # Breath motes and air's streaks.
    motes = Layer()
    for m in range(MOTES):
        motes.circle(f"mote{m}", 0, 0, 0.9, "#9a9a9a", "#9a9a9a", 0.2)
    tables["mote"] = emit(motes)
    air = Layer()
    for m in range(STREAKS):
        air.shape(f"air{m}", [(-3, -0.2), (3, -0.2), (3.3, 0), (3, 0.2), (-3, 0.2), (-3.3, 0)], "none", "#6e6e6e", 0.25)
    tables["air"] = emit(air)
    tables["nodes"] = counter[0]

    for i, (fill, stroke, width) in enumerate(PALETTE):
        out.append(f'<circle id="{ID}_palette_{i}" cx="640" cy="0" r="1" fill="{fill}" stroke="{stroke}" stroke-width="{num(width, 4)}"/>')
    out.append("</g>")
    return "\n".join(out) + "\n", sheet, tables


MOTES, STREAKS = 24, 16
PALETTE = [
    ("#f2f2f2", "#f2f2f2", 0.3),    # 0 a strand ringing (at scale 2: 0.6 px)
    ("#4d4d4d", "#4d4d4d", 0.22),   # 1 a strand at rest
    ("#7a7a7a", "#7a7a7a", 0.25),   # 2 the sympathetic strand (the second glass's rim at rest)
    ("#b4b4b4", "#b4b4b4", 0.3),    # 3 the first glass's rim at rest, or the second's ringing
    ("#8a8a8a", "#8a8a8a", 0.22),   # 4 Spread's copies
    # A flame's line scales with it, so each step has its own width: about 0.5 px low to 0.9 px tall.
    ("#7a7a7a", "#7a7a7a", 1.1),    # 5, 6 a low flame, 8 to 15 px and 15 to 26 px
    ("#9a9a9a", "#9a9a9a", 0.75),
    ("#c8c8c8", "#c8c8c8", 1.1),    # 7, 8 a tall flame, 26 to 40 px and over 40 px
    ("#f2f2f2", "#f2f2f2", 0.95),
    ("#6b6b6b", "#6b6b6b", 0.5),    # 9 a resonator at rest
    ("#c8c8c8", "#c8c8c8", 0.5),    # 10 a resonator ringing
    ("#a6a6a6", "#a6a6a6", 0.2),    # 11 breath mote
    ("#4d4d4d", "#4d4d4d", 0.2),    # 12 breath mote, fading
    ("#707070", "#707070", 0.25),   # 13 air streak
    ("#c8c8c8", "#c8c8c8", 0.5),    # 14 the bow
    ("#3d3d3d", "#3d3d3d", 0.5),    # 15 the bow, faint
]


# ---------------------------------------------------------------------------
# The strands' tables for display.h. Each body's main strand and partner:
# rest points relative to the strand's node (placed at scale 2), the way each
# point moves, and each mode's shape along it as a sine of the sample index,
# phase = step * i + start (Q32 turns), so display.cpp never needs a table of
# shapes. Rates are how fast each mode turns on screen (Q16 hertz): a slowed,
# stroboscopic picture of the real vibration, its partials in their ratios.

REACH = [(5.5, 3.5), (8.0, 5.5), (10.5, 6.5), (1.8, 1.6)]   # each strand's largest swing, px: a rim's stays on its rim
TURN = 2 ** 32
MODES = [  # (shape step in turns a sample, start), per family and mode
    [(0.5 * r / (4 * 31), 0.0) for r in (1, 2, 3, 4, 5, 6, 7, 9)],                 # vox: glottis to lips
    [(r / (2 * 31), 0.0) for r in range(1, 9)],                                     # str: nut to bridge
    [(r / (4 * 31), 0.25) for r in (1, 3, 5, 7, 9, 11, 13, 15)],                     # pipe: mouth to stopper
    [(n / 64, 0.25 + q) for n, q in ((2, 0), (2, 0.25), (3, 0), (3, 0.25), (4, 0), (4, 0.25), (5, 0), (6, 0))],  # glass: rim
]
PARTNER = [
    [((2 * j - 1) / (4 * 31), 0.0) for j in (1, 2, 3)],   # the nasal cavity, closed behind, open at the nostrils
    [(j / (2 * 31), 0.0) for j in (1, 2, 3)],             # the open G string
    [((2 * j - 1) / (4 * 31), 0.25) for j in (1, 2, 3)],  # the neighbouring pipe
    [(n / 64, 0.25) for n in (2, 3, 4)],                  # the second glass's rim
]
RATES = [
    [0.42 * r for r in (1, 2, 3, 4, 5, 6, 7, 9)],
    [0.42 * r for r in range(1, 9)],
    [0.3 * r for r in (1, 3, 5, 7, 9, 11, 13, 15)],
    [0.45 * r for r in (1, 1.07, 2.32, 2.45, 4.25, 4.42, 6.63, 9.38)],
]
PARTNER_RATES = [[0.42 * 1.25 * j for j in (1, 3, 5)], [0.42 * 1.5 * j for j in (1, 2, 3)],
                 [0.3 * 1.5 * j for j in (1, 3, 5)], [0.45 * 1.33 * r for r in (1, 2.32, 4.25)]]


def strand_tables():
    anchors, bases, dirs, counts = [], [], [], []
    for fam in range(4):
        row_a, row_b, row_d = [], [], []
        for which in range(2):
            points, direction, loop = STRANDS[fam][which]
            if direction is None:
                direction = normals(points)
            xs, ys = [p[0] for p in points], [p[1] for p in points]
            ax, ay = round((min(xs) + max(xs)) / 2), round((min(ys) + max(ys)) / 2)
            reach = REACH[fam][which] + 4
            for x, y in points:
                assert abs(x - ax) + reach <= 126 and abs(y - ay) + reach <= 126, (fam, which, x - ax, y - ay)
            row_a.append((ax, ay))
            pad = [(0, 0)] * (64 - len(points))
            row_b.append([(round((x - ax) * 16), round((y - ay) * 16)) for x, y in points] + pad)
            row_d.append([(round(dx * 4096), round(dy * 4096)) for dx, dy in direction] + pad)
        anchors.append(row_a)
        bases.append(row_b)
        dirs.append(row_d)
        counts.append(len(STRANDS[fam][0][0]))
    return anchors, bases, dirs, counts


def c_array(values, indent="    "):
    if isinstance(values, (list, tuple)) and values and isinstance(values[0], (list, tuple)):
        inner = [c_array(v, indent + "    ") for v in values]
        if all("\n" not in i for i in inner) and sum(len(i) for i in inner) < 100:
            return "{" + ", ".join(inner) + "}"
        return "{\n" + ",\n".join(indent + "    " + i for i in inner) + "\n" + indent + "}"
    text = ", ".join(str(v) for v in values)
    if len(text) > 110:
        parts, line = [], ""
        for v in values:
            if len(line) + len(str(v)) > 100:
                parts.append(line.rstrip())
                line = ""
            line += str(v) + ", "
        parts.append(line.rstrip(", "))
        return "{" + ("\n" + indent + " ").join(parts) + "}"
    return "{" + text + "}"


def display_block(tables):
    anchors, bases, dirs, counts = strand_tables()
    sp = tables["special"]
    q32 = lambda t: (round(t * TURN)) % TURN
    lines = [
        "// Generated by tools/make_panel.py from the artwork: node numbers, the strands'",
        "// rest lines and their modes' shapes. Edit the script, not this block.",
        f"const u32 header_first = {HEADER_FIRST};",
        f"const u32 figure_first[kBodies] = {c_array([a for a, _ in tables['figure']])};",
        f"const u32 figure_count[kBodies] = {c_array([b for _, b in tables['figure']])};",
        f"const u32 tongue_node = {sp['tongue']}, mouth_node = {sp['mouth']}, bow_hair_node = {sp['bowhair']}, "
        f"bow_stick_node = {sp['bowstick']},",
        f"          main_jet_node = {sp['mainjet']}, side_jet_node = {sp['sidejet']};",
        f"const u32 resonator_first = {tables['resonator']}, shine_first = {tables['shine']}, flame_first = {tables['flame']}, core_first = {tables['core']},",
        f"          mote_first = {tables['mote']}, mote_count = {MOTES}, air_first = {tables['air']}, air_count = {STREAKS};",
        f"const u32 node_count = {tables['nodes']};",
        "// Fig. 2: the slots' x, the burners' tips, the resonators' necks and their",
        "// authored sizes (Q10 of the fundamental's), and the flames' heights (px):",
        "// a tall flame's, a low flame's, and the lowest that shows over the pilot.",
        f"const i32 resonator_size[kModes] = {c_array([round(s * 1024) for s in RES_SIZE])};",
        f"const i32 flame_height = {FLAME_H}, flame_small = {FLAME_SMALL}, flame_lowest = {FLAME_LOWEST};",
        f"const i32 slot_x[kModes] = {c_array([round(x * 1024) for x in SLOT_X])};  // Q10",
        f"const i32 burner_y = {round((RAIL_Y - 4.5) * 1024)}, neck_y = {NECK_Y * 1024};  // Q10",
        "// The bow's and the jets' authored origins (px, Q10).",
        f"const i32 bow_x = {round(V(172, 0)[0] * 1024)}, bow_y = {round(VY * 1024)};",
        f"const i32 jet_origin[2][2] = {c_array([[round(c * 1024) for c in JETS[t]] for t in ('main', 'side')])};",
        f"const i32 tract_follow[32] = {c_array(FOLLOW)};  // Q8: how far the tract follows the tongue",
        f"const i32 tongue_origin[2] = {c_array([round(c * 1024) for c in (158, 146)])}, "
        f"mouth_origin[2] = {c_array([round(c * 1024) for c in (220.5, 108)])};",
        "// Breath leaves each body here (px) and drifts this way (px a second).",
        f"const i32 breath_source[kBodies][4] = {c_array([[round(SOURCES[f][0][0]), round(SOURCES[f][0][1]), SOURCES[f][1][0], SOURCES[f][1][1]] for f in range(4)])};",
        "// Strands: samples a strand has (32 along a line, drawn as a ribbon; 64",
        "// round a glass's rim), each strand's node origin (px), its largest swing",
        "// (Q10 px), and per sample its rest point (Q4 px from the origin) and the",
        "// way it swings (Q12).",
        f"const u32 strand_samples[kBodies] = {c_array(counts)};",
        f"const i32 strand_anchor[kBodies][2][2] = {c_array(anchors)};",
        f"const i32 strand_reach[kBodies][2] = {c_array([[round(r * 1024) for r in row] for row in REACH])};",
        f"const opxy::i16 strand_rest[kBodies][2][64][2] = {c_array(bases)};",
        f"const opxy::i16 strand_way[kBodies][2][64][2] = {c_array(dirs)};",
        "// Each mode's shape along its strand: sin(step * sample + start), Q32 turns;",
        "// and how fast it turns on screen, Q16 hertz. The partner rings in three.",
        f"const u32 mode_shape[kBodies][kModes][2] = {c_array([[[q32(a), q32(b)] for a, b in row] for row in MODES])};",
        f"const u32 mode_rate[kBodies][kModes] = {c_array([[round(r * 65536) for r in row] for row in RATES])};",
        f"const u32 partner_shape[kBodies][3][2] = {c_array([[[q32(a), q32(b)] for a, b in row] for row in PARTNER])};",
        f"const u32 partner_rate[kBodies][3] = {c_array([[round(r * 65536) for r in row] for row in PARTNER_RATES])};",
    ]
    return "\n".join(lines)


text, sheet, tables = build()
if SHEET:
    SHEET.write_text('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 480 220">' + "\n".join(sheet) + "</svg>\n")
    print("wrote", SHEET)
assert len(text.encode()) <= 61000, len(text.encode())
check(text)
target = ROOT / "panel.svg.fragment" if ID == "hollow" else pathlib.Path(ARGS[1])
target.write_text(text)
header = ROOT / "display.h"
block = display_block(tables)
source = header.read_text()
begin, end = "// <make_panel>\n", "// </make_panel>\n"
if begin in source:
    source = source[:source.index(begin) + len(begin)] + block + "\n" + source[source.index(end):]
    header.write_text(source)
else:
    print(block)
# definition.json declares the same frame.
definition = ROOT / "definition.json"
declared = re.sub(r'"nodes": \d+', f'"nodes": {tables["nodes"]}', definition.read_text())
definition.write_text(re.sub(r'"paths": \d+', f'"paths": {PATHS}', declared))
print("wrote", target, len(text.encode()), "bytes,", tables["nodes"], "nodes")
