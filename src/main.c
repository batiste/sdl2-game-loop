/*
  Copyright (C) 2013 batiste.bieler@gmail.com

  SDL game loop

  Uses SDL3's main callbacks: SDL calls SDL_AppIterate() once per frame and
  SDL_AppEvent() for every event. Physics runs at a fixed rate (TICK_NS) and
  rendering interpolates between the last two physics states, so the game
  plays at the same speed whether vsync is on or the frame rate is uncapped.

  The world is procedurally generated from a seed (see worldgen.c).
*/

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL_main.h>

#include "atlas.h"
#include "engine.h"
#include "npc.h"
#include "world.h"

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 600
#define WORLD_SIZE 160  // cells

#define TICK_NS SDL_MS_TO_NS(20)  // 50 physics ticks per second
#define MAX_FRAME_NS SDL_MS_TO_NS(250)

#define SPLASH_FADE_NS SDL_MS_TO_NS(850)
#define SPLASH_HOLD_NS SDL_MS_TO_NS(1000)
#define BANNER_NS SDL_MS_TO_NS(3000)

#define PLAYER_SPEED 5.0f  // pixels per tick
#define CHARACTER_OFFSET_X 24.0f  // feet position inside the 48x48 sprite
#define CHARACTER_OFFSET_Y 40.0f
#define GREETING_DISTANCE 64.0f

static const char *const GREETINGS[] = {"Hello!", "Nice day!", "Hi there!", "Welcome!", "Lovely weather.", "Oh, hi."};
#define NUM_GREETINGS ((int)SDL_arraysize(GREETINGS))

typedef enum { SCENE_SPLASH, SCENE_PLAYING } Scene;

// something drawn in the depth-sorted pass
typedef struct Drawable {
  float key;  // y of the base, lower is further away
  const Sprite *sprite;
  float x, y;
  bool flip;
} Drawable;

typedef struct Game {
  Engine engine;
  Atlas atlas;
  Scene scene;
  Uint64 sceneStart;

  SDL_Texture *splash;
  TTF_Font *font;
  TTF_Font *smallFont;
  MIX_Audio *swish;
  MIX_Track *sfxTrack;

  World *world;
  Npcs npcs;
  SDL_Texture *minimap;
  bool showMap;

  // player feet position, current and previous tick
  float x, y;
  float prevX, prevY;
  Direction facing;
  const Sprite *sprite;

  Uint64 tick;
  Uint64 lastTime;
  Uint64 accumulator;

  SDL_Texture *fpsText;
  int frames;
  Uint64 fpsTimer;
  SDL_Texture *infoText;
  SDL_Texture *bannerText;
  Uint64 bannerShownAt;
  int currentVillage;
  SDL_Texture *greetings[NUM_GREETINGS];
  SDL_Texture *oink;

  Drawable *drawables;
  int capDrawables;
} Game;

// ---------------------------------------------------------------- world

static void build_minimap(Game *game) {
  static const SDL_Color COLORS[] = {
      [CELL_GRASS] = {112, 200, 64, 255}, [CELL_FOREST] = {52, 120, 40, 255},
      [CELL_WATER] = {72, 152, 224, 255}, [CELL_PATH] = {222, 210, 120, 255},
      [CELL_HOUSE] = {200, 70, 40, 255},
  };
  const World *world = game->world;
  SDL_DestroyTexture(game->minimap);
  game->minimap = NULL;
  SDL_Surface *surface = SDL_CreateSurface(world->width, world->height, SDL_PIXELFORMAT_RGBA32);
  if (!surface) {
    return;
  }
  for (int y = 0; y < world->height; y++) {
    Uint8 *row = (Uint8 *)surface->pixels + y * surface->pitch;
    for (int x = 0; x < world->width; x++) {
      SDL_Color c = COLORS[world->kind[y * world->width + x]];
      row[x * 4 + 0] = c.r;
      row[x * 4 + 1] = c.g;
      row[x * 4 + 2] = c.b;
      row[x * 4 + 3] = 255;
    }
  }
  game->minimap = SDL_CreateTextureFromSurface(game->engine.renderer, surface);
  SDL_DestroySurface(surface);
}

