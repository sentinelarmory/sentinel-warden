"""Draws the Tartarus plugin's images (our own simple 4x5 key-grid illustration).
The key geometry must match GRID_X0/GRID_Y0/KEY_W/KEY_H/KEY_GAP in src/tartarus_plugin.c.

Usage: python3 tools/make_tartarus_images.py <out_dir>
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw

BODY = (38, 40, 44, 255)
EDGE = (70, 74, 80, 255)
KEY = (120, 124, 130, 255)

VIEW_W, VIEW_H = 615, 408
ROWS, COLS = 4, 5
X0, Y0, KW, KH, GAP = 205, 72, 40, 40, 14
PAD = (X0 - 40, Y0 - 40, X0 + COLS * KW + (COLS - 1) * GAP + 40, Y0 + ROWS * KH + (ROWS - 1) * GAP + 110)


def draw(size, scale=1.0, offset=(0, 0)):
    img = Image.new("RGBA", size, (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    def box(b):
        return tuple(round(v * scale + offset[i % 2]) for i, v in enumerate(b))

    d.rounded_rectangle(box(PAD), radius=round(36 * scale), fill=BODY, outline=EDGE, width=max(1, round(3 * scale)))
    for r in range(ROWS):
        for c in range(COLS):
            x = X0 + c * (KW + GAP)
            y = Y0 + r * (KH + GAP)
            d.rounded_rectangle(box((x, y, x + KW, y + KH)), radius=round(8 * scale), fill=KEY)
    y = Y0 + ROWS * (KH + GAP) + 6
    d.rounded_rectangle(box((X0, y, X0 + 2 * KW + GAP, y + KH)), radius=round(8 * scale), fill=KEY)
    x = X0 + 3 * (KW + GAP)
    d.rounded_rectangle(box((x, y, x + KW, y + KH)), radius=round(8 * scale), fill=KEY)
    return img


def fit(size):
    bw, bh = PAD[2] - PAD[0], PAD[3] - PAD[1]
    s = min(size[0] * 0.8 / bw, size[1] * 0.9 / bh)
    ox = size[0] / 2 - (PAD[0] + bw / 2) * s
    oy = size[1] / 2 - (PAD[1] + bh / 2) * s
    return s, (ox, oy)


def main(out):
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    view = draw((VIEW_W, VIEW_H))
    view.save(out / "device_view.png")
    view.save(out / "mask.png")
    for name, size in (("thumbnail.png", (180, 124)), ("promo.png", (496, 496))):
        s, off = fit(size)
        draw(size, s, off).save(out / name)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "build/tartarus-plugin/Razer")
