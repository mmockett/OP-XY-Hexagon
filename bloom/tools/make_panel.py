#!/usr/bin/env python3
"""Writes panel.svg.fragment, Bloom's screen artwork, with the node numbers
display.h names: run it after changing a shape here, and commit both.

The scene: a low striped sun behind a meadow of partials, each a stem with a
blossom (and, with blur, a halo), over a horizon line. Nothing is a mutable
path: stems are fixed 150 px ribbons hung from their tips, and a black band
below the horizon hides what reaches past it (it also sets the sun). Every
node is one closed outline of distinct points, so every outline encloses area.
Each stroke width is the palette's 0.5 px; shapes are authored that much smaller
so they draw at the board's sizes."""
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
ID = sys.argv[1] if len(sys.argv) > 1 else "bloom"   # another prefix for a preview copy
FONT = 'font-family="UniversNextPro-Light"'
BASE = 206                    # the horizon (display.cpp: base)
PARTIALS, MOTES = 24, 14
STROKE = 0.5

# Greys, black to white: the board's shades (display.h names them).
PALETTE = ["#000000", "#141414", "#262626", "#2e2e2e", "#3a3a3a", "#4a4a4a", "#6b6b6b", "#787878",
           "#969696", "#a6a6a6", "#b5b5b5", "#cfcfcf", "#d4d4d4", "#d9d9d9", "#f2f2f2", "#ffffff"]


def num(v):
    return f"{v:.4f}".rstrip("0").rstrip(".")


def outline(points):
    return "M" + " L".join(f"{num(x)},{num(y)}" for x, y in points) + " Z"


def rect(x0, y0, x1, y1):
    return outline([(x0, y0), (x1, y0), (x1, y1), (x0, y1)])


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
for i, colour in enumerate(bar):   # nodes 0..7
    out.append(f'<path id="{ID}_node_{i}" transform="matrix(1 0 0 1 {60 * i} 0)" '
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
# Node 8: the sun, 36 px; 9..12: its four black stripes, below its centre.
out.append(disc(8, 36 - h, PALETTE[3]))
for n, (dy, height) in enumerate(((6, 1.6), (14, 2.2), (21, 2.8), (27, 3.4))):
    out.append(node(9 + n, rect(-40 + h, dy + h, 40 - h, dy + height - h), PALETTE[0]))
# Nodes 13 + 3k, 14 + 3k, 15 + 3k: partial k's stem (from its tip down), halo and blossom.
for k in range(PARTIALS):
    out.append(node(13 + 3 * k, rect(-0.5 + h, 0, 0.5 - h, 150), PALETTE[6]))
    out.append(disc(14 + 3 * k, 12, PALETTE[2]))
    out.append(disc(15 + 3 * k, 4, PALETTE[5]))
# Nodes 85..98: pollen.
for m in range(MOTES):
    out.append(disc(85 + m, 0.8 - h, PALETTE[11]))
# Static: below the horizon, black, over the stems' feet and the setting sun.
out.append(f'<path d="{rect(0, BASE + 0.5, 480, 220)}" fill="#000000" stroke="#000000" stroke-width="0.5"/>')
# Node 99: the horizon, 448 x 0.8 px from x 16.
out.append(node(99, rect(h, h, 448 - h, 0.8 - h), PALETTE[4]))
for i, grey in enumerate(PALETTE):
    out.append(f'<circle id="{ID}_palette_{i}" cx="640" cy="0" r="1" fill="{grey}" stroke="{grey}" '
               f'stroke-width="{STROKE}"/>')
out.append("</g>")
target = ROOT / "panel.svg.fragment" if len(sys.argv) < 3 else pathlib.Path(sys.argv[2])
target.write_text("\n".join(out) + "\n")
print("wrote", target)
