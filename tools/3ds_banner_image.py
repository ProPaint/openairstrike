#!/usr/bin/env python3
"""Draws apps/3ds/banner.png, the HOME menu banner of the 3DS CIA (256x128): the project's own
icon (apps/web/site/icons/icon-512.png) on its dark background, the name beside it. No game art.
Run once after changing the icon; tools/3ds_package.sh uses the committed PNG.
    python3 tools/3ds_banner_image.py
"""
import os

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
icon = Image.open(os.path.join(ROOT, "apps/web/site/icons/icon-512.png")).convert("RGBA")
# The icon's dark corner colour as the banner's background.
bg = icon.getpixel((4, 4))
out = Image.new("RGBA", (256, 128), bg)
# The helicopter disc and its tail, with a margin.
disc = icon.crop((136, 96, 376, 400)).resize((92, 116), Image.LANCZOS)
out.alpha_composite(disc, (10, 6))
draw = ImageDraw.Draw(out)
font = ImageFont.load_default(size=24)
small = ImageFont.load_default(size=12)
draw.text((112, 30), "Open", fill=(255, 176, 32, 255), font=font)
draw.text((112, 56), "AirStrike", fill=(255, 176, 32, 255), font=font)
draw.text((113, 90), "open source engine", fill=(220, 200, 170, 255), font=small)
out.convert("RGB").save(os.path.join(ROOT, "apps/3ds/banner.png"))
print("apps/3ds/banner.png")
