/*
  Copyright (C) 2013 batiste.bieler@gmail.com

  Villagers and animals

  Villagers live in a house. They come out, walk the roads to the plaza,
  visit neighbours, wander around, sometimes travel to another village, and
  go back inside for a while. Pigs just potter around their village.
 */

#include "npc.h"

#define MAX_PATH 1024
#define VILLAGER_SPEED 2.0f  // pixels per tick
#define PIG_SPEED 1.0f
#define TICKS_PER_SECOND 50

static int rnd(Npc *npc, int n) {
  return SDL_rand_r(&npc->rng, n);
}

static int seconds(Npc *npc, float min, float max) {
  return (int)((min + SDL_randf_r(&npc->rng) * (max - min)) * TICKS_PER_SECOND);
}

static float cell_center_x(int x) {
  return (float)(x * TILE_SIZE + TILE_SIZE / 2);
}

static float cell_feet_y(int y) {
  return (float)(y * TILE_SIZE + 34);
}

// Point just past the door: walking there takes the villager behind the wall.
static void door_point(const House *house, float *x, float *y) {
  *x = cell_center_x(house->doorX);
  *y = (float)(house->doorY * TILE_SIZE - 6);
}

static bool walk_to(Npc *npc, const World *world, int gx, int gy) {
  int sx = (int)(npc->x / TILE_SIZE), sy = (int)(npc->y / TILE_SIZE);
  if (sx == gx && sy == gy) {
    return false;
  }
  int n = world_find_path(world, sx, sy, gx, gy, npc->path, MAX_PATH);
  if (n < 2) {
    return false;
  }
  npc->pathLen = n;
  npc->pathPos = 1;
  npc->state = NPC_WALKING;
  int cell = npc->path[1];
  npc->targetX = cell_center_x(cell % world->width);
  npc->targetY = cell_feet_y(cell / world->width);
  return true;
}

static bool random_cell_near(Npc *npc, const World *world, int cx, int cy, int radius, int *ox, int *oy) {
  for (int attempt = 0; attempt < 20; attempt++) {
    int x = cx - radius + rnd(npc, 2 * radius + 1);
    int y = cy - radius + rnd(npc, 2 * radius + 1);
    if (world_walkable(world, x, y)) {
      *ox = x;
      *oy = y;
      return true;
    }
  }
  return false;
}

static void choose_goal(Npc *npc, const World *world) {
  npc->house = -1;
  const Village *village = &world->villages[npc->village];
  int gx = 0, gy = 0;
  bool ok = false;

  if (npc->kind == NPC_PIG) {
    int cx = (int)(npc->x / TILE_SIZE), cy = (int)(npc->y / TILE_SIZE);
    // stay around the village
    int dx = cx - village->centerX, dy = cy - village->centerY;
    if (dx * dx + dy * dy > village->radius * village->radius) {
      cx = village->centerX;
      cy = village->centerY;
    }
    ok = random_cell_near(npc, world, cx, cy, 3, &gx, &gy);
  } else {
    float r = SDL_randf_r(&npc->rng);
    if (r < 0.35f) {
      // hang around the plaza
      ok = random_cell_near(npc, world, village->centerX, village->centerY, 2, &gx, &gy);
    } else if (r < 0.6f && village->numHouses > 0) {
      npc->house = village->firstHouse + rnd(npc, village->numHouses);
    } else if (r < 0.75f) {
      npc->house = npc->home;
    } else if (r < 0.78f && world->numVillages > 1) {
      // trip to another village
      int other = rnd(npc, world->numVillages - 1);
      npc->village = other >= npc->village ? other + 1 : other;
      village = &world->villages[npc->village];
      ok = random_cell_near(npc, world, village->centerX, village->centerY, 2, &gx, &gy);
    } else {
      int cx = (int)(npc->x / TILE_SIZE), cy = (int)(npc->y / TILE_SIZE);
      ok = random_cell_near(npc, world, cx, cy, 6, &gx, &gy);
    }
    if (npc->house >= 0) {
      const House *house = &world->houses[npc->house];
      gx = house->doorX;
      gy = house->doorY;
      ok = true;
    }
  }

  if (!ok || !walk_to(npc, world, gx, gy)) {
    if (ok && npc->house >= 0) {
      // already standing at the door
      npc->state = NPC_ENTERING;
      door_point(&world->houses[npc->house], &npc->targetX, &npc->targetY);
      return;
    }
    npc->house = -1;
    npc->state = NPC_IDLE;
    npc->timer = seconds(npc, 0.5f, 2.0f);
  }
}

// Move towards the target; returns true once there.
static bool step_towards(Npc *npc, float speed) {
  float dx = npc->targetX - npc->x, dy = npc->targetY - npc->y;
  float dist = SDL_sqrtf(dx * dx + dy * dy);
  if (dist <= speed) {
    npc->x = npc->targetX;
    npc->y = npc->targetY;
    return true;
  }
  npc->x += dx / dist * speed;
  npc->y += dy / dist * speed;
  if (SDL_fabsf(dx) > 0.1f) {
    npc->faceLeft = dx < 0;
  }
  if (SDL_fabsf(dx) > SDL_fabsf(dy)) {
    npc->dir = dx > 0 ? DIR_RIGHT : DIR_LEFT;
  } else {
    npc->dir = dy > 0 ? DIR_DOWN : DIR_UP;
  }
  npc->moving = true;
  return false;
}

