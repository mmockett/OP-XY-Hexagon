#!/usr/bin/env python3
"""Writes panel.svg.fragment, Bloom's screen artwork, with the node numbers
display.h names: run it after changing a shape here, and commit both.

The scene: a low striped sun, the vowel's two hills, then a meadow of
partials, each a stem with a blossom and its haze twin (and, with blur, a
halo ring), over a horizon line. The hills are the only mutable paths (nodes 0
and 1, 64 points each, reshaped by display.cpp); stems are fixed 150 px ribbons
hung from their tips, and a black band below the horizon hides what reaches
past it (it also sets the sun). Every node is one closed outline of distinct
points, so every outline encloses area.

Nothing large that moves is filled: the sun and the halos are outlines, and the
sun is one outline, its stripes slits cut in from the right, so moving it is
one node. Only the small blossoms, stems and pollen are solid.
Palette entries lend their stroke width to what they paint (WIDTHS); solid parts
are authored that much smaller so they draw at the board's sizes."""
import math
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
ID = sys.argv[1] if len(sys.argv) > 1 else "bloom"   # another prefix for a preview copy
FONT = 'font-family="UniversNextPro-Light"'
BASE = 206                    # the horizon (display.cpp: base)
PARTIALS, MOTES = 24, 14
HILLS, HILL_POINTS = 2, 64   # display.h: hill_first 0, hill_points
HEADER, SUN, PARTIAL, MOTE, HORIZON = 2, 10, 11, 107, 121   # display.h's first nodes
STROKE = 0.5
HALO_RADIUS = 10              # display.cpp: halo_authored

# Greys, black to white: the board's shades (display.h names them).
PALETTE = ["#000000", "#2a2a2a", "#3a3a3a", "#4a4a4a", "#3a3a3a", "#4a4a4a", "#6b6b6b", "#787878",
           "#969696", "#a6a6a6", "#b5b5b5", "#cfcfcf", "#d4d4d4", "#d9d9d9", "#f2f2f2", "#ffffff"]
# Each entry's stroke width, which it lends to whatever it paints: 1 frozen sun
# and halos, 2 halo, 3 sun are outlines; the rest paint solid parts.
WIDTHS = {1: 0.7, 2: 0.7, 3: 0.7}


def num(v):
    return f"{v:.4f}".rstrip("0").rstrip(".")


def outline(points):
    return "M" + " L".join(f"{num(x)},{num(y)}" for x, y in points) + " Z"


def rect(x0, y0, x1, y1):
    return outline([(x0, y0), (x1, y0), (x1, y1), (x0, y1)])


def checked(points):
    """The device's rules for one outline: at most 64 points, neighbours at
    least 1/8 px apart, the closing edge at least 1/4 px."""
    assert len(points) <= 64, len(points)
    for a, b in zip(points, points[1:]):
        assert max(abs(a[0] - b[0]), abs(a[1] - b[1])) >= 0.125, (a, b)
    a, b = points[-1], points[0]
    assert math.hypot(a[0] - b[0], a[1] - b[1]) >= 0.25, (a, b)
    return points


def arc(radius, start, stop, step=12):
    """Points on the circle from angle start to stop, degrees clockwise from
    east, not counting the two ends."""
    n = max(1, int(abs(stop - start) / step + 0.5))
    return [(radius * math.cos(math.radians(start + (stop - start) * i / n)),
             radius * math.sin(math.radians(start + (stop - start) * i / n))) for i in range(1, n)]


def sun(radius, stripes, neck):
    """One closed outline: the disc, with each stripe a slit cut in from the
    right edge to neck px short of the left. Starts on the top of the disc."""
    points = [(0, -radius)]
    angle = -90
    for dy, height in stripes:
        a0 = math.degrees(math.asin(dy / radius))
        a1 = math.degrees(math.asin((dy + height) / radius))
        points += arc(radius, angle, a0)
        for y, end in ((dy, a0), (dy + height, a1)):
            right = (radius * math.cos(math.radians(end)), y)
            left = (-math.sqrt(radius * radius - y * y) + neck, y)
            points += [right, left] if y == dy else [left, right]
        angle = a1
    points += arc(radius, angle, 270)
    return checked(points)


