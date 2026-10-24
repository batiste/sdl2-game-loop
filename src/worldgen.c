/*
  Copyright (C) 2013 batiste.bieler@gmail.com

  Procedural world generation

  1. lakes from fractal noise
  2. villages: a plaza, houses around it and a footpath from every door
  3. roads joining the villages (minimum spanning tree + a few extra links),
     routed with A* so they bend around lakes, houses and each other
  4. terrain tiles picked from the water/path masks (blob autotiling)
  5. forests, flowers, reeds, lily pads, garden fences and signs
 */

#include "world.h"

#include "pathfind.h"

// A house to build: either a small cottage with a door step, or a large
// house of any size assembled from corner, edge and middle pieces.
typedef struct HousePlan {
  int w, h;
  int doorCol;  // relative to the house
  bool small;
  bool roof;
  int shape;  // 0 hipped (tiles.png), 1 gabled, 2 thatched (roofs.png)
  int style;

  // walls, in pixels relative to the house: outer bounds, thickness of
  // each side and the door opening in the bottom wall
  int bodyX, bodyY, bodyW, bodyH;
  int top, left, right, bottom;
  int door0, door1;
} HousePlan;

enum { RES_NONE, RES_MARGIN, RES_HOUSE };

#define MAX_ROUTE 4096

typedef struct Gen {
  World *world;
  int w, h;
  Uint64 rng;
  Uint32 seed;
  Uint8 *water;
  Uint8 *nearWater;  // water dilated by one cell
  Uint8 *path;
  Uint8 *reserved;
  int *route;
} Gen;

// ---------------------------------------------------------------- random

static int rnd(Gen *g, int n) {
  return SDL_rand_r(&g->rng, n);
}

static float rndf(Gen *g) {
  return SDL_randf_r(&g->rng);
}

static Uint32 hash(Uint32 seed, int x, int y) {
  Uint32 h = seed ^ ((Uint32)x * 0x27d4eb2du) ^ ((Uint32)y * 0x165667b1u);
  h ^= h >> 15;
  h *= 0x2c1b3c6du;
  h ^= h >> 12;
  h *= 0x297a2d39u;
  h ^= h >> 15;
  return h;
}

static float hashf(Uint32 seed, int x, int y) {
  return (float)(hash(seed, x, y) >> 8) / (float)(1 << 24);
}

static float value_noise(Uint32 seed, float x, float y) {
  int x0 = (int)SDL_floorf(x), y0 = (int)SDL_floorf(y);
  float fx = x - (float)x0, fy = y - (float)y0;
  fx = fx * fx * (3 - 2 * fx);
  fy = fy * fy * (3 - 2 * fy);
  float a = hashf(seed, x0, y0), b = hashf(seed, x0 + 1, y0);
  float c = hashf(seed, x0, y0 + 1), d = hashf(seed, x0 + 1, y0 + 1);
  return (a + (b - a) * fx) * (1 - fy) + (c + (d - c) * fx) * fy;
}

// fractal noise in [0, 1]
static float fbm(Uint32 seed, float x, float y) {
  float sum = 0, amp = 1, norm = 0;
  for (int octave = 0; octave < 4; octave++) {
    sum += amp * value_noise(seed + (Uint32)octave * 1013u, x, y);
    norm += amp;
    amp *= 0.5f;
    x *= 2;
    y *= 2;
  }
  return sum / norm;
}

// ---------------------------------------------------------------- masks

static bool mask_at(const Gen *g, const Uint8 *m, int x, int y, bool outside) {
  if (x < 0 || y < 0 || x >= g->w || y >= g->h) {
    return outside;
  }
  return m[y * g->w + x] != 0;
}

// Terrain tiles need every cell to be part of a full 2x2 block.
static void keep_2x2(Gen *g, Uint8 *m, bool outside) {
  bool changed = true;
  while (changed) {
    changed = false;
    for (int y = 0; y < g->h; y++) {
      for (int x = 0; x < g->w; x++) {
        if (!m[y * g->w + x]) {
          continue;
        }
        bool ok = false;
        for (int oy = -1; oy <= 0 && !ok; oy++) {
          for (int ox = -1; ox <= 0 && !ok; ox++) {
            ok = mask_at(g, m, x + ox, y + oy, outside) && mask_at(g, m, x + ox + 1, y + oy, outside) &&
                 mask_at(g, m, x + ox, y + oy + 1, outside) && mask_at(g, m, x + ox + 1, y + oy + 1, outside);
          }
        }
        if (!ok) {
          m[y * g->w + x] = 0;
          changed = true;
        }
      }
    }
  }
}