static void set_text(Game *game, SDL_Texture **slot, TTF_Font *font, const char *text) {
  SDL_DestroyTexture(*slot);
  *slot = engine_render_text(&game->engine, font, text);
}

static bool new_world(Game *game, Uint64 seed) {
  Uint64 start = SDL_GetTicksNS();
  World *world = world_generate(seed, WORLD_SIZE, WORLD_SIZE);
  if (!world) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "World generation failed: %s", SDL_GetError());
    return false;
  }
  npcs_free(&game->npcs);
  world_free(game->world);
  game->world = world;
  if (!npcs_spawn(&game->npcs, world, seed)) {
    return false;
  }
  SDL_Log("World %" SDL_PRIu64 ": %d villages, %d houses, %d inhabitants, generated in %.1f ms",
          seed, world->numVillages, world->numHouses, game->npcs.count,
          (double)(SDL_GetTicksNS() - start) / (double)SDL_NS_PER_MS);

  game->x = game->prevX = world->spawnX;
  game->y = game->prevY = world->spawnY;
  game->facing = DIR_DOWN;
  game->currentVillage = -2;  // show the banner for the starting village
  build_minimap(game);

  char info[128];
  SDL_snprintf(info, sizeof(info), "Seed %" SDL_PRIu64 "   R: new world   M: map   C: vsync", seed);
  set_text(game, &game->infoText, game->smallFont, info);
  return true;
}

static Uint64 random_seed(void) {
  return SDL_rand_bits() % 1000000u;
}

// ---------------------------------------------------------------- update

static bool player_blocked(const Game *game, float x, float y) {
  return world_blocked(game->world, x - 8, y - 6, x + 8, y + 2);
}

static void update_banner(Game *game) {
  int village = world_village_at(game->world, game->x, game->y);
  if (village == game->currentVillage) {
    return;
  }
  game->currentVillage = village;
  if (village >= 0) {
    char text[64];
    SDL_snprintf(text, sizeof(text), "%s", game->world->villages[village].name);
    set_text(game, &game->bannerText, game->font, text);
    game->bannerShownAt = SDL_GetTicksNS();
  }
}

static void update(Game *game) {
  const bool *keys = SDL_GetKeyboardState(NULL);
  // scancodes are physical positions, so WASD also works on AZERTY keyboards
  bool up = keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP];
  bool left = keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT];
  bool down = keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN];
  bool right = keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT];
  bool sword = keys[SDL_SCANCODE_SPACE];

  game->prevX = game->x;
  game->prevY = game->y;

  float dx = (float)(right - left) * PLAYER_SPEED;
  float dy = (float)(down - up) * PLAYER_SPEED;
  if (dx != 0 && !player_blocked(game, game->x + dx, game->y)) {
    game->x += dx;
  }
  if (dy != 0 && !player_blocked(game, game->x, game->y + dy)) {
    game->y += dy;
  }

  bool moving = up || down || left || right;
  if (up) game->facing = DIR_UP;
  if (down) game->facing = DIR_DOWN;
  if (left) game->facing = DIR_LEFT;
  if (right) game->facing = DIR_RIGHT;

  const CharacterSheet *sheet = &game->atlas.player;
  game->sprite = animation_frame(sheet->walk[game->facing], moving ? game->tick : 0);
  if (sword) {
    const Animation *swing = game->facing == DIR_LEFT ? sheet->swordLeft : sheet->swordRight;
    game->sprite = animation_frame(swing, game->tick);
    if (game->sfxTrack && !MIX_TrackPlaying(game->sfxTrack)) {
      MIX_PlayTrack(game->sfxTrack, 0);
    }
  }

  npcs_update(&game->npcs, game->world);
  update_banner(game);
  game->tick++;
}

// ---------------------------------------------------------------- render

static bool is_flat(Uint16 gid) {
  return gid == GID_DECO(DECO_RED_FLOWERS) || gid == GID_DECO(DECO_PURPLE_FLOWERS) ||
         gid == GID_DECO(DECO_PEBBLES) || gid == GID_DECO(DECO_LILY) ||
         gid == GID_DECO(DECO_LILY_FLOWER) || GID_IS_DOOR_STEP(gid);
}

