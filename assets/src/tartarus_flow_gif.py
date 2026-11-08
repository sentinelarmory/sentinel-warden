"""Renders assets/tartarus-flow.gif: how a colour travels from an iCUE Mural to the Razer Tartarus V2,
and that Razer Synapse (key mappings, macros, profiles) is not part of that path.

Usage: python3 assets/src/tartarus_flow_gif.py   (needs Pillow + the Rajdhani/Orbitron fonts)
"""
import colorsys
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

W, H = 1160, 380
FRAMES = 72
OUT = Path(__file__).resolve().parent.parent / "tartarus-flow.gif"
BG = (42, 47, 50)
METAL = (56, 63, 67)
EDGE = (88, 98, 104)
TEXT = (214, 232, 236)
MUTED = (140, 156, 162)


def font(name, size):
    for path in (f"~/.local/share/fonts/{name}", f"/usr/share/fonts/truetype/{name}"):
        p = Path(path).expanduser()
        if p.exists():
            return ImageFont.truetype(str(p), size)
    return ImageFont.load_default()


TITLE = font("Orbitron.ttf", 15)
LABEL = font("Rajdhani-SemiBold.ttf", 21)
SMALL = font("Rajdhani-Medium.ttf", 18)


def wave_color(x, t):
    hue = (x * 0.9 - t) % 1.0
    r, g, b = colorsys.hsv_to_rgb(hue, 0.85, 1.0)
    return int(r * 255), int(g * 255), int(b * 255)


BOXES = {  # name: (x, y, w, h)
    "mural": (40, 70, 210, 150),
    "plugin": (320, 70, 270, 150),
    "usb": (655, 70, 250, 150),
    "pad": (970, 60, 150, 170),
}
ARROWS = [(250, 112, 320), (590, 112, 655), (905, 112, 970)]  # (x0, y, x1)
PULSE_X0, PULSE_X1 = 250, 985


def box(d, glow, key, title, lines, lit):
    x, y, w, h = BOXES[key]
    d.rounded_rectangle((x, y, x + w, y + h), 14, fill=METAL, outline=lit or EDGE, width=3 if lit else 2)
    if lit:
        glow.rounded_rectangle((x, y, x + w, y + h), 14, outline=lit, width=5)
    d.text((x + 16, y + 14), title, font=TITLE, fill=(169, 196, 201))
    for i, ln in enumerate(lines):
        d.text((x + 16, y + 66 + i * 25), ln, font=LABEL if i == 0 else SMALL, fill=TEXT if i == 0 else MUTED)


def arrow(d, x0, y, x1, color):
    d.line((x0 + 6, y, x1 - 10, y), fill=color, width=3)
    d.polygon([(x1 - 4, y), (x1 - 16, y - 8), (x1 - 16, y + 8)], fill=color)


def frame(i):
    t = i / FRAMES
    base = Image.new("RGB", (W, H), BG)
    glow = Image.new("RGB", (W, H), (0, 0, 0))
    d, g = ImageDraw.Draw(base), ImageDraw.Draw(glow)

    # one pulse travels left to right; its colour is the Mural wave at that moment
    px = PULSE_X0 + (PULSE_X1 - PULSE_X0) * t
    pc = wave_color(0.3, t * 0.9)

    def lit_for(key):
        x, y, w, h = BOXES[key]
        return pc if x - 14 <= px <= x + w + 14 else None

    # Mural box with a live colour strip
    box(d, g, "mural", "iCUE MURALS", ["Effect or Mural", "painted by iCUE"], lit_for("mural"))
    mx, my, mw, mh = BOXES["mural"]
    for sx in range(mx + 16, mx + mw - 16, 4):
        d.rectangle((sx, my + mh - 34, sx + 4, my + mh - 18), fill=wave_color((sx - mx) / mw * 0.5, t))

    box(d, g, "plugin", "iCUE PLUGIN", ["tartarus_plugin.dll", "20 key LEDs, one colour,", "glides between frames"], lit_for("plugin"))
    box(d, g, "usb", "USB CONTROL CHANNEL", ["HID interface 2", "Razer custom frames,", "lighting only"], lit_for("usb"))

    # keypad: lit with the colour that just arrived
    arrived = wave_color(0.3, max(0.0, t * 0.9 - 0.06)) if px > BOXES["pad"][0] - 20 else (70, 76, 84)
    x, y, w, h = BOXES["pad"]
    d.rounded_rectangle((x, y, x + w, y + h), 14, fill=METAL, outline=EDGE, width=2)
    for row in range(4):
        for col in range(5):
            kx, ky = x + 15 + col * 25, y + 14 + row * 25
            d.rounded_rectangle((kx, ky, kx + 20, ky + 20), 4, fill=(26, 29, 36))
            for target in (d, g):
                target.rounded_rectangle((kx - 3, ky - 3, kx + 23, ky + 23), 6, outline=arrived, width=2)
    d.ellipse((x + w / 2 - 22, y + 122, x + w / 2 + 22, y + 160), fill=(30, 33, 40), outline=EDGE, width=2)
    d.text((x - 6, y + h + 12), "RAZER TARTARUS V2", font=LABEL, fill=TEXT)

    # arrows, brightened when the pulse is on them
    for x0, ay, x1 in ARROWS:
        on = x0 - 10 <= px <= x1 + 10
        arrow(d, x0, ay, x1, pc if on else EDGE)
    # the pulse itself
    for target in (d, g):
        target.ellipse((px - 9, 112 - 9, px + 9, 112 + 9), fill=pc)

    # Synapse: separate, not part of the lighting path
    sx, sy, sw, sh = 560, 290, 560, 64
    d.rounded_rectangle((sx, sy, sx + sw, sy + sh), 12, fill=(48, 54, 58), outline=EDGE, width=2)
    d.text((sx + 16, sy + 8), "RAZER SYNAPSE", font=TITLE, fill=(169, 196, 201))
    d.text((sx + 16, sy + 32), "key mappings, macros, profiles: never touched", font=SMALL, fill=MUTED)
    for yy in range(y + h + 44, sy, 10):  # dashed line to the keypad: the keys stay with Synapse
        d.line((x + w / 2, yy, x + w / 2, yy + 5), fill=EDGE, width=2)
    d.text((40, 306), "Lighting follows iCUE.", font=LABEL, fill=TEXT)
    d.text((40, 334), "The keys stay with Synapse.", font=SMALL, fill=MUTED)

    return ImageChops.add(base, glow.filter(ImageFilter.GaussianBlur(12)))


def render():
    frames = [frame(i).convert("P", palette=Image.ADAPTIVE, colors=128) for i in range(FRAMES)]
    frames[0].save(OUT, save_all=True, append_images=frames[1:], duration=50, loop=0, optimize=True)
    print(f"wrote {OUT} ({OUT.stat().st_size // 1024} KiB, {FRAMES} frames)")


if __name__ == "__main__":
    render()
