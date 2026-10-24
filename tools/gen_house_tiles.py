#!/usr/bin/env python3
"""
Generate assets/roofs.png and assets/furniture.png in the style of tiles.png.

roofs.png: two roof shapes for the large houses, gabled (tile courses and a
ridge) and thatched (rounded, straw). Both are drawn in the red roof palette
of tiles.png and recoloured per house style at load time; the facade (wall
band and door) is copied from the hipped roof of tiles.png.

  8 x 6 tiles: cols 0-3 gable, cols 4-7 thatch
  columns: left end, middle with door, middle, right end
  rows:    top, ridge in the middle, ridge at the bottom, ridge at the top,
           bottom (eave and facade)
  row 5:   facade with a window for the gable (col 0) and the thatch (col 4),
           and the door copied from the cottage for the hipped roof (col 1)
           and for the interior (col 2)
  row 6:   glazed windows: hipped facade (col 0), interior back, left,
           right and front walls (cols 1-4). In tiles.png they are openings.

A house 3 rows deep uses rows 0, 1, 4; 4 rows deep uses rows 0, 2, 3, 4.
The large house in tiles.png only has a window in its facade: the door,
which has a landing in front of it, comes from the cottage.

furniture.png: 6 x 3 tiles on a transparent background, see FURNITURE.

Usage: python3 tools/gen_house_tiles.py   (requires Pillow)
"""

import random
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gen_nature_tiles import (  # noqa: E402
    ART, SCALE, Canvas, T, in_ellipse, fill_ellipse, OUTLINE, CREAM, YELLOW, RED, RED_DARK,
    LEAF, LEAF_LIGHT, LEAF_DARK, LEAF_DARKER, WOOD_LIGHT, WOOD, WOOD_DARK,
    STONE_LIGHT, STONE, STONE_DARK, PURPLE,
)

ROOT = Path(__file__).resolve().parent.parent

# red roof palette of tiles.png
R_LIGHT = (231, 111, 69, 255)
R_BASE = (219, 78, 29, 255)
R_SHADE = (202, 72, 26, 255)
R_DARK = (159, 57, 21, 255)
R_DARKER = (90, 28, 6, 255)
R_DEEP = (73, 19, 0, 255)
R_OUTLINE = (35, 35, 35, 255)

GRASS = {(112, 200, 64), (103, 185, 59)}

# glass stays blue whatever the house style, the frame takes the wall colour
GLASS = (150, 200, 228, 255)
GLASS_DARK = (104, 150, 190, 255)
GLASS_LIGHT = (226, 242, 250, 255)
FRAME = (212, 203, 107, 255)  # wall beige
WALL_LINE = (47, 47, 47, 255)
SHUTTER = R_DARK
SHUTTER_EDGE = R_DARKER


def load_tile(col, row):
    """A tile of tiles.png at art resolution, grass made transparent."""
    tiles = Image.open(ROOT / "assets" / "tiles.png").convert("RGBA")
    cell = tiles.crop((col * 48, row * 48, col * 48 + 48, row * 48 + 48))
    c = Canvas(ART, ART)
    for y in range(ART):
        for x in range(ART):
            p = cell.getpixel((x * 2, y * 2))
            if p[:3] not in GRASS:
                c.set(x, y, p)
    return c


def paste_region(dst, src, x0, y0, x1, y1):
    for y in range(y0, y1):
        for x in range(x0, x1):
            dst.set(x, y, src.get(x, y))