// The tileset has one inner corner per tile: fill the diagonal gaps of
// cells that would need an inner corner combined with anything else.
static void fill_pinches(Gen *g, Uint8 *m, bool outside, const Uint8 *forbid) {
  static const int SX[4] = {1, -1, 1, -1}, SY[4] = {1, 1, -1, -1};
  for (int iter = 0; iter < 64; iter++) {
    bool changed = false;
    for (int y = 0; y < g->h; y++) {
      for (int x = 0; x < g->w; x++) {
        if (!m[y * g->w + x]) {
          continue;
        }
        int exposures = !mask_at(g, m, x + 1, y, outside) + !mask_at(g, m, x - 1, y, outside) +
                        !mask_at(g, m, x, y + 1, outside) + !mask_at(g, m, x, y - 1, outside);
        bool gap[4];
        for (int d = 0; d < 4; d++) {
          gap[d] = mask_at(g, m, x + SX[d], y, outside) && mask_at(g, m, x, y + SY[d], outside) &&
                   !mask_at(g, m, x + SX[d], y + SY[d], outside);
          exposures += gap[d];
        }
        if (exposures < 2) {
          continue;
        }
        for (int d = 0; d < 4; d++) {
          int nx = x + SX[d], ny = y + SY[d];
          if (gap[d] && nx >= 0 && ny >= 0 && nx < g->w && ny < g->h &&
              !(forbid && forbid[ny * g->w + nx])) {
            m[ny * g->w + nx] = 1;
            changed = true;
          }
        }
      }
    }
    if (!changed) {
      break;
    }
  }
}

static Uint16 autotile(const Gen *g, const Uint8 *m, int x, int y, int set, bool outside) {
  bool n = mask_at(g, m, x, y - 1, outside), s = mask_at(g, m, x, y + 1, outside);
  bool e = mask_at(g, m, x + 1, y, outside), w = mask_at(g, m, x - 1, y, outside);
  int col, row;
  if (!n && !w) col = 0, row = 0;
  else if (!n && !e) col = 2, row = 0;
  else if (!s && !w) col = 0, row = 2;
  else if (!s && !e) col = 2, row = 2;
  else if (!n) col = 1, row = 0;
  else if (!s) col = 1, row = 2;
  else if (!w) col = 0, row = 1;
  else if (!e) col = 2, row = 1;
  else if (!mask_at(g, m, x + 1, y - 1, outside)) col = 3, row = 1;  // grass top-right
  else if (!mask_at(g, m, x - 1, y - 1, outside)) col = 4, row = 1;  // grass top-left
  else if (!mask_at(g, m, x + 1, y + 1, outside)) col = 3, row = 0;  // grass bottom-right
  else if (!mask_at(g, m, x - 1, y + 1, outside)) col = 4, row = 0;  // grass bottom-left
  else {
    // centre: mostly the standard tile, sometimes the plain or busy variant
    float v = hashf(g->seed + 77u, x, y);
    float standard = set == TERRAIN_WATER ? 0.5f : 0.8f;
    float plain = set == TERRAIN_WATER ? 0.8f : 0.9f;
    if (v < standard) col = 1, row = 1;
    else if (v < plain) col = 5, row = 1;
    else col = 5, row = 0;
  }
  return (Uint16)GID_TERRAIN(set, col, row);
}

// ---------------------------------------------------------------- lakes

static void make_lakes(Gen *g) {
  Uint32 seed = g->seed + 1u;
  for (int y = 0; y < g->h; y++) {
    for (int x = 0; x < g->w; x++) {
      g->water[y * g->w + x] = fbm(seed, (float)x / 22.0f, (float)y / 22.0f) > 0.64f;
    }
  }
  keep_2x2(g, g->water, true);
  fill_pinches(g, g->water, true, NULL);

  for (int y = 0; y < g->h; y++) {
    for (int x = 0; x < g->w; x++) {
      bool near = false;
      for (int dy = -1; dy <= 1 && !near; dy++) {
        for (int dx = -1; dx <= 1 && !near; dx++) {
          near = mask_at(g, g->water, x + dx, y + dy, false);
        }
      }
      g->nearWater[y * g->w + x] = near;
    }
  }
}

// ---------------------------------------------------------------- routing

typedef struct RouteCtx {
  Gen *gen;
} RouteCtx;

// Roads are 2 cells wide: a route cell is the top-left of a 2x2 brush.
static float road_cost(void *ctx, int fx, int fy, int x, int y) {
  (void)fx;
  (void)fy;
  Gen *g = ctx;
  if (x + 1 >= g->w || y + 1 >= g->h || x < 1 || y < 1) {
    return -1;
  }
  int onPath = 0;
  float cost = 0;
  for (int dy = 0; dy <= 1; dy++) {
    for (int dx = 0; dx <= 1; dx++) {
      int i = (y + dy) * g->w + x + dx;
      if (g->reserved[i] == RES_HOUSE || g->nearWater[i]) {
        return -1;
      }
      onPath += g->path[i];
      if (g->reserved[i] == RES_MARGIN) {
        cost += 0.4f;
      }
    }
  }
  if (onPath == 4) {
    return 0.3f;  // follow existing roads
  }
  // a little noise makes roads meander
  return 1.0f + cost + 3.0f * fbm(g->seed + 5u, (float)x / 9.0f, (float)y / 9.0f);
}

static bool route(Gen *g, int sx, int sy, int gx, int gy, float turnCost) {
  PathQuery q = {
      .width = g->w, .height = g->h,
      .startX = sx, .startY = sy, .goalX = gx, .goalY = gy,
      .cost = road_cost, .ctx = g,
      .minCost = 0.3f, .turnCost = turnCost,
  };
  int n = path_find(&q, g->route, MAX_ROUTE);
  if (n < 0) {
    return false;
  }
  for (int i = 0; i < n; i++) {
    int x = g->route[i] % g->w, y = g->route[i] / g->w;
    for (int dy = 0; dy <= 1; dy++) {
      for (int dx = 0; dx <= 1; dx++) {
        g->path[(y + dy) * g->w + x + dx] = 1;
      }
    }
  }
  return true;
}

