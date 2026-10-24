/*
  Copyright (C) 2013 batiste.bieler@gmail.com

  SDL game loop

  Uses SDL3's main callbacks: SDL calls SDL_AppIterate() once per frame and
  SDL_AppEvent() for every event. Physics runs at a fixed rate (TICK_NS) and
  rendering interpolates between the last two physics states, so the game
  plays at the same speed whether vsync is on or the frame rate is uncapped.
*/

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL_main.h>

#include "engine.h"
#include "sprite.h"
#include "tmx.h"

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 600

#define TICK_NS SDL_MS_TO_NS(20)  // 50 physics ticks per second
#define MAX_FRAME_NS SDL_MS_TO_NS(250)

#define SPLASH_FADE_NS SDL_MS_TO_NS(850)
#define SPLASH_HOLD_NS SDL_MS_TO_NS(1000)

#define CHARACTER_SIZE 48
#define CHARACTER_SPEED 5.0f  // pixels per tick
// collision is tested at the character's feet
#define FEET_X 24.0f
#define FEET_Y 40.0f

typedef enum { SCENE_SPLASH, SCENE_PLAYING } Scene;

typedef enum { DIR_DOWN, DIR_RIGHT, DIR_UP, DIR_LEFT, DIR_COUNT } Direction;

typedef struct Game {
  Engine engine;
  Scene scene;
  Uint64 sceneStart;

  SDL_Texture *splash;
  TTF_Font *font;
  MIX_Audio *swish;
  MIX_Track *sfxTrack;

  TmxMap *map;
  const TmxObjectGroup *collisions;
  Sprite *tiles;  // indexed by gid

  Animation *stand;
  Animation *walk[DIR_COUNT];
  Animation *swordRight;
  Animation *swordLeft;

  // character top-left corner in world coordinates, current and previous tick
  float x, y;
  float prevX, prevY;
  const Sprite *sprite;

  Uint64 tick;
  Uint64 lastTime;
  Uint64 accumulator;

  SDL_Texture *fpsText;
  int frames;
  Uint64 fpsTimer;
} Game;

static bool has_collision(const Game *game, float x, float y) {
  if (!game->collisions) {
    return false;
  }
  for (int i = 0; i < game->collisions->numObjects; i++) {
    const TmxObject *o = &game->collisions->objects[i];
    if (x >= o->x && y >= o->y && x <= o->x + o->width && y <= o->y + o->height) {
      return true;
    }
  }
  return false;
}

static bool load_tiles(Game *game) {
  const TmxMap *map = game->map;
  game->tiles = SDL_calloc((size_t)map->maxGid + 1, sizeof(*game->tiles));
  if (!game->tiles) {
    return false;
  }
  for (int i = 0; i < map->numTilesets; i++) {
    const TmxTileset *set = &map->tilesets[i];
    SDL_Texture *texture = engine_load_texture(&game->engine, set->image);
    if (!texture) {
      return false;
    }
    for (int t = 0; t < set->numTiles; t++) {
      int col = t % set->columns;
      int row = t / set->columns;
      game->tiles[set->firstgid + t] = (Sprite){
          .texture = texture,
          .source = {(float)(set->margin + col * (set->tilewidth + set->spacing)),
                     (float)(set->margin + row * (set->tileheight + set->spacing)),
                     (float)set->tilewidth, (float)set->tileheight},
      };
    }
  }
  return true;
}