static bool is_building(Uint16 gid) {
  return gid >= GID_TILES(0, 0) && gid < GID_NATURE(0, 0);
}

static void push_drawable(Game *game, int *count, Drawable d) {
  if (*count == game->capDrawables) {
    int cap = game->capDrawables ? game->capDrawables * 2 : 512;
    Drawable *grown = SDL_realloc(game->drawables, (size_t)cap * sizeof(*grown));
    if (!grown) {
      return;
    }
    game->drawables = grown;
    game->capDrawables = cap;
  }
  game->drawables[(*count)++] = d;
}

static int SDLCALL compare_drawables(const void *a, const void *b) {
  float ka = ((const Drawable *)a)->key, kb = ((const Drawable *)b)->key;
  return (ka > kb) - (ka < kb);
}

static float lerp(float a, float b, float t) {
  return a + (b - a) * t;
}

static void render_world(Game *game, float camX, float camY, float t) {
  SDL_Renderer *renderer = game->engine.renderer;
  const World *world = game->world;
  const Sprite *tiles = game->atlas.tiles;

  int col0 = SDL_max(0, (int)SDL_floorf(camX / TILE_SIZE));
  int row0 = SDL_max(0, (int)SDL_floorf(camY / TILE_SIZE));
  int col1 = SDL_min(world->width - 1, (int)((camX + (float)game->engine.width) / TILE_SIZE));
  int row1 = SDL_min(world->height - 1, (int)((camY + (float)game->engine.height) / TILE_SIZE));

  // ground, then flat decorations that everything walks over
  for (int pass = 0; pass < 2; pass++) {
    for (int row = row0; row <= row1; row++) {
      for (int col = col0; col <= col1; col++) {
        int i = row * world->width + col;
        Uint16 gid = pass == 0 ? world->ground[i] : world->top[i];
        if (gid && (pass == 0 || is_flat(gid))) {
          sprite_draw(renderer, &tiles[gid], (float)(col * TILE_SIZE) - camX, (float)(row * TILE_SIZE) - camY);
        }
      }
    }
  }

  // everything with height is sorted by the y of its base
  int count = 0;
  for (int row = row0; row <= row1; row++) {
    for (int col = col0; col <= col1; col++) {
      Uint16 gid = world->top[row * world->width + col];
      if (!gid || is_flat(gid)) {
        continue;
      }
      float base = is_building(gid) ? TILE_SIZE : 40;
      push_drawable(game, &count, (Drawable){
          .key = (float)(row * TILE_SIZE) + base, .sprite = &tiles[gid],
          .x = (float)(col * TILE_SIZE) - camX, .y = (float)(row * TILE_SIZE) - camY,
      });
    }
  }

  for (int i = 0; i < game->npcs.count; i++) {
    const Npc *npc = &game->npcs.items[i];
    float x = lerp(npc->prevX, npc->x, t), y = lerp(npc->prevY, npc->y, t);
    if (!npc_visible(npc) || x < camX - TILE_SIZE || y < camY - TILE_SIZE ||
        x > camX + (float)game->engine.width + TILE_SIZE || y > camY + (float)game->engine.height + TILE_SIZE) {
      continue;
    }
    bool flip;
    SDL_FPoint offset;
    const Sprite *sprite = npc_sprite(npc, &game->atlas, game->tick, &flip, &offset);
    push_drawable(game, &count, (Drawable){
        .key = y, .sprite = sprite, .flip = flip,
        .x = SDL_roundf(x + offset.x - camX), .y = SDL_roundf(y + offset.y - camY),
    });
  }

  float px = lerp(game->prevX, game->x, t), py = lerp(game->prevY, game->y, t);
  push_drawable(game, &count, (Drawable){
      .key = py, .sprite = game->sprite,
      .x = SDL_roundf(px - CHARACTER_OFFSET_X - camX), .y = SDL_roundf(py - CHARACTER_OFFSET_Y - camY),
  });

  SDL_qsort(game->drawables, (size_t)count, sizeof(Drawable), compare_drawables);
  for (int i = 0; i < count; i++) {
    const Drawable *d = &game->drawables[i];
    sprite_draw_flipped(renderer, d->sprite, d->x, d->y, d->flip);
  }

  // the closest villager or pig says something
  int closest = -1;
  float best = GREETING_DISTANCE * GREETING_DISTANCE;
  for (int i = 0; i < game->npcs.count; i++) {
    const Npc *npc = &game->npcs.items[i];
    float dx = npc->x - game->x, dy = npc->y - game->y;
    if (npc_visible(npc) && dx * dx + dy * dy < best) {
      best = dx * dx + dy * dy;
      closest = i;
    }
  }
  if (closest >= 0) {
    const Npc *npc = &game->npcs.items[closest];
    SDL_Texture *text = npc->kind == NPC_PIG ? game->oink : game->greetings[closest % NUM_GREETINGS];
    if (text) {
      SDL_FRect dest;
      SDL_GetTextureSize(text, &dest.w, &dest.h);
      float top = npc->kind == NPC_PIG ? 30 : 50;
      dest.x = SDL_roundf(lerp(npc->prevX, npc->x, t) - camX - dest.w / 2);
      dest.y = SDL_roundf(lerp(npc->prevY, npc->y, t) - camY - top - dest.h);
      SDL_FRect bubble = {dest.x - 6, dest.y - 3, dest.w + 10, dest.h + 6};
      SDL_SetRenderDrawColor(renderer, 58, 41, 41, 255);
      SDL_RenderFillRect(renderer, &(SDL_FRect){bubble.x - 2, bubble.y - 2, bubble.w + 4, bubble.h + 4});
      SDL_RenderFillRect(renderer, &(SDL_FRect){bubble.x + bubble.w / 2 - 4, bubble.y + bubble.h, 8, 6});
      SDL_SetRenderDrawColor(renderer, 246, 239, 158, 255);
      SDL_RenderFillRect(renderer, &bubble);
      SDL_RenderFillRect(renderer, &(SDL_FRect){bubble.x + bubble.w / 2 - 2, bubble.y + bubble.h, 4, 4});
      SDL_RenderTexture(renderer, text, NULL, &dest);
    }
  }
}

