#!/usr/bin/env python3
"""Contact sheet of screenshots: sheet.py OUT.png COLUMNS a.png b.png ..."""
import sys
from PIL import Image
out, cols, files = sys.argv[1], int(sys.argv[2]), sys.argv[3:]
imgs = [Image.open(f).convert("RGB") for f in files]
w, h = imgs[0].size
rows = (len(imgs) + cols - 1) // cols
sheet = Image.new("RGB", (cols * (w + 8) + 8, rows * (h + 8) + 8), (40, 40, 40))
for i, im in enumerate(imgs):
    sheet.paste(im, (8 + (i % cols) * (w + 8), 8 + (i // cols) * (h + 8)))
sheet.save(out)
