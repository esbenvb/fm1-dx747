#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
"""Generate app/dx747/vfont.inc, the stroke font DX747 draws medium and large text with.

Each glyph is a few strokes (polylines) on a grid of 9 x 13 points: x 0..8 from left to right, y 0..12 from the top to
the baseline, so a capital fills the whole grid. The firmware maps the grid onto the same box the 5x7 bitmap font fills
(5*scale x 7*scale pixels) and draws every stroke with round ends and soft edges (anti-aliased). The shapes are original
to this project.

    python3 tools/dx747/gen-vfont.py > app/dx747/vfont.inc
    python3 tools/dx747/gen-vfont.py --specimen OUT.png     # a sheet of every glyph at three sizes (needs Pillow)
"""
import sys

# Strokes are lists of (x, y). A one-point-longer polyline is a line; a stroke whose two points are one apart is a dot.
GLYPHS = {
    "0": [[(3, 0), (5, 0), (8, 3), (8, 9), (5, 12), (3, 12), (0, 9), (0, 3), (3, 0)], [(2, 10), (6, 2)]],
    "1": [[(1, 3), (4, 0), (4, 12)], [(1, 12), (7, 12)]],
    "2": [[(0, 3), (2, 0), (6, 0), (8, 2), (8, 4), (0, 12), (8, 12)]],
    "3": [[(0, 2), (2, 0), (6, 0), (8, 2), (8, 4), (6, 6), (3, 6)], [(6, 6), (8, 8), (8, 10), (6, 12), (2, 12), (0, 10)]],
    "4": [[(6, 12), (6, 0), (0, 8), (8, 8)]],
    "5": [[(8, 0), (1, 0), (0, 6), (4, 5), (7, 6), (8, 9), (7, 11), (4, 12), (1, 12), (0, 10)]],
    "6": [[(7, 1), (5, 0), (3, 0), (0, 3), (0, 9), (3, 12), (5, 12), (8, 9), (8, 7), (5, 5), (3, 5), (0, 8)]],
    "7": [[(0, 0), (8, 0), (3, 12)]],
    "8": [[(4, 6), (1, 5), (0, 3), (1, 1), (3, 0), (5, 0), (7, 1), (8, 3), (7, 5), (4, 6)],
          [(4, 6), (1, 7), (0, 9), (1, 11), (3, 12), (5, 12), (7, 11), (8, 9), (7, 7), (4, 6)]],
    "9": [[(1, 11), (3, 12), (5, 12), (8, 9), (8, 3), (5, 0), (3, 0), (0, 3), (0, 5), (3, 7), (5, 7), (8, 4)]],
    "A": [[(0, 12), (4, 0), (8, 12)], [(1, 8), (7, 8)]],
    "B": [[(0, 12), (0, 0), (5, 0), (7, 1), (8, 3), (7, 5), (5, 6), (0, 6)], [(5, 6), (7, 7), (8, 9), (7, 11), (5, 12), (0, 12)]],
    "C": [[(8, 2), (6, 0), (3, 0), (1, 2), (0, 4), (0, 8), (1, 10), (3, 12), (6, 12), (8, 10)]],
    "D": [[(0, 0), (0, 12), (4, 12), (7, 10), (8, 8), (8, 4), (7, 2), (4, 0), (0, 0)]],
    "E": [[(8, 0), (0, 0), (0, 12), (8, 12)], [(0, 6), (6, 6)]],
    "F": [[(8, 0), (0, 0), (0, 12)], [(0, 6), (6, 6)]],
    "G": [[(8, 2), (6, 0), (3, 0), (1, 2), (0, 4), (0, 8), (1, 10), (3, 12), (6, 12), (8, 10), (8, 7), (4, 7)]],
    "H": [[(0, 0), (0, 12)], [(8, 0), (8, 12)], [(0, 6), (8, 6)]],
    "I": [[(2, 0), (6, 0)], [(4, 0), (4, 12)], [(2, 12), (6, 12)]],
    "J": [[(8, 0), (8, 9), (6, 12), (3, 12), (1, 10), (0, 8)]],
    "K": [[(0, 0), (0, 12)], [(8, 0), (1, 7), (8, 12)]],
    "L": [[(0, 0), (0, 12), (8, 12)]],
    "M": [[(0, 12), (0, 0), (4, 7), (8, 0), (8, 12)]],
    "N": [[(0, 12), (0, 0), (8, 12), (8, 0)]],
    "O": [[(3, 0), (5, 0), (8, 3), (8, 9), (5, 12), (3, 12), (0, 9), (0, 3), (3, 0)]],
    "P": [[(0, 12), (0, 0), (5, 0), (7, 1), (8, 3), (7, 5), (5, 6), (0, 6)]],
    "Q": [[(3, 0), (5, 0), (8, 3), (8, 9), (5, 12), (3, 12), (0, 9), (0, 3), (3, 0)], [(5, 8), (8, 12)]],
    "R": [[(0, 12), (0, 0), (5, 0), (7, 1), (8, 3), (7, 5), (5, 6), (0, 6)], [(4, 6), (8, 12)]],
    "S": [[(8, 2), (6, 0), (2, 0), (0, 2), (0, 4), (2, 6), (6, 6), (8, 8), (8, 10), (6, 12), (2, 12), (0, 10)]],
    "T": [[(0, 0), (8, 0)], [(4, 0), (4, 12)]],
    "U": [[(0, 0), (0, 9), (2, 12), (6, 12), (8, 9), (8, 0)]],
    "V": [[(0, 0), (4, 12), (8, 0)]],
    "W": [[(0, 0), (2, 12), (4, 5), (6, 12), (8, 0)]],
    "X": [[(0, 0), (8, 12)], [(8, 0), (0, 12)]],
    "Y": [[(0, 0), (4, 6), (8, 0)], [(4, 6), (4, 12)]],
    "Z": [[(0, 0), (8, 0), (0, 12), (8, 12)]],
    "-": [[(1, 6), (7, 6)]],
    "/": [[(7, 0), (1, 12)]],
    "+": [[(1, 6), (7, 6)], [(4, 3), (4, 9)]],
    "#": [[(3, 0), (2, 12)], [(6, 0), (5, 12)], [(0, 4), (8, 4)], [(0, 8), (8, 8)]],
    "%": [[(0, 0), (3, 0), (3, 4), (0, 4), (0, 0)], [(5, 8), (8, 8), (8, 12), (5, 12), (5, 8)], [(8, 0), (0, 12)]],
    ">": [[(1, 1), (7, 6), (1, 11)]],
    "<": [[(7, 1), (1, 6), (7, 11)]],
    "?": [[(0, 3), (1, 1), (3, 0), (5, 0), (7, 1), (8, 3), (7, 5), (4, 7), (4, 8)], [(4, 11), (4, 12)]],
    "!": [[(4, 0), (4, 8)], [(4, 11), (4, 12)]],
    "(": [[(6, 0), (3, 3), (3, 9), (6, 12)]],
    ")": [[(2, 0), (5, 3), (5, 9), (2, 12)]],
    ",": [[(5, 10), (5, 11), (3, 12)]],
    ".": [[(4, 11), (4, 12)]],
    ":": [[(4, 3), (4, 4)], [(4, 9), (4, 10)]],
    "=": [[(1, 4), (7, 4)], [(1, 8), (7, 8)]],
    "_": [[(0, 12), (8, 12)]],
    "|": [[(4, 0), (4, 12)]],
}
ORDER = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
PUNCT = "-/+#%><?!(),.:=_|"
VEND, VSTOP = 0xFF, 0xFE