static void render_minimap(Game *game, float t) {
  if (!game->showMap || !game->minimap) {
    return;
  }
  SDL_Renderer *renderer = game->engine.renderer;
  const World *world = game->world;
  float scale = 200.0f / (float)world->width;
  SDL_FRect map = {(float)game->engine.width - (float)world->width * scale - 12, 12,
                   (float)world->width * scale, (float)world->height * scale};
  SDL_SetRenderDrawColor(renderer, 34, 34, 34, 255);
  SDL_RenderFillRect(renderer, &(SDL_FRect){map.x - 2, map.y - 2, map.w + 4, map.h + 4});
  SDL_SetTextureAlphaModFloat(game->minimap, 0.9f);
  SDL_RenderTexture(renderer, game->minimap, NULL, &map);

  SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
  for (int i = 0; i < game->npcs.count; i++) {
    const Npc *npc = &game->npcs.items[i];
    if (npc_visible(npc) && npc->kind == NPC_VILLAGER) {
      SDL_RenderFillRect(renderer, &(SDL_FRect){map.x + npc->x / TILE_SIZE * scale - 0.5f,
                                                map.y + npc->y / TILE_SIZE * scale - 0.5f, 1.5f, 1.5f});
    }
  }
  float px = lerp(game->prevX, game->x, t) / TILE_SIZE * scale;
  float py = lerp(game->prevY, game->y, t) / TILE_SIZE * scale;
  SDL_SetRenderDrawColor(renderer, 34, 34, 34, 255);
  SDL_RenderFillRect(renderer, &(SDL_FRect){map.x + px - 3, map.y + py - 3, 6, 6});
  SDL_SetRenderDrawColor(renderer, 230, 40, 40, 255);
  SDL_RenderFillRect(renderer, &(SDL_FRect){map.x + px - 2, map.y + py - 2, 4, 4});
}