static void update_npc(Npc *npc, const World *world) {
  float speed = npc->kind == NPC_PIG ? PIG_SPEED : VILLAGER_SPEED;
  npc->prevX = npc->x;
  npc->prevY = npc->y;
  npc->moving = false;

  switch (npc->state) {
    case NPC_IDLE:
      if (--npc->timer <= 0) {
        choose_goal(npc, world);
      }
      break;

    case NPC_WALKING:
      if (step_towards(npc, speed)) {
        if (++npc->pathPos < npc->pathLen) {
          int cell = npc->path[npc->pathPos];
          npc->targetX = cell_center_x(cell % world->width);
          npc->targetY = cell_feet_y(cell / world->width);
        } else if (npc->house >= 0) {
          npc->state = NPC_ENTERING;
          door_point(&world->houses[npc->house], &npc->targetX, &npc->targetY);
        } else {
          npc->state = NPC_IDLE;
          npc->timer = npc->kind == NPC_PIG ? seconds(npc, 1, 5) : seconds(npc, 1, 4);
        }
      }
      break;

    case NPC_ENTERING:
      if (step_towards(npc, speed)) {
        npc->state = NPC_INSIDE;
        npc->village = world->houses[npc->house].village;
        npc->timer = seconds(npc, 4, 15);
      }
      break;

    case NPC_INSIDE:
      if (--npc->timer <= 0) {
        const House *house = &world->houses[npc->house];
        npc->state = NPC_EXITING;
        door_point(house, &npc->x, &npc->y);
        npc->prevX = npc->x;
        npc->prevY = npc->y;
        npc->targetX = cell_center_x(house->doorX);
        npc->targetY = cell_feet_y(house->doorY);
      }
      break;

    case NPC_EXITING:
      if (step_towards(npc, speed)) {
        choose_goal(npc, world);
      }
      break;
  }
}

static Npc *add_npc(Npcs *npcs, NpcKind kind, int village, Uint64 seed) {
  Npc *npc = &npcs->items[npcs->count];
  int *path = SDL_malloc(MAX_PATH * sizeof(int));
  if (!path) {
    return NULL;
  }
  npcs->count++;
  *npc = (Npc){.kind = kind, .village = village, .home = -1, .house = -1,
               .path = path, .rng = seed, .dir = DIR_DOWN};
  return npc;
}

bool npcs_spawn(Npcs *npcs, const World *world, Uint64 seed) {
  *npcs = (Npcs){0};
  int capacity = world->numHouses * 2 + world->numVillages * 3;
  npcs->items = SDL_calloc((size_t)SDL_max(capacity, 1), sizeof(*npcs->items));
  if (!npcs->items) {
    return false;
  }
  Uint64 rng = seed ^ 0x9e3779b97f4a7c15ull;

  for (int h = 0; h < world->numHouses; h++) {
    const House *house = &world->houses[h];
    int residents = 1 + SDL_rand_r(&rng, 2);
    for (int i = 0; i < residents; i++) {
      Npc *npc = add_npc(npcs, NPC_VILLAGER, house->village, SDL_rand_bits_r(&rng) | ((Uint64)h << 32));
      if (!npc) {
        return false;
      }
      npc->look = SDL_rand_r(&rng, NUM_VILLAGER_LOOKS);
      npc->home = h;
      npc->house = h;
      // start inside, coming out at different times
      npc->state = NPC_INSIDE;
      npc->timer = SDL_rand_r(&rng, 4 * TICKS_PER_SECOND);
      door_point(house, &npc->x, &npc->y);
    }
  }

  for (int v = 0; v < world->numVillages; v++) {
    const Village *village = &world->villages[v];
    int pigs = SDL_rand_r(&rng, 4);
    for (int i = 0; i < pigs; i++) {
      Npc *npc = add_npc(npcs, NPC_PIG, v, SDL_rand_bits_r(&rng) | ((Uint64)(v + 1000) << 32));
      if (!npc) {
        return false;
      }
      int x, y;
      if (!random_cell_near(npc, world, village->centerX, village->centerY, 7, &x, &y)) {
        x = village->centerX;
        y = village->centerY;
      }
      npc->x = npc->prevX = cell_center_x(x);
      npc->y = npc->prevY = cell_feet_y(y);
      npc->timer = SDL_rand_r(&rng, 3 * TICKS_PER_SECOND);
    }
  }
  return true;
}

void npcs_free(Npcs *npcs) {
  for (int i = 0; i < npcs->count; i++) {
    SDL_free(npcs->items[i].path);
  }
  SDL_free(npcs->items);
  *npcs = (Npcs){0};
}

void npcs_update(Npcs *npcs, const World *world) {
  for (int i = 0; i < npcs->count; i++) {
    update_npc(&npcs->items[i], world);
  }
}

const Sprite *npc_sprite(const Npc *npc, const Atlas *atlas, Uint64 tick, bool *flip, SDL_FPoint *offset) {
  if (npc->kind == NPC_PIG) {
    *flip = npc->faceLeft;
    *offset = (SDL_FPoint){-24, -32};
    return animation_frame(atlas->pig, npc->moving ? tick : 0);
  }
  *flip = false;
  *offset = (SDL_FPoint){-24, -40};
  return animation_frame(atlas->villagers[npc->look].walk[npc->dir], npc->moving ? tick : 0);
}
