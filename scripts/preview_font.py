#!/usr/bin/env python3
"""Renders text through the generated Font5x7.cpp tables as terminal ASCII art.

The point is to eyeball glyph correctness without flashing hardware - it reads
the *generated* byte tables (not gen_font.py's source art), so it catches a
broken generator as well as broken art. See README.md's bring-up checklist.

    python3 scripts/preview_font.py            # the full glyph set
    python3 scripts/preview_font.py "GGA OK 5 (2 BAD) 3S"
"""
import pathlib
import re
import sys

PROJECT_ROOT = pathlib.Path(__file__).resolve().parent.parent
CPP = PROJECT_ROOT / "src" / "ui" / "Font5x7.cpp"

NAME_TO_CHAR = {
    "SPACE": " ", "DASH": "-", "DOT": ".", "COLON": ":", "SLASH": "/",
    "LPAREN": "(", "RPAREN": ")", "PCT": "%",
}


def load_glyphs():
    src = CPP.read_text()
    tables = {}
    for name, body in re.findall(r"constexpr uint8_t (\w+)\[5\] = \{([^}]*)\};", src):
        tables[name] = [int(b, 16) for b in re.findall(r"0x([0-9A-Fa-f]{2})", body)]
    glyphs = {}
    for name, cols in tables.items():
        ch = NAME_TO_CHAR.get(name) or (name[3:] if name.startswith("CH_") else None)
        if ch:
            glyphs[ch] = cols
    return glyphs


def render(text, glyphs):
    rows = []
    for row in range(7):
        line = ""
        for ch in text.upper():
            cols = glyphs.get(ch)
            if cols is None:
                line += "?????"  # unsupported - would draw blank on the panel
            else:
                line += "".join("#" if cols[c] & (1 << row) else "." for c in range(5))
            line += "."  # the 1px inter-glyph gap EinkCanvas adds
        rows.append(line)
    return "\n".join(rows)


def main():
    glyphs = load_glyphs()
    if len(sys.argv) > 1:
        print(render(" ".join(sys.argv[1:]), glyphs))
        return
    chars = sorted(glyphs)
    for i in range(0, len(chars), 16):
        chunk = "".join(chars[i:i + 16])
        print(render(chunk, glyphs))
        print()
    missing = [c for c in "GGA OK (2 BAD) 3S -.:/0123456789" if c.upper() not in glyphs]
    print(f"glyphs: {len(glyphs)}   missing from sample text: {missing or 'none'}")


if __name__ == "__main__":
    main()
