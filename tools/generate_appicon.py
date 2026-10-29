#!/usr/bin/env python3
"""
tools/generate_appicon.py
Generates Apple iOS/iPadOS AppIcon.appiconset assets for Need for Speed: Carbon iOS recompilation.
Supports:
  1. Procedural generation of a sleek, high-contrast, Canyon / Carbon-fiber themed 1024x1024 icon.
  2. Resizing from a user-supplied cover image via --input <path>.
  3. Strict zero-alpha RGB export to pass Xcode actool / App Store validation.
"""

import argparse
import json
import math
import os
import sys
from PIL import Image, ImageDraw, ImageFont, ImageFilter

# iOS App Icon specifications: (idiom, size, scale, filename)
ICON_SPECS = [
    # Universal / App Store
    ("universal", "1024x1024", "1x", 1024, "AppIcon-1024.png"),
    # iPhone
    ("iphone", "20x20", "2x", 40, "AppIcon-20@2x.png"),
    ("iphone", "20x20", "3x", 60, "AppIcon-20@3x.png"),
    ("iphone", "29x29", "2x", 58, "AppIcon-29@2x.png"),
    ("iphone", "29x29", "3x", 87, "AppIcon-29@3x.png"),
    ("iphone", "40x40", "2x", 80, "AppIcon-40@2x.png"),
    ("iphone", "40x40", "3x", 120, "AppIcon-40@3x.png"),
    ("iphone", "60x60", "2x", 120, "AppIcon-60@2x.png"),
    ("iphone", "60x60", "3x", 180, "AppIcon-60@3x.png"),
    # iPad
    ("ipad", "20x20", "1x", 20, "AppIcon-20@1x.png"),
    ("ipad", "20x20", "2x", 40, "AppIcon-20@2x-ipad.png"),
    ("ipad", "29x29", "1x", 29, "AppIcon-29@1x.png"),
    ("ipad", "29x29", "2x", 58, "AppIcon-29@2x-ipad.png"),
    ("ipad", "40x40", "1x", 40, "AppIcon-40@1x.png"),
    ("ipad", "40x40", "2x", 80, "AppIcon-40@2x-ipad.png"),
    ("ipad", "76x76", "1x", 76, "AppIcon-76@1x.png"),
    ("ipad", "76x76", "2x", 152, "AppIcon-76@2x.png"),
    ("ipad", "83.5x83.5", "2x", 167, "AppIcon-83.5@2x.png"),
]


