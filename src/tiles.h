/*
  Copyright (C) 2013 batiste.bieler@gmail.com

  Tile ids for the three tilesets, all 48x48:

    ground.png   4 x 2    ids   1 ..   8
    tiles.png   21 x 10   ids   9 .. 218
    nature.png   6 x 9    ids 219 .. 272   (see tools/gen_nature_tiles.py)
    houses       generated at load time from tiles.png, recoloured per
                 house style (see atlas.c), ids 273 ..

  0 means "no tile".
 */

#ifndef GAME_TILES_H
#define GAME_TILES_H

#define TILE_SIZE 48

#define GID_GROUND(i) (1 + (i))
#define GID_TILES(col, row) (9 + (row) * 21 + (col))
#define GID_NATURE(col, row) (219 + (row) * 6 + (col))
#define GID_HOUSE_BASE (219 + 6 * 9)

// Pieces of a house style. Large houses are assembled from the corners,
// edges and middles of a 4-wide piece set; small houses are used as is.
#define NUM_HOUSE_STYLES 12
#define HOUSE_PIECES 46
#define PIECE_ROOF(c, r) ((r) * 4 + (c))              // large roof, 4 x 3
#define PIECE_SMALL_ROOF(c, r) (12 + (r) * 3 + (c))   // small roof, 3 x 3, row 2 is the door step
#define PIECE_ROOM(c, r) (21 + (r) * 4 + (c))         // large interior, 4 x 4
#define PIECE_SMALL_ROOM(c, r) (37 + (r) * 3 + (c))   // small interior, 3 x 3
#define GID_HOUSE(style, piece) (GID_HOUSE_BASE + (style) * HOUSE_PIECES + (piece))
#define GID_IS_HOUSE(gid) ((gid) >= GID_HOUSE_BASE && (gid) < GID_COUNT)
#define HOUSE_PIECE(gid) (((gid) - GID_HOUSE_BASE) % HOUSE_PIECES)
// door steps and interiors are seen from above: drawn flat
#define GID_IS_FLAT_HOUSE(gid) (GID_IS_HOUSE(gid) && HOUSE_PIECE(gid) >= PIECE_SMALL_ROOF(0, 2))

#define GID_COUNT (GID_HOUSE_BASE + NUM_HOUSE_STYLES * HOUSE_PIECES)

#define GID_PALM GID_GROUND(0)
#define GID_GRASS GID_GROUND(7)
// ground.png 1-3 have yellow flowers, 4-6 small specks
#define GID_GRASS_FLOWERS(i) GID_GROUND(1 + (i))
#define GID_GRASS_SPECKS(i) GID_GROUND(4 + (i))

// nature.png terrain sets: 3x3 blob in cols 0-2, inner corners in cols 3-4,
// plain centre variants in col 5
#define TERRAIN_WATER 0
#define TERRAIN_PATH 3
#define GID_TERRAIN(set, col, row) GID_NATURE(col, (set) + (row))

typedef enum Deco {
  DECO_BUSH,
  DECO_ROCK,
  DECO_STUMP,
  DECO_RED_FLOWERS,
  DECO_PURPLE_FLOWERS,
  DECO_TALL_GRASS,
  DECO_MUSHROOMS,
  DECO_REEDS,
  DECO_LILY,
  DECO_LILY_FLOWER,
  DECO_WATER_ROCK,
  DECO_SIGN,
  DECO_FENCE_LEFT,
  DECO_FENCE,
  DECO_FENCE_RIGHT,
  DECO_FENCE_VERTICAL,
  DECO_PEBBLES,
  DECO_CHIMNEY,
  DECO_COUNT
} Deco;

#define GID_DECO(d) GID_NATURE((d) % 6, 6 + (d) / 6)

#endif