def node(i, d, grey):
    return (f'<path id="{ID}_node_{i}" transform="matrix(1 0 0 1 640 320)" d="{d}" '
            f'fill="{grey}" stroke="{grey}" stroke-width="{STROKE}"/>')


def disc(i, r, grey):
    return (f'<circle id="{ID}_node_{i}" transform="matrix(1 0 0 1 640 320)" cx="0" cy="0" r="{num(r)}" '
            f'fill="{grey}" stroke="{grey}" stroke-width="{STROKE}"/>')


out = [f'<g id="{ID}_panel">',
       "<!-- SDK two-page header: keep this block before machine artwork below y=38. -->",
       '<path d="M0,0 L480,0 L480,38 L0,38 Z" fill="#000000"/>']
bar = ["#000000", "#323232", "#4B4B4B", "#646464", "#7D7D7D", "#969696", "#AFAFAF", "#CDCDCD"]
for i, colour in enumerate(bar):   # nodes 2..9
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
# Node 10: the sun, 36 px, its four stripes below its centre, in one outline.
out.append(f'<path id="{ID}_node_{SUN}" transform="matrix(1 0 0 1 640 320)" '
           f'd="{outline(sun(36, ((-25, 3), (-16, 3), (-8, 3), (-1, 3), (6, 3), (12.5, 3.5), (19.5, 4), (27, 4.5)), 1.5))}" '
           f'fill="none" stroke="{PALETTE[3]}" stroke-width="{WIDTHS[3]}"/>')
# Nodes 0 and 1: the vowel's hills, one for each formant, in front of the sun:
# unfilled outlines (the device reshapes only those), 62 points along the top
# (a flat sliver until display.cpp raises them) and two along the horizon.
for i in range(HILLS):
    top = HILL_POINTS - 2
    points = [(-34 + 68 * j / (top - 1), -0.3) for j in range(top)] + [(34, 0.4), (-34, 0.4)]
    out.append(f'<path id="{ID}_node_{i}" transform="matrix(1 0 0 1 640 320)" d="{outline(points)}" '
               f'fill="none" stroke="{PALETTE[4]}" stroke-width="0.8"/>')
# Nodes 11 + 4k .. 14 + 4k: partial k's stem (from its tip down), halo, blossom and twin.
for k in range(PARTIALS):
    first = PARTIAL + 4 * k
    out.append(node(first, rect(-0.5 + h, 0, 0.5 - h, 150), PALETTE[6]))
    out.append(f'<circle id="{ID}_node_{first + 1}" transform="matrix(1 0 0 1 640 320)" cx="0" cy="0" '
               f'r="{HALO_RADIUS}" fill="none" stroke="{PALETTE[2]}" stroke-width="{WIDTHS[2]}"/>')
    out.append(disc(first + 2, 4, PALETTE[5]))
    out.append(disc(first + 3, 4, PALETTE[5]))
# Nodes 107..120: pollen.
for m in range(MOTES):
    out.append(disc(MOTE + m, 0.8 - h, PALETTE[11]))
# Static: below the horizon, black, over the stems' feet and the setting sun.
out.append(f'<path d="{rect(0, BASE + 0.5, 480, 220)}" fill="#000000" stroke="#000000" stroke-width="0.5"/>')
# Node 121: the horizon, 448 x 0.8 px from x 16.
out.append(node(HORIZON, rect(h, h, 448 - h, 0.8 - h), PALETTE[4]))
for i, grey in enumerate(PALETTE):
    out.append(f'<circle id="{ID}_palette_{i}" cx="640" cy="0" r="1" fill="{grey}" stroke="{grey}" '
               f'stroke-width="{WIDTHS.get(i, STROKE)}"/>')
out.append("</g>")
target = ROOT / "panel.svg.fragment" if len(sys.argv) < 3 else pathlib.Path(sys.argv[2])
target.write_text("\n".join(out) + "\n")
print("wrote", target)
