/*
  Copyright (C) 2013 batiste.bieler@gmail.com

  All the game graphics: tiles indexed by id, character and animal animations
 */

#include "atlas.h"

#define CHARACTER_CELL 48

// shirt colours for the villagers, the player keeps the original purple
static const SDL_Color SHIRTS[NUM_VILLAGER_LOOKS] = {
    {214, 72, 56, 255}, {72, 118, 218, 255}, {84, 170, 72, 255},
    {232, 150, 48, 255}, {64, 178, 170, 255}, {150, 98, 60, 255},
};

// The buildings in tiles.png are drawn on grass: make it transparent so they
// sit on whatever ground is generated around them.
static void clear_grass(SDL_Surface *surface) {
  for (int y = 0; y < surface->h; y++) {
    Uint8 *p = (Uint8 *)surface->pixels + y * surface->pitch;
    for (int x = 0; x < surface->w; x++, p += 4) {
      bool grass = p[0] == 112 && p[1] == 200 && p[2] == 64;
      bool grid = p[0] == 103 && p[1] == 185 && p[2] == 59;
      if (grass || grid) {
        p[3] = 0;
      }
    }
  }
}

static bool add_tileset(Atlas *atlas, Engine *engine, const char *file, int firstGid, bool transparentGrass) {
  SDL_Surface *surface = engine_load_surface(file);
  if (!surface) {
    return false;
  }
  if (transparentGrass) {
    clear_grass(surface);
  }
  SDL_Texture *texture = engine_texture_from_surface(engine, surface);
  int columns = surface->w / TILE_SIZE, rows = surface->h / TILE_SIZE;
  SDL_DestroySurface(surface);
  if (!texture) {
    return false;
  }
  for (int i = 0; i < columns * rows && firstGid + i < GID_COUNT; i++) {
    atlas->tiles[firstGid + i] = (Sprite){
        .texture = texture,
        .source = {(float)(i % columns * TILE_SIZE), (float)(i / columns * TILE_SIZE), TILE_SIZE, TILE_SIZE},
    };
  }
  return true;
}

// ---------------------------------------------------------------- houses

// Change of colour applied to one material, keeping the shading.
typedef struct Tint {
  float hue;  // degrees, negative keeps the original hue
  float sat;  // saturation multiplier
  float val;  // brightness multiplier
} Tint;

// kept close to the original palette so villages stay harmonious
#define KEEP {-1, 1, 1}
static const Tint ROOF_RED = KEEP, ROOF_RUST = {14, 0.85f, 0.8f}, ROOF_TERRACOTTA = {24, 0.75f, 0.88f},
                  ROOF_UMBER = {28, 0.5f, 0.62f}, ROOF_SLATE = {215, 0.22f, 0.72f},
                  ROOF_CHARCOAL = {-1, 0.12f, 0.68f}, ROOF_MOSS = {85, 0.3f, 0.62f};
static const Tint WALL_BEIGE = KEEP, WALL_PLASTER = {48, 0.35f, 1.05f}, WALL_STONE = {45, 0.18f, 0.85f},
                  WALL_TIMBER = {32, 0.55f, 0.8f};
static const Tint FLOOR_BRICK = KEEP, FLOOR_WOOD = {30, 0.6f, 0.8f}, FLOOR_STONE = {-1, 0.15f, 0.85f};
static const Tint STRAW = {44, 0.62f, 1.0f}, OLD_STRAW = {38, 0.42f, 0.82f};
#undef KEEP

typedef struct HouseStyle {
  Tint roof, wall, floor, thatch;
} HouseStyle;

