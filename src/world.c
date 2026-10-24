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
  SDL_free(world->interior);
  SDL_free(world->furniture);
  SDL_free(world->solid);
  SDL_free(world->kind);
  SDL_free(world->houseAt);
  SDL_free(world->steps);
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
      const Box *boxes = world_boxes(world, cx, cy);
      for (int k = 0; k < BOXES_PER_CELL; k++) {
        Box b = boxes[k];
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
  }
  return false;
}

// walkers are treated as a small box around their feet
static bool walker_blocked(const World *world, float x, float y) {
  return world_blocked(world, x - 6, y - 4, x + 6, y + 2);
}

bool world_walkable(const World *world, int x, int y) {
  return world_in_bounds(world, x, y) && !walker_blocked(world, world_cell_feet_x(x), world_cell_feet_y(y));
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

int world_house_at(const World *world, float x, float y) {
  int cx = (int)SDL_floorf(x / TILE_SIZE), cy = (int)SDL_floorf(y / TILE_SIZE);
  if (!world_in_bounds(world, cx, cy)) {
    return -1;
  }
  int h = world->houseAt[cy * world->width + cx];
  if (h < 0) {
    return -1;
  }
  const SDL_FRect *b = &world->houses[h].body;
  return x >= b->x && y >= b->y && x < b->x + b->w && y < b->y + b->h ? h : -1;
}

static const int STEP_DX[4] = {1, -1, 0, 0}, STEP_DY[4] = {0, 0, 1, -1};

// The slow check: the target cell is free and the walk between the two cell
// centres doesn't cross a wall.
static bool can_step(const World *world, int fx, int fy, int x, int y) {
  if (!world_walkable(world, x, y)) {
    return false;
  }
  float ax = world_cell_feet_x(fx), ay = world_cell_feet_y(fy);
  float bx = world_cell_feet_x(x), by = world_cell_feet_y(y);
  for (int s = 1; s < 8; s++) {
    float t = (float)s / 8.0f;
    if (walker_blocked(world, ax + (bx - ax) * t, ay + (by - ay) * t)) {
      return false;
    }
  }
  return true;
}

bool world_build_steps(World *world) {
  SDL_free(world->steps);
  world->steps = SDL_calloc((size_t)(world->width * world->height), 1);
  if (!world->steps) {
    return false;
  }
  for (int y = 0; y < world->height; y++) {
    for (int x = 0; x < world->width; x++) {
      Uint8 bits = 0;
      for (int d = 0; d < 4; d++) {
        int nx = x + STEP_DX[d], ny = y + STEP_DY[d];
        if (world_in_bounds(world, nx, ny) && can_step(world, x, y, nx, ny)) {
          bits |= (Uint8)(1 << d);
        }
      }
      world->steps[y * world->width + x] = bits;
    }
  }
  return true;
}

static float walker_cost(void *ctx, int fx, int fy, int x, int y) {
  const World *world = ctx;
  int d = x > fx ? 0 : x < fx ? 1 : y > fy ? 2 : 3;
  if (!(world->steps[fy * world->width + fx] & (1 << d))) {
    return -1;
  }
  switch (world->kind[y * world->width + x]) {
    case CELL_PATH: return 1.0f;
    case CELL_HOUSE: return 4.0f;  // only go in on purpose
    default: return 2.5f;
  }
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

typedef struct FieldCtx {
  const World *world;
  int goalX, goalY;
} FieldCtx;

static float field_cost(void *ctx, int fx, int fy, int x, int y) {
  const FieldCtx *f = ctx;
  // the goal may be a cell a walker can't stand in the middle of (the hero
  // squeezed next to a tree): stepping into it is always allowed
  if (x == f->goalX && y == f->goalY) {
    return world_walkable(f->world, fx, fy) ? 1.0f : -1.0f;
  }
  return walker_cost((void *)f->world, fx, fy, x, y);
}

bool world_distance_field(const World *world, int gx, int gy, float *out) {
  FieldCtx ctx = {world, gx, gy};
  return path_distance_field(world->width, world->height, gx, gy, field_cost, &ctx, out);
}
