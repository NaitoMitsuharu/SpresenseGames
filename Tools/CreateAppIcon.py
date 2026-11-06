"""Generate the Spresense Games launcher icon.

One picture for the three games: a maze path (cyan) starts at the ball (white),
turns into the flight game's jet (orange) and ends in Light Box motes.

    python Tools/CreateAppIcon.py

Writes Build/Android/IconSource/SpresenseGamesIcon.png (1024 px) and every
Build/Android/res/drawable*/icon.png density.
"""
import math
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "Build/Android/IconSource/SpresenseGamesIcon.png"
DENSITIES = {"drawable": 48, "drawable-ldpi": 36, "drawable-mdpi": 48, "drawable-hdpi": 72,
             "drawable-xhdpi": 96, "drawable-xxhdpi": 144, "drawable-xxxhdpi": 192}

NAVY = (17, 24, 58)
NAVY_EDGE = (11, 16, 42)
CYAN = (123, 227, 255)
WHITE = (243, 247, 252)
ORANGE = (255, 143, 26)
MINT = (140, 255, 214)

K = 4  # supersampling factor
N = 1024 * K


def p(x, y):
    return (x * K, y * K)


def draw_icon() -> Image.Image:
    img = Image.new("RGB", (N, N), NAVY)
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, N, 72 * K], fill=NAVY_EDGE)
    d.rectangle([0, N - 72 * K, N, N], fill=NAVY_EDGE)

    # Light Box motes: soft glow first, then crisp cores.
    motes = [(760, 196, 26, MINT), (858, 300, 18, CYAN), (676, 120, 14, WHITE),
             (880, 168, 12, ORANGE), (612, 236, 10, MINT), (800, 392, 12, WHITE),
             (930, 410, 9, CYAN), (720, 330, 8, ORANGE)]
    glow = Image.new("RGB", (N, N), (0, 0, 0))
    g = ImageDraw.Draw(glow)
    for x, y, r, c in motes:
        g.ellipse([*p(x - r * 2.6, y - r * 2.6), *p(x + r * 2.6, y + r * 2.6)], fill=tuple(v // 2 for v in c))
    glow = glow.filter(ImageFilter.GaussianBlur(28 * K))
    img = ImageChops.add(img, glow)
    d = ImageDraw.Draw(img)
    for x, y, r, c in motes:
        d.ellipse([*p(x - r, y - r), *p(x + r, y + r)], fill=c)

    # Maze path: rounded corners, thick stroke.
    path = [(272, 736), (272, 504), (504, 504), (504, 300), (600, 300)]
    w = 80 * K
    for a, b in zip(path, path[1:]):
        d.line([p(*a), p(*b)], fill=CYAN, width=w)
    for q in path[1:-1]:
        d.ellipse([*p(q[0] - 40, q[1] - 40), *p(q[0] + 40, q[1] + 40)], fill=CYAN)

    # Jet at the end of the path, nose pointing up-right.
    cx, cy, s = 660, 268, 1.0
    ang = math.radians(-35)

    def jet(pts):
        out = []
        for x, y in pts:
            rx = x * math.cos(ang) - y * math.sin(ang)
            ry = x * math.sin(ang) + y * math.cos(ang)
            out.append(p(cx + rx * s, cy + ry * s))
        return out

    body = [(150, 0), (60, -26), (-20, -30), (-40, -150), (-90, -150), (-80, -30),
            (-120, -28), (-150, -80), (-180, -80), (-170, 0),
            (-180, 80), (-150, 80), (-120, 28), (-80, 30), (-90, 150), (-40, 150), (-20, 30), (60, 26)]
    d.polygon(jet(body), fill=ORANGE)

    # Ball at the start of the maze.
    d.ellipse([*p(272 - 120, 736 - 120), *p(272 + 120, 736 + 120)], fill=WHITE)

    return img.resize((1024, 1024), Image.LANCZOS)


def main():
    icon = draw_icon()
    SOURCE.parent.mkdir(parents=True, exist_ok=True)
    icon.save(SOURCE, optimize=True)
    for folder, size in DENSITIES.items():
        out = ROOT / "Build/Android/res" / folder / "icon.png"
        out.parent.mkdir(parents=True, exist_ok=True)
        icon.resize((size, size), Image.LANCZOS).save(out, optimize=True)
    print("wrote", SOURCE)


if __name__ == "__main__":
    main()
