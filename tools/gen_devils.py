#!/usr/bin/env python3
"""
Generate assets/devil.png, devil2.png and devil3.png: the underling, the lord
and the boss.

Drawn with 2x2 "art pixels" like the rest of the art, cropped to the sprite.
They face right (the game flips them) and stand on the bottom row: the game
anchors a demon by the middle of its feet.

Usage: python3 tools/gen_devils.py   (requires Pillow)
"""

from pathlib import Path

from PIL import Image

SCALE = 2  # screen pixels per art pixel
ASSETS = Path(__file__).resolve().parent.parent / "assets"

OUTLINE = (44, 12, 16, 255)
HORN = (246, 236, 206, 255)
HORN_DARK = (184, 164, 128, 255)
FANG = (255, 255, 255, 255)
MOUTH = (90, 10, 16, 255)

# Shared shapes. k outline, L/b/d light/base/dark body, h/H horn,
# y eye, e eye shine, w fang, m mouth.
UNDERLING = [
    "..h.....h",
    "..Hk...kH",
    "..kLLbbbk",
    "..kLbbbdk",
    "..kbybydk",
    "..kbbbbdk",
    "..kbwmwdk",
    "...kdddk.",
    "....kbk..",
    "...kLbdk.",
    "k..kLbdk.",
    "dk.kbbdk.",
    ".dkkbddk.",
    "...kkkkk.",
    "....k.k..",
    "...kk.kk.",
]

LORD = [
    "..h......h",
    "..Hh....hH",
    "..kHkkkkHk",
    "..kLLbbbbk",
    "..kLbbbbdk",
    "..kbybbydk",
    "..kbbbbbdk",
    "..kbwmmwdk",
    "...kddddk.",
    ".....kbk..",
    "....kLbdk.",
    "k..kLLbddk",
    "dk.kLbbddk",
    ".dkkbbbddk",
    "...kbbbdk.",
    "....kkkk..",
    "....k..k..",
    "...kk.kk..",
]

BOSS = [
    "..h..........h",
    "..Hh........hH",
    "...Hh......hH.",
    "...kHkkkkkkHk.",
    "...kLLLbbbbbk.",
    "...kLLbbbbbdk.",
    "...kLyybbyydk.",
    "...kbyebbyedk.",
    "...kbbbbbbbdk.",
    "...kbwmmmmwdk.",
    "...kbbwmmwbdk.",
    "....kddddddk..",
    "......kbbk....",
    "..kkkLLbbddkkk",
    "..kLkLLbbbdkdk",
    "..kbkLbbbbdkdk",
    "..kkkLbbbbdkkk",
    "k...kbbbbbdk..",
    "dk..kbbbbddk..",
    ".dkkkbbbbddk..",
    "....kkkkkkkk..",
    ".....kd..dk...",
    ".....kd..dk...",
    "....kkk..kkk..",
]

DEVILS = {
    # name: shape, light, base, dark, eye, eye shine
    "devil.png": (UNDERLING, (250, 92, 76), (222, 38, 40), (150, 18, 28), (255, 232, 96), None),
    "devil2.png": (LORD, (170, 240, 90), (96, 200, 30), (52, 128, 24), (255, 70, 50), None),
    "devil3.png": (BOSS, (200, 50, 52), (150, 16, 22), (96, 8, 18), (255, 230, 90), (255, 255, 230)),
}


def draw(shape, light, base, dark, eye, shine):
    colors = {
        "k": OUTLINE, "h": HORN, "H": HORN_DARK, "w": FANG, "m": MOUTH,
        "L": light + (255,), "b": base + (255,), "d": dark + (255,),
        "y": eye + (255,), "e": (shine or eye) + (255,),
    }
    width = max(len(row) for row in shape)
    image = Image.new("RGBA", (width, len(shape)), (0, 0, 0, 0))
    for y, row in enumerate(shape):
        for x, ch in enumerate(row):
            if ch != ".":
                image.putpixel((x, y), colors[ch])
    return image.resize((width * SCALE, len(shape) * SCALE), Image.NEAREST)


def main():
    for name, (shape, *palette) in DEVILS.items():
        image = draw(shape, *palette)
        image.save(ASSETS / name)
        print(f"{name}: {image.width}x{image.height}")


if __name__ == "__main__":
    main()
