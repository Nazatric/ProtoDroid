#!/usr/bin/env python3
"""Build Android app icons from the checked-in high-resolution Prototype artwork master.

The master is never modified. Cropping the thin outer margin slightly zooms the art for legacy/README/setup icons;
the adaptive foreground is centered inside its existing mask-safe layer.

Usage: tools/make_icon.py"""
import os

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.path.join(ROOT, "tools", "assets", "protodroid_icon_master.png")
SIZES = {"mdpi": 48, "hdpi": 72, "xhdpi": 96, "xxhdpi": 144, "xxxhdpi": 192}
CROP_MARGIN = 0.025
ADAPTIVE_FOREGROUND_SCALE = 0.70
BACKGROUND = (5, 5, 5, 255)

try:
    RESAMPLE = Image.Resampling.LANCZOS
except AttributeError:  # Pillow before 9.1
    RESAMPLE = Image.LANCZOS


def zoom_crop(image):
    """Crop the same proportional edge from all sides, then let the caller resize to its square output."""
    width, height = image.size
    inset_x = int(round(width * CROP_MARGIN))
    inset_y = int(round(height * CROP_MARGIN))
    if width - 2 * inset_x <= 0 or height - 2 * inset_y <= 0:
        raise ValueError("icon crop leaves no artwork")
    return image.crop((inset_x, inset_y, width - inset_x, height - inset_y))


def main():
    if not os.path.isfile(SOURCE):
        raise SystemExit(f"missing icon master: {SOURCE}")
    master = Image.open(SOURCE).convert("RGBA")
    if master.width != master.height:
        raise SystemExit("icon master must be square")
    cropped = zoom_crop(master)
    big = cropped.resize((512, 512), RESAMPLE)

    for name, px in SIZES.items():
        directory = os.path.join(ROOT, "android", "res", f"mipmap-{name}")
        os.makedirs(directory, exist_ok=True)
        big.resize((px, px), RESAMPLE).save(os.path.join(directory, "ic_launcher.png"), optimize=True)

    big.save(os.path.join(ROOT, "android", "icon-512.png"), optimize=True)
    big.resize((256, 256), RESAMPLE).save(
        os.path.join(ROOT, "android", "assets", "setup", "images", "protodroid_icon.png"), optimize=True)

    # Android's adaptive icon canvas is 108 dp. Keep the artwork centered in the current mask-safe foreground area;
    # the solid background owns the outer corners, so the transparent source does not draw a dark border over it.
    for name, px in SIZES.items():
        layer_px = px * 108 // 48
        directory = os.path.join(ROOT, "android", "res", f"mipmap-{name}")
        background = Image.new("RGBA", (layer_px, layer_px), BACKGROUND)
        background.save(os.path.join(directory, "ic_launcher_bg.png"), optimize=True)
        foreground = Image.new("RGBA", (layer_px, layer_px), (0, 0, 0, 0))
        inner = int(round(layer_px * ADAPTIVE_FOREGROUND_SCALE))
        artwork = cropped.resize((inner, inner), RESAMPLE)
        foreground.alpha_composite(artwork, ((layer_px - inner) // 2, (layer_px - inner) // 2))
        foreground.save(os.path.join(directory, "ic_launcher_fg.png"), optimize=True)

    xml = os.path.join(ROOT, "android", "res", "mipmap-anydpi-v26", "ic_launcher.xml")
    os.makedirs(os.path.dirname(xml), exist_ok=True)
    with open(xml, "w", encoding="utf-8") as f:
        f.write('<?xml version="1.0" encoding="utf-8"?>\n'
                '<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">\n'
                '    <background android:drawable="@mipmap/ic_launcher_bg" />\n'
                '    <foreground android:drawable="@mipmap/ic_launcher_fg" />\n'
                '</adaptive-icon>\n')
    print("cropped Prototype launcher icons written")


if __name__ == "__main__":
    main()
