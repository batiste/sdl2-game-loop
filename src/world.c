/*
  Copyright (C) 2013 batiste.bieler@gmail.com

  World queries. Generation lives in worldgen.c.
 */

#include "world.h"

#include "pathfind.h"

void world_free(World *world) {
  if (!world) {
    return;
  }
  SDL_free(world->ground);
  SDL_free(world->top);
  SDL_free(world->solid);
  SDL_free(world->kind);
  SDL_free(world->houses);
  SDL_free(world->villages);
  SDL_free(world);
}

bool world_blocked(const World *world, float x0, float y0, float x1, float y1) {
  float maxX = (float)(world->width * TILE_SIZE), maxY = (float)(world->height * TILE_SIZE);
  if (x0 < 0 || y0 < 0 || x1 > maxX || y1 > maxY) {
    return true;
  }
  int cx0 = (int)(x0 / TILE_SIZE), cy0 = (int)(y0 / TILE_SIZE);
  int cx1 = (int)((x1 - 0.001f) / TILE_SIZE), cy1 = (int)((y1 - 0.001f) / TILE_SIZE);
  for (int cy = cy0; cy <= cy1; cy++) {
    for (int cx = cx0; cx <= cx1; cx++) {
      Box b = world->solid[cy * world->width + cx];
      if (b.x1 <= b.x0) {
        continue;
      }
      float bx0 = (float)(cx * TILE_SIZE + b.x0), by0 = (float)(cy * TILE_SIZE + b.y0);
      float bx1 = (float)(cx * TILE_SIZE + b.x1), by1 = (float)(cy * TILE_SIZE + b.y1);
      if (x0 < bx1 && x1 > bx0 && y0 < by1 && y1 > by0) {
        return true;
      }
    }
  }
  return false;
}

bool world_walkable(const World *world, int x, int y) {
  if (!world_in_bounds(world, x, y)) {
    return false;
  }
  Box b = world->solid[y * world->width + x];
  return b.x1 <= b.x0;
}

int world_village_at(const World *world, float x, float y) {
  for (int i = 0; i < world->numVillages; i++) {
    const Village *v = &world->villages[i];
    float dx = x / TILE_SIZE - ((float)v->centerX + 0.5f);
    float dy = y / TILE_SIZE - ((float)v->centerY + 0.5f);
    if (dx * dx + dy * dy <= (float)(v->radius * v->radius)) {
      return i;
    }
  }
  return -1;
}

static float walker_cost(void *ctx, int x, int y) {
  const World *world = ctx;
  if (!world_walkable(world, x, y)) {
    return -1;
  }
  return world->kind[y * world->width + x] == CELL_PATH ? 1.0f : 2.5f;
}

int world_find_path(const World *world, int sx, int sy, int gx, int gy, int *out, int maxOut) {
  PathQuery q = {
      .width = world->width, .height = world->height,
      .startX = sx, .startY = sy, .goalX = gx, .goalY = gy,
      .cost = walker_cost, .ctx = (void *)world,
      .minCost = 1.0f, .turnCost = 0.2f,
  };
  return path_find(&q, out, maxOut);
}