static const HouseStyle STYLES[NUM_HOUSE_STYLES] = {
    {ROOF_RED, WALL_BEIGE, FLOOR_BRICK, STRAW},         {ROOF_RED, WALL_PLASTER, FLOOR_WOOD, STRAW},
    {ROOF_TERRACOTTA, WALL_BEIGE, FLOOR_BRICK, STRAW},  {ROOF_TERRACOTTA, WALL_TIMBER, FLOOR_WOOD, OLD_STRAW},
    {ROOF_RUST, WALL_PLASTER, FLOOR_WOOD, STRAW},       {ROOF_RUST, WALL_STONE, FLOOR_STONE, OLD_STRAW},
    {ROOF_SLATE, WALL_PLASTER, FLOOR_STONE, OLD_STRAW}, {ROOF_SLATE, WALL_STONE, FLOOR_STONE, OLD_STRAW},
    {ROOF_CHARCOAL, WALL_STONE, FLOOR_STONE, OLD_STRAW}, {ROOF_CHARCOAL, WALL_PLASTER, FLOOR_BRICK, STRAW},
    {ROOF_UMBER, WALL_TIMBER, FLOOR_WOOD, OLD_STRAW},   {ROOF_MOSS, WALL_BEIGE, FLOOR_WOOD, STRAW},
};

static void rgb_to_hsv(const Uint8 *p, float *h, float *s, float *v) {
  float r = p[0] / 255.0f, g = p[1] / 255.0f, b = p[2] / 255.0f;
  float max = SDL_max(r, SDL_max(g, b)), min = SDL_min(r, SDL_min(g, b)), d = max - min;
  *v = max;
  *s = max > 0 ? d / max : 0;
  if (d == 0) *h = 0;
  else if (max == r) *h = 60 * SDL_fmodf((g - b) / d + 6, 6);
  else if (max == g) *h = 60 * ((b - r) / d + 2);
  else *h = 60 * ((r - g) / d + 4);
}

static void hsv_to_rgb(float h, float s, float v, Uint8 *p) {
  float c = v * s, x = c * (1 - SDL_fabsf(SDL_fmodf(h / 60, 2) - 1)), m = v - c;
  float r = 0, g = 0, b = 0;
  if (h < 60) r = c, g = x;
  else if (h < 120) r = x, g = c;
  else if (h < 180) g = c, b = x;
  else if (h < 240) g = x, b = c;
  else if (h < 300) r = x, b = c;
  else r = c, b = x;
  p[0] = (Uint8)SDL_clamp((r + m) * 255 + 0.5f, 0, 255);
  p[1] = (Uint8)SDL_clamp((g + m) * 255 + 0.5f, 0, 255);
  p[2] = (Uint8)SDL_clamp((b + m) * 255 + 0.5f, 0, 255);
}

static void apply_tint(Uint8 *p, Tint t) {
  float h, s, v;
  rgb_to_hsv(p, &h, &s, &v);
  hsv_to_rgb(t.hue >= 0 ? t.hue : h, SDL_min(1.0f, s * t.sat), SDL_min(1.0f, v * t.val), p);
}

// Copy a tile, recolouring the yellow walls with `wall` and the red/brown
// roof or floor with `other`. Outlines and greys are kept.
static void tint_tile(SDL_Surface *dst, int dx, int dy, const SDL_Surface *src, int sx, int sy, Tint wall, Tint other) {
  for (int y = 0; y < TILE_SIZE; y++) {
    const Uint8 *in = (const Uint8 *)src->pixels + (sy + y) * src->pitch + sx * 4;
    Uint8 *out = (Uint8 *)dst->pixels + (dy + y) * dst->pitch + dx * 4;
    for (int x = 0; x < TILE_SIZE; x++, in += 4, out += 4) {
      SDL_memcpy(out, in, 4);
      float h, s, v;
      rgb_to_hsv(in, &h, &s, &v);
      if (in[3] == 0 || v < 0.26f || s < 0.15f) {
        continue;
      }
      apply_tint(out, h >= 40 && h <= 70 ? wall : other);
    }
  }
}

