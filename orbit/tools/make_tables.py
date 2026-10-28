#!/usr/bin/env python3
"""Writes waves.h, Orbit's wave ROM: run it after changing a wave here, and
commit both.

Eight banks of four single-cycle waves, corners A, B, C and D, exactly as the
design board defines them: each sampled 64 times, its DC removed and its peak
normalised, as a vintage wave ROM would hold it.

- `face` keeps those 64 samples (int8, +-127) for the screen, which draws
  them as the board does.
- `rom` is what the voice plays: each wave's 64 samples resynthesised from
  their own harmonics at 128 points, at five band limits (31, 15, 7, 3 and 1
  harmonics) so the voice can fade to a gentler table an octave at a time as
  the note rises and never alias. Values are Q14, the four corners of one
  point side by side in two words ([bank][level][point], A and B, then C and
  D) so the block's mix reads them together. Each wave is scaled so its loudest table (with the band
  limit's ripple) peaks at 15500.
- `gram` holds each band's products of corners, so the voice knows a mix's
  loudness (its mean square) from its weights alone, and `peaks` each
  corner's loudest point, which bounds the mix's.
- `sine_table` is one cycle of a sine, Q15, 1024 points and a guard point,
  for the orbit's paths."""
import cmath
import math
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
TAU = 2 * math.pi
POINTS, ROM_POINTS = 64, 128
LIMITS = (31, 15, 7, 3, 1)
PEAK = 15500
NAMES = ("glass", "vox", "organ", "reed", "bell", "pluck", "wire", "dusk")


def s(k, p, ph=0.0):
    return math.sin(TAU * (k * p + ph))


def add(hs):
    return lambda p: sum(g * s(k, p, ph[0] if ph else 0) for k, g, *ph in hs)


def burst(n, q):
    return lambda p: s(n, p) * (1 - p) ** q + 0.3 * s(1, p)


def sync(r):
    return lambda p: 2 * ((r * p) % 1) - 1


# The board's banks (Orbit.dc.html, tables()), corners A, B, C, D.
BANKS = [
    # glass: sine, a 1:2 FM, a sine ringing its 4th and 6th, a tine
    [add([(1, 1)]), lambda p: math.sin(TAU * p + 1.3 * s(2, p)), add([(1, 1), (4, 0.45), (6, 0.3)]),
     add([(1, 0.6), (3, 0.25), (5, 0.5)])],
    # vox: a glottal pulse ringing a formant, oo, oh, ah, ee
    [burst(2, 1.5), burst(3, 2), burst(4, 2.5), burst(6, 3)],
    # organ: three drawbar settings and a principal
    [add([(1, 1), (2, 1), (3, 1)]), add([(1, 1), (2, 0.8), (4, 0.7), (8, 0.5)]),
     add([(1, 1), (3, 0.8), (5, 0.6), (6, 0.4), (8, 0.5)]), add([(1, 0.5), (2, 1), (3, 0.3), (4, 0.7)])],
    # reed: a narrow pulse, a band-limited square, a nasal peak at the fifth, a clarinet
    [lambda p: 1 if p < 0.18 else -0.22, add([(1, 1), (3, 1 / 3), (5, 1 / 5), (7, 1 / 7)]),
     add([(1, 0.4), (3, 0.6), (5, 1), (7, 0.5)]), add([(1, 1), (3, 0.33), (5, 0.2), (7, 0.14)])],
    # bell: deep 1:3 FM, a partial cluster, a light 1:5 FM, a hollow chime
    [lambda p: math.sin(TAU * p + 1.8 * s(3, p)), add([(1, 0.6), (3, 1), (4, 0.6), (7, 0.4)]),
     lambda p: math.sin(TAU * p + 0.9 * s(5, p)), add([(2, 1), (5, 0.7), (7, 0.3), (1, 0.3)])],
    # pluck: a saw, a bright band-limited saw, a hard-synced sine, a chirp
    [lambda p: 1 - 2 * p, add([(k, 1 / k) for k in range(1, 9)]), lambda p: s(2.6, p),
     lambda p: math.sin(TAU * (1.5 * p + 3.5 * p * p))],
    # wire: hard sync at 1.5 and 2.5, a synced sine at 3.7, a wavefold
    [sync(1.5), sync(2.5), lambda p: s(3.7, p), lambda p: math.sin(math.pi * 2.5 * s(1, p))],
    # dusk: a triangle, a soft saw, a hollow square, a sine with its octave and fifth
    [lambda p: 4 * p - 1 if p < 0.5 else 3 - 4 * p, add([(k, k ** -1.6) for k in range(1, 7)]),
     add([(1, 1), (3, 3 ** -1.5), (5, 5 ** -1.5)]), add([(1, 1), (2, 0.4), (3, 0.25)])],
]