// ---------------------------------------------------------------- villages

static const char *const PREFIXES[] = {
    "Oak", "Ash", "Bram", "Wil", "Thorn", "Elm", "Fen", "Moss",
    "Hol", "Red", "Sun", "Pine", "Stone", "Brook", "Hazel", "Mill",
};
static const char *const SUFFIXES[] = {
    "vale", "by", "ford", "stead", "wick", "ton", "mere", "field",
    "hollow", "dale", "ham", "well",
};

static void name_village(Gen *g, Village *v) {
  for (int attempt = 0; attempt < 20; attempt++) {
    SDL_snprintf(v->name, sizeof(v->name), "%s%s",
                 PREFIXES[rnd(g, (int)SDL_arraysize(PREFIXES))],
                 SUFFIXES[rnd(g, (int)SDL_arraysize(SUFFIXES))]);
    bool taken = false;
    for (int i = 0; i < g->world->numVillages; i++) {
      taken = taken || (&g->world->villages[i] != v && SDL_strcmp(g->world->villages[i].name, v->name) == 0);
    }
    if (!taken) {
      return;
    }
  }
}

static bool area_clear_of_water(Gen *g, int cx, int cy, int r) {
  for (int y = cy - r; y <= cy + r; y++) {
    for (int x = cx - r; x <= cx + r; x++) {
      if (mask_at(g, g->water, x, y, true)) {
        return false;
      }
    }
  }
  return true;
}

static void add_box(World *world, int x, int y, Box b) {
  if (b.x1 <= b.x0 || b.y1 <= b.y0) {
    return;
  }
  Box *boxes = world_boxes(world, x, y);
  for (int k = 0; k < BOXES_PER_CELL; k++) {
    if (boxes[k].x1 <= boxes[k].x0) {
      boxes[k] = b;
      return;
    }
  }
  SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Too many collision boxes in cell %d,%d", x, y);
}

// Cut the wall rectangles of a house along the cell grid.
static void add_walls(World *world, const HousePlan *p, int hx, int hy) {
  int x0 = p->bodyX, y0 = p->bodyY, x1 = p->bodyX + p->bodyW, y1 = p->bodyY + p->bodyH;
  const SDL_Rect walls[] = {
      {x0, y0, x1 - x0, p->top},
      {x0, y0, p->left, y1 - y0},
      {x1 - p->right, y0, p->right, y1 - y0},
      {x0, y1 - p->bottom, p->door0 - x0, p->bottom},
      {p->door1, y1 - p->bottom, x1 - p->door1, p->bottom},
  };
  for (int r = 0; r < p->h; r++) {
    for (int c = 0; c < p->w; c++) {
      SDL_Rect cell = {c * TILE_SIZE, r * TILE_SIZE, TILE_SIZE, TILE_SIZE};
      for (int k = 0; k < (int)SDL_arraysize(walls); k++) {
        SDL_Rect part;
        if (SDL_GetRectIntersection(&walls[k], &cell, &part)) {
          add_box(world, hx + c, hy + r, (Box){(Uint8)(part.x - cell.x), (Uint8)(part.y - cell.y),
                                               (Uint8)(part.x + part.w - cell.x), (Uint8)(part.y + part.h - cell.y)});
        }
      }
    }
  }
}

static Box furniture_box(Furniture f) {
  switch (f) {
    case FURN_SHELF:
    case FURN_CUPBOARD: return (Box){8, 0, 40, 36};
    case FURN_BED_RED:
    case FURN_BED_BLUE:
    case FURN_BED_GREEN: return (Box){12, 4, 36, 44};
    case FURN_TABLE: return (Box){10, 14, 38, 34};
    case FURN_CHAIR_LEFT:
    case FURN_CHAIR_RIGHT: return (Box){18, 22, 30, 38};
    case FURN_CHEST: return (Box){10, 18, 38, 38};
    case FURN_BARREL: return (Box){14, 12, 34, 38};
    case FURN_PLANT: return (Box){18, 24, 30, 38};
    case FURN_FIRE: return (Box){4, 0, 44, 36};
    default: return (Box){0};  // the rug is walked over
  }
}

static void put_furniture(World *world, int x, int y, Furniture f) {
  world->furniture[y * world->width + x] = (Uint16)GID_FURNITURE(f);
  add_box(world, x, y, furniture_box(f));
}

