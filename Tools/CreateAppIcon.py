"""Generate the Spresense Games launcher icon.

A six-core circuit board and game controller, drawn from editable source.

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
    img = Image.new("RGB", (N, N), NAVY_EDGE)
    d = ImageDraw.Draw(img)
    for y in range(N):
        t=y/(N-1)
        d.line([(0,y),(N,y)],fill=(int(12+6*t),int(23+8*t),int(44+12*t)))

    # Six cores and pin headers evoke the Spresense board; large controls read at launcher size.
    glow = Image.new("RGB", (N, N), (0, 0, 0))
    g=ImageDraw.Draw(glow)
    g.rounded_rectangle([*p(150,190),*p(874,814)],radius=100*K,outline=(35,150,135),width=24*K)
    img=ImageChops.add(img,glow.filter(ImageFilter.GaussianBlur(36*K)))
    d=ImageDraw.Draw(img)
    d.rounded_rectangle([*p(150,190),*p(874,814)],radius=100*K,fill=(22,73,76),outline=(107,227,205),width=12*K)
    for x in range(230,821,94):
        d.rounded_rectangle([*p(x-15,172),*p(x+15,212)],radius=7*K,fill=(200,227,210))
        d.rounded_rectangle([*p(x-15,792),*p(x+15,832)],radius=7*K,fill=(200,227,210))
    for row in range(2):
        for col in range(3):
            x,y=335+col*132,266+row*94
            d.rounded_rectangle([*p(x,y),*p(x+90,y+62)],radius=12*K,fill=(16,35,50),outline=(98,182,174),width=5*K)
    for x in (210,814):
        d.line([p(x,305),p(x,680)],fill=(64,148,145),width=7*K)
        d.ellipse([*p(x-12,295),*p(x+12,319)],fill=MINT)

    # Soft controller silhouette, with separated grips and a clear D-pad / four-button pattern.
    d.rounded_rectangle([*p(225,448),*p(799,710)],radius=115*K,fill=WHITE)
    d.ellipse([*p(214,542),*p(420,758)],fill=WHITE)
    d.ellipse([*p(604,542),*p(810,758)],fill=WHITE)
    d.rounded_rectangle([*p(287,540),*p(405,578)],radius=9*K,fill=NAVY)
    d.rounded_rectangle([*p(327,500),*p(365,618)],radius=9*K,fill=NAVY)
    for x,y,c in [(682,515,(72,171,158)),(725,558,(102,146,225)),(682,601,(160,113,204)),(639,558,(72,171,158))]:
        d.ellipse([*p(x-22,y-22),*p(x+22,y+22)],fill=c)
    for x in (487,537):
        d.rounded_rectangle([*p(x-13,586),*p(x+13,598)],radius=6*K,fill=(115,144,159))
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
