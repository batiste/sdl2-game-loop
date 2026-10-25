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

// a cell can hold a few boxes, e.g. two walls meeting in a corner
#define BOXES_PER_CELL 3

typedef struct House {
  int x, y, w, h;        // footprint in cells
  int doorX, doorY;      // walkable cell just outside the door
  int village;
  bool roof;             // has a roof hiding the interior
  int style;             // colours, see atlas.c
  SDL_FRect body;        // the walls' outer bounds, in world pixels
  bool chimney;
  float chimneyX, chimneyY;  // top-left of the chimney sprite, world pixels
} House;

typedef struct Village {
  char name[24];
  int centerX, centerY;  // plaza centre cell
  int radius;            // in cells
  int firstHouse;
  int numHouses;
  int styles[3];         // house styles used in this village
  int roofShape;         // the roof shape most houses have
} Village;

typedef struct World {
  Uint64 seed;
  int width, height;

  // ground: always drawn first; top: decorations and buildings, depth sorted
  Uint16 *ground;
  Uint16 *top;
  Uint16 *interior;  // what is under the roof, drawn when the player is inside
  Uint16 *furniture; // inside houses, shown with the interior
  Box *solid;        // BOXES_PER_CELL per cell
  Uint8 *kind;       // CellKind, for the minimap and path costs
  Sint16 *houseAt;   // house index per cell, -1 outside houses
  Uint8 *steps;      // per cell, bit d set when a walker can step to the
                     // neighbour in direction d (right, left, down, up)

  House *houses;
  int numHouses;
  Village *villages;
  int numVillages;

  float spawnX, spawnY;  // feet position of the player, in pixels
} World;

World *world_generate(Uint64 seed, int width, int height);

// Work out the steps once the world is built: it never changes afterwards.
bool world_build_steps(World *world);
void world_free(World *world);

static inline bool world_in_bounds(const World *w, int x, int y) {
  return x >= 0 && y >= 0 && x < w->width && y < w->height;
}

static inline Box *world_boxes(const World *w, int x, int y) {
  return &w->solid[(y * w->width + x) * BOXES_PER_CELL];
}

// Walking on a path (roads, plazas) is this much faster than elsewhere.
#define PATH_SPEED_BONUS 1.25f

// Speed multiplier for someone with their feet at (x, y), in world pixels.
static inline float world_ground_speed(const World *w, float x, float y) {
  int cx = (int)(x / TILE_SIZE), cy = (int)(y / TILE_SIZE);
  return world_in_bounds(w, cx, cy) && w->kind[cy * w->width + cx] == CELL_PATH ? PATH_SPEED_BONUS : 1.0f;
}

// Does the rectangle (in world pixels) overlap anything solid?
bool world_blocked(const World *world, float x0, float y0, float x1, float y1);

// Can a walker stand in the middle of this cell?
bool world_walkable(const World *world, int x, int y);

// Feet position of a walker standing in the middle of a cell.
static inline float world_cell_feet_x(int x) {
  return (float)(x * TILE_SIZE + TILE_SIZE / 2);
}
static inline float world_cell_feet_y(int y) {
  return (float)(y * TILE_SIZE + 34);
}

// Village whose area contains the pixel position, or -1.
int world_village_at(const World *world, float x, float y);

// House whose walls enclose the pixel position, or -1.
int world_house_at(const World *world, float x, float y);

// Path for walkers between two cells, preferring roads and going around
// walls. Returns the cell count or -1.
int world_find_path(const World *world, int sx, int sy, int gx, int gy, int *out, int maxOut);
// Walking cost from every cell to (gx, gy), for chasers. See path_distance_field().
bool world_distance_field(const World *world, int gx, int gy, float *out);

#endif
