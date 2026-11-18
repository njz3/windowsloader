# windowsloader - runs Windows arcade games with emulated cabinet hardware
# Copyright (C) 2026 Benjamin Maurin - njz3
# License: GNU General Public License version 3 (GPL v3)
#
# This program is free software: you can redistribute it and/or modify it under the terms of
# the GNU General Public License version 3 as published by the Free Software Foundation.
# It is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even
# the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE
# file for the full text. The author reserves the right to change the license of this code.

"""windowsloader's logo: a steering wheel on a blue tile, its hub amber (the settings menu's
highlight colour). Writes res/windowsloader.ico (16 to 256 pixels, each size drawn on its own
so that the small ones stay legible) and res/windowsloader.png (256 pixels). Needs Pillow.

    python scripts/make_logo.py
"""
import math
import os

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "res")
SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]
SUPER = 8  # drawn this many times larger, then reduced (smooth edges)

TOP, BOTTOM = (40, 92, 178), (20, 46, 104)  # the tile's gradient
WHEEL = (255, 255, 255)
HUB = (255, 191, 51)                        # ImVec4(1.0, 0.75, 0.2): the menu's highlight


def tile(size):
    """The rounded tile with its vertical gradient, as a mask and a picture."""
    s = size * SUPER
    grad = Image.new("RGBA", (s, s))
    px = grad.load()
    for y in range(s):
        t = y / (s - 1)
        c = tuple(int(TOP[i] + (BOTTOM[i] - TOP[i]) * t) for i in range(3)) + (255,)
        for x in range(s):
            px[x, y] = c
    mask = Image.new("L", (s, s), 0)
    margin = 0 if size <= 24 else s * 0.03
    ImageDraw.Draw(mask).rounded_rectangle((margin, margin, s - 1 - margin, s - 1 - margin), radius=s * 0.22, fill=255)
    out = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    out.paste(grad, (0, 0), mask)
    return out


def wheel(img, size):
    s = size * SUPER
    d = ImageDraw.Draw(img)
    cx = cy = s / 2
    # Thicker strokes for the small sizes, so that 16 pixels still show a ring and its spokes
    ring = s * (0.36 if size > 32 else 0.37)
    width = s * (0.085 if size > 32 else 0.12 if size > 20 else 0.14)
    spoke = width * (0.85 if size > 32 else 1.0)
    hub = s * (0.11 if size > 32 else 0.14)
    d.ellipse((cx - ring - width / 2, cy - ring - width / 2, cx + ring + width / 2, cy + ring + width / 2), fill=WHEEL)
    d.ellipse((cx - ring + width / 2, cy - ring + width / 2, cx + ring - width / 2, cy + ring - width / 2), fill=(0, 0, 0, 0))
    # The inside cut out shows the tile again: redraw the gradient there
    inner = tile(size)
    m = Image.new("L", (s, s), 0)
    ImageDraw.Draw(m).ellipse((cx - ring + width / 2, cy - ring + width / 2, cx + ring - width / 2, cy + ring - width / 2), fill=255)
    img.paste(inner, (0, 0), m)
    d = ImageDraw.Draw(img)
    # Three spokes: left, right and down, as a rally wheel's
    for angle in (180, 0, 90):
        a = math.radians(angle)
        x, y = cx + math.cos(a) * ring, cy + math.sin(a) * ring
        d.line((cx, cy, x, y), fill=WHEEL, width=int(spoke))
    d.ellipse((cx - hub, cy - hub, cx + hub, cy + hub), fill=HUB)
    if size > 32:  # the top mark of a racing wheel's rim, the centre position
        mark = width * 0.55
        d.rectangle((cx - mark / 2, cy - ring - width / 2, cx + mark / 2, cy - ring + width / 2), fill=HUB)
    return img


def logo(size):
    img = wheel(tile(size), size)
    return img.resize((size, size), Image.LANCZOS)


def main():
    os.makedirs(OUT, exist_ok=True)
    images = [logo(n) for n in SIZES]
    images[-1].save(os.path.join(OUT, "windowsloader.png"))
    # Pillow writes one entry per size, each from its own drawing
    images[-1].save(os.path.join(OUT, "windowsloader.ico"), format="ICO", sizes=[(n, n) for n in SIZES],
                    append_images=images[:-1])
    sheet = Image.new("RGBA", (sum(SIZES) + 10 * len(SIZES), 256), (240, 240, 240, 255))
    x = 0
    for img in images:
        sheet.paste(img, (x, 256 - img.size[1]), img)
        x += img.size[0] + 10
    print("res/windowsloader.ico and res/windowsloader.png written")
    return sheet


if __name__ == "__main__":
    main()