// Furniture against the back and side walls, a table with chairs and a rug
// by the door. The door column stays clear so the room can be reached.
static void furnish(Gen *g, const HousePlan *p, int hx, int hy, int fireCol) {
  static const Furniture BACK[] = {FURN_SHELF, FURN_CUPBOARD, FURN_BED_RED, FURN_BED_BLUE,
                                   FURN_BED_GREEN, FURN_BARREL, FURN_PLANT, FURN_CHEST};
  static const Furniture SIDE[] = {FURN_CHEST, FURN_BARREL, FURN_PLANT, FURN_BED_RED,
                                   FURN_BED_BLUE, FURN_BED_GREEN};
  World *world = g->world;
  if (p->small) {
    put_furniture(world, hx + 1, hy, fireCol >= 0 ? FURN_FIRE : BACK[rnd(g, 5)]);
    return;
  }

  bool used[6][4] = {{false}};
  for (int c = 0; c < p->w; c++) {
    if (c == fireCol || rndf(g) < 0.55f) {
      put_furniture(world, hx + c, hy, c == fireCol ? FURN_FIRE : BACK[rnd(g, (int)SDL_arraysize(BACK))]);
      used[c][0] = true;
    }
  }
  for (int r = 1; r < p->h - 1; r++) {
    for (int c = 0; c < p->w; c += p->w - 1) {
      if (rndf(g) < 0.45f) {
        put_furniture(world, hx + c, hy + r, SIDE[rnd(g, (int)SDL_arraysize(SIDE))]);
        used[c][r] = true;
      }
    }
  }

  int tableRow = p->h == 4 ? 1 + rnd(g, 2) : 1;
  int candidates[6], count = 0;
  for (int c = 1; c < p->w - 1; c++) {
    if (c != p->doorCol && !used[c][tableRow]) {
      candidates[count++] = c;
    }
  }
  if (p->w >= 4 && count > 0) {
    int c = candidates[rnd(g, count)];
    put_furniture(world, hx + c, hy + tableRow, FURN_TABLE);
    used[c][tableRow] = true;
    for (int side = -1; side <= 1; side += 2) {
      int cc = c + side;
      if (cc >= 0 && cc < p->w && cc != p->doorCol && !used[cc][tableRow]) {
        put_furniture(world, hx + cc, hy + tableRow, side < 0 ? FURN_CHAIR_LEFT : FURN_CHAIR_RIGHT);
        used[cc][tableRow] = true;
      }
    }
  }
  if (p->h > 2 && rndf(g) < 0.7f) {
    put_furniture(world, hx + p->doorCol, hy + p->h - 2, FURN_RUG);
  }
}

static HousePlan plan_house(Gen *g, const Village *v) {
  HousePlan p = {.style = v->styles[rnd(g, (int)SDL_arraysize(v->styles))], .roof = rndf(g) > 0.08f};
  // most houses of a village share its roof shape
  p.shape = rndf(g) < 0.6f ? v->roofShape : rnd(g, 3);
  if (rndf(g) < 0.12f) {
    p.shape = 0;  // only the hipped roof exists for cottages
    // cottage, measured from the small house art
    p.small = true;
    p.w = 3, p.h = 3, p.doorCol = 1;
    p.bodyX = 22, p.bodyY = 0, p.bodyW = 98, p.bodyH = 96;
    p.top = 19, p.left = 15, p.right = 16, p.bottom = 14;
    p.door0 = 58, p.door1 = 83;
  } else {
    static const int WIDTHS[] = {4, 4, 5, 5, 5, 6};
    p.w = WIDTHS[rnd(g, (int)SDL_arraysize(WIDTHS))];
    p.h = rndf(g) < (p.w >= 5 ? 0.55f : 0.35f) ? 4 : 3;
    p.doorCol = 1 + rnd(g, p.w - 2);
    p.bodyW = p.w * TILE_SIZE, p.bodyH = p.h * TILE_SIZE;
    p.top = 18, p.left = 12, p.right = 18, p.bottom = 14;
    // the opening of the cottage door, copied into the large facades
    p.door0 = p.doorCol * TILE_SIZE + 10, p.door1 = p.doorCol * TILE_SIZE + 35;
  }
  return p;
}

// Which windows a large house has: per column in the back and front walls,
// per row in the side walls.
typedef struct Windows {
  bool back[6], front[6], side[4];
} Windows;

// Which piece of the large house set goes in cell (c, r).
static int large_piece(const HousePlan *p, const Windows *win, int c, int r, bool room) {
  bool backWindow = win->back[c], frontWindow = win->front[c];
  int last = p->w - 1, bottom = p->h - 1;
  int col = c == 0 ? 0 : (c == last ? 3 : 2);
  bool middle = col == 2;
  bool door = r == bottom && c == p->doorCol;
  bool window = r == bottom && middle && frontWindow && !door;
  if (!room && p->shape == 0) {
    if (door) return PIECE_ROOF_DOOR;
    if (window) return PIECE_ROOF_WINDOW;
    return PIECE_ROOF(col, r == 0 ? 0 : (r == bottom ? 2 : 1));
  }
  if (!room) {
    // the ridge runs along the middle: inside row 1 for 3 deep houses,
    // between rows 1 and 2 for 4 deep ones
    static const int ROWS3[] = {0, 1, 4}, ROWS4[] = {0, 2, 3, 4};
    if (window) return PIECE_SHAPED_WINDOW(p->shape - 1);
    int row = p->h == 3 ? ROWS3[r] : ROWS4[r];
    return PIECE_SHAPED(p->shape - 1, door ? 1 : col, row);
  }
  // inside: windows in the walls, matching the facade
  if (door) return PIECE_ROOM_DOOR;
  if (window) return PIECE_ROOM_WINDOW_FRONT;
  if (r == 0 && middle && backWindow) return PIECE_ROOM_WINDOW_BACK;
  bool side = r > 0 && r < bottom && !middle;
  if (side && win->side[r]) return c == 0 ? PIECE_ROOM_WINDOW_LEFT : PIECE_ROOM_WINDOW_RIGHT;
  // row 2 of the original interior is the plain one (row 1 has openings)
  return PIECE_ROOM(col, r == 0 ? 0 : (r == bottom ? 3 : 2));
}