def glaze(tile, x0, y0, x1, y1, vertical=False):
    """Fill the opening (inclusive bounds) with framed glass."""
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            tile.set(x, y, GLASS_DARK if (y == y1 and not vertical) or (x == x1 and vertical) else GLASS)
    # bar across the middle and a glint in the top-left pane
    if vertical:
        for x in range(x0, x1 + 1):
            tile.set(x, (y0 + y1) // 2, FRAME)
    else:
        for y in range(y0, y1 + 1):
            tile.set((x0 + x1) // 2, y, FRAME)
    tile.set(x0, y0, GLASS_LIGHT)
    return tile


def facade_window(tile):
    """The original slot in the facade becomes a window with shutters."""
    glaze(tile, 8, 19, 16, 20)
    for y in range(18, 22):
        for x, col in ((5, SHUTTER_EDGE), (6, SHUTTER), (18, SHUTTER), (19, SHUTTER_EDGE)):
            tile.set(x, y, col)
    return tile


def room_windows():
    """Interior walls: the notches of the roofless house become windows."""
    back = load_tile(2, 1)
    for x in range(7, 16):
        back.set(x, 1, WALL_LINE)
        back.set(x, 2, FRAME)
        back.set(x, 5, FRAME)
    glaze(back, 7, 3, 15, 4)
    left = load_tile(1, 2)
    for y in range(8, 17):
        left.set(0, y, WALL_LINE)
    glaze(left, 1, 8, 2, 16, vertical=True)
    right = load_tile(4, 2)
    for y in range(8, 17):
        right.set(21, y, WALL_LINE)
    glaze(right, 19, 8, 20, 16, vertical=True)
    front = load_tile(2, 4)
    glaze(front, 7, 19, 15, 20)
    return [back, left, right, front]


def with_door(base, door_tile, y0):
    """Replace the middle of a facade with the cottage door."""
    out = base.crop(0, 0, ART, ART)
    paste_region(out, door_tile, 4, y0, 19, ART)
    return out


def load_facade():
    """Facades under the roof (art rows 17-23): left, door, middle, right,
    and the window of the original large house."""
    cols = [load_tile(1 + col, 9) for col in range(4)]
    out = []
    for tile in cols:
        c = Canvas(ART, ART)
        paste_region(c, tile, 0, 17, ART, ART)
        out.append(c)
    window = facade_window(out[1])
    out[1] = with_door(out[2], load_tile(14, 2), 17)
    return out, window


def course_noise(x, y, salt):
    # periodic over a tile width so middle pieces repeat seamlessly
    return random.Random((x % ART) * 7919 + y * 104729 + salt).random()


# ------------------------------------------------------------------ roofs

def gable(rows):
    """Gabled roof over a 4 x len(rows) house. rows: ridge position in art px."""
    w, h = 4 * ART, rows * ART
    eave = h - ART + 17  # last roof row, the facade starts below
    ridge = h // 2 if rows == 4 else ART + ART // 2
    c = Canvas(w, h)
    for y in range(eave):
        upper = y < ridge
        # courses of tiles, counted from the top eave or from the bottom one
        k = y - 1 if upper else eave - 1 - y
        course, row = k // 5, k % 5
        for x in range(w):
            off = 3 if course % 2 else 0
            seam = (x + off) % 6 == 0
            if row == 0:
                col = R_DARKER if upper else R_DARK
            elif seam:
                col = R_DARK if upper else R_DARKER
            elif row == 1:
                col = R_SHADE if upper else R_LIGHT
            else:
                col = R_DARK if upper and row == 4 else (R_SHADE if upper else R_BASE)
            c.set(x, y, col)
    # ridge cap
    for x in range(w):
        c.set(x, ridge - 1, R_DEEP)
        c.set(x, ridge, R_LIGHT if x % 3 else R_BASE)
        c.set(x, ridge + 1, R_DARKER)
    # bargeboards at the gable ends, outline around
    for y in range(eave):
        for x in (1, w - 2):
            c.set(x, y, R_DARKER)
        c.set(0, y, R_OUTLINE)
        c.set(w - 1, y, R_OUTLINE)
    for x in range(w):
        c.set(x, 0, R_OUTLINE)
        c.set(x, eave - 1, R_DEEP)
        c.set(x, eave, R_OUTLINE)
    return c, eave


def thatch(rows):
    w, h = 4 * ART, rows * ART
    eave = h - ART + 17
    ridge = h // 2 if rows == 4 else ART + ART // 2
    c = Canvas(w, h)
    r = 7  # rounded top corners
    for y in range(eave):
        for x in range(w):
            if (x < r or x >= w - r) and y < r:
                cx = r if x < r else w - r - 1
                if (x - cx) ** 2 + (y - r) ** 2 > r * r:
                    continue
            upper = y < ridge
            # layers of straw bundles, counted from the nearest eave
            k = (y - 2) if upper else (eave - 2 - y)
            layer, row = k // 6, k % 6
            off = 2 if layer % 2 else 0
            strand = course_noise(x + off, layer, 5) < 0.4
            # bundles end in a scalloped edge every 4 px
            scallop = (x + off) % 4 in (0, 3)
            if row == 0 or (row == 1 and scallop):
                col = R_DARK if upper else R_DARK
            elif row in (1, 2):
                col = R_DARK if strand else R_SHADE
            else:
                col = R_SHADE if strand else R_LIGHT
            if upper and col == R_LIGHT:
                col = R_BASE  # the far slope is in shade
            c.set(x, y, col)
    # thick rounded ridge bundle, tied every 8 px
    for x in range(r // 2, w - r // 2):
        for dy, col in ((-2, R_DARKER), (-1, R_BASE), (0, R_LIGHT), (1, R_BASE), (2, R_DARKER)):
            if c.get(x, ridge + dy)[3]:
                c.set(x, ridge + dy, R_DARK if (x - 4) % 8 == 0 and dy else col)
    c.outline(R_OUTLINE)
    for y in range(eave):  # the canvas border needs an outline too
        for x in (0, w - 1):
            if c.get(x, y)[3]:
                c.set(x, y, R_OUTLINE)
    for x in range(w):
        if c.get(x, 0)[3]:
            c.set(x, 0, R_OUTLINE)
    # ragged fringe hanging over the facade
    for x in range(w):
        c.set(x, eave - 1, R_DARK)
        c.set(x, eave, R_OUTLINE)
        if (x % ART) % 3 == 1:
            c.set(x, eave, R_DARK)
            c.set(x, eave + 1, R_OUTLINE)
    return c, eave


def bottom_piece(roof, eave, r, col, facade):
    """Last row of a roof over a facade."""
    base = Canvas(ART, ART)
    base.blit(facade, 0, 0)
    base.blit(roof.crop(col * ART, r * ART, ART, ART), 0, 0)
    # the facade shows below the eave
    for y in range(eave - r * ART + 2, ART):
        for x in range(ART):
            if facade.get(x, y)[3]:
                base.set(x, y, facade.get(x, y))
    return base


def slice_roof(draw, facade):
    """Return the 4 x 5 pieces of a roof shape."""
    pieces = {}
    for rows, mapping in ((3, {0: 0, 1: 1, 2: 4}), (4, {0: 0, 1: 2, 2: 3, 3: 4})):
        roof, eave = draw(rows)
        for r, piece_row in mapping.items():
            # columns: left end, middle with door, middle, right end; both
            # middles are the same roof, the door is in the facade
            for col in range(4):
                if piece_row == 4:
                    pieces[(col, piece_row)] = bottom_piece(roof, eave, r, col, facade[col])
                else:
                    pieces[(col, piece_row)] = roof.crop(col * ART, r * ART, ART, ART)
    return pieces


# -------------------------------------------------------------- furniture

def with_outline(c, color=OUTLINE):
    c.outline(color)
    return c


def shelf():
    c = Canvas(ART, ART)
    for y in range(1, 17):
        for x in range(4, 20):
            c.set(x, y, WOOD_DARK if x in (4, 19) or y in (1, 8, 16) else WOOD)
    spines = [RED, (72, 118, 218, 255), CREAM, LEAF, PURPLE, RED_DARK, YELLOW]
    rnd = random.Random(3)
    for top, bottom in ((2, 7), (9, 15)):
        x = 5
        while x < 19:
            col = rnd.choice(spines)
            tall = rnd.randint(top, top + 2)
            for y in range(tall, bottom + 1):
                c.set(x, y, col)
            x += 1 + (rnd.random() < 0.3)
    return with_outline(c)


def cupboard():
    c = Canvas(ART, ART)
    for y in range(2, 18):
        for x in range(4, 20):
            c.set(x, y, WOOD_DARK if x in (4, 11, 12, 19) or y in (2, 17) else WOOD_LIGHT if y < 4 else WOOD)
    for x, y in ((10, 10), (13, 10)):
        c.set(x, y, YELLOW)
    return with_outline(c)


def bed(blanket, blanket_dark):
    c = Canvas(ART, ART)
    for y in range(2, 22):
        for x in range(6, 18):
            c.set(x, y, WOOD_DARK if x in (6, 17) or y in (2, 21) else WOOD)
    for y in range(4, 8):
        for x in range(8, 16):
            c.set(x, y, CREAM)
    for y in range(8, 21):
        for x in range(7, 17):
            c.set(x, y, blanket_dark if x >= 15 or y == 8 else blanket)
    for x in range(7, 17):
        c.set(x, 10, CREAM)  # folded sheet
    return with_outline(c)


def table():
    c = Canvas(ART, ART)
    for x, y in ((6, 15), (6, 16), (17, 15), (17, 16)):
        c.set(x, y, WOOD_DARK)
    for y in range(7, 15):
        for x in range(5, 19):
            c.set(x, y, WOOD_DARK if y == 14 else WOOD_LIGHT if y < 9 else WOOD)
    # a jug and a plate
    fill_ellipse(c, 9.5, 10, 2.2, 1.3, CREAM)
    for y in range(7, 11):
        c.set(14, y, (72, 118, 218, 255))
        c.set(15, y, (52, 90, 180, 255))
    return with_outline(c)


def chair(facing_right):
    c = Canvas(ART, ART)
    x0 = 9
    for y in range(11, 17):
        for x in range(x0, x0 + 6):
            c.set(x, y, WOOD if y > 11 else WOOD_LIGHT)
    back = x0 if facing_right else x0 + 5
    for y in range(6, 17):
        c.set(back, y, WOOD_DARK)
    for x in (x0, x0 + 5):
        c.set(x, 17, WOOD_DARK)
        c.set(x, 18, WOOD_DARK)
    return with_outline(c)


def chest():
    c = Canvas(ART, ART)
    for y in range(9, 19):
        for x in range(5, 19):
            band = x in (8, 15)
            c.set(x, y, STONE_DARK if band else (WOOD_LIGHT if y < 12 else WOOD))
    for x in range(5, 19):
        c.set(x, 12, WOOD_DARK)
    c.set(11, 13, YELLOW)
    c.set(12, 13, YELLOW)
    return with_outline(c)


def barrel():
    c = Canvas(ART, ART)
    for y in range(6, 19):
        for x in range(7, 17):
            if in_ellipse(x, y, 12, 12.5, 5.2, 7):
                hoop = y in (8, 16)
                c.set(x, y, STONE_DARK if hoop else (WOOD_LIGHT if x < 10 else WOOD_DARK if x > 14 else WOOD))
    fill_ellipse(c, 12, 7, 4, 1.4, WOOD_LIGHT)
    return with_outline(c)


def plant():
    c = Canvas(ART, ART)
    for y in range(14, 19):
        for x in range(9, 15):
            c.set(x, y, RED_DARK if x >= 13 else (208, 110, 48, 255))
    for cx, cy, r in ((10, 10, 3), (14, 9, 3), (12, 7, 3), (12, 11, 3)):
        fill_ellipse(c, cx, cy, r, r, LEAF)
    for x, y in ((11, 6), (13, 8), (10, 9)):
        c.set(x, y, LEAF_LIGHT)
    for x, y in ((14, 11), (12, 12), (9, 11)):
        c.set(x, y, LEAF_DARK)
    return with_outline(c, LEAF_DARKER)


def rug():
    c = Canvas(ART, ART)
    for y in range(5, 19):
        for x in range(3, 21):
            edge = x in (3, 20) or y in (5, 18)
            inner = x in (5, 18) or y in (7, 16)
            c.set(x, y, RED_DARK if edge else CREAM if inner else RED if (x + y) % 4 else (208, 110, 48, 255))
    for x in range(3, 21, 2):  # tassels
        c.set(x, 4, CREAM)
        c.set(x, 19, CREAM)
    return c


def fireplace(frame):
    c = Canvas(ART, ART)
    for y in range(0, 18):
        for x in range(2, 22):
            stone = STONE_LIGHT if (x // 3 + y // 3) % 2 else STONE
            c.set(x, y, STONE_DARK if y in (5, 11) and x % 4 else stone)
    for x in range(1, 23):  # mantel
        c.set(x, 4, WOOD_DARK)
        c.set(x, 3, WOOD)
    for y in range(8, 18):
        for x in range(7, 17):
            c.set(x, y, (34, 34, 34, 255))
    # logs
    for x in range(8, 16):
        c.set(x, 16, WOOD_DARK)
        c.set(x, 15, WOOD)
    rnd = random.Random(frame * 17 + 5)
    flame = [(255, 222, 92, 255), (247, 150, 48, 255), (214, 72, 32, 255)]
    for x in range(8, 16):
        height = 3 + rnd.randint(0, 3) + (2 if 10 <= x <= 13 else 0)
        for k in range(height):
            y = 14 - k
            col = flame[0] if k < height - 3 else flame[1] if k < height - 1 else flame[2]
            c.set(x, y, col)
    return with_outline(c)


FURNITURE = [
    shelf, cupboard,
    lambda: bed(RED, RED_DARK), lambda: bed((72, 118, 218, 255), (52, 90, 180, 255)),
    lambda: bed((84, 170, 72, 255), (60, 130, 52, 255)), table,
    lambda: chair(True), lambda: chair(False), chest, barrel, plant, rug,
    lambda: fireplace(0), lambda: fireplace(1), lambda: fireplace(2),
]


def save(canvas, name):
    img = Image.new("RGBA", (canvas.w, canvas.h))
    img.putdata([c for row in canvas.px for c in row])
    img = img.resize((canvas.w * SCALE, canvas.h * SCALE), Image.NEAREST)
    out = ROOT / "assets" / name
    img.save(out, optimize=True)
    print(f"wrote {out} ({img.width}x{img.height})")


def main():
    facade, window = load_facade()
    roofs = Canvas(8 * ART, 7 * ART)
    for shape, draw in enumerate((gable, thatch)):
        for (col, row), tile in slice_roof(draw, facade).items():
            roofs.blit(tile, (shape * 4 + col) * ART, row * ART, transparent=False)
        roof, eave = draw(3)
        roofs.blit(bottom_piece(roof, eave, 2, 2, window), shape * 4 * ART, 5 * ART, transparent=False)
    # doors for the hipped roof and the interior of tiles.png
    roofs.blit(with_door(load_tile(3, 9), load_tile(14, 2), 17), 1 * ART, 5 * ART, transparent=False)
    roofs.blit(with_door(load_tile(3, 4), load_tile(11, 2), 16), 2 * ART, 5 * ART, transparent=False)
    roofs.blit(facade_window(load_tile(2, 9)), 0, 6 * ART, transparent=False)
    for i, tile in enumerate(room_windows()):
        roofs.blit(tile, (1 + i) * ART, 6 * ART, transparent=False)
    save(roofs, "roofs.png")

    furniture = Canvas(6 * ART, 3 * ART)
    for i, draw in enumerate(FURNITURE):
        furniture.blit(draw(), (i % 6) * ART, (i // 6) * ART, transparent=False)
    save(furniture, "furniture.png")


if __name__ == "__main__":
    main()