static bool load_animations(Game *game, SDL_Texture *sheet) {
  SDL_Rect cell = {0, 0, CHARACTER_SIZE, CHARACTER_SIZE};
  game->stand = animation_create(sheet, cell, 4, 1);
  // the sprite sheet rows are: down, right, up, left, sword right, sword left
  for (int dir = 0; dir < DIR_COUNT; dir++) {
    cell.y = dir * CHARACTER_SIZE;
    game->walk[dir] = animation_create(sheet, cell, 4, 4);
  }
  cell.y = 4 * CHARACTER_SIZE;
  game->swordRight = animation_create(sheet, cell, 2, 6);
  cell.y = 5 * CHARACTER_SIZE;
  game->swordLeft = animation_create(sheet, cell, 2, 6);

  bool ok = game->stand && game->swordRight && game->swordLeft;
  for (int dir = 0; dir < DIR_COUNT; dir++) {
    ok = ok && game->walk[dir];
  }
  return ok;
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

  float feetX = game->x + FEET_X;
  float feetY = game->y + FEET_Y;
  if (up && !has_collision(game, feetX, feetY - CHARACTER_SPEED)) {
    game->y -= CHARACTER_SPEED;
  }
  if (left && !has_collision(game, feetX - CHARACTER_SPEED, feetY)) {
    game->x -= CHARACTER_SPEED;
  }
  if (down && !has_collision(game, feetX, feetY + CHARACTER_SPEED)) {
    game->y += CHARACTER_SPEED;
  }
  if (right && !has_collision(game, feetX + CHARACTER_SPEED, feetY)) {
    game->x += CHARACTER_SPEED;
  }

  const Animation *anim = game->stand;
  if (up) anim = game->walk[DIR_UP];
  if (left) anim = game->walk[DIR_LEFT];
  if (down) anim = game->walk[DIR_DOWN];
  if (right) anim = game->walk[DIR_RIGHT];
  if (sword) {
    anim = left && !right ? game->swordLeft : game->swordRight;
    if (game->sfxTrack && !MIX_TrackPlaying(game->sfxTrack)) {
      MIX_PlayTrack(game->sfxTrack, 0);
    }
  }
  game->sprite = animation_frame(anim, game->tick);
  game->tick++;
}

static void render_map(const Game *game, float camX, float camY) {
  const TmxMap *map = game->map;
  SDL_Renderer *renderer = game->engine.renderer;
  int tw = map->tilewidth, th = map->tileheight;

  for (int i = 0; i < map->numLayers; i++) {
    const TmxLayer *layer = &map->layers[i];
    // only draw the tiles intersecting the screen
    int col0 = SDL_max(0, (int)SDL_floorf(camX / (float)tw));
    int row0 = SDL_max(0, (int)SDL_floorf(camY / (float)th));
    int col1 = SDL_min(layer->width - 1, (int)((camX + (float)game->engine.width) / (float)tw));
    int row1 = SDL_min(layer->height - 1, (int)((camY + (float)game->engine.height) / (float)th));

    for (int row = row0; row <= row1; row++) {
      for (int col = col0; col <= col1; col++) {
        uint32_t gid = layer->tiles[row * layer->width + col] & TMX_GID_MASK;
        if (gid == 0 || gid > (uint32_t)map->maxGid || !game->tiles[gid].texture) {
          continue;
        }
        sprite_draw(renderer, &game->tiles[gid],
                    (float)(col * tw) - camX, (float)(row * th) - camY);
      }
    }
  }
}