static bool build_houses(Atlas *atlas, Engine *engine) {
  SDL_Surface *src = engine_load_surface("tiles.png");
  SDL_Surface *shaped = engine_load_surface("roofs.png");
  SDL_Surface *sheet = SDL_CreateSurface(HOUSE_PIECES * TILE_SIZE, NUM_HOUSE_STYLES * TILE_SIZE,
                                         SDL_PIXELFORMAT_RGBA32);
  bool ok = src && shaped && sheet;
  if (ok) {
    clear_grass(src);
    for (int style = 0; style < NUM_HOUSE_STYLES; style++) {
      const HouseStyle *st = &STYLES[style];
      int y = style * TILE_SIZE;
      for (int piece = 0; piece < HOUSE_PIECES; piece++) {
        // where each piece comes from in tiles.png or roofs.png
        int col, row;
        const SDL_Surface *from = src;
        Tint other = st->floor;
        if (piece < PIECE_SMALL_ROOF(0, 0)) col = 1 + piece % 4, row = 7 + piece / 4, other = st->roof;
        else if (piece < PIECE_ROOM(0, 0)) col = 13 + (piece - 12) % 3, row = 1 + (piece - 12) / 3, other = st->roof;
        else if (piece < PIECE_SMALL_ROOM(0, 0)) col = 1 + (piece - 21) % 4, row = 1 + (piece - 21) / 4;
        else if (piece < PIECE_SHAPED(0, 0, 0)) col = 10 + (piece - 37) % 3, row = 1 + (piece - 37) / 3;
        else if (piece < PIECE_SHAPED_WINDOW(0)) {
          int k = piece - PIECE_SHAPED(0, 0, 0), shape = k / 20;
          col = shape * 4 + k % 4, row = (k % 20) / 4;
          from = shaped;
          other = shape == 1 ? st->thatch : st->roof;
        } else {
          // the extra facades on the last row of roofs.png
          from = shaped;
          row = 5;
          if (piece == PIECE_SHAPED_WINDOW(0)) col = 0, other = st->roof;
          else if (piece == PIECE_SHAPED_WINDOW(1)) col = 4, other = st->thatch;
          else if (piece == PIECE_ROOF_DOOR) col = 1, other = st->roof;
          else col = 2;
        }
        tint_tile(sheet, piece * TILE_SIZE, y, from, col * TILE_SIZE, row * TILE_SIZE, st->wall, other);
      }
      Uint8 red[4] = {219, 78, 29, 255};
      apply_tint(red, st->roof);
      atlas->roofColors[style] = (SDL_Color){red[0], red[1], red[2], 255};
    }
    SDL_Texture *texture = engine_texture_from_surface(engine, sheet);
    ok = texture != NULL;
    for (int style = 0; ok && style < NUM_HOUSE_STYLES; style++) {
      for (int piece = 0; piece < HOUSE_PIECES; piece++) {
        atlas->tiles[GID_HOUSE(style, piece)] = (Sprite){
            .texture = texture,
            .source = {(float)(piece * TILE_SIZE), (float)(style * TILE_SIZE), TILE_SIZE, TILE_SIZE},
        };
      }
    }
  }
  SDL_DestroySurface(sheet);
  SDL_DestroySurface(shaped);
  SDL_DestroySurface(src);
  return ok;
}

// ---------------------------------------------------------------- characters

static bool load_sheet(CharacterSheet *sheet, SDL_Texture *texture) {
  SDL_Rect cell = {0, 0, CHARACTER_CELL, CHARACTER_CELL};
  bool ok = true;
  for (int dir = 0; dir < DIR_COUNT; dir++) {
    cell.y = dir * CHARACTER_CELL;
    sheet->walk[dir] = animation_create(texture, cell, 4, 4);
    ok = ok && sheet->walk[dir];
  }
  cell.y = 4 * CHARACTER_CELL;
  sheet->swordRight = animation_create(texture, cell, 2, 6);
  cell.y = 5 * CHARACTER_CELL;
  sheet->swordLeft = animation_create(texture, cell, 2, 6);
  return ok && sheet->swordRight && sheet->swordLeft;
}

static void free_sheet(CharacterSheet *sheet) {
  for (int dir = 0; dir < DIR_COUNT; dir++) {
    animation_destroy(sheet->walk[dir]);
  }
  animation_destroy(sheet->swordRight);
  animation_destroy(sheet->swordLeft);
  *sheet = (CharacterSheet){0};
}

static bool is_shirt(Uint8 r, Uint8 g, Uint8 b) {
  return b > r && r > g && b - g > 50;
}