static bool try_house(Gen *g, Village *v, int villageIndex) {
  HousePlan p = plan_house(g, v);
  float angle = rndf(g) * 2 * SDL_PI_F;
  float dist = 4.5f + (float)p.w * 0.4f + rndf(g) * 5.0f;
  int hx = v->centerX + (int)SDL_roundf(SDL_cosf(angle) * dist * 1.3f) - p.w / 2;
  int hy = v->centerY + (int)SDL_roundf(SDL_sinf(angle) * dist) - p.h / 2;

  // the footprint and a one cell ring around it must be free
  for (int y = hy - 1; y <= hy + p.h; y++) {
    for (int x = hx - 1; x <= hx + p.w; x++) {
      if (x < 2 || y < 2 || x >= g->w - 2 || y >= g->h - 3) {
        return false;
      }
      int i = y * g->w + x;
      if (g->water[i] || g->nearWater[i] || g->path[i] || g->reserved[i] == RES_HOUSE) {
        return false;
      }
    }
  }

  // the footpath starts at the door step, or right below the wall
  int doorX = hx + p.doorCol;
  int doorY = p.small ? hy + p.h - 1 : hy + p.h;
  int stepRow = p.small ? hy + p.h - 1 : -1;
  if (road_cost(g, doorX, doorY, doorX, doorY) < 0) {
    return false;
  }

  // reserve, then make sure a footpath can reach the plaza
  Uint8 saved[(6 + 2) * (4 + 2)];
  int n = 0;
  for (int y = hy - 1; y <= hy + p.h; y++) {
    for (int x = hx - 1; x <= hx + p.w; x++) {
      int i = y * g->w + x;
      saved[n++] = g->reserved[i];
      bool inside = x >= hx && y >= hy && x < hx + p.w && y < hy + p.h && y != stepRow;
      g->reserved[i] = inside ? RES_HOUSE : SDL_max(g->reserved[i], RES_MARGIN);
    }
  }
  if (!route(g, doorX, doorY, v->centerX, v->centerY, 1.0f)) {
    n = 0;
    for (int y = hy - 1; y <= hy + p.h; y++) {
      for (int x = hx - 1; x <= hx + p.w; x++) {
        g->reserved[y * g->w + x] = saved[n++];
      }
    }
    return false;
  }

  World *world = g->world;
  int index = world->numHouses++;
  House *house = &world->houses[index];
  float ox = (float)(hx * TILE_SIZE), oy = (float)(hy * TILE_SIZE);
  *house = (House){
      .x = hx, .y = hy, .w = p.w, .h = p.h, .doorX = doorX, .doorY = doorY,
      .village = villageIndex, .roof = p.roof, .style = p.style,
      .body = {ox + (float)p.bodyX, oy + (float)p.bodyY, (float)p.bodyW, (float)p.bodyH},
  };
  // a fireplace against the back wall, with the chimney right above it
  int fireCol = -1;
  if (p.roof && rndf(g) < 0.6f) {
    fireCol = p.small ? 1 : rnd(g, p.w);
    house->chimney = true;
    float x = ox + (float)(fireCol * TILE_SIZE) - 1 + (float)(rnd(g, 9) - 4);
    house->chimneyX = SDL_clamp(x, house->body.x - 14, house->body.x + house->body.w - 34);
    house->chimneyY = oy - 6;
  }

  Windows win;
  for (int k = 0; k < 6; k++) {
    win.back[k] = rndf(g) < 0.4f;
    win.front[k] = rndf(g) < 0.5f;
  }
  for (int k = 0; k < 4; k++) {
    win.side[k] = rndf(g) < 0.5f;
  }
  for (int c = 0; c < p.w; c++) {
    for (int r = 0; r < p.h; r++) {
      int i = (hy + r) * g->w + hx + c;
      int outside = p.small ? (p.roof ? PIECE_SMALL_ROOF(c, r) : PIECE_SMALL_ROOM(c, r))
                            : large_piece(&p, &win, c, r, !p.roof);
      world->top[i] = (Uint16)GID_HOUSE(p.style, outside);
      world->houseAt[i] = (Sint16)index;
      if (hy + r == stepRow) {
        continue;  // the step is flat and walkable
      }
      world->kind[i] = CELL_HOUSE;
      if (p.roof) {
        int inside = p.small ? PIECE_SMALL_ROOM(c, r) : large_piece(&p, &win, c, r, true);
        world->interior[i] = (Uint16)GID_HOUSE(p.style, inside);
      }
    }
  }
  if (!p.small) {
    // landing in front of the door, over the start of the footpath
    world->top[doorY * g->w + doorX] = (Uint16)GID_HOUSE(p.style, PIECE_LANDING);
  }
  add_walls(world, &p, hx, hy);
  furnish(g, &p, hx, hy, fireCol);
  return true;
}

