#!/usr/bin/env python3
"""Writes tables.h, Bend's lookup tables: the cosine every line reads its bent
phase through, and the powers of two that pitch and the contour use. Run it
after changing a table here and commit both."""
import math
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]


def rows(values, per=12):
    out = []
    for i in range(0, len(values), per):
        out.append("    " + ", ".join(str(v) for v in values[i:i + per]) + ",")
    return "\n".join(out)


# -cos(2 pi i / 2048), Q15, with a guard entry for interpolation: the CZ's line
# starts each cycle at its trough.
cosine = [round(-math.cos(2 * math.pi * i / 2048) * 32767) for i in range(2049)]
# 2^(k/32), k = 0..32, Q29: transposition and the contour's decays.
power = [round(2 ** (k / 32) * 2 ** 29) for k in range(33)]

text = f"""#pragma once
// Written by tools/make_tables.py: edit there and run it.
#include <opxy/types.hpp>

namespace bend {{ namespace tables {{
// -cos(2 pi i / 2048), Q15, i = 0..2048 (the last a guard for interpolation).
static const opxy::i16 cosine[2049] = {{
{rows(cosine)}
}};
// 2^(k/32), k = 0..32, Q29.
static const opxy::i32 power[33] = {{
{rows(power, 8)}
}};
}}}}
"""
(ROOT / "tables.h").write_text(text)
print("wrote", ROOT / "tables.h")