static void render_fps(Game *game, Uint64 now) {
  game->frames++;
  if (now - game->fpsTimer >= SDL_NS_PER_SECOND || !game->fpsText) {
    char buffer[64];
    SDL_snprintf(buffer, sizeof(buffer), "Press C to %s. Current FPS: %d",
                 game->engine.vsync ? "uncap FPS" : "enable vsync", game->frames);
    SDL_DestroyTexture(game->fpsText);
    game->fpsText = engine_render_text(&game->engine, game->font, buffer);
    game->frames = 0;
    game->fpsTimer = now;
  }
  if (game->fpsText) {
    SDL_FRect dest = {15, 10, 0, 0};
    SDL_GetTextureSize(game->fpsText, &dest.w, &dest.h);
    SDL_RenderTexture(game->engine.renderer, game->fpsText, NULL, &dest);
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
  SDL_Renderer *renderer = game->engine.renderer;
  const TmxMap *map = game->map;

  // interpolate between the last two physics ticks for smooth motion
  float t = (float)game->accumulator / (float)TICK_NS;
  float x = game->prevX + (game->x - game->prevX) * t;
  float y = game->prevY + (game->y - game->prevY) * t;

  // camera follows the character, without showing outside the map
  float maxCamX = (float)(map->width * map->tilewidth - game->engine.width);
  float maxCamY = (float)(map->height * map->tileheight - game->engine.height);
  float camX = SDL_roundf(SDL_clamp(x + CHARACTER_SIZE / 2.0f - (float)game->engine.width / 2, 0, SDL_max(maxCamX, 0)));
  float camY = SDL_roundf(SDL_clamp(y + CHARACTER_SIZE / 2.0f - (float)game->engine.height / 2, 0, SDL_max(maxCamY, 0)));

  SDL_SetRenderDrawColor(renderer, 0x70, 0xc8, 0x40, 0xff);
  SDL_RenderClear(renderer);
  render_map(game, camX, camY);
  sprite_draw(renderer, game->sprite, SDL_roundf(x - camX), SDL_roundf(y - camY));
  render_fps(game, now);
}

static void start_game(Game *game, Uint64 now) {
  game->scene = SCENE_PLAYING;
  game->sceneStart = now;
  game->lastTime = now;
  game->accumulator = 0;
  game->fpsTimer = now;
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  SDL_SetAppMetadata("Game loop example", "2.0", "com.github.batiste.sdl-game-loop");

  Game *game = SDL_calloc(1, sizeof(*game));
  if (!game) {
    return SDL_APP_FAILURE;
  }
  *appstate = game;

  if (!engine_init(&game->engine, "Game example", SCREEN_WIDTH, SCREEN_HEIGHT)) {
    return SDL_APP_FAILURE;
  }

  Engine *engine = &game->engine;
  game->splash = engine_load_texture(engine, "splash.png");
  game->font = engine_load_font(engine, "yoster.ttf", 26);
  SDL_Texture *character = engine_load_texture(engine, "character.png");
  if (!game->splash || !game->font || !character || !load_animations(game, character)) {
    return SDL_APP_FAILURE;
  }

  game->swish = engine_load_audio(engine, "swish-2.ogg");
  if (game->swish) {
    game->sfxTrack = MIX_CreateTrack(engine->mixer);
    MIX_SetTrackAudio(game->sfxTrack, game->swish);
  }

  char *mapPath = engine_asset_path("map1.tmx");
  game->map = tmx_load(mapPath);
  if (!game->map) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't load %s: %s", mapPath, SDL_GetError());
    SDL_free(mapPath);
    return SDL_APP_FAILURE;
  }
  SDL_free(mapPath);
  if (!load_tiles(game)) {
    return SDL_APP_FAILURE;
  }
  game->collisions = tmx_find_object_group(game->map, "collisions");

  game->x = game->prevX = SCREEN_WIDTH / 2.0f;
  game->y = game->prevY = SCREEN_HEIGHT / 2.0f;
  game->sprite = animation_frame(game->stand, 0);

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
      } else if (event->key.scancode == SDL_SCANCODE_C) {
        engine_set_vsync(&game->engine, !game->engine.vsync);
        game->fpsTimer = 0;  // refresh the help text right away
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
  animation_destroy(game->stand);
  for (int dir = 0; dir < DIR_COUNT; dir++) {
    animation_destroy(game->walk[dir]);
  }
  animation_destroy(game->swordRight);
  animation_destroy(game->swordLeft);
  if (game->sfxTrack) {
    MIX_DestroyTrack(game->sfxTrack);
  }
  SDL_DestroyTexture(game->fpsText);
  SDL_free(game->tiles);
  tmx_free(game->map);
  engine_shutdown(&game->engine);
  SDL_free(game);
}
