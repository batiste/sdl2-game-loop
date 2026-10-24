#!/usr/bin/env python3
"""
Generate assets/nature.png: water and path terrain sets plus decorations,
in the style of ground.png / tiles.png.

The existing art is drawn with 2x2 "art pixels" on 48x48 tiles, so everything
here is drawn on a 24x24 grid per tile and scaled up 2x without filtering.

Layout (6 columns x 9 rows, 48px tiles):

  rows 0-2  water   3x3 blob (cols 0-2), inner corners (cols 3-4), extras (col 5)
  rows 3-5  path    same layout as water
  rows 6-8  decorations on a transparent background (the last one is a
            chimney, drawn over roofs)

Inner corners: col 3 row 0 / col 4 row 0 / col 3 row 1 / col 4 row 1 have
grass in the bottom-right / bottom-left / top-right / top-left corner.

Usage: python3 tools/gen_nature_tiles.py   (requires Pillow)
"""

import math
import random
from pathlib import Path

from PIL import Image

ART = 24  # art pixels per tile side
SCALE = 2  # screen pixels per art pixel
COLS, ROWS = 6, 9
EDGE = 6  # inset of terrain edges from the outer tile border, in art pixels
RADIUS = 6  # rounding of terrain corners

T = (0, 0, 0, 0)

# Palette, mostly taken from ground.png and tiles.png
GRASS = (112, 200, 64, 255)
GRID = (103, 185, 59, 255)
SHADOW = (74, 132, 42, 255)  # palm tree shadow
FLOWER_OUTLINE = (73, 123, 46, 255)
YELLOW = (216, 255, 0, 255)
LEAF_LIGHT = (170, 198, 0, 255)
LEAF = (142, 170, 0, 255)
LEAF_DARK = (113, 142, 0, 255)
LEAF_DARKER = (85, 113, 0, 255)
OUTLINE = (58, 41, 41, 255)  # house outlines
RED = (219, 78, 29, 255)
RED_DARK = (159, 57, 21, 255)
CREAM = (246, 239, 158, 255)
SAND = (212, 203, 107, 255)
SAND_DARK = (192, 184, 96, 255)
SAND_EDGE = (138, 133, 71, 255)
WOOD_LIGHT = (208, 110, 48, 255)
WOOD = (172, 77, 17, 255)
WOOD_DARK = (90, 28, 6, 255)
PURPLE = (159, 117, 210, 255)
PURPLE_DARK = (112, 80, 160, 255)

WATER_OUTLINE = (34, 70, 112, 255)
WATER_DEEP = (52, 118, 196, 255)
WATER = (72, 152, 224, 255)
WATER_LIGHT = (132, 196, 240, 255)
FOAM = (214, 240, 255, 255)

STONE_LIGHT = (196, 196, 186, 255)
STONE = (156, 156, 146, 255)
STONE_DARK = (112, 112, 106, 255)