def create_carbon_base(size=1024):
    """Procedurally renders a stylized high-contrast Carbon Canyon themed 1024x1024 master icon."""
    img = Image.new("RGB", (size, size), (10, 11, 15))
    draw = ImageDraw.Draw(img)

    # 1. Carbon-fiber diagonal weave texture
    step = 8
    for y in range(0, size, step):
        for x in range(0, size, step):
            base_col = 14 + ((x // step + y // step) % 2) * 8
            # subtle diagonal grain
            for sub in range(step):
                px = (x + sub) % size
                py = (y + sub) % size
                img.putpixel((px, py), (base_col + 4, base_col + 5, base_col + 8))

    # 2. Canyon Dusk Radial & Atmospheric Lighting
    # Blazing Canyon Red / Amber glow from the bottom-right and top-left
    glow = Image.new("RGB", (size, size), (0, 0, 0))
    glow_draw = ImageDraw.Draw(glow)

    cx, cy = size * 0.5, size * 0.52
    for r in range(int(size * 0.75), 0, -8):
        factor = 1.0 - (r / (size * 0.75))
        # Canyon flame gradient: Crimson (#E51926) to Vivid Amber (#FF6A00)
        red = int(220 * (factor ** 1.8))
        green = int(45 * (factor ** 2.2))
        blue = int(15 * (factor ** 3.0))
        glow_draw.ellipse(
            [cx - r * 1.1, cy - r * 0.9, cx + r * 1.1, cy + r * 0.9],
            fill=(red, green, blue)
        )

    # Blur the atmospheric canyon glow
    glow = glow.filter(ImageFilter.GaussianBlur(radius=48))
    img = Image.blend(img, glow, alpha=0.68)
    draw = ImageDraw.Draw(img)

    # 3. Dynamic Canyon Slash / Angular Speed Geometry (Need for Speed signature red slash)
    slash_poly = [
        (int(size * 0.12), int(size * 0.82)),
        (int(size * 0.78), int(size * 0.18)),
        (int(size * 0.88), int(size * 0.24)),
        (int(size * 0.22), int(size * 0.88)),
    ]
    draw.polygon(slash_poly, fill=(240, 25, 35))

    accent_poly = [
        (int(size * 0.18), int(size * 0.85)),
        (int(size * 0.84), int(size * 0.27)),
        (int(size * 0.89), int(size * 0.31)),
        (int(size * 0.23), int(size * 0.89)),
    ]
    draw.polygon(accent_poly, fill=(255, 120, 20))

    # 4. Stylized "NFSC" Crest & Typography
    # Try to load a clean sans/display font or draw stylized bold vectors
    font_large = None
    font_sub = None
    for font_name in ["Arial Black", "Arial", "Trebuchet MS", "Impact", "DejaVuSans-Bold", "LiberationSans-Bold"]:
        try:
            font_large = ImageFont.truetype(font_name, int(size * 0.28))
            font_sub = ImageFont.truetype(font_name, int(size * 0.11))
            break
        except Exception:
            continue

    if font_large is None:
        font_large = ImageFont.load_default()
        font_sub = ImageFont.load_default()

    # Draw "CARBON" emblem
    text_c = "CARBON"
    bbox_c = font_sub.getbbox(text_c) if hasattr(font_sub, 'getbbox') else (0, 0, 400, 100)
    w_c = bbox_c[2] - bbox_c[0]
    h_c = bbox_c[3] - bbox_c[1]
    pos_c = (int((size - w_c) / 2), int(size * 0.63))

    # Shadow
    draw.text((pos_c[0] + 4, pos_c[1] + 4), text_c, fill=(0, 0, 0), font=font_sub)
    draw.text(pos_c, text_c, fill=(245, 245, 250), font=font_sub)

    # Draw "NFS" emblem in italicized bold stylized layout
    text_n = "NFS"
    bbox_n = font_large.getbbox(text_n) if hasattr(font_large, 'getbbox') else (0, 0, 500, 200)
    w_n = bbox_n[2] - bbox_n[0]
    pos_n = (int((size - w_n) / 2), int(size * 0.30))

    # Outer drop glow
    draw.text((pos_n[0] + 6, pos_n[1] + 6), text_n, fill=(10, 10, 15), font=font_large)
    draw.text((pos_n[0] + 2, pos_n[1] + 2), text_n, fill=(255, 60, 20), font=font_large)
    draw.text(pos_n, text_n, fill=(255, 255, 255), font=font_large)

    # 5. Canyon Road Silhouette / Badge Accent at the base
    road_poly = [
        (0, size),
        (int(size * 0.25), int(size * 0.91)),
        (int(size * 0.50), int(size * 0.94)),
        (int(size * 0.75), int(size * 0.89)),
        (size, size),
    ]
    draw.polygon(road_poly, fill=(18, 20, 26))

    # 6. Sleek Border & Vignette
    draw.rectangle([0, 0, size - 1, size - 1], outline=(50, 55, 68), width=3)
    draw.rectangle([4, 4, size - 5, size - 5], outline=(255, 80, 40), width=2)

    # Ensure strictly 24-bit RGB (zero alpha channel)
    if img.mode != "RGB":
        img = img.convert("RGB")

    return img


def generate_icon_catalog(input_image=None, output_dir=None):
    if output_dir is None:
        script_dir = os.path.dirname(os.path.abspath(__file__))
        output_dir = os.path.join(script_dir, "..", "ios", "resources", "Assets.xcassets", "AppIcon.appiconset")

    os.makedirs(output_dir, exist_ok=True)

    if input_image and os.path.exists(input_image):
        print(f"[AppIcon] Loading custom cover source: {input_image}")
        master = Image.open(input_image)
        if master.mode != "RGB":
            # Flatten with black background if transparent
            bg = Image.new("RGB", master.size, (10, 11, 15))
            if "A" in master.mode:
                bg.paste(master, mask=master.split()[-1])
            else:
                bg.paste(master)
            master = bg
        master = master.resize((1024, 1024), Image.Resampling.LANCZOS)
    else:
        print("[AppIcon] Generating procedural Carbon Canyon high-contrast master icon...")
        master = create_carbon_base(1024)

    # Generate each required size with strict zero alpha channel
    contents_images = []
    for idiom, size_str, scale_str, px, filename in ICON_SPECS:
        target_path = os.path.join(output_dir, filename)
        resized = master.resize((px, px), Image.Resampling.LANCZOS)
        # Ensure RGB mode strictly
        if resized.mode != "RGB":
            resized = resized.convert("RGB")
        resized.save(target_path, "PNG", optimize=True)
        print(f"  [OK] Saved {filename} ({px}x{px}, {idiom} {size_str} @ {scale_str})")

        contents_images.append({
            "size": size_str,
            "idiom": idiom,
            "filename": filename,
            "scale": scale_str
        })

    # Write Contents.json
    contents_json = {
        "images": contents_images,
        "info": {
            "version": 1,
            "author": "xcode"
        }
    }

    contents_path = os.path.join(output_dir, "Contents.json")
    with open(contents_path, "w", encoding="utf-8") as f:
        json.dump(contents_json, f, indent=2)

    # Also save the root Assets.xcassets/Contents.json if missing
    assets_dir = os.path.dirname(output_dir)
    root_contents = os.path.join(assets_dir, "Contents.json")
    if not os.path.exists(root_contents):
        with open(root_contents, "w", encoding="utf-8") as f:
            json.dump({"info": {"version": 1, "author": "xcode"}}, f, indent=2)

    # Save a primary AppIcon.png 1024x1024 directly for single-asset configs & LiveContainer
    main_1024 = os.path.join(output_dir, "AppIcon.png")
    master.save(main_1024, "PNG", optimize=True)
    print(f"[AppIcon] Complete! All icons written to: {output_dir}")


def main():
    parser = argparse.ArgumentParser(description="Need for Speed: Carbon iOS AppIcon Generator")
    parser.add_argument("--input", "-i", help="Path to input cover image (PNG/JPEG)", default=None)
    parser.add_argument("--output", "-o", help="Target AppIcon.appiconset directory", default=None)
    args = parser.parse_args()

    generate_icon_catalog(args.input, args.output)


if __name__ == "__main__":
    main()
