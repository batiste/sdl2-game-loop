/*
  Copyright (C) 2013 batiste.bieler@gmail.com

  Tile ids for the three tilesets, all 48x48:

    ground.png   4 x 2    ids   1 ..   8
    tiles.png   21 x 10   ids   9 .. 218
    nature.png   6 x 9    ids 219 .. 272   (see tools/gen_nature_tiles.py)

  0 means "no tile".
 */

#ifndef GAME_TILES_H
#define GAME_TILES_H

#define TILE_SIZE 48

#define GID_GROUND(i) (1 + (i))
#define GID_TILES(col, row) (9 + (row) * 21 + (col))
#define GID_NATURE(col, row) (219 + (row) * 6 + (col))
#define GID_COUNT (219 + 6 * 9)

#define GID_PALM GID_GROUND(0)
#define GID_GRASS GID_GROUND(7)
// ground.png 1-3 have yellow flowers, 4-6 small specks
#define GID_GRASS_FLOWERS(i) GID_GROUND(1 + (i))
#define GID_GRASS_SPECKS(i) GID_GROUND(4 + (i))

// door steps of the small houses, flat tiles in front of the wall
#define GID_IS_DOOR_STEP(gid) ((gid) >= GID_TILES(10, 3) && (gid) <= GID_TILES(15, 3))

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
  DECO_COUNT
} Deco;

#define GID_DECO(d) GID_NATURE((d) % 6, 6 + (d) / 6)

#endif