static void make_villages(Gen *g) {
  World *world = g->world;
  int target = SDL_max(2, g->w * g->h / 3000);
  world->villages = SDL_calloc((size_t)target, sizeof(*world->villages));
  world->houses = SDL_calloc((size_t)target * 8, sizeof(*world->houses));
  if (!world->villages || !world->houses) {
    return;
  }

  for (int attempt = 0; attempt < 4000 && world->numVillages < target; attempt++) {
    int margin = 14;
    int cx = margin + rnd(g, g->w - 2 * margin);
    int cy = margin + rnd(g, g->h - 2 * margin);
    // later attempts accept water closer to the village
    int clear = attempt < 3000 ? 11 : 6;
    if (!area_clear_of_water(g, cx, cy, clear)) {
      continue;
    }
    bool tooClose = false;
    for (int i = 0; i < world->numVillages; i++) {
      int dx = world->villages[i].centerX - cx, dy = world->villages[i].centerY - cy;
      tooClose = tooClose || dx * dx + dy * dy < 34 * 34;
    }
    if (tooClose) {
      continue;
    }

    int index = world->numVillages++;
    Village *v = &world->villages[index];
    *v = (Village){.centerX = cx, .centerY = cy, .radius = 12, .firstHouse = world->numHouses};
    name_village(g, v);
    // each village has its own few house styles and a favourite roof shape
    for (int k = 0; k < (int)SDL_arraysize(v->styles); k++) {
      v->styles[k] = rnd(g, NUM_HOUSE_STYLES);
    }
    v->roofShape = rnd(g, 3);

    // plaza
    int pw = 4 + rnd(g, 3), ph = 4;
    for (int y = cy - ph / 2 + 1; y < cy - ph / 2 + 1 + ph; y++) {
      for (int x = cx - pw / 2 + 1; x < cx - pw / 2 + 1 + pw; x++) {
        g->path[y * g->w + x] = 1;
      }
    }

    int wanted = 4 + rnd(g, 5);
    for (int tries = 0; tries < 120 && v->numHouses < wanted; tries++) {
      if (try_house(g, v, index)) {
        v->numHouses++;
      }
    }
  }
}

// Minimum spanning tree over the village centres, plus a few shortcuts.
static void make_roads(Gen *g) {
  World *world = g->world;
  int n = world->numVillages;
  if (n < 2) {
    return;
  }
  bool *inTree = SDL_calloc((size_t)n, sizeof(bool));
  bool *linked = SDL_calloc((size_t)(n * n), sizeof(bool));
  if (!inTree || !linked) {
    SDL_free(inTree);
    SDL_free(linked);
    return;
  }
#define DIST2(a, b)                                                             \
  ((world->villages[a].centerX - world->villages[b].centerX) *                  \
       (world->villages[a].centerX - world->villages[b].centerX) +              \
   (world->villages[a].centerY - world->villages[b].centerY) *                  \
       (world->villages[a].centerY - world->villages[b].centerY))

  inTree[0] = true;
  for (int added = 1; added < n; added++) {
    int bestA = -1, bestB = -1, best = SDL_MAX_SINT32;
    for (int a = 0; a < n; a++) {
      for (int b = 0; b < n && inTree[a]; b++) {
        if (!inTree[b] && DIST2(a, b) < best) {
          best = DIST2(a, b);
          bestA = a;
          bestB = b;
        }
      }
    }
    inTree[bestB] = true;
    linked[bestA * n + bestB] = linked[bestB * n + bestA] = true;
    const Village *va = &world->villages[bestA], *vb = &world->villages[bestB];
    route(g, va->centerX, va->centerY, vb->centerX, vb->centerY, 1.2f);
  }

  // a few loops so the network isn't a pure tree
  for (int extra = 0; extra < n / 3; extra++) {
    int bestA = -1, bestB = -1, best = 70 * 70;
    for (int a = 0; a < n; a++) {
      for (int b = a + 1; b < n; b++) {
        if (!linked[a * n + b] && DIST2(a, b) < best) {
          best = DIST2(a, b);
          bestA = a;
          bestB = b;
        }
      }
    }
    if (bestA < 0) {
      break;
    }
    linked[bestA * n + bestB] = linked[bestB * n + bestA] = true;
    const Village *va = &world->villages[bestA], *vb = &world->villages[bestB];
    route(g, va->centerX, va->centerY, vb->centerX, vb->centerY, 1.2f);
  }
#undef DIST2
  SDL_free(inTree);
  SDL_free(linked);
}

// ---------------------------------------------------------------- decoration

static Box deco_box(Deco d) {
  switch (d) {
    case DECO_BUSH: return (Box){6, 10, 42, 38};
    case DECO_ROCK: return (Box){8, 12, 40, 36};
    case DECO_STUMP: return (Box){10, 14, 38, 36};
    case DECO_SIGN: return (Box){10, 10, 38, 40};
    case DECO_FENCE_LEFT: return (Box){22, 14, 48, 36};
    case DECO_FENCE: return (Box){0, 14, 48, 36};
    case DECO_FENCE_RIGHT: return (Box){0, 14, 26, 36};
    case DECO_FENCE_VERTICAL: return (Box){18, 0, 30, 48};
    default: return (Box){0};
  }
}

static bool cell_free(Gen *g, int x, int y) {
  if (x < 0 || y < 0 || x >= g->w || y >= g->h) {
    return false;
  }
  int i = y * g->w + x;
  return !g->water[i] && !g->path[i] && g->reserved[i] == RES_NONE && !g->world->top[i];
}

static void put_deco(Gen *g, int x, int y, Deco d) {
  int i = y * g->w + x;
  g->world->top[i] = (Uint16)GID_DECO(d);
  add_box(g->world, x, y, deco_box(d));
  if (d == DECO_BUSH) {
    g->world->kind[i] = CELL_FOREST;
  }
}

