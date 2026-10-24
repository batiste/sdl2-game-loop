/*
  Copyright (C) 2013 batiste.bieler@gmail.com

  Procedurally generated tile world: terrain, villages, houses and roads
 */

#ifndef GAME_WORLD_H
#define GAME_WORLD_H

#include <SDL3/SDL.h>

#include "tiles.h"

typedef enum CellKind {
  CELL_GRASS,
  CELL_FOREST,
  CELL_WATER,
  CELL_PATH,
  CELL_HOUSE,
} CellKind;

// Solid part of a tile in tile-local pixels, empty when x1 <= x0.
typedef struct Box {
  Uint8 x0, y0, x1, y1;
} Box;

typedef struct House {
  int x, y, w, h;        // footprint in cells
  int doorX, doorY;      // walkable cell in front of the door
  int village;
} House;

typedef struct Village {
  char name[24];
  int centerX, centerY;  // plaza centre cell
  int radius;            // in cells
  int firstHouse;
  int numHouses;
} Village;

typedef struct World {
  Uint64 seed;
  int width, height;

  // ground: always drawn first; top: decorations and buildings, depth sorted
  Uint16 *ground;
  Uint16 *top;
  Box *solid;
  Uint8 *kind;  // CellKind, for the minimap and path costs

  House *houses;
  int numHouses;
  Village *villages;
  int numVillages;

  float spawnX, spawnY;  // feet position of the player, in pixels
} World;

World *world_generate(Uint64 seed, int width, int height);
void world_free(World *world);

static inline bool world_in_bounds(const World *w, int x, int y) {
  return x >= 0 && y >= 0 && x < w->width && y < w->height;
}

// Does the rectangle (in world pixels) overlap anything solid?
bool world_blocked(const World *world, float x0, float y0, float x1, float y1);

// Can a villager stand on this cell?
bool world_walkable(const World *world, int x, int y);

// Village whose area contains the pixel position, or -1.
int world_village_at(const World *world, float x, float y);

// Path for walkers between two cells, preferring roads. Returns cell count or -1.
int world_find_path(const World *world, int sx, int sy, int gx, int gy, int *out, int maxOut);

#endif