class Canvas:
    def __init__(self, w, h, fill=T):
        self.w, self.h = w, h
        self.px = [[fill] * w for _ in range(h)]

    def get(self, x, y, default=T):
        if 0 <= x < self.w and 0 <= y < self.h:
            return self.px[y][x]
        return default

    def set(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[y][x] = c

    def blit(self, other, ox, oy, transparent=True):
        for y in range(other.h):
            for x in range(other.w):
                c = other.px[y][x]
                if not transparent or c[3]:
                    self.set(ox + x, oy + y, c)

    def crop(self, x0, y0, w, h):
        out = Canvas(w, h)
        for y in range(h):
            for x in range(w):
                out.px[y][x] = self.get(x0 + x, y0 + y)
        return out

    def outline(self, color):
        """Surround every opaque pixel with `color` (4-neighbourhood)."""
        add = []
        for y in range(self.h):
            for x in range(self.w):
                if self.px[y][x][3]:
                    continue
                if any(self.get(x + dx, y + dy)[3] for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                    add.append((x, y))
        for x, y in add:
            self.set(x, y, color)


def in_ellipse(x, y, cx, cy, rx, ry):
    return ((x + 0.5 - cx) / rx) ** 2 + ((y + 0.5 - cy) / ry) ** 2 <= 1


def fill_ellipse(c, cx, cy, rx, ry, color):
    for y in range(c.h):
        for x in range(c.w):
            if in_ellipse(x, y, cx, cy, rx, ry):
                c.set(x, y, color)


def in_round_rect(x, y, x0, y0, x1, y1, r):
    """Is art pixel (x, y) inside the rounded rectangle [x0, x1) x [y0, y1)?"""
    px, py = x + 0.5, y + 0.5
    if not (x0 <= px < x1 and y0 <= py < y1):
        return False
    cx = min(max(px, x0 + r), x1 - r)
    cy = min(max(py, y0 + r), y1 - r)
    return (px - cx) ** 2 + (py - cy) ** 2 <= r * r


def grass_tile(w=ART, h=ART, ox=0, oy=0):
    """Plain grass with the 1 art pixel grid line on the right and bottom."""
    c = Canvas(w, h, GRASS)
    for y in range(h):
        for x in range(w):
            if (ox + x) % ART == ART - 1 or (oy + y) % ART == ART - 1:
                c.set(x, y, GRID)
    return c


# ---------------------------------------------------------------- terrain

def terrain_sheet(inside, paint):
    """Render a 3x3 tile area where inside(x, y) tells terrain pixels,
    with an outline on the terrain side of the border."""
    size = ART * 3
    c = grass_tile(size, size)
    mask = [[inside(x, y) for x in range(size)] for y in range(size)]

    def m(x, y):
        # outside the sheet, continue whatever is on the border
        x = min(max(x, 0), size - 1)
        y = min(max(y, 0), size - 1)
        return mask[y][x]

    for y in range(size):
        for x in range(size):
            if not mask[y][x]:
                continue
            edge = not all(m(x + dx, y + dy) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))
            near = not all(m(x + dx, y + dy) for dx in (-1, 0, 1) for dy in (-1, 0, 1))
            c.set(x, y, paint(x, y, edge, near, m))
    return c


def blob(x, y):
    return in_round_rect(x, y, EDGE, EDGE, ART * 3 - EDGE, ART * 3 - EDGE, RADIUS)


def hole(x, y):
    # grass hole whose borders line up with the edges of the blob tiles
    inner = ART - EDGE
    return not in_round_rect(x, y, inner, inner, ART * 3 - inner, ART * 3 - inner, RADIUS)


def hashed(x, y, salt):
    return random.Random(x * 7919 + y * 104729 + salt).random()


def water_paint(x, y, edge, near, m):
    if edge:
        return WATER_OUTLINE
    if not m(x, y - 1) or not m(x, y - 2) and not m(x - 1, y - 1) and not m(x + 1, y - 1):
        return WATER_DEEP  # shadow cast by the top bank
    if near:
        return WATER_LIGHT
    return WATER


def path_paint(x, y, edge, near, m):
    if edge:
        return SAND_EDGE
    if not m(x, y + 1) or not m(x, y + 2):
        return SAND_DARK  # slight lip at the bottom border
    if hashed(x, y, 1) < 0.045:
        return SAND_DARK
    if hashed(x, y, 2) < 0.02:
        return CREAM
    return SAND


def add_waves(tile, seed, count=2):
    """Little wavelets on water, kept away from the banks."""
    rnd = random.Random(seed)
    placed = 0
    for _ in range(50):
        if placed == count:
            break
        x, y = rnd.randint(2, ART - 6), rnd.randint(3, ART - 4)
        area = [(x + dx, y + dy) for dx in range(-1, 5) for dy in range(-2, 2)]
        if all(tile.get(px, py) == WATER for px, py in area):
            for dx in range(3):
                tile.set(x + dx + 1, y, WATER_LIGHT)
            tile.set(x, y - 1, WATER_LIGHT)
            tile.set(x + 4, y - 1, WATER_LIGHT)
            placed += 1


def terrain_tiles(paint, waves):
    outer = terrain_sheet(blob, paint)
    inner = terrain_sheet(hole, paint)
    tiles = {}
    for ty in range(3):
        for tx in range(3):
            tiles[(tx, ty)] = outer.crop(tx * ART, ty * ART, ART, ART)
    # inner corners come from the corners of the hole sheet
    tiles[(3, 0)] = inner.crop(0, 0, ART, ART)  # grass bottom-right
    tiles[(4, 0)] = inner.crop(2 * ART, 0, ART, ART)  # grass bottom-left
    tiles[(3, 1)] = inner.crop(0, 2 * ART, ART, ART)  # grass top-right
    tiles[(4, 1)] = inner.crop(2 * ART, 2 * ART, ART, ART)  # grass top-left
    plain = outer.crop(ART, ART, ART, ART)
    tiles[(5, 0)] = plain.crop(0, 0, ART, ART)
    tiles[(5, 1)] = plain.crop(0, 0, ART, ART)
    if waves:
        for key, tile in tiles.items():
            if key != (5, 1):
                add_waves(tile, key[0] * 31 + key[1], 3 if key == (5, 0) else 2)
    else:
        # pebbles on the alternative path tile
        alt = tiles[(5, 0)]
        for px, py in ((5, 6), (15, 4), (10, 14), (18, 17), (4, 18)):
            alt.set(px, py, STONE)
            alt.set(px + 1, py, STONE_LIGHT)
            alt.set(px, py + 1, STONE_DARK)
            alt.set(px + 1, py + 1, STONE)
    return tiles


# ------------------------------------------------------------ decorations

def shadow(cx, cy, rx, ry):
    c = Canvas(ART, ART)
    fill_ellipse(c, cx, cy, rx, ry, SHADOW)
    return c


def shade_round(c, cx, cy, r, light, mid, dark):
    """Fill a sphere-ish circle lit from the top-left."""
    for y in range(c.h):
        for x in range(c.w):
            if in_ellipse(x, y, cx, cy, r, r):
                d = (x + 0.5 - (cx - r * 0.35)) ** 2 + (y + 0.5 - (cy - r * 0.4)) ** 2
                if d < (r * 0.45) ** 2:
                    col = light
                elif (x + 0.5 - cx) + (y + 0.5 - cy) > r * 0.55:
                    col = dark
                else:
                    col = mid
                c.set(x, y, col)


def with_shadow(sprite, sh, outline):
    sprite.outline(outline)
    sh.blit(sprite, 0, 0)
    return sh


def bush():
    s = Canvas(ART, ART)
    for cx, cy, r in ((8, 14, 5.5), (16, 14, 5.5), (12, 9.5, 6)):
        shade_round(s, cx, cy, r, LEAF_LIGHT, LEAF, LEAF_DARK)
    for x, y in ((9, 12), (15, 9), (16, 15), (7, 16)):
        s.set(x, y, RED)
    return with_shadow(s, shadow(12.5, 19, 9.5, 3.5), LEAF_DARKER)


def rock():
    s = Canvas(ART, ART)
    for y in range(ART):
        for x in range(ART):
            if in_ellipse(x, y, 12, 13, 8, 6.5) and y < 18:
                if in_ellipse(x, y, 10, 10.5, 4.5, 2.5):
                    col = STONE_LIGHT
                elif y >= 15 or x >= 17:
                    col = STONE_DARK
                else:
                    col = STONE
                s.set(x, y, col)
    s.set(14, 14, STONE_DARK)
    s.set(15, 13, STONE_DARK)
    s.set(15, 12, STONE_DARK)
    return with_shadow(s, shadow(12.5, 18.5, 9.5, 3), OUTLINE)


def stump():
    s = Canvas(ART, ART)
    for y in range(9, 18):
        for x in range(6, 18):
            col = WOOD_DARK if x >= 15 else WOOD
            s.set(x, y, col)
    # roots
    for x, y in ((5, 16), (5, 17), (18, 17), (11, 18), (12, 18)):
        s.set(x, y, WOOD)
    fill_ellipse(s, 12, 9, 6, 3, WOOD_LIGHT)
    # growth rings
    for x, y in ((10, 9), (11, 8), (12, 8), (13, 9), (12, 10), (11, 10)):
        s.set(x, y, WOOD)
    s.set(9, 11, WOOD_DARK)
    s.set(9, 13, WOOD_DARK)
    return with_shadow(s, shadow(12.5, 18, 9, 3), OUTLINE)


def flower(s, x, y, petal):
    s.set(x, y, YELLOW)
    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        s.set(x + dx, y + dy, petal)


def flowers(petal, spots):
    s = Canvas(ART, ART)
    for x, y in spots:
        s.set(x, y + 2, LEAF_DARK)
        s.set(x, y + 3, LEAF_DARK)
        s.set(x + 1, y + 3, LEAF)
        flower(s, x, y, petal)
    s.outline(FLOWER_OUTLINE)
    return s


def tall_grass():
    s = Canvas(ART, ART)
    blades = ((5, 10, LEAF), (7, 6, LEAF_LIGHT), (9, 9, LEAF_DARK), (11, 4, LEAF),
              (13, 8, LEAF_LIGHT), (15, 5, LEAF_DARK), (17, 9, LEAF), (19, 11, LEAF_LIGHT))
    for x, top, col in blades:
        for y in range(top, 19):
            # blades lean a little to the right near the top
            s.set(x + (1 if y < top + 3 and x % 4 == 3 else 0), y, col)
    return with_shadow(s, shadow(12.5, 19, 9, 2.5), FLOWER_OUTLINE)


def mushrooms():
    s = Canvas(ART, ART)
    for cx, cy, r, stem in ((9, 11, 5, 5), (16.5, 14, 3.5, 3)):
        top = int(cy)
        for y in range(top, top + stem):
            for x in range(int(cx) - 1, int(cx) + 1):
                s.set(x, y, CREAM)
        for y in range(s.h):
            for x in range(s.w):
                if in_ellipse(x, y, cx, cy, r, r * 0.8) and y + 0.5 <= cy:
                    s.set(x, y, RED_DARK if x + 0.5 > cx + r * 0.5 else RED)
        s.set(int(cx) - 2, int(cy - r * 0.45), CREAM)
        s.set(int(cx) + 1, int(cy - r * 0.6), CREAM)
    return with_shadow(s, shadow(12, 18, 8, 2.5), OUTLINE)


def reeds():
    s = Canvas(ART, ART)
    for x, top in ((7, 5), (11, 3), (15, 6), (18, 9)):
        for y in range(top, 20):
            s.set(x, y, LEAF if x != 15 else LEAF_LIGHT)
        for y in range(top + 1, top + 5):  # cattail head
            s.set(x, y, WOOD)
            s.set(x + 1, y, WOOD_DARK)
    for x, y in ((5, 15), (6, 14), (9, 13), (13, 14), (14, 12), (17, 15), (20, 13)):
        s.set(x, y, LEAF_DARK)
    s.outline(LEAF_DARKER)
    return s


def lily(with_flower):
    s = Canvas(ART, ART)
    for y in range(ART):
        for x in range(ART):
            if in_ellipse(x, y, 12, 12, 7, 5):
                # wedge cut out of the pad
                if x >= 12 and abs(y + 0.5 - 12) < (x + 0.5 - 12) * 0.35:
                    continue
                s.set(x, y, LEAF_DARK if y >= 14 else LEAF)
    s.set(9, 10, LEAF_LIGHT)
    s.set(10, 10, LEAF_LIGHT)
    if with_flower:
        for x, y in ((9, 10), (8, 11), (10, 11), (9, 12), (9, 9)):
            s.set(x, y, (240, 150, 190, 255))
        s.set(9, 11, YELLOW)
    s.outline(FLOWER_OUTLINE)
    return s


def water_rock():
    s = Canvas(ART, ART)
    for x in range(5, 20):  # foam ring at the waterline
        s.set(x, 16, FOAM if x % 3 else WATER_LIGHT)
    fill_ellipse(s, 12.5, 15, 7.5, 2, FOAM)
    for y in range(7, 16):
        for x in range(7, 18):
            if in_ellipse(x, y, 12.5, 12, 5.5, 5):
                col = STONE_LIGHT if in_ellipse(x, y, 11, 10, 2.5, 1.8) else (
                    STONE_DARK if x >= 15 or y >= 14 else STONE)
                s.set(x, y, col)
    stone = Canvas(ART, ART)
    for y in range(ART):
        for x in range(ART):
            if s.get(x, y) in (STONE, STONE_LIGHT, STONE_DARK):
                stone.set(x, y, s.get(x, y))
    stone.outline(OUTLINE)
    s.blit(stone, 0, 0)
    return s


def sign():
    s = Canvas(ART, ART)
    for y in range(12, 20):
        s.set(11, y, WOOD)
        s.set(12, y, WOOD_DARK)
    for y in range(5, 12):
        for x in range(5, 19):
            s.set(x, y, WOOD_DARK if y == 11 else WOOD_LIGHT)
    for x0, x1, y in ((7, 12, 7), (7, 16, 9)):  # "text"
        for x in range(x0, x1):
            s.set(x, y, WOOD)
    return with_shadow(s, shadow(12, 19.5, 5, 1.5), OUTLINE)


def fence(left, right, post=True):
    s = Canvas(ART, ART)
    x0 = 0 if left else 11
    x1 = ART if right else 13
    for x in range(x0, x1):
        for y, col in ((9, WOOD_LIGHT), (10, WOOD), (14, WOOD_LIGHT), (15, WOOD)):
            s.set(x, y, col)
    if post:
        for y in range(6, 19):
            s.set(11, y, WOOD_LIGHT)
            s.set(12, y, WOOD)
    s.outline(OUTLINE)
    # rails run into the neighbouring tiles; outlines past the border are clipped
    sh = Canvas(ART, ART)
    for x in range(x0, x1):
        sh.set(x, 17, SHADOW)
    sh.set(11, 19, SHADOW)
    sh.set(12, 19, SHADOW)
    sh.blit(s, 0, 0)
    return sh


def fence_vertical():
    s = Canvas(ART, ART)
    for y in range(ART):
        s.set(11, y, WOOD_LIGHT)
        s.set(12, y, WOOD)
    for y in range(8, 18):
        for x in (10, 11, 12, 13):
            s.set(x, y, WOOD_LIGHT if x < 12 else WOOD)
    s.outline(OUTLINE)
    return s


def pebbles():
    s = Canvas(ART, ART)
    for cx, cy, r in ((7, 9, 2.2), (15, 7, 1.8), (12, 15, 2.6), (18, 16, 1.6), (6, 18, 1.5)):
        for y in range(ART):
            for x in range(ART):
                if in_ellipse(x, y, cx, cy, r, r * 0.8):
                    s.set(x, y, STONE_LIGHT if y + 0.5 < cy else STONE)
    s.outline(STONE_DARK)
    return s


def chimney():
    """Brick chimney, drawn over a roof tile."""
    s = Canvas(ART, ART)
    for y in range(7, 19):
        for x in range(9, 16):
            mortar = y % 3 == 0 or (x + (y // 3) * 2) % 4 == 0
            col = WOOD_DARK if mortar else (WOOD if x >= 14 else WOOD_LIGHT)
            s.set(x, y, col)
    for x in range(8, 17):  # stone cap
        s.set(x, 5, STONE_LIGHT)
        s.set(x, 6, STONE if x < 15 else STONE_DARK)
    for x in range(10, 15):
        s.set(x, 5, (34, 34, 34, 255))  # flue
    s.outline(OUTLINE)
    return s


def main():
    sheet = Canvas(COLS * ART, ROWS * ART)
    for (tx, ty), tile in terrain_tiles(water_paint, waves=True).items():
        sheet.blit(tile, tx * ART, ty * ART, transparent=False)
    for (tx, ty), tile in terrain_tiles(path_paint, waves=False).items():
        sheet.blit(tile, tx * ART, (ty + 3) * ART, transparent=False)

    decorations = [
        bush(), rock(), stump(),
        flowers(RED, ((7, 8), (15, 6), (11, 14), (17, 15), (5, 16))),
        flowers(PURPLE, ((6, 6), (13, 9), (18, 7), (9, 15), (16, 16))),
        tall_grass(),
        mushrooms(), reeds(), lily(False), lily(True), water_rock(), sign(),
        fence(False, True), fence(True, True), fence(True, False), fence_vertical(), pebbles(),
        chimney(),
    ]
    for i, tile in enumerate(decorations):
        sheet.blit(tile, (i % COLS) * ART, (6 + i // COLS) * ART, transparent=False)

    img = Image.new("RGBA", (sheet.w, sheet.h))
    img.putdata([c for row in sheet.px for c in row])
    img = img.resize((sheet.w * SCALE, sheet.h * SCALE), Image.NEAREST)
    out = Path(__file__).resolve().parent.parent / "assets" / "nature.png"
    img.save(out, optimize=True)
    print(f"wrote {out} ({img.width}x{img.height})")


if __name__ == "__main__":
    main()