def sampled(f):
    """The board's ROM wave: 64 samples, DC removed, peak normalised."""
    raw = [f(i / POINTS) for i in range(POINTS)]
    mean = sum(raw) / POINTS
    peak = max(abs(y - mean) for y in raw) or 1
    return [(y - mean) / peak for y in raw]


def harmonics(wave):
    """Complex amplitudes of harmonics 1..31 of the 64 samples."""
    return [sum(y * cmath.exp(-1j * TAU * k * i / POINTS) for i, y in enumerate(wave)) * 2 / POINTS
            for k in range(1, 32)]


def resynth(c, limit):
    return [sum((c[k - 1] * cmath.exp(1j * TAU * k * i / ROM_POINTS)).real for k in range(1, limit + 1))
            for i in range(ROM_POINTS)]


faces, rom = [], []
for bank in BANKS:
    faces.append([[max(-127, min(127, round(127 * y))) for y in sampled(f)] for f in bank])
    levels = [[None] * 4 for _ in LIMITS]
    for corner, f in enumerate(bank):
        c = harmonics(sampled(f))
        tables = [resynth(c, limit) for limit in LIMITS]
        gain = PEAK / max(abs(y) for table in tables for y in table)
        for level, table in enumerate(tables):
            levels[level][corner] = [round(y * gain) for y in table]
    rom.append(levels)
loudest = max(abs(v) for bank in rom for level in bank for wave in level for v in wave)
assert loudest <= PEAK, loudest

out = ["#pragma once",
       "// Orbit's wave ROM, written by tools/make_tables.py: run it after changing a",
       "// wave there, and commit both.",
       "#include <opxy/types.hpp>",
       "",
       "namespace orbit {",
       f"const opxy::u32 banks = 8, rom_points = {ROM_POINTS}, rom_levels = {len(LIMITS)}, face_points = {POINTS};",
       f"// The loudest value in any table, Q14.",
       f"const opxy::i32 rom_peak = {loudest};",
       "",
       "// The board's 64 samples of each wave, +-127, for the screen: [bank][corner][point].",
       "static const signed char face[8][4][64] = {"]
for b, bank in enumerate(faces):
    out.append(f"    {{  // {NAMES[b]}")
    for wave in bank:
        out.append("        {" + ", ".join(str(v) for v in wave) + "},")
    out.append("    },")
out.append("};")
def pair(a, b):
    return f"0x{(a & 0xffff) | ((b & 0xffff) << 16):08x}"


out += ["",
        "// The voice's tables, Q14, at 31, 15, 7, 3 and 1 harmonics: [bank][level][point]",
        "// as two words, corners A and B (low and high halves), then C and D.",
        "static const opxy::u32 rom[8][5][128][2] = {"]
for b, bank in enumerate(rom):
    out.append(f"    {{  // {NAMES[b]}")
    for level in bank:
        out.append("        {")
        row = [f"{{{pair(level[0][i], level[1][i])},{pair(level[2][i], level[3][i])}}}" for i in range(ROM_POINTS)]
        for i in range(0, ROM_POINTS, 6):
            out.append("            " + ", ".join(row[i:i + 6]) + ",")
        out.append("        },")
    out.append("    },")
out.append("};")
out += ["",
        "// Each mix's mean square: [bank][level] the corners' products, A.A, A.B, A.C,",
        "// A.D, B.B, B.C, B.D, C.C, C.D, D.D, each the mean over the table, Q14 (>> 14).",
        "static const opxy::i32 gram[8][5][10] = {"]
PAIRS = [(0, 0), (0, 1), (0, 2), (0, 3), (1, 1), (1, 2), (1, 3), (2, 2), (2, 3), (3, 3)]
for b, bank in enumerate(rom):
    rows = []
    for level in bank:
        rows.append("{" + ", ".join(str(round(sum(level[j][i] * level[k][i] for i in range(ROM_POINTS)) / ROM_POINTS / 16384))
                                    for j, k in PAIRS) + "}")
    out.append(f"    {{{', '.join(rows)}}},  // {NAMES[b]}")
out.append("};")
out += ["",
        "// Each band's loudest point of each corner, Q14: [bank][level][corner]. A mix",
        "// is never louder than its corners' peaks, weighted.",
        "static const opxy::i16 peaks[8][5][4] = {"]
for b, bank in enumerate(rom):
    out.append("    {" + ", ".join("{" + ", ".join(str(max(abs(v) for v in level[k])) for k in range(4)) + "}" for level in bank)
               + f"}},  // {NAMES[b]}")
out.append("};")
sine = [round(32767 * math.sin(TAU * i / 1024)) for i in range(1025)]
out += ["", "// One cycle of a sine, Q15, and a guard point.", "static const opxy::i16 sine_table[1025] = {"]
for i in range(0, 1025, 16):
    out.append("    " + ", ".join(str(v) for v in sine[i:i + 16]) + ",")
out += ["};", "}", ""]
(ROOT / "waves.h").write_text("\n".join(out))
print("wrote", ROOT / "waves.h", "loudest", loudest)