static void recolor_shirt(SDL_Surface *dst, const SDL_Surface *src, SDL_Color shirt) {
  for (int y = 0; y < src->h; y++) {
    const Uint8 *in = (const Uint8 *)src->pixels + y * src->pitch;
    Uint8 *out = (Uint8 *)dst->pixels + y * dst->pitch;
    for (int x = 0; x < src->w; x++, in += 4, out += 4) {
      SDL_memcpy(out, in, 4);
      if (in[3] && is_shirt(in[0], in[1], in[2])) {
        // keep the shading: scale the new colour by the pixel brightness
        float shade = (float)(in[0] + in[1] + in[2]) / (166.0f + 122.0f + 221.0f);
        out[0] = (Uint8)SDL_min(255.0f, (float)shirt.r * shade);
        out[1] = (Uint8)SDL_min(255.0f, (float)shirt.g * shade);
        out[2] = (Uint8)SDL_min(255.0f, (float)shirt.b * shade);
      }
    }
  }
}

static bool load_characters(Atlas *atlas, Engine *engine) {
  SDL_Surface *base = engine_load_surface("character.png");
  if (!base) {
    return false;
  }
  bool ok = load_sheet(&atlas->player, engine_texture_from_surface(engine, base));

  SDL_Surface *recolored = SDL_CreateSurface(base->w, base->h, SDL_PIXELFORMAT_RGBA32);
  for (int i = 0; ok && recolored && i < NUM_VILLAGER_LOOKS; i++) {
    recolor_shirt(recolored, base, SHIRTS[i]);
    SDL_Texture *texture = engine_texture_from_surface(engine, recolored);
    ok = texture && load_sheet(&atlas->villagers[i], texture);
  }
  SDL_DestroySurface(recolored);
  SDL_DestroySurface(base);
  return ok;
}

// The pig in tiles.png (column 15, rows 6-7) sits on a white square:
// copy both frames side by side and make the background transparent.
static bool load_pig(Atlas *atlas, Engine *engine) {
  SDL_Surface *tiles = engine_load_surface("tiles.png");
  SDL_Surface *pig = SDL_CreateSurface(2 * TILE_SIZE, TILE_SIZE, SDL_PIXELFORMAT_RGBA32);
  bool ok = tiles && pig;
  if (ok) {
    for (int frame = 0; frame < 2; frame++) {
      SDL_Rect src = {15 * TILE_SIZE, (6 + frame) * TILE_SIZE, TILE_SIZE, TILE_SIZE};
      SDL_BlitSurface(tiles, &src, pig, &(SDL_Rect){frame * TILE_SIZE, 0, TILE_SIZE, TILE_SIZE});
    }
    for (int y = 0; y < pig->h; y++) {
      Uint8 *p = (Uint8 *)pig->pixels + y * pig->pitch;
      for (int x = 0; x < pig->w; x++, p += 4) {
        if (p[0] > 240 && p[1] > 240 && p[2] > 240) {
          p[3] = 0;
        }
      }
    }
    clear_grass(pig);  // the cell also has the grid line on two sides
    SDL_Texture *texture = engine_texture_from_surface(engine, pig);
    atlas->pig = texture ? animation_create(texture, (SDL_Rect){0, 0, TILE_SIZE, TILE_SIZE}, 8, 2) : NULL;
    ok = atlas->pig != NULL;
  }
  SDL_DestroySurface(pig);
  SDL_DestroySurface(tiles);
  return ok;
}

bool atlas_load(Atlas *atlas, Engine *engine) {
  *atlas = (Atlas){0};
  return add_tileset(atlas, engine, "ground.png", GID_GROUND(0), false) &&
         add_tileset(atlas, engine, "tiles.png", GID_TILES(0, 0), true) &&
         add_tileset(atlas, engine, "nature.png", GID_NATURE(0, 0), false) &&
         build_houses(atlas, engine) &&
         add_tileset(atlas, engine, "furniture.png", GID_FURNITURE(0), false) &&
         load_characters(atlas, engine) &&
         load_pig(atlas, engine);
}

void atlas_free(Atlas *atlas) {
  free_sheet(&atlas->player);
  for (int i = 0; i < NUM_VILLAGER_LOOKS; i++) {
    free_sheet(&atlas->villagers[i]);
  }
  animation_destroy(atlas->pig);
  atlas->pig = NULL;
}
