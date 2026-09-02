#!/usr/bin/env python3
"""Generates Font5x7.h/.cpp for eNMEA from hand-drawn 5x7 glyph art.

Each glyph is 7 rows x 5 cols, '#'=pixel on, '.'=pixel off.
Emitted as 5 column bytes per glyph, bit0=top row .. bit6=bottom row,
matching the classic GLCD vertical-byte-per-column convention.

Run from anywhere: `python3 scripts/gen_font.py`. Edit the row-art below and
rerun rather than hand-editing the generated src/ui/Font5x7.cpp byte tables.
"""

import pathlib

PROJECT_ROOT = pathlib.Path(__file__).resolve().parent.parent

GLYPHS = {
    " ": [
        ".....",
        ".....",
        ".....",
        ".....",
        ".....",
        ".....",
        ".....",
    ],
    "-": [
        ".....",
        ".....",
        ".....",
        ".###.",
        ".....",
        ".....",
        ".....",
    ],
    ".": [
        ".....",
        ".....",
        ".....",
        ".....",
        ".....",
        ".....",
        "..#..",
    ],
    ":": [
        ".....",
        "..#..",
        ".....",
        ".....",
        "..#..",
        ".....",
        ".....",
    ],
    "/": [
        "....#",
        "...#.",
        "...#.",
        "..#..",
        ".#...",
        ".#...",
        "#....",
    ],
    "%": [
        "##...",
        "##..#",
        "...#.",
        "..#..",
        ".#...",
        "#..##",
        "...##",
    ],
    "(": [
        "..##.",
        ".#...",
        ".#...",
        ".#...",
        ".#...",
        ".#...",
        "..##.",
    ],
    ")": [
        ".##..",
        "...#.",
        "...#.",
        "...#.",
        "...#.",
        "...#.",
        ".##..",
    ],
    "0": [
        ".###.",
        "#...#",
        "#...#",
        "#...#",
        "#...#",
        "#...#",
        ".###.",
    ],
    "1": [
        "..#..",
        ".##..",
        "..#..",
        "..#..",
        "..#..",
        "..#..",
        ".###.",
    ],
    "2": [
        ".###.",
        "#...#",
        "....#",
        "...#.",
        "..#..",
        ".#...",
        "#####",
    ],
    "3": [
        ".###.",
        "#...#",
        "....#",
        "..##.",
        "....#",
        "#...#",
        ".###.",
    ],
    "4": [
        "...#.",
        "..##.",
        ".#.#.",
        "#..#.",
        "#####",
        "...#.",
        "...#.",
    ],
    "5": [
        "#####",
        "#....",
        "####.",
        "....#",
        "....#",
        "#...#",
        ".###.",
    ],
    "6": [
        "..##.",
        ".#...",
        "#....",
        "####.",
        "#...#",
        "#...#",
        ".###.",
    ],
    "7": [
        "#####",
        "....#",
        "...#.",
        "..#..",
        ".#...",
        ".#...",
        ".#...",
    ],
    "8": [
        ".###.",
        "#...#",
        "#...#",
        ".###.",
        "#...#",
        "#...#",
        ".###.",
    ],
    "9": [
        ".###.",
        "#...#",
        "#...#",
        ".####",
        "....#",
        "...#.",
        ".##..",
    ],
    "A": [
        ".###.",
        "#...#",
        "#...#",
        "#####",
        "#...#",
        "#...#",
        "#...#",
    ],
    "B": [
        "####.",
        "#...#",
        "#...#",
        "####.",
        "#...#",
        "#...#",
        "####.",
    ],
    "C": [
        ".###.",
        "#...#",
        "#....",
        "#....",
        "#....",
        "#...#",
        ".###.",
    ],
    "D": [
        "####.",
        "#...#",
        "#...#",
        "#...#",
        "#...#",
        "#...#",
        "####.",
    ],
    "E": [
        "#####",
        "#....",
        "#....",
        "####.",
        "#....",
        "#....",
        "#####",
    ],
    "F": [
        "#####",
        "#....",
        "#....",
        "####.",
        "#....",
        "#....",
        "#....",
    ],
    "G": [
        ".###.",
        "#...#",
        "#....",
        "#.###",
        "#...#",
        "#...#",
        ".###.",
    ],
    "H": [
        "#...#",
        "#...#",
        "#...#",
        "#####",
        "#...#",
        "#...#",
        "#...#",
    ],
    "I": [
        ".###.",
        "..#..",
        "..#..",
        "..#..",
        "..#..",
        "..#..",
        ".###.",
    ],
    "J": [
        "..###",
        "...#.",
        "...#.",
        "...#.",
        "...#.",
        "#..#.",
        ".##..",
    ],
    "K": [
        "#...#",
        "#..#.",
        "#.#..",
        "##...",
        "#.#..",
        "#..#.",
        "#...#",
    ],
    "L": [
        "#....",
        "#....",
        "#....",
        "#....",
        "#....",
        "#....",
        "#####",
    ],
    "M": [
        "#...#",
        "##.##",
        "#.#.#",
        "#.#.#",
        "#...#",
        "#...#",
        "#...#",
    ],
    "N": [
        "#...#",
        "##..#",
        "#.#.#",
        "#..##",
        "#...#",
        "#...#",
        "#...#",
    ],
    "O": [
        ".###.",
        "#...#",
        "#...#",
        "#...#",
        "#...#",
        "#...#",
        ".###.",
    ],
    "P": [
        "####.",
        "#...#",
        "#...#",
        "####.",
        "#....",
        "#....",
        "#....",
    ],
    "Q": [
        ".###.",
        "#...#",
        "#...#",
        "#...#",
        "#.#.#",
        "#..#.",
        ".##.#",
    ],
    "R": [
        "####.",
        "#...#",
        "#...#",
        "####.",
        "#.#..",
        "#..#.",
        "#...#",
    ],
    "S": [
        ".####",
        "#....",
        "#....",
        ".###.",
        "....#",
        "....#",
        "####.",
    ],
    "T": [
        "#####",
        "..#..",
        "..#..",
        "..#..",
        "..#..",
        "..#..",
        "..#..",
    ],
    "U": [
        "#...#",
        "#...#",
        "#...#",
        "#...#",
        "#...#",
        "#...#",
        ".###.",
    ],
    "V": [
        "#...#",
        "#...#",
        "#...#",
        "#...#",
        "#...#",
        ".#.#.",
        "..#..",
    ],
    "W": [
        "#...#",
        "#...#",
        "#...#",
        "#.#.#",
        "#.#.#",
        "##.##",
        "#...#",
    ],
    "X": [
        "#...#",
        "#...#",
        ".#.#.",
        "..#..",
        ".#.#.",
        "#...#",
        "#...#",
    ],
    "Y": [
        "#...#",
        "#...#",
        ".#.#.",
        "..#..",
        "..#..",
        "..#..",
        "..#..",
    ],
    "Z": [
        "#####",
        "....#",
        "...#.",
        "..#..",
        ".#...",
        "#....",
        "#####",
    ],
}

