"""Draws the plugin's mouse images (our own simple illustration) and prints the
SVG zone paths for the device view, in device_view.png pixel coordinates.

Usage: python3 tools/make_images.py <out_dir>
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw

BODY = (38, 40, 44, 255)
EDGE = (70, 74, 80, 255)
ZONE = (120, 124, 130, 255)

# device_view canvas (same size as the ASUS plugin's device views)
VIEW_W, VIEW_H = 615, 408
# mouse body box, centred, portrait
BODY_BOX = (230, 24, 385, 384)
WHEEL_BOX = (299, 70, 316, 130)
LOGO_CENTER, LOGO_R = (307, 286), 26


def draw_mouse(size, scale=1.0, offset=(0, 0)):
    img = Image.new("RGBA", size, (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    def box(b):
        return tuple(round(v * scale + offset[i % 2]) for i, v in enumerate(b))

    d.rounded_rectangle(box(BODY_BOX), radius=round(70 * scale), fill=BODY, outline=EDGE, width=max(1, round(3 * scale)))
    # split between the two buttons
    x = round(307 * scale + offset[0])
    d.line([(x, round(24 * scale + offset[1])), (x, round(160 * scale + offset[1]))], fill=EDGE, width=max(1, round(2 * scale)))
    d.rounded_rectangle(box(WHEEL_BOX), radius=round(7 * scale), fill=ZONE)
    cx, cy = LOGO_CENTER
    r = LOGO_R
    d.ellipse(box((cx - r, cy - r, cx + r, cy + r)), outline=ZONE, width=max(1, round(5 * scale)))
    return img


def fit(size):
    """Scale + offset that centre the view's mouse body inside a canvas of `size`."""
    bw, bh = BODY_BOX[2] - BODY_BOX[0], BODY_BOX[3] - BODY_BOX[1]
    s = min(size[0] * 0.8 / bw, size[1] * 0.9 / bh)
    ox = size[0] / 2 - (BODY_BOX[0] + bw / 2) * s
    oy = size[1] / 2 - (BODY_BOX[1] + bh / 2) * s
    return s, (ox, oy)


def svg_rect(b):
    x0, y0, x1, y1 = b
    return f"M {x0} {y0} L {x1} {y0} L {x1} {y1} L {x0} {y1} Z"


def svg_circle(c, r, n=16):
    import math
    pts = [(round(c[0] + r * math.cos(2 * math.pi * i / n)), round(c[1] + r * math.sin(2 * math.pi * i / n))) for i in range(n)]
    return "M " + " L ".join(f"{x} {y}" for x, y in pts) + " Z"


def main(out):
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    view = draw_mouse((VIEW_W, VIEW_H))
    view.save(out / "device_view.png")
    view.save(out / "mask.png")
    for name, size in (("thumbnail.png", (180, 124)), ("promo.png", (496, 496))):
        s, off = fit(size)
        draw_mouse(size, s, off).save(out / name)
    print("WHEEL_PATH", svg_rect(WHEEL_BOX))
    print("LOGO_PATH", svg_circle(LOGO_CENTER, LOGO_R))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "plugin/images")