static void put_palm(Gen *g, int x, int y) {
  int i = y * g->w + x;
  g->world->top[i] = GID_PALM;
  add_box(g->world, x, y, (Box){16, 26, 32, 44});
  g->world->kind[i] = CELL_FOREST;
}

static bool near_path(Gen *g, int x, int y) {
  for (int dy = -1; dy <= 1; dy++) {
    for (int dx = -1; dx <= 1; dx++) {
      if (mask_at(g, g->path, x + dx, y + dy, false)) {
        return true;
      }
    }
  }
  return false;
}

static void make_gardens(Gen *g) {
  World *world = g->world;
  for (int h = 0; h < world->numHouses; h++) {
    const House *house = &world->houses[h];
    if (rndf(g) < 0.4f) {
      continue;
    }
    int length = 3 + rnd(g, 2);
    int y = house->y + house->h - 2;
    int x0 = rndf(g) < 0.5f ? house->x + house->w + 1 : house->x - 1 - length;
    bool ok = true;
    for (int x = x0; x < x0 + length && ok; x++) {
      ok = cell_free(g, x, y) && cell_free(g, x, y - 1) && !near_path(g, x, y) && !g->nearWater[y * g->w + x];
    }
    if (!ok) {
      continue;
    }
    for (int x = x0; x < x0 + length; x++) {
      Deco fence = x == x0 ? DECO_FENCE_LEFT : (x == x0 + length - 1 ? DECO_FENCE_RIGHT : DECO_FENCE);
      static const Deco BED[] = {DECO_RED_FLOWERS, DECO_PURPLE_FLOWERS, DECO_TALL_GRASS, DECO_RED_FLOWERS};
      put_deco(g, x, y, fence);
      put_deco(g, x, y - 1, BED[rnd(g, (int)SDL_arraysize(BED))]);
      g->reserved[y * g->w + x] = g->reserved[(y - 1) * g->w + x] = RES_MARGIN;
    }
  }
}

static void make_signs(Gen *g) {
  World *world = g->world;
  for (int i = 0; i < world->numVillages; i++) {
    const Village *v = &world->villages[i];
    // first free spot around the plaza
    for (int r = 3; r < 6; r++) {
      int x = v->centerX + r, y = v->centerY - 2;
      if (cell_free(g, x, y)) {
        put_deco(g, x, y, DECO_SIGN);
        break;
      }
    }
  }
}

static bool in_any_village(Gen *g, int x, int y) {
  for (int i = 0; i < g->world->numVillages; i++) {
    const Village *v = &g->world->villages[i];
    int dx = x - v->centerX, dy = y - v->centerY;
    if (dx * dx + dy * dy < v->radius * v->radius) {
      return true;
    }
  }
  return false;
}

static void decorate(Gen *g) {
  Uint32 forestSeed = g->seed + 3u;
  for (int y = 0; y < g->h; y++) {
    for (int x = 0; x < g->w; x++) {
      int i = y * g->w + x;
      float r = rndf(g);

      if (g->water[i]) {
        bool interior = true;
        for (int dy = -1; dy <= 1; dy++) {
          for (int dx = -1; dx <= 1; dx++) {
            interior = interior && mask_at(g, g->water, x + dx, y + dy, true);
          }
        }
        if (!interior) continue;
        if (r < 0.04f) put_deco(g, x, y, DECO_LILY);
        else if (r < 0.06f) put_deco(g, x, y, DECO_LILY_FLOWER);
        else if (r < 0.072f) put_deco(g, x, y, DECO_WATER_ROCK);
        continue;
      }
      if (!cell_free(g, x, y)) {
        continue;
      }
      if (g->nearWater[i]) {
        if (r < 0.3f) put_deco(g, x, y, DECO_REEDS);
        continue;
      }
      if (near_path(g, x, y)) {
        if (r < 0.05f) put_deco(g, x, y, rndf(g) < 0.5f ? DECO_RED_FLOWERS : DECO_PURPLE_FLOWERS);
        else if (r < 0.06f) put_deco(g, x, y, DECO_PEBBLES);
        continue;
      }
      if (in_any_village(g, x, y)) {
        if (r < 0.04f) put_deco(g, x, y, rndf(g) < 0.5f ? DECO_RED_FLOWERS : DECO_PURPLE_FLOWERS);
        else if (r < 0.06f) put_deco(g, x, y, DECO_TALL_GRASS);
        else if (r < 0.075f) put_deco(g, x, y, DECO_BUSH);
        else if (r < 0.085f) put_palm(g, x, y);
        continue;
      }

      float forest = fbm(forestSeed, (float)x / 16.0f, (float)y / 16.0f);
      if (forest > 0.58f) {
        if (r < 0.38f) put_palm(g, x, y);
        else if (r < 0.50f) put_deco(g, x, y, DECO_BUSH);
        else if (r < 0.56f) put_deco(g, x, y, DECO_TALL_GRASS);
        else if (r < 0.60f) put_deco(g, x, y, DECO_MUSHROOMS);
        else if (r < 0.63f) put_deco(g, x, y, DECO_STUMP);
      } else {
        if (r < 0.03f) put_palm(g, x, y);
        else if (r < 0.045f) put_deco(g, x, y, DECO_BUSH);
        else if (r < 0.055f) put_deco(g, x, y, DECO_ROCK);
        else if (r < 0.075f) put_deco(g, x, y, DECO_RED_FLOWERS);
        else if (r < 0.095f) put_deco(g, x, y, DECO_PURPLE_FLOWERS);
        else if (r < 0.115f) put_deco(g, x, y, DECO_TALL_GRASS);
        else if (r < 0.121f) put_deco(g, x, y, DECO_PEBBLES);
        else if (r < 0.125f) put_deco(g, x, y, DECO_STUMP);
      }
    }
  }
}

