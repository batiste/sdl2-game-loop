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
// copy both frames side by side and make the white transparent.
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