def emit():
    data, offsets = [], []
    for ch in ORDER + PUNCT:
        offsets.append(len(data))
        for stroke in GLYPHS[ch]:
            for (x, y) in stroke:
                assert 0 <= x <= 8 and 0 <= y <= 12, (ch, x, y)
                data.append(x << 4 | y)
            data.append(VEND)
        data.append(VSTOP)
    assert len(data) < 65536
    out = ["/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */",
           "/* Generated by tools/dx747/gen-vfont.py: do not edit. The stroke font for medium and large text (see ui_core.inc).",
           " * A point is x << 4 | y on a 9 x 13 grid; 0xFF ends a stroke, 0xFE ends the glyph. */",
           'static const char vf_order[] = "%s";' % (ORDER + PUNCT),
           "static const uint16_t vf_off[%d] = {" % len(offsets)]
    for i in range(0, len(offsets), 12):
        out.append("    " + ", ".join(str(o) for o in offsets[i:i + 12]) + ",")
    out.append("};")
    out.append("static const uint8_t vf_data[%d] = {" % len(data))
    for i in range(0, len(data), 16):
        out.append("    " + ", ".join("0x%02x" % b for b in data[i:i + 16]) + ",")
    out.append("};")
    return "\n".join(out) + "\n"


def specimen(path):
    # A rough preview with plain polylines (the firmware's anti-aliased renderer is the real thing: use a native recipe shot).
    from PIL import Image, ImageDraw
    sizes, zoom, width = [2, 3, 5], 3, 1500
    glyphs = ORDER + PUNCT
    layout, y = [], 10                      # lay everything out first so the canvas is as tall as the sheet needs
    for s in sizes:
        x = 10
        for ch in glyphs:
            if x + 6 * s * zoom > width - 20:
                x, y = 10, y + 7 * s * zoom + 20
            layout.append((s, ch, x, y))
            x += 6 * s * zoom
        y += 7 * s * zoom + 30
    img = Image.new("RGB", (width, y + 10), (0, 0, 0))
    d = ImageDraw.Draw(img)
    for s, ch, x, y in layout:
        for stroke in GLYPHS[ch]:
            pts = [(x + px * 5 * s * zoom / 8, y + py * 7 * s * zoom / 12) for (px, py) in stroke]
            d.line(pts, fill=(255, 255, 255), width=max(1, int(s * zoom * 0.9)))
    img.save(path)


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--specimen":
        specimen(sys.argv[2])
    else:
        sys.stdout.write(emit())