// ---------------------------------------------------------------- terrain

static void paint_terrain(Gen *g) {
  World *world = g->world;
  Uint32 meadowSeed = g->seed + 9u;
  for (int y = 0; y < g->h; y++) {
    for (int x = 0; x < g->w; x++) {
      int i = y * g->w + x;
      if (world->kind[i] == CELL_HOUSE) {
        world->ground[i] = GID_GRASS;
        continue;
      }
      if (g->water[i]) {
        world->ground[i] = autotile(g, g->water, x, y, TERRAIN_WATER, true);
        world->kind[i] = CELL_WATER;
        // the bank is walkable: only the water itself is solid
        Box b = {0, 0, TILE_SIZE, TILE_SIZE};
        if (!mask_at(g, g->water, x - 1, y, true)) b.x0 = 12;
        if (!mask_at(g, g->water, x + 1, y, true)) b.x1 = TILE_SIZE - 12;
        if (!mask_at(g, g->water, x, y - 1, true)) b.y0 = 12;
        if (!mask_at(g, g->water, x, y + 1, true)) b.y1 = TILE_SIZE - 12;
        add_box(world, x, y, b);
      } else if (g->path[i]) {
        world->ground[i] = autotile(g, g->path, x, y, TERRAIN_PATH, false);
        world->kind[i] = CELL_PATH;
      } else {
        float meadow = fbm(meadowSeed, (float)x / 10.0f, (float)y / 10.0f);
        float r = hashf(g->seed + 11u, x, y);
        if (meadow > 0.6f && r < 0.6f) {
          world->ground[i] = (Uint16)GID_GRASS_FLOWERS(hash(g->seed, x, y) % 3);
        } else if (r < 0.3f) {
          world->ground[i] = (Uint16)GID_GRASS_SPECKS(hash(g->seed, x, y) % 3);
        } else {
          world->ground[i] = GID_GRASS;
        }
      }
    }
  }
}

World *world_generate(Uint64 seed, int width, int height) {
  size_t cells = (size_t)width * (size_t)height;
  World *world = SDL_calloc(1, sizeof(*world));
  Gen g = {
      .world = world, .w = width, .h = height,
      .rng = seed, .seed = (Uint32)(seed ^ (seed >> 32)),
      .water = SDL_calloc(cells, 1), .nearWater = SDL_calloc(cells, 1),
      .path = SDL_calloc(cells, 1), .reserved = SDL_calloc(cells, 1),
      .route = SDL_malloc(MAX_ROUTE * sizeof(int)),
  };
  if (world) {
    *world = (World){
        .seed = seed, .width = width, .height = height,
        .ground = SDL_calloc(cells, sizeof(Uint16)), .top = SDL_calloc(cells, sizeof(Uint16)),
        .interior = SDL_calloc(cells, sizeof(Uint16)), .furniture = SDL_calloc(cells, sizeof(Uint16)),
        .solid = SDL_calloc(cells * BOXES_PER_CELL, sizeof(Box)), .kind = SDL_calloc(cells, 1),
        .houseAt = SDL_malloc(cells * sizeof(Sint16)),
    };
    for (size_t i = 0; world->houseAt && i < cells; i++) {
      world->houseAt[i] = -1;
    }
  }
  bool ok = world && world->ground && world->top && world->interior && world->furniture && world->solid &&
            world->kind && world->houseAt &&
            g.water && g.nearWater && g.path && g.reserved && g.route;

  if (ok) {
    make_lakes(&g);
    make_villages(&g);
    ok = world->villages && world->houses;
  }
  if (ok) {
    make_roads(&g);
    Uint8 *forbid = SDL_calloc(cells, 1);
    for (size_t i = 0; forbid && i < cells; i++) {
      forbid[i] = g.water[i] || g.reserved[i] == RES_HOUSE;
    }
    fill_pinches(&g, g.path, false, forbid);
    SDL_free(forbid);

    paint_terrain(&g);
    make_gardens(&g);
    make_signs(&g);
    decorate(&g);

    if (world->numVillages > 0) {
      const Village *v = &world->villages[0];
      world->spawnX = (float)((v->centerX + 1) * TILE_SIZE);
      world->spawnY = (float)((v->centerY + 1) * TILE_SIZE);
    } else {
      world->spawnX = (float)(width * TILE_SIZE / 2);
      world->spawnY = (float)(height * TILE_SIZE / 2);
    }
  }

  SDL_free(g.water);
  SDL_free(g.nearWater);
  SDL_free(g.path);
  SDL_free(g.reserved);
  SDL_free(g.route);
  if (!ok) {
    world_free(world);
    SDL_SetError("Out of memory");
    return NULL;
  }
  return world;
}
