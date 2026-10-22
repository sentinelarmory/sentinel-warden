"""Renders assets/sync.gif: an iCUE Murals wave sweeping across an ASUS GPU,
the motherboard I/O shroud and a Logitech G403, all lit in sync.

Usage: python3 assets/src/sync_gif.py   (needs Pillow + the Rajdhani/Orbitron fonts)
"""
import colorsys
import math
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

W, H = 1000, 380
FRAMES = 60
OUT = Path(__file__).resolve().parent.parent / "sync.gif"
BG = (42, 47, 50)
METAL = (56, 63, 67)
EDGE = (88, 98, 104)


def font(name, size):
    for path in (f"~/.local/share/fonts/{name}", f"/usr/share/fonts/truetype/{name}"):
        p = Path(path).expanduser()
        if p.exists():
            return ImageFont.truetype(str(p), size)
    return ImageFont.load_default()


LABEL = font("Rajdhani-SemiBold.ttf", 22)
TITLE = font("Orbitron.ttf", 20)


def wave_color(x, t):
    """Mural colour at canvas x (0..1) and time t (0..1): a hue wave moving right."""
    hue = (x * 0.9 - t) % 1.0
    r, g, b = colorsys.hsv_to_rgb(hue, 0.85, 1.0)
    return int(r * 255), int(g * 255), int(b * 255)


def draw_gpu(d, glow, x, y, t):
    w, h = 380, 130
    d.rounded_rectangle((x, y, x + w, y + h), 14, fill=METAL, outline=EDGE, width=2)
    for i, cx in enumerate((x + 105, x + 275)):
        d.ellipse((cx - 52, y + 13, cx + 52, y + 117), fill=(24, 27, 34), outline=EDGE, width=2)
        for k in range(7):  # fan blades, slowly turning
            a = k * (2 * math.pi / 7) + t * 2 * math.pi * (1 if i else -1)
            d.line((cx, y + 65, cx + 44 * math.cos(a), y + 65 + 44 * math.sin(a)), fill=(70, 76, 90), width=6)
        d.ellipse((cx - 14, y + 51, cx + 14, y + 79), fill=(48, 52, 64))
    # LED strip along the top edge, 23 LEDs like the Strix card
    for k in range(23):
        lx = x + 20 + k * (w - 40) / 22
        c = wave_color(lx / W, t)
        for target in (d, glow):
            target.rounded_rectangle((lx - 6, y - 14, lx + 6, y - 4), 3, fill=c)
    d.text((x, y + h + 14), "ASUS GPU", font=LABEL, fill=(214, 232, 236))


def draw_shroud(d, glow, x, y, t):
    w, h = 150, 170
    d.rounded_rectangle((x, y, x + w, y + h), 12, fill=METAL, outline=EDGE, width=2)
    for k in range(5):  # ports
        d.rounded_rectangle((x + 20, y + 20 + k * 28, x + 70, y + 36 + k * 28), 3, fill=(22, 24, 30))
    # 3 LEDs on the shroud, like the Z890-F
    for k in range(3):
        ly = y + 30 + k * 50
        c = wave_color((x + 110) / W, t + k * 0.03)
        for target in (d, glow):
            target.rounded_rectangle((x + 100, ly, x + 128, ly + 36), 6, fill=c)
    d.text((x, y + h + 14), "MOTHERBOARD I/O", font=LABEL, fill=(214, 232, 236))


def draw_mouse(d, glow, x, y, t):
    w, h = 110, 190
    d.rounded_rectangle((x, y, x + w, y + h), 52, fill=METAL, outline=EDGE, width=2)
    d.line((x + w / 2, y + 4, x + w / 2, y + 70), fill=EDGE, width=2)
    wheel = wave_color((x + 40) / W, t)
    logo = wave_color((x + 70) / W, t)
    for target in (d, glow):
        target.rounded_rectangle((x + w / 2 - 7, y + 26, x + w / 2 + 7, y + 62), 6, fill=wheel)
        target.ellipse((x + w / 2 - 20, y + 120, x + w / 2 + 20, y + 160), outline=logo, width=7)
    d.text((x - 10, y + h + 14), "LOGITECH G403", font=LABEL, fill=(214, 232, 236))


def frame(t):
    base = Image.new("RGB", (W, H), BG)
    glow = Image.new("RGB", (W, H), (0, 0, 0))
    d, g = ImageDraw.Draw(base), ImageDraw.Draw(glow)

    # the Mural strip: what iCUE is "painting" across the desk
    for px in range(40, W - 40, 4):
        c = wave_color(px / W, t)
        d.rectangle((px, 30, px + 4, 44), fill=c)
    d.text((40, 54), "iCUE MURALS", font=TITLE, fill=(169, 196, 201))

    draw_gpu(d, g, 60, 150, t)
    draw_shroud(d, g, 520, 120, t)
    draw_mouse(d, g, 800, 110, t)

    # LEDs light up their surroundings: blur the LED layer and add it on top
    return ImageChops.add(base, glow.filter(ImageFilter.GaussianBlur(14)))


def render():
    frames = [frame(i / FRAMES).convert("P", palette=Image.ADAPTIVE, colors=128) for i in range(FRAMES)]
    frames[0].save(OUT, save_all=True, append_images=frames[1:], duration=50, loop=0, optimize=True)
    print(f"wrote {OUT} ({OUT.stat().st_size // 1024} KiB, {FRAMES} frames)")


if __name__ == "__main__":
    render()