static void render_hud(Game *game, Uint64 now) {
  SDL_Renderer *renderer = game->engine.renderer;
  game->frames++;
  if (now - game->fpsTimer >= SDL_NS_PER_SECOND || !game->fpsText) {
    char buffer[64];
    SDL_snprintf(buffer, sizeof(buffer), "FPS: %d%s", game->frames, game->engine.vsync ? "" : " (uncapped)");
    set_text(game, &game->fpsText, game->smallFont, buffer);
    game->frames = 0;
    game->fpsTimer = now;
  }

  SDL_FRect dest;
  if (game->fpsText) {
    dest = (SDL_FRect){12, 10, 0, 0};
    SDL_GetTextureSize(game->fpsText, &dest.w, &dest.h);
    SDL_RenderTexture(renderer, game->fpsText, NULL, &dest);
  }
  if (game->infoText) {
    SDL_GetTextureSize(game->infoText, &dest.w, &dest.h);
    dest.x = 12;
    dest.y = (float)game->engine.height - dest.h - 8;
    SDL_RenderTexture(renderer, game->infoText, NULL, &dest);
  }
  Uint64 shown = now - game->bannerShownAt;
  if (game->bannerText && shown < BANNER_NS) {
    float fade = SDL_min(1.0f, (float)(BANNER_NS - shown) / (float)SDL_MS_TO_NS(500));
    SDL_GetTextureSize(game->bannerText, &dest.w, &dest.h);
    dest.x = SDL_roundf(((float)game->engine.width - dest.w) / 2);
    dest.y = 48;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 34, 34, 34, (Uint8)(150 * fade));
    SDL_RenderFillRect(renderer, &(SDL_FRect){dest.x - 24, dest.y - 8, dest.w + 48, dest.h + 16});
    SDL_SetTextureAlphaModFloat(game->bannerText, fade);
    SDL_RenderTexture(renderer, game->bannerText, NULL, &dest);
  }
}

static void render_splash(Game *game, Uint64 elapsed) {
  SDL_Renderer *renderer = game->engine.renderer;
  float alpha = SDL_min(1.0f, (float)elapsed / (float)SPLASH_FADE_NS);
  float w, h;
  SDL_GetTextureSize(game->splash, &w, &h);
  SDL_FRect dest = {((float)game->engine.width - w) / 2, ((float)game->engine.height - h) / 2, w, h};

  SDL_SetRenderDrawColor(renderer, 0xff, 0xff, 0xff, 0xff);
  SDL_RenderClear(renderer);
  SDL_SetTextureAlphaModFloat(game->splash, alpha);
  SDL_RenderTexture(renderer, game->splash, NULL, &dest);
}

static void render_game(Game *game, Uint64 now) {
  const World *world = game->world;
  // interpolate between the last two physics ticks for smooth motion
  float t = (float)game->accumulator / (float)TICK_NS;
  float x = lerp(game->prevX, game->x, t), y = lerp(game->prevY, game->y, t);

  // camera follows the player, without showing outside the world
  float maxCamX = (float)(world->width * TILE_SIZE - game->engine.width);
  float maxCamY = (float)(world->height * TILE_SIZE - game->engine.height);
  float camX = SDL_roundf(SDL_clamp(x - (float)game->engine.width / 2, 0, maxCamX));
  float camY = SDL_roundf(SDL_clamp(y - 16 - (float)game->engine.height / 2, 0, maxCamY));

  SDL_SetRenderDrawColor(game->engine.renderer, 0x70, 0xc8, 0x40, 0xff);
  SDL_RenderClear(game->engine.renderer);
  render_world(game, camX, camY, t);
  render_minimap(game, t);
  render_hud(game, now);
}

static void start_game(Game *game, Uint64 now) {
  game->scene = SCENE_PLAYING;
  game->sceneStart = now;
  game->lastTime = now;
  game->accumulator = 0;
  game->fpsTimer = now;
  game->bannerShownAt = now;
}