def glyph_to_bytes(rows):
    assert len(rows) == 7
    for r in rows:
        assert len(r) == 5, r
    cols = []
    for c in range(5):
        b = 0
        for r in range(7):
            if rows[r][c] == '#':
                b |= (1 << r)
        cols.append(b)
    return cols

order = sorted(GLYPHS.keys())

lines = []
lines.append("#pragma once")
lines.append("")
lines.append("#include <cstdint>")
lines.append("")
lines.append("// Self-designed 5x7 monospace glyph set (space, - . : / ( ) %, 0-9, A-Z).")
lines.append("// Generated by scripts/gen_font.py - do not hand-edit the byte tables.")
lines.append("// Column-major: 5 bytes per glyph, bit0 = top row, bit6 = bottom row.")
lines.append("// Only uppercase + digits are defined: Dashboard.cpp uppercases all text")
lines.append("// before drawing, so lowercase glyphs are intentionally omitted.")
lines.append("")
lines.append("namespace font5x7 {")
lines.append("")
lines.append("constexpr uint8_t GLYPH_WIDTH = 5;")
lines.append("constexpr uint8_t GLYPH_HEIGHT = 7;")
lines.append("")
lines.append("// Returns nullptr for characters outside the supported set; caller should")
lines.append("// fall back to a blank glyph (EinkCanvas::drawText skips them).")
lines.append("const uint8_t* glyphFor(char c);")
lines.append("")
lines.append("}  // namespace font5x7")
lines.append("")

header_path = str(PROJECT_ROOT / "src" / "ui" / "Font5x7.h")
with open(header_path, "w") as f:
    f.write("\n".join(lines) + "\n")

cpp_lines = []
cpp_lines.append('#include "Font5x7.h"')
cpp_lines.append("")
cpp_lines.append("namespace font5x7 {")
cpp_lines.append("")
cpp_lines.append("namespace {")
for ch in order:
    b = glyph_to_bytes(GLYPHS[ch])
    name = {
        " ": "SPACE", "-": "DASH", ".": "DOT", ":": "COLON", "/": "SLASH",
        "(": "LPAREN", ")": "RPAREN", "%": "PCT",
    }.get(ch, "CH_" + ch)
    cpp_lines.append(
        f"constexpr uint8_t {name}[5] = {{{', '.join('0x%02X' % x for x in b)}}};"
    )
cpp_lines.append("}  // namespace")
cpp_lines.append("")
cpp_lines.append("const uint8_t* glyphFor(char c) {")
cpp_lines.append("  switch (c) {")
for ch in order:
    name = {
        " ": "SPACE", "-": "DASH", ".": "DOT", ":": "COLON", "/": "SLASH",
        "(": "LPAREN", ")": "RPAREN", "%": "PCT",
    }.get(ch, "CH_" + ch)
    lit = ch.replace("\\", "\\\\").replace("'", "\\'")
    cpp_lines.append(f"    case '{lit}':")
    cpp_lines.append(f"      return {name};")
cpp_lines.append("    default:")
cpp_lines.append("      return nullptr;")
cpp_lines.append("  }")
cpp_lines.append("}")
cpp_lines.append("")
cpp_lines.append("}  // namespace font5x7")
cpp_lines.append("")

cpp_path = str(PROJECT_ROOT / "src" / "ui" / "Font5x7.cpp")
with open(cpp_path, "w") as f:
    f.write("\n".join(cpp_lines))

print("wrote", header_path)
print("wrote", cpp_path)
print("glyph count:", len(order))