// ---------------------------------------------------------------- callbacks

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
  SDL_SetAppMetadata("Game loop example", "3.0", "com.github.batiste.sdl-game-loop");

  Game *game = SDL_calloc(1, sizeof(*game));
  if (!game) {
    return SDL_APP_FAILURE;
  }
  *appstate = game;

  if (!engine_init(&game->engine, "Game example", SCREEN_WIDTH, SCREEN_HEIGHT)) {
    return SDL_APP_FAILURE;
  }

  Uint64 seed = random_seed();
  for (int i = 1; i < argc; i++) {
    if (SDL_strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
      seed = SDL_strtoull(argv[++i], NULL, 10);
    }
  }

  Engine *engine = &game->engine;
  game->splash = engine_load_texture(engine, "splash.png");
  game->font = engine_load_font(engine, "yoster.ttf", 26);
  game->smallFont = engine_load_font(engine, "yoster.ttf", 16);
  if (!game->splash || !game->font || !game->smallFont || !atlas_load(&game->atlas, engine)) {
    return SDL_APP_FAILURE;
  }
  for (int i = 0; i < NUM_GREETINGS; i++) {
    game->greetings[i] = engine_render_text(engine, game->smallFont, GREETINGS[i]);
  }
  game->oink = engine_render_text(engine, game->smallFont, "Oink!");

  game->swish = engine_load_audio(engine, "swish-2.ogg");
  if (game->swish) {
    game->sfxTrack = MIX_CreateTrack(engine->mixer);
    MIX_SetTrackAudio(game->sfxTrack, game->swish);
  }

  if (!new_world(game, seed)) {
    return SDL_APP_FAILURE;
  }
  game->sprite = animation_frame(game->atlas.player.walk[DIR_DOWN], 0);
  game->showMap = true;

  game->scene = SCENE_SPLASH;
  game->sceneStart = SDL_GetTicksNS();
  return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
  Game *game = appstate;

  switch (event->type) {
    case SDL_EVENT_QUIT:
      return SDL_APP_SUCCESS;

    case SDL_EVENT_KEY_DOWN:
      if (event->key.repeat) {
        break;
      }
      if (event->key.scancode == SDL_SCANCODE_ESCAPE) {
        return SDL_APP_SUCCESS;
      }
      if (game->scene == SCENE_SPLASH) {
        start_game(game, SDL_GetTicksNS());
        break;
      }
      switch (event->key.scancode) {
        case SDL_SCANCODE_C:
          engine_set_vsync(&game->engine, !game->engine.vsync);
          game->fpsTimer = 0;  // refresh the text right away
          break;
        case SDL_SCANCODE_M:
          game->showMap = !game->showMap;
          break;
        case SDL_SCANCODE_R:
          if (!new_world(game, random_seed())) {
            return SDL_APP_FAILURE;
          }
          break;
        default:
          break;
      }
      break;

    default:
      break;
  }
  return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
  Game *game = appstate;
  Uint64 now = SDL_GetTicksNS();

  if (game->scene == SCENE_SPLASH) {
    Uint64 elapsed = now - game->sceneStart;
    if (elapsed >= SPLASH_FADE_NS + SPLASH_HOLD_NS) {
      start_game(game, now);
    } else {
      render_splash(game, elapsed);
    }
  }

  if (game->scene == SCENE_PLAYING) {
    // never try to catch up more than MAX_FRAME_NS, e.g. after a breakpoint
    game->accumulator += SDL_min(now - game->lastTime, MAX_FRAME_NS);
    game->lastTime = now;
    while (game->accumulator >= TICK_NS) {
      update(game);
      game->accumulator -= TICK_NS;
    }
    render_game(game, now);
  }

  SDL_RenderPresent(game->engine.renderer);
  return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
  (void)result;
  Game *game = appstate;
  if (!game) {
    return;
  }
  if (game->sfxTrack) {
    MIX_DestroyTrack(game->sfxTrack);
  }
  SDL_DestroyTexture(game->fpsText);
  SDL_DestroyTexture(game->infoText);
  SDL_DestroyTexture(game->bannerText);
  SDL_DestroyTexture(game->minimap);
  for (int i = 0; i < NUM_GREETINGS; i++) {
    SDL_DestroyTexture(game->greetings[i]);
  }
  SDL_DestroyTexture(game->oink);
  SDL_free(game->drawables);
  npcs_free(&game->npcs);
  world_free(game->world);
  atlas_free(&game->atlas);
  engine_shutdown(&game->engine);
  SDL_free(game);
}
