/*
  Copyright (C) 2013 batiste.bieler@gmail.com

  The invasion of the evil lords

  Uses SDL3's main callbacks: SDL calls SDL_AppIterate() once per frame and
  SDL_AppEvent() for every event. Physics runs at a fixed rate (TICK_NS) and
  rendering interpolates between the last two physics states, so the game
  plays at the same speed whether vsync is on or the frame rate is uncapped.

  The world is procedurally generated from a seed (see worldgen.c), the
  fighting happens in combat.c.
*/

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL_main.h>

#include "atlas.h"
#include "combat.h"
#include "engine.h"
#include "npc.h"
#include "world.h"

#define WINDOW_WIDTH 1280  // initial window size, in points
#define WINDOW_HEIGHT 800
#define VIEW_HEIGHT 600    // the view is about this tall, as wide as the window allows
#define WORLD_SIZE 160  // cells

#define TICK_NS SDL_MS_TO_NS(20)  // 50 physics ticks per second
#define MAX_FRAME_NS SDL_MS_TO_NS(250)

#define SPLASH_FADE_NS SDL_MS_TO_NS(850)
#define SPLASH_HOLD_NS SDL_MS_TO_NS(1000)
#define BANNER_NS SDL_MS_TO_NS(3500)

#define PLAYER_SPEED 2.2f  // pixels per tick
#define CHARACTER_OFFSET_X 24.0f  // feet position inside the 48x48 sprite
#define CHARACTER_OFFSET_Y 40.0f
#define GREETING_DISTANCE 64.0f
#define MUSIC_GAIN 0.6f
#define MUSIC_FADE_IN_MS 2000
#define MUSIC_FADE_OUT_MS 1500
#define ROOF_INSIDE_ALPHA 0.12f
#define ROOF_FADE_PER_SECOND 4.0f
#define DOOR_ASSIST 12  // pixels the player is nudged sideways towards a gap
#define PANEL_WIDTH 600
#define DEATH_VOICES 4          // death sounds playing at the same time, at most
#define DEATH_SOUND_TICKS 3     // and not more often than this
#define DEATH_GAIN 0.5f

static const char *const GREETINGS[] = {"Hello!", "Nice day!", "Hi there!", "Welcome!", "Lovely weather.", "Oh, hi."};
#define NUM_GREETINGS ((int)SDL_arraysize(GREETINGS))

typedef enum {
  SCENE_SPLASH,
  SCENE_INTRO,
  SCENE_PLAYING,
  SCENE_LEVEL_UP,
  SCENE_GAME_OVER,
  SCENE_VICTORY,
} Scene;

// something drawn in the depth-sorted pass
typedef struct Drawable {
  float key;  // y of the base, lower is further away
  const Sprite *sprite;
  float x, y;
  bool flip;
  float scale;  // 0 means 1
  float fade;   // 0 opaque, 1 invisible
  bool tinted;
  SDL_Color tint;
} Drawable;

// a line of text that is only rendered again when it changes. The pixel font
// has no $ % ( ) / @ \ ^ _ characters: keep them out of texts.
typedef struct TextSlot {
  char text[160];
  SDL_Texture *texture;
} TextSlot;

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
  MIX_Audio *deathSounds[3];
  MIX_Audio *bossDeath;
  MIX_Track *deathTracks[DEATH_VOICES];
  Uint64 lastDeathSound;
  // village music when calm, combat music during waves
  MIX_Audio *music[3];
  MIX_Track *musicTrack[3];
  int musicPlaying;
  bool musicMuted;

  World *world;
  Npcs npcs;
  Combat combat;
  Uint64 seed;
  SDL_Texture *minimap;
  bool showMap;

  // player feet position, current and previous tick
  float x, y;
  float prevX, prevY;
  Direction facing;
  const Sprite *sprite;

  Uint64 tick;
  Uint64 worldTick;  // stands still while the hero is inside a house
  bool frozen;
  SDL_FRect nextWaveButton;  // empty when not shown
  Uint64 lastTime;
  Uint64 accumulator;

  int frames;
  int fps;
  Uint64 fpsTimer;
  TextSlot fpsText, infoText, hpText, statusText, villagersText, buttonText;
  SDL_Texture *bannerText;
  Uint64 bannerShownAt;
  int currentVillage;
  SDL_Texture *greetings[NUM_GREETINGS];
  SDL_Texture *oink;
  SDL_Texture *help;

  // modal panel: intro, level up, game over, victory
  SDL_Texture *panelTitle;
  SDL_Texture *panelBody;
  Upgrade choices[UPGRADE_CHOICES];
  int numChoices;

  // house whose roof is faded (player inside, or just left), -1 if none
  int roofHouse;
  float roofAlpha;
  Uint64 lastFrame;

  Drawable *drawables;
  int capDrawables;
  Sprite debrisSprites[MAX_DEBRIS];
} Game;

// ---------------------------------------------------------------- text

static void set_text(Game *game, SDL_Texture **slot, TTF_Font *font, const char *text) {
  SDL_DestroyTexture(*slot);
  *slot = engine_render_text(&game->engine, font, text);
}

static SDL_PRINTF_VARARG_FUNC(6) void draw_text(Game *game, TextSlot *slot, TTF_Font *font, float x, float y,
                                                 SDL_PRINTF_FORMAT_STRING const char *fmt, ...) {
  char text[sizeof(slot->text)];
  va_list args;
  va_start(args, fmt);
  SDL_vsnprintf(text, sizeof(text), fmt, args);
  va_end(args);
  if (!slot->texture || SDL_strcmp(text, slot->text) != 0) {
    SDL_strlcpy(slot->text, text, sizeof(slot->text));
    SDL_DestroyTexture(slot->texture);
    slot->texture = engine_render_text(&game->engine, font, text);
  }
  if (slot->texture) {
    SDL_FRect dest = {x, y, 0, 0};
    SDL_GetTextureSize(slot->texture, &dest.w, &dest.h);
    if (x < 0) {
      dest.x = (float)game->engine.width + x - dest.w;  // right aligned
    }
    SDL_RenderTexture(game->engine.renderer, slot->texture, NULL, &dest);
  }
}

static void show_banner(Game *game, const char *text) {
  set_text(game, &game->bannerText, game->font, text);
  game->bannerShownAt = SDL_GetTicksNS();
}

static void open_panel(Game *game, Scene scene, const char *title, const char *body) {
  SDL_DestroyTexture(game->panelTitle);
  SDL_DestroyTexture(game->panelBody);
  game->panelTitle = engine_render_text(&game->engine, game->font, title);
  game->panelBody = engine_render_text_wrapped(&game->engine, game->smallFont, body, PANEL_WIDTH - 48);
  game->scene = scene;
}

// ---------------------------------------------------------------- world

enum { MUSIC_VILLAGE, MUSIC_COMBAT, MUSIC_GAME_OVER, NUM_MUSIC };
static void play_music(Game *game, int which, bool abrupt);

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
      int i = y * world->width + x;
      SDL_Color c = COLORS[world->kind[i]];
      if (world->kind[i] == CELL_HOUSE && world->houseAt[i] >= 0) {
        c = game->atlas.roofColors[world->houses[world->houseAt[i]].style];
      }
      row[x * 4 + 0] = c.r;
      row[x * 4 + 1] = c.g;
      row[x * 4 + 2] = c.b;
      row[x * 4 + 3] = 255;
    }
  }
  game->minimap = SDL_CreateTextureFromSurface(game->engine.renderer, surface);
  SDL_DestroySurface(surface);
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
  game->seed = seed;
  if (!npcs_spawn(&game->npcs, world, seed)) {
    return false;
  }
  combat_init(&game->combat, seed);
  play_music(game, MUSIC_VILLAGE, false);
  SDL_Log("World %" SDL_PRIu64 ": %d villages, %d houses, %d inhabitants, generated in %.1f ms",
          seed, world->numVillages, world->numHouses, game->npcs.count,
          (double)(SDL_GetTicksNS() - start) / (double)SDL_NS_PER_MS);

  game->x = game->prevX = world->spawnX;
  game->y = game->prevY = world->spawnY;
  game->facing = DIR_DOWN;
  game->roofHouse = -1;
  game->roofAlpha = 1;
  game->currentVillage = -2;  // show the banner for the starting village
  build_minimap(game);
  return true;
}

static Uint64 random_seed(void) {
  return SDL_rand_bits() % 1000000u;
}

// ---------------------------------------------------------------- panels

static void open_intro(Game *game) {
  open_panel(game, SCENE_INTRO, "The invasion of the evil lords",
             "Demons are gathering in the forests and will soon attack the villages. Protect the "
             "villagers: the ones the demons kill rise again as skeletons.\n\n"
             "Move with WASD or the arrow keys, your weapons strike on their own. Collect the souls "
             "of the fallen demons to grow stronger. Demons cannot enter houses: villagers hide "
             "there, and time stands still while you are inside.\n\n"
             "Press Enter to begin.");
}

static void open_level_up(Game *game) {
  Combat *c = &game->combat;
  game->numChoices = combat_upgrade_options(c, game->choices);
  char body[1024] = "";
  for (int i = 0; i < game->numChoices; i++) {
    Upgrade u = game->choices[i];
    int level = u < UP_COUNT ? c->hero.levels[u] : 0;
    char line[256];
    if (u == UP_HEAL) {
      SDL_snprintf(line, sizeof(line), "%d   %s\n     %s\n\n", i + 1, upgrade_name(u), upgrade_description(u));
    } else if (level == 0) {
      SDL_snprintf(line, sizeof(line), "%d   %s - new\n     %s\n\n", i + 1, upgrade_name(u), upgrade_description(u));
    } else {
      SDL_snprintf(line, sizeof(line), "%d   %s - level %d\n     %s\n\n", i + 1, upgrade_name(u), level + 1,
                   upgrade_description(u));
    }
    SDL_strlcat(body, line, sizeof(body));
  }
  SDL_strlcat(body, "Press 1, 2 or 3 to choose.", sizeof(body));
  // the level reached with this choice, when several are pending
  char title[64];
  SDL_snprintf(title, sizeof(title), "Level %d!", c->hero.level + 2 - c->hero.pendingLevels);
  open_panel(game, SCENE_LEVEL_UP, title, body);
}

static void open_game_over(Game *game) {
  char body[512];
  SDL_snprintf(body, sizeof(body),
               "You held out for %d wave%s and slew %d demons.\n%d villager%s lost to the demons.\n\n"
               "Press Enter to try again in a new world.",
               game->combat.wave, game->combat.wave == 1 ? "" : "s", game->combat.kills,
               game->combat.villagersLost, game->combat.villagersLost == 1 ? " was" : "s were");
  open_panel(game, SCENE_GAME_OVER, "You perished in battle", body);
}

static void open_victory(Game *game) {
  char body[512];
  SDL_snprintf(body, sizeof(body),
               "Congratulations, you saved the villages! You slew %d demons, %d villager%s lost.\n\n"
               "Press Enter to keep fighting, or R for a new world.",
               game->combat.kills, game->combat.villagersLost, game->combat.villagersLost == 1 ? " was" : "s were");
  open_panel(game, SCENE_VICTORY, "You killed the evil lord!", body);
}

// ---------------------------------------------------------------- update

static bool player_blocked(const Game *game, float x, float y) {
  return world_blocked(game->world, x - 8, y - 6, x + 8, y + 2);
}

// Move along one axis; when blocked, slide sideways if a gap (a door) is close.
static void move_player(Game *game, float dx, float dy) {
  if (dx == 0 && dy == 0) {
    return;
  }
  if (!player_blocked(game, game->x + dx, game->y + dy)) {
    game->x += dx;
    game->y += dy;
    return;
  }
  for (int d = 1; d <= DOOR_ASSIST; d++) {
    for (int sign = -1; sign <= 1; sign += 2) {
      float sx = dy != 0 ? (float)(sign * d) : 0, sy = dx != 0 ? (float)(sign * d) : 0;
      if (!player_blocked(game, game->x + sx + dx, game->y + sy + dy)) {
        float step = SDL_min((float)d, 2.0f) * (float)sign;
        float nx = game->x + (dy != 0 ? step : 0), ny = game->y + (dx != 0 ? step : 0);
        if (!player_blocked(game, nx, ny)) {
          game->x = nx;
          game->y = ny;
        }
        return;
      }
    }
  }
}

static void update_banner(Game *game) {
  int village = world_village_at(game->world, game->x, game->y);
  if (village == game->currentVillage) {
    return;
  }
  game->currentVillage = village;
  if (village >= 0) {
    show_banner(game, game->world->villages[village].name);
  }
}

// A death sound on a free voice; skipped when they are all busy.
static void play_death(Game *game, MIX_Audio *sound) {
  for (int i = 0; sound && i < DEATH_VOICES; i++) {
    MIX_Track *track = game->deathTracks[i];
    if (track && !MIX_TrackPlaying(track)) {
      MIX_SetTrackAudio(track, sound);
      MIX_PlayTrack(track, 0);
      game->lastDeathSound = game->tick;
      return;
    }
  }
}

// Everything stops where it is: no interpolation towards the last step.
static void freeze_motion(Game *game) {
  for (int i = 0; i < game->npcs.count; i++) {
    Npc *npc = &game->npcs.items[i];
    npc->prevX = npc->x, npc->prevY = npc->y;
    npc->moving = false;
  }
  Combat *c = &game->combat;
  for (int i = 0; i < c->numMonsters; i++) {
    c->monsters[i].prevX = c->monsters[i].x, c->monsters[i].prevY = c->monsters[i].y;
    c->monsters[i].moving = false;
  }
  for (int i = 0; i < c->numShots; i++) {
    c->shots[i].prevX = c->shots[i].x, c->shots[i].prevY = c->shots[i].y;
  }
}

static void update(Game *game) {
  const bool *keys = SDL_GetKeyboardState(NULL);
  // scancodes are physical positions, so WASD also works on AZERTY keyboards
  bool up = keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP];
  bool left = keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT];
  bool down = keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN];
  bool right = keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT];

  game->prevX = game->x;
  game->prevY = game->y;

  // same speed in every direction, diagonals included
  float dx = (float)(right - left), dy = (float)(down - up);
  float length = SDL_sqrtf(dx * dx + dy * dy);
  if (length > 0) {
    float speed = PLAYER_SPEED * combat_speed_factor(&game->combat) / length;
    move_player(game, dx * speed, 0);
    move_player(game, 0, dy * speed);
  }

  bool moving = length > 0;
  if (up) game->facing = DIR_UP;
  if (down) game->facing = DIR_DOWN;
  if (left) game->facing = DIR_LEFT;
  if (right) game->facing = DIR_RIGHT;

  // time stands still while the hero is inside a house
  bool frozen = world_house_at(game->world, game->x, game->y) >= 0;
  if (frozen && !game->frozen) {
    freeze_motion(game);
  }
  game->frozen = frozen;
  int events = 0;
  if (!frozen) {
    npcs_update(&game->npcs, game->world);
    events = combat_update(&game->combat, game->world, &game->npcs, game->x, game->y);
    game->worldTick++;
  }

  const Hero *hero = &game->combat.hero;
  const CharacterSheet *sheet = &game->atlas.player;
  game->sprite = animation_frame(sheet->walk[game->facing], moving ? game->tick : 0);
  if (hero->slashTimer > 0) {
    const Animation *swing = hero->slashLeft ? sheet->swordLeft : sheet->swordRight;
    game->sprite = animation_frame(swing, (Uint64)(12 - hero->slashTimer));
  }
  if ((events & EV_SLASH) && game->sfxTrack && !MIX_TrackPlaying(game->sfxTrack)) {
    MIX_PlayTrack(game->sfxTrack, 0);
  }
  if (events & EV_BOSS_KILLED) {
    play_death(game, game->bossDeath);
  } else if ((events & EV_MONSTER_DIED) && game->tick - game->lastDeathSound >= DEATH_SOUND_TICKS) {
    play_death(game, game->deathSounds[SDL_rand(3)]);
  }
  if (events & EV_MESSAGE) {
    show_banner(game, game->combat.message);
  }
  if (events & EV_WAVE_STARTED) {
    play_music(game, MUSIC_COMBAT, false);
  }
  if (events & EV_WAVE_CLEARED) {
    play_music(game, MUSIC_VILLAGE, false);
  }

  if (events & EV_PLAYER_DIED) {
    open_game_over(game);
    play_music(game, MUSIC_GAME_OVER, true);
  } else if (events & EV_BOSS_KILLED) {
    open_victory(game);
  } else if (events & EV_LEVEL_UP) {
    open_level_up(game);
  }

  update_banner(game);
  game->tick++;
}

// ---------------------------------------------------------------- render

static bool is_flat(Uint16 gid) {
  return gid == GID_DECO(DECO_RED_FLOWERS) || gid == GID_DECO(DECO_PURPLE_FLOWERS) ||
         gid == GID_DECO(DECO_PEBBLES) || gid == GID_DECO(DECO_LILY) ||
         gid == GID_DECO(DECO_LILY_FLOWER) || GID_IS_FLAT_HOUSE(gid);
}

// Furniture is seen through a faded roof or in houses without one.
static Uint16 visible_furniture(const Game *game, int i) {
  const World *world = game->world;
  Uint16 gid = world->furniture[i];
  int h = world->houseAt[i];
  if (!gid || h < 0 || (world->houses[h].roof && h != game->roofHouse)) {
    return 0;
  }
  if (gid == GID_FURNITURE(FURN_FIRE)) {
    gid = (Uint16)(gid + SDL_GetTicksNS() / SDL_MS_TO_NS(140) % 3);
  }
  return gid;
}

static bool in_house(const House *house, int col, int row) {
  return col >= house->x && row >= house->y && col < house->x + house->w && row < house->y + house->h;
}

// Fade the roof of the house the player is in.
static void update_roof(Game *game, Uint64 now) {
  float dt = (float)(now - game->lastFrame) / (float)SDL_NS_PER_SECOND;
  game->lastFrame = now;
  int inside = world_house_at(game->world, game->x, game->y);
  if (inside >= 0 && game->world->houses[inside].roof && inside != game->roofHouse) {
    game->roofHouse = inside;
  }
  if (game->roofHouse < 0) {
    return;
  }
  float target = inside == game->roofHouse ? ROOF_INSIDE_ALPHA : 1.0f;
  float step = ROOF_FADE_PER_SECOND * SDL_min(dt, 0.1f);
  game->roofAlpha = target < game->roofAlpha ? SDL_max(target, game->roofAlpha - step)
                                             : SDL_min(target, game->roofAlpha + step);
  if (target == 1.0f && game->roofAlpha >= 1.0f) {
    game->roofHouse = -1;
  }
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

static void draw_drawable(SDL_Renderer *renderer, const Drawable *d) {
  SDL_Texture *texture = d->sprite->texture;
  float scale = d->scale > 0 ? d->scale : 1;
  if (d->tinted) {
    SDL_SetTextureColorMod(texture, d->tint.r, d->tint.g, d->tint.b);
  }
  if (d->fade > 0) {
    SDL_SetTextureAlphaModFloat(texture, 1 - d->fade);
  }
  SDL_FRect dest = {d->x, d->y, d->sprite->source.w * scale, d->sprite->source.h * scale};
  SDL_RenderTextureRotated(renderer, texture, &d->sprite->source, &dest, 0, NULL,
                           d->flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
  if (d->tinted) {
    SDL_SetTextureColorMod(texture, 255, 255, 255);
  }
  if (d->fade > 0) {
    SDL_SetTextureAlphaModFloat(texture, 1);
  }
}

static float lerp(float a, float b, float t) {
  return a + (b - a) * t;
}

static void fill(SDL_Renderer *renderer, float x, float y, float w, float h, SDL_Color c) {
  SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
  // on the 2x art pixel grid
  SDL_RenderFillRect(renderer, &(SDL_FRect){SDL_floorf(x / 2) * 2, SDL_floorf(y / 2) * 2, w, h});
}

// A few puffs rising from every chimney in view, on the 2x art pixel grid.
static void render_smoke(Game *game, float camX, float camY) {
  SDL_Renderer *renderer = game->engine.renderer;
  const World *world = game->world;
  // seconds, wrapped so the float keeps its precision
  float time = (float)((double)(SDL_GetTicksNS() % (3200 * SDL_NS_PER_SECOND)) / (double)SDL_NS_PER_SECOND);
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  for (int h = 0; h < world->numHouses; h++) {
    const House *house = &world->houses[h];
    float baseX = house->chimneyX + 25 - camX, baseY = house->chimneyY + 8 - camY;
    if (!house->chimney || baseX < -40 || baseY < -80 || baseX > (float)game->engine.width + 40 ||
        baseY > (float)game->engine.height + 20) {
      continue;
    }
    for (int k = 0; k < 4; k++) {
      float phase = SDL_fmodf(time / 3.2f + (float)k / 4 + (float)h * 0.37f, 1.0f);
      float size = SDL_floorf((4 + phase * 8) / 2) * 2;
      float x = baseX + SDL_sinf(phase * 5 + (float)h) * 3 + phase * 14 - size / 2;
      float y = baseY - phase * 46 - size / 2;
      Uint8 alpha = (Uint8)(170 * (1 - phase) * SDL_min(1.0f, phase * 8));
      fill(renderer, x, y, size, size, (SDL_Color){236, 236, 236, alpha});
    }
  }
}

// Souls (blue diamonds) and hearts lying on the ground.
static void render_souls(Game *game, float camX, float camY) {
  SDL_Renderer *renderer = game->engine.renderer;
  const Combat *c = &game->combat;
  float time = (float)(game->worldTick % 1000);
  for (int i = 0; i < c->numSouls; i++) {
    const Soul *s = &c->souls[i];
    float x = s->x - camX, y = s->y - camY + SDL_sinf(time * 0.15f + (float)i) * 2;
    if (x < -10 || y < -10 || x > (float)game->engine.width + 10 || y > (float)game->engine.height + 10) {
      continue;
    }
    if (s->value == 0) {
      SDL_Color red = {220, 40, 40, 255}, dark = {110, 16, 16, 255};
      fill(renderer, x - 6, y - 5, 12, 10, dark);
      fill(renderer, x - 4, y - 4, 4, 2, red);
      fill(renderer, x + 2, y - 4, 4, 2, red);
      fill(renderer, x - 4, y - 2, 10, 2, red);
      fill(renderer, x - 2, y, 6, 2, red);
      fill(renderer, x, y + 2, 2, 2, red);
    } else {
      SDL_Color dark = {30, 64, 140, 255}, blue = {110, 180, 255, 255}, light = {220, 240, 255, 255};
      float big = s->value > 1 ? 2 : 0;
      fill(renderer, x - 2, y - 6 - big, 4, 2, dark);
      fill(renderer, x - 4 - big, y - 4, 8 + 2 * big, 6, dark);
      fill(renderer, x - 2, y + 2, 4, 2 + big, dark);
      fill(renderer, x - 2, y - 4, 4, 6, blue);
      fill(renderer, x - 2, y - 4, 2, 2, light);
    }
  }
}

static void render_aura(Game *game, float px, float py, float camX, float camY) {
  int level = game->combat.hero.levels[UP_AURA];
  if (level == 0) {
    return;
  }
  SDL_Renderer *renderer = game->engine.renderer;
  float r = combat_aura_radius(&game->combat);
  float pulse = 0.5f + 0.5f * SDL_sinf((float)(game->worldTick % 1000) * 0.12f);
  Uint8 alpha = (Uint8)(70 + 60 * pulse);
  int points = 40;
  for (int k = 0; k < points; k++) {
    float a = (float)k * 2 * SDL_PI_F / (float)points + (float)(game->worldTick % 10000) * 0.01f;
    fill(renderer, px - camX + SDL_cosf(a) * r - 2, py - camY + SDL_sinf(a) * r * 0.6f - 2, 4, 4,
         (SDL_Color){255, 236, 150, alpha});
  }
}

static void render_effects(Game *game, float px, float py, float camX, float camY, float t) {
  SDL_Renderer *renderer = game->engine.renderer;
  const Combat *c = &game->combat;

  for (int i = 0; i < c->numShots; i++) {
    const Shot *s = &c->shots[i];
    float x = lerp(s->prevX, s->x, t) - camX, y = lerp(s->prevY, s->y, t) - camY;
    if (!s->hostile) {
      fill(renderer, x - 4, y - 4, 8, 8, (SDL_Color){47, 47, 47, 255});
      fill(renderer, x - 2, y - 2, 4, 4, (SDL_Color){156, 156, 146, 255});
    } else {
      bool pulse = (game->worldTick / 4 + (Uint64)i) % 2;
      SDL_Color outline = s->boss ? (SDL_Color){110, 16, 16, 255} : (SDL_Color){30, 90, 20, 255};
      SDL_Color body = s->boss ? (SDL_Color){240, 70, 40, 255} : (SDL_Color){130, 230, 60, 255};
      fill(renderer, x - 5, y - 5, 10, 10, outline);
      fill(renderer, x - 3, y - 3, pulse ? 6 : 4, pulse ? 6 : 4, body);
    }
  }

  // spirit flames orbiting the hero
  int flames = combat_flame_count(c);
  for (int k = 0; k < flames; k++) {
    float a = c->hero.flameAngle + (float)k * 2 * SDL_PI_F / (float)flames;
    float x = px - camX + SDL_cosf(a) * combat_flame_radius();
    float y = py - 14 - camY + SDL_sinf(a) * combat_flame_radius() * 0.8f;
    bool flicker = (game->worldTick / 3 + (Uint64)k) % 2;
    fill(renderer, x - 6, y - 8, 12, 14, (SDL_Color){110, 30, 10, 255});
    fill(renderer, x - 4, y - 6 - (flicker ? 2 : 0), 8, 10 + (flicker ? 2 : 0), (SDL_Color){247, 130, 40, 255});
    fill(renderer, x - 2, y - 2, 4, 6, (SDL_Color){255, 230, 110, 255});
  }

  // sword slashes: a crescent fading out
  for (int i = 0; i < c->numSlashes; i++) {
    const Slash *s = &c->slashes[i];
    Uint8 alpha = (Uint8)(255 * s->life / 10);
    for (int k = -5; k <= 5; k++) {
      float a = s->angle + (float)k * s->arc / 5;
      float r = s->range * (0.75f + 0.1f * (1 - SDL_fabsf((float)k) / 5));
      float size = k == 0 ? 6 : 4;
      fill(renderer, s->x - camX + SDL_cosf(a) * r - size / 2, s->y - camY + SDL_sinf(a) * r - size / 2, size, size,
           (SDL_Color){255, 255, 255, alpha});
    }
  }

  // health bars of the big demons
  for (int i = 0; i < c->numMonsters; i++) {
    const Monster *m = &c->monsters[i];
    if (m->hp >= m->maxHp || m->kind == MON_UNDERLING) {
      continue;
    }
    float w = m->kind == MON_BOSS ? 60 : 24, top = m->kind == MON_BOSS ? 76 : 38;
    float x = lerp(m->prevX, m->x, t) - camX - w / 2, y = lerp(m->prevY, m->y, t) - camY - top;
    fill(renderer, x - 2, y - 2, w + 4, 6, (SDL_Color){34, 34, 34, 255});
    fill(renderer, x, y, SDL_max(2.0f, w * m->hp / m->maxHp), 2, (SDL_Color){220, 40, 40, 255});
  }
}

static void render_world(Game *game, float camX, float camY, float t) {
  SDL_Renderer *renderer = game->engine.renderer;
  const World *world = game->world;
  const Sprite *tiles = game->atlas.tiles;
  const Combat *combat = &game->combat;

  int col0 = SDL_max(0, (int)SDL_floorf(camX / TILE_SIZE));
  int row0 = SDL_max(0, (int)SDL_floorf(camY / TILE_SIZE));
  int col1 = SDL_min(world->width - 1, (int)((camX + (float)game->engine.width) / TILE_SIZE));
  int row1 = SDL_min(world->height - 1, (int)((camY + (float)game->engine.height) / TILE_SIZE));

  const House *open = game->roofHouse >= 0 ? &world->houses[game->roofHouse] : NULL;

  // ground, then flat decorations that everything walks over, and the
  // inside of the house whose roof is faded
  for (int pass = 0; pass < 2; pass++) {
    for (int row = row0; row <= row1; row++) {
      for (int col = col0; col <= col1; col++) {
        int i = row * world->width + col;
        Uint16 gid = pass == 0 ? world->ground[i] : world->top[i];
        if (pass == 1 && open && in_house(open, col, row) && world->interior[i]) {
          gid = world->interior[i];
        } else if (pass == 1 && !is_flat(gid)) {
          gid = 0;
        }
        if (gid) {
          sprite_draw(renderer, &tiles[gid], (float)(col * TILE_SIZE) - camX, (float)(row * TILE_SIZE) - camY);
        }
        if (pass == 1 && world->furniture[i] == GID_FURNITURE(FURN_RUG) && visible_furniture(game, i)) {
          sprite_draw(renderer, &tiles[world->furniture[i]], (float)(col * TILE_SIZE) - camX,
                      (float)(row * TILE_SIZE) - camY);
        }
      }
    }
  }

  float px = lerp(game->prevX, game->x, t), py = lerp(game->prevY, game->y, t);
  render_souls(game, camX, camY);
  render_aura(game, px, py, camX, camY);

  // everything with height is sorted by the y of its base
  int count = 0;
  for (int row = row0; row <= row1; row++) {
    for (int col = col0; col <= col1; col++) {
      Uint16 furniture = visible_furniture(game, row * world->width + col);
      if (furniture && furniture != GID_FURNITURE(FURN_RUG)) {
        push_drawable(game, &count, (Drawable){
            .key = (float)(row * TILE_SIZE) + 36, .sprite = &tiles[furniture],
            .x = (float)(col * TILE_SIZE) - camX, .y = (float)(row * TILE_SIZE) - camY,
        });
      }
      Uint16 gid = world->top[row * world->width + col];
      if (!gid || is_flat(gid) || (open && in_house(open, col, row))) {
        continue;
      }
      float base = GID_IS_HOUSE(gid) ? TILE_SIZE : 40;
      push_drawable(game, &count, (Drawable){
          .key = (float)(row * TILE_SIZE) + base, .sprite = &tiles[gid],
          .x = (float)(col * TILE_SIZE) - camX, .y = (float)(row * TILE_SIZE) - camY,
      });
    }
  }

  // chimneys sit on the roof: drawn right after its first row
  const Sprite *chimney = &tiles[GID_DECO(DECO_CHIMNEY)];
  for (int h = 0; h < world->numHouses; h++) {
    const House *house = &world->houses[h];
    if (house->chimney && house != open && house->x <= col1 && house->x + house->w > col0 &&
        house->y <= row1 && house->y + house->h > row0) {
      push_drawable(game, &count, (Drawable){
          .key = (float)((house->y + 1) * TILE_SIZE) + 0.5f, .sprite = chimney,
          .x = SDL_roundf(house->chimneyX - camX), .y = SDL_roundf(house->chimneyY - camY),
      });
    }
  }

  float viewW = (float)game->engine.width, viewH = (float)game->engine.height;
  for (int i = 0; i < game->npcs.count; i++) {
    const Npc *npc = &game->npcs.items[i];
    float x = lerp(npc->prevX, npc->x, t), y = lerp(npc->prevY, npc->y, t);
    if (npc->dead || x < camX - TILE_SIZE || y < camY - TILE_SIZE || x > camX + viewW + TILE_SIZE ||
        y > camY + viewH + TILE_SIZE) {
      continue;
    }
    bool flip;
    SDL_FPoint offset;
    const Sprite *sprite = npc_sprite(npc, &game->atlas, game->worldTick, &flip, &offset);
    push_drawable(game, &count, (Drawable){
        .key = y, .sprite = sprite, .flip = flip,
        .x = SDL_roundf(x + offset.x - camX), .y = SDL_roundf(y + offset.y - camY),
        .tinted = npc->hurtTimer > 0, .tint = {255, 90, 90, 255},
    });
  }

  for (int i = 0; i < combat->numMonsters; i++) {
    const Monster *m = &combat->monsters[i];
    float x = lerp(m->prevX, m->x, t), y = lerp(m->prevY, m->y, t);
    if (x < camX - 60 || y < camY - 20 || x > camX + viewW + 60 || y > camY + viewH + 90) {
      continue;
    }
    const Sprite *sprite = &game->atlas.monsters[m->kind];
    float scale = m->kind == MON_BOSS ? 2 : 1;
    float bob = m->moving && (game->worldTick / 8 + (Uint64)i) % 2 ? -2 : 0;
    push_drawable(game, &count, (Drawable){
        .key = y, .sprite = sprite, .flip = m->faceLeft, .scale = scale,
        .x = SDL_roundf(x - sprite->source.w * scale / 2 - camX),
        .y = SDL_roundf(y - sprite->source.h * scale - camY + bob),
        .tinted = m->hurtTimer > 0, .tint = {255, 110, 110, 255},
    });
  }

  // halves of the fallen demons, drifting apart
  for (int i = 0; i < combat->numDebris; i++) {
    const Debris *d = &combat->debris[i];
    Sprite *half = &game->debrisSprites[i];
    *half = game->atlas.monsters[d->kind];
    float scale = d->kind == MON_BOSS ? 2 : 1;
    half->source.w = SDL_floorf(half->source.w / 2);
    if (d->right) {
      half->source.x += half->source.w;
    }
    push_drawable(game, &count, (Drawable){
        .key = d->y, .sprite = half, .scale = scale, .fade = 1 - (float)d->life / 30,
        .x = SDL_roundf(d->x - camX - (d->right ? 0 : half->source.w * scale)),
        .y = SDL_roundf(d->y - half->source.h * scale - camY),
    });
  }

  const Hero *hero = &combat->hero;
  bool blink = hero->invulnerable > 0 && (game->tick / 3) % 2;
  push_drawable(game, &count, (Drawable){
      .key = py, .sprite = game->sprite,
      .x = SDL_roundf(px - CHARACTER_OFFSET_X - camX), .y = SDL_roundf(py - CHARACTER_OFFSET_Y - camY),
      .tinted = hero->invulnerable > 0, .tint = {255, 120, 120, 255}, .fade = blink ? 0.5f : 0,
  });

  SDL_qsort(game->drawables, (size_t)count, sizeof(Drawable), compare_drawables);
  for (int i = 0; i < count; i++) {
    draw_drawable(renderer, &game->drawables[i]);
  }

  // the faded roof goes over everything inside
  if (open) {
    for (int row = open->y; row < open->y + open->h; row++) {
      for (int col = open->x; col < open->x + open->w; col++) {
        Uint16 gid = world->top[row * world->width + col];
        if (gid && !is_flat(gid)) {
          SDL_SetTextureAlphaModFloat(tiles[gid].texture, game->roofAlpha);
          sprite_draw(renderer, &tiles[gid], (float)(col * TILE_SIZE) - camX, (float)(row * TILE_SIZE) - camY);
          SDL_SetTextureAlphaModFloat(tiles[gid].texture, 1.0f);
        }
      }
    }
    if (open->chimney) {
      SDL_SetTextureAlphaModFloat(chimney->texture, game->roofAlpha);
      sprite_draw(renderer, chimney, SDL_roundf(open->chimneyX - camX), SDL_roundf(open->chimneyY - camY));
      SDL_SetTextureAlphaModFloat(chimney->texture, 1.0f);
    }
  }

  render_smoke(game, camX, camY);
  render_effects(game, px, py, camX, camY, t);

  // the closest villager or pig on the same side of the walls says something
  int closest = -1;
  float best = GREETING_DISTANCE * GREETING_DISTANCE;
  int playerHouse = world_house_at(world, game->x, game->y);
  for (int i = 0; i < game->npcs.count; i++) {
    const Npc *npc = &game->npcs.items[i];
    float dx = npc->x - game->x, dy = npc->y - game->y;
    if (!npc->dead && dx * dx + dy * dy < best && world_house_at(world, npc->x, npc->y) == playerHouse) {
      best = dx * dx + dy * dy;
      closest = i;
    }
  }
  if (closest >= 0) {
    const Npc *npc = &game->npcs.items[closest];
    SDL_Texture *text = npc->kind == NPC_PIG ? game->oink
                        : npc->fear > 0      ? game->help
                                             : game->greetings[closest % NUM_GREETINGS];
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
    if (!npc->dead && npc->state != NPC_INSIDE && npc->kind == NPC_VILLAGER) {
      SDL_RenderFillRect(renderer, &(SDL_FRect){map.x + npc->x / TILE_SIZE * scale - 0.5f,
                                                map.y + npc->y / TILE_SIZE * scale - 0.5f, 1.5f, 1.5f});
    }
  }
  SDL_SetRenderDrawColor(renderer, 200, 0, 0, 255);
  for (int i = 0; i < game->combat.numMonsters; i++) {
    const Monster *m = &game->combat.monsters[i];
    float size = m->kind == MON_BOSS ? 4 : 2;
    SDL_RenderFillRect(renderer, &(SDL_FRect){map.x + m->x / TILE_SIZE * scale - size / 2,
                                              map.y + m->y / TILE_SIZE * scale - size / 2, size, size});
  }
  float px = lerp(game->prevX, game->x, t) / TILE_SIZE * scale;
  float py = lerp(game->prevY, game->y, t) / TILE_SIZE * scale;
  SDL_SetRenderDrawColor(renderer, 34, 34, 34, 255);
  SDL_RenderFillRect(renderer, &(SDL_FRect){map.x + px - 3, map.y + py - 3, 6, 6});
  SDL_SetRenderDrawColor(renderer, 250, 220, 60, 255);
  SDL_RenderFillRect(renderer, &(SDL_FRect){map.x + px - 2, map.y + py - 2, 4, 4});
}

static void render_hud(Game *game, Uint64 now) {
  SDL_Renderer *renderer = game->engine.renderer;
  const Combat *c = &game->combat;
  const Hero *hero = &c->hero;
  float width = (float)game->engine.width;

  game->frames++;
  if (now - game->fpsTimer >= SDL_NS_PER_SECOND) {
    game->fps = game->frames;
    game->frames = 0;
    game->fpsTimer = now;
  }

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  // experience along the top edge
  fill(renderer, 0, 0, width, 8, (SDL_Color){34, 34, 34, 220});
  fill(renderer, 0, 2, width * (float)hero->xp / (float)hero->nextLevel, 4, (SDL_Color){110, 180, 255, 255});

  // health
  fill(renderer, 12, 16, 164, 16, (SDL_Color){34, 34, 34, 255});
  fill(renderer, 14, 18, 160, 12, (SDL_Color){90, 20, 20, 255});
  fill(renderer, 14, 18, 160.0f * (float)hero->hp / (float)hero->maxHp, 12, (SDL_Color){220, 50, 40, 255});
  draw_text(game, &game->hpText, game->smallFont, 184, 14, "%d of %d   Level %d", hero->hp, hero->maxHp,
            hero->level + 1);

  game->nextWaveButton = (SDL_FRect){0};
  if (game->frozen) {
    draw_text(game, &game->statusText, game->smallFont, 12, 38, "Time stands still");
  } else if (c->waveState == WAVE_ATTACK) {
    draw_text(game, &game->statusText, game->smallFont, 12, 38, "Wave %d: %d demon%s", c->wave,
              c->numMonsters + c->toSpawn, c->numMonsters + c->toSpawn == 1 ? "" : "s");
  } else {
    draw_text(game, &game->statusText, game->smallFont, 12, 38, "Next wave in %d s",
              (c->waveTimer + 49) / 50);
  }
  if (c->waveState == WAVE_CALM && game->scene == SCENE_PLAYING) {
    // the button to call the next wave right away (or press Enter)
    SDL_FRect b = {12, 82, 150, 26};
    fill(renderer, b.x, b.y, b.w, b.h, (SDL_Color){58, 41, 41, 255});
    fill(renderer, b.x + 2, b.y + 2, b.w - 4, b.h - 4, (SDL_Color){246, 239, 158, 255});
    draw_text(game, &game->buttonText, game->smallFont, b.x + 12, b.y + 3, "Next wave");
    game->nextWaveButton = b;
  }
  int alive = 0, villagers = 0;
  for (int i = 0; i < game->npcs.count; i++) {
    if (game->npcs.items[i].kind == NPC_VILLAGER) {
      villagers++;
      alive += !game->npcs.items[i].dead;
    }
  }
  draw_text(game, &game->villagersText, game->smallFont, 12, 58, "Villagers %d of %d", alive, villagers);

  draw_text(game, &game->infoText, game->smallFont, 12, (float)game->engine.height - 26,
            "Seed %" SDL_PRIu64 "   R: new world   M: map   N: music   F: fullscreen   C: vsync", game->seed);
  draw_text(game, &game->fpsText, game->smallFont, -12, (float)game->engine.height - 26, "%d fps%s", game->fps,
            game->engine.vsync ? "" : ", uncapped");

  Uint64 shown = now - game->bannerShownAt;
  if (game->bannerText && shown < BANNER_NS) {
    float fade = SDL_min(1.0f, (float)(BANNER_NS - shown) / (float)SDL_MS_TO_NS(500));
    SDL_FRect dest;
    SDL_GetTextureSize(game->bannerText, &dest.w, &dest.h);
    dest.x = SDL_roundf((width - dest.w) / 2);
    dest.y = 86;
    SDL_SetRenderDrawColor(renderer, 34, 34, 34, (Uint8)(150 * fade));
    SDL_RenderFillRect(renderer, &(SDL_FRect){dest.x - 24, dest.y - 8, dest.w + 48, dest.h + 16});
    SDL_SetTextureAlphaModFloat(game->bannerText, fade);
    SDL_RenderTexture(renderer, game->bannerText, NULL, &dest);
  }
}

static void render_panel(Game *game) {
  if (game->scene == SCENE_PLAYING || !game->panelTitle) {
    return;
  }
  SDL_Renderer *renderer = game->engine.renderer;
  float tw, th, bw = 0, bh = 0;
  SDL_GetTextureSize(game->panelTitle, &tw, &th);
  if (game->panelBody) {
    SDL_GetTextureSize(game->panelBody, &bw, &bh);
  }
  float w = PANEL_WIDTH, h = th + bh + 72;
  float x = ((float)game->engine.width - w) / 2, y = ((float)game->engine.height - h) / 2;
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 110);
  SDL_RenderFillRect(renderer, NULL);
  fill(renderer, x - 4, y - 4, w + 8, h + 8, (SDL_Color){58, 41, 41, 255});
  fill(renderer, x, y, w, h, (SDL_Color){246, 239, 158, 255});
  SDL_RenderTexture(renderer, game->panelTitle, NULL, &(SDL_FRect){SDL_roundf(x + (w - tw) / 2), y + 20, tw, th});
  if (game->panelBody) {
    SDL_RenderTexture(renderer, game->panelBody, NULL, &(SDL_FRect){x + 24, y + 44 + th, bw, bh});
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
  float t = game->scene == SCENE_PLAYING ? (float)game->accumulator / (float)TICK_NS : 1;
  float x = lerp(game->prevX, game->x, t), y = lerp(game->prevY, game->y, t);

  // camera follows the player, without showing outside the world
  float maxCamX = (float)(world->width * TILE_SIZE - game->engine.width);
  float maxCamY = (float)(world->height * TILE_SIZE - game->engine.height);
  float camX = SDL_roundf(SDL_clamp(x - (float)game->engine.width / 2, 0, maxCamX));
  float camY = SDL_roundf(SDL_clamp(y - 16 - (float)game->engine.height / 2, 0, maxCamY));

  update_roof(game, now);
  SDL_SetRenderDrawColor(game->engine.renderer, 0x70, 0xc8, 0x40, 0xff);
  SDL_RenderClear(game->engine.renderer);
  render_world(game, camX, camY, t);
  render_minimap(game, t);
  render_hud(game, now);
  render_panel(game);
}

// Background music, streamed and looping. The game works without it.
static void load_music(Game *game) {
  static const char *const FILES[NUM_MUSIC] = {"village-music-1.mp3", "combat-music-1.mp3", "game-over.mp3"};
  Engine *engine = &game->engine;
  for (int i = 0; i < NUM_MUSIC; i++) {
    game->music[i] = engine_load_audio(engine, FILES[i], false);
    game->musicTrack[i] = game->music[i] ? MIX_CreateTrack(engine->mixer) : NULL;
    if (game->musicTrack[i]) {
      MIX_SetTrackAudio(game->musicTrack[i], game->music[i]);
      MIX_SetTrackGain(game->musicTrack[i], MUSIC_GAIN);
    }
  }
  game->musicPlaying = -1;
}

// Fade out whatever plays and fade in another tune, or cut straight to it.
static void play_music(Game *game, int which, bool abrupt) {
  if (which == game->musicPlaying) {
    return;
  }
  if (game->musicPlaying >= 0 && game->musicTrack[game->musicPlaying]) {
    MIX_Track *old = game->musicTrack[game->musicPlaying];
    MIX_StopTrack(old, abrupt ? 0 : MIX_TrackMSToFrames(old, MUSIC_FADE_OUT_MS));
  }
  game->musicPlaying = which;
  MIX_Track *track = game->musicTrack[which];
  if (!track) {
    return;
  }
  SDL_PropertiesID options = SDL_CreateProperties();
  // the game over tune plays once, the others loop
  SDL_SetNumberProperty(options, MIX_PROP_PLAY_LOOPS_NUMBER, which == MUSIC_GAME_OVER ? 0 : -1);
  SDL_SetNumberProperty(options, MIX_PROP_PLAY_FADE_IN_MILLISECONDS_NUMBER, abrupt ? 0 : MUSIC_FADE_IN_MS);
  if (!MIX_PlayTrack(track, options)) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Couldn't play music: %s", SDL_GetError());
  }
  SDL_DestroyProperties(options);
  if (game->musicMuted) {
    MIX_PauseTrack(track);
  }
}

static void toggle_music(Game *game) {
  game->musicMuted = !game->musicMuted;
  for (int i = 0; i < NUM_MUSIC; i++) {
    if (game->musicTrack[i]) {
      if (game->musicMuted) {
        MIX_PauseTrack(game->musicTrack[i]);
      } else if (i == game->musicPlaying) {
        MIX_ResumeTrack(game->musicTrack[i]);
      }
    }
  }
}

static void resume_play(Game *game) {
  game->scene = SCENE_PLAYING;
  game->lastTime = SDL_GetTicksNS();
  game->accumulator = 0;
}

// ---------------------------------------------------------------- callbacks

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
  SDL_SetAppMetadata("The invasion of the evil lords", "3.0", "com.github.batiste.sdl-game-loop");

  Game *game = SDL_calloc(1, sizeof(*game));
  if (!game) {
    return SDL_APP_FAILURE;
  }
  *appstate = game;

  if (!engine_init(&game->engine, "The invasion of the evil lords", WINDOW_WIDTH, WINDOW_HEIGHT, VIEW_HEIGHT)) {
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
  game->help = engine_render_text(engine, game->smallFont, "Help!");

  game->swish = engine_load_audio(engine, "swish-2.ogg", true);
  if (game->swish) {
    game->sfxTrack = MIX_CreateTrack(engine->mixer);
    MIX_SetTrackAudio(game->sfxTrack, game->swish);
  }
  for (int i = 0; i < 3; i++) {
    char file[32];
    SDL_snprintf(file, sizeof(file), "demon-death-%d.wav", i + 1);
    game->deathSounds[i] = engine_load_audio(engine, file, true);
  }
  game->bossDeath = engine_load_audio(engine, "demon-death-boss.wav", true);
  for (int i = 0; engine->mixer && i < DEATH_VOICES; i++) {
    game->deathTracks[i] = MIX_CreateTrack(engine->mixer);
    if (game->deathTracks[i]) {
      MIX_SetTrackGain(game->deathTracks[i], DEATH_GAIN);
    }
  }
  load_music(game);
  play_music(game, MUSIC_VILLAGE, false);

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
  if (event->type == SDL_EVENT_QUIT) {
    return SDL_APP_SUCCESS;
  }
  if (event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
    engine_fit_view(&game->engine);
    return SDL_APP_CONTINUE;
  }
  if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && game->scene == SCENE_PLAYING) {
    // the button is in logical coordinates, like everything drawn
    SDL_ConvertEventToRenderCoordinates(game->engine.renderer, event);
    SDL_FPoint p = {event->button.x, event->button.y};
    if (SDL_PointInRectFloat(&p, &game->nextWaveButton)) {
      combat_next_wave(&game->combat);
    }
    return SDL_APP_CONTINUE;
  }
  if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat &&
      (event->key.scancode == SDL_SCANCODE_F || event->key.scancode == SDL_SCANCODE_F11)) {
    engine_toggle_fullscreen(&game->engine);
    return SDL_APP_CONTINUE;
  }
  if (event->type != SDL_EVENT_KEY_DOWN || event->key.repeat) {
    return SDL_APP_CONTINUE;
  }
  SDL_Scancode key = event->key.scancode;
  bool enter = key == SDL_SCANCODE_RETURN || key == SDL_SCANCODE_KP_ENTER || key == SDL_SCANCODE_SPACE;
  if (key == SDL_SCANCODE_ESCAPE) {
    return SDL_APP_SUCCESS;
  }

  switch (game->scene) {
    case SCENE_SPLASH:
      open_intro(game);
      return SDL_APP_CONTINUE;
    case SCENE_INTRO:
      if (enter) {
        resume_play(game);
        game->bannerShownAt = 0;
        game->currentVillage = -2;
      }
      return SDL_APP_CONTINUE;
    case SCENE_LEVEL_UP: {
      int choice = (int)key - (int)SDL_SCANCODE_1;
      if (choice >= 0 && choice < game->numChoices) {
        combat_apply_upgrade(&game->combat, game->choices[choice]);
        if (game->combat.hero.pendingLevels > 0) {
          open_level_up(game);
        } else {
          resume_play(game);
        }
      }
      return SDL_APP_CONTINUE;
    }
    case SCENE_GAME_OVER:
      if (enter) {
        if (!new_world(game, random_seed())) {
          return SDL_APP_FAILURE;
        }
        resume_play(game);
      }
      return SDL_APP_CONTINUE;
    case SCENE_VICTORY:
      if (enter) {
        resume_play(game);
        return SDL_APP_CONTINUE;
      }
      break;
    case SCENE_PLAYING:
      break;
  }

  switch (key) {
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
      combat_next_wave(&game->combat);
      break;
    case SDL_SCANCODE_C:
      engine_set_vsync(&game->engine, !game->engine.vsync);
      break;
    case SDL_SCANCODE_M:
      game->showMap = !game->showMap;
      break;
    case SDL_SCANCODE_N:
      toggle_music(game);
      break;
    case SDL_SCANCODE_R:
      if (!new_world(game, random_seed())) {
        return SDL_APP_FAILURE;
      }
      resume_play(game);
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
      open_intro(game);
      game->lastFrame = now;
    } else {
      render_splash(game, elapsed);
    }
  }

  if (game->scene != SCENE_SPLASH) {
    if (game->scene == SCENE_PLAYING) {
      // never try to catch up more than MAX_FRAME_NS, e.g. after a breakpoint
      game->accumulator += SDL_min(now - game->lastTime, MAX_FRAME_NS);
      game->lastTime = now;
      while (game->accumulator >= TICK_NS && game->scene == SCENE_PLAYING) {
        update(game);
        game->accumulator -= TICK_NS;
      }
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
  for (int i = 0; i < NUM_MUSIC; i++) {
    if (game->musicTrack[i]) {
      MIX_DestroyTrack(game->musicTrack[i]);
    }
  }
  for (int i = 0; i < DEATH_VOICES; i++) {
    if (game->deathTracks[i]) {
      MIX_DestroyTrack(game->deathTracks[i]);
    }
  }
  TextSlot *slots[] = {&game->fpsText, &game->infoText, &game->hpText, &game->statusText, &game->villagersText,
                       &game->buttonText};
  for (int i = 0; i < (int)SDL_arraysize(slots); i++) {
    SDL_DestroyTexture(slots[i]->texture);
  }
  SDL_DestroyTexture(game->bannerText);
  SDL_DestroyTexture(game->panelTitle);
  SDL_DestroyTexture(game->panelBody);
  SDL_DestroyTexture(game->minimap);
  for (int i = 0; i < NUM_GREETINGS; i++) {
    SDL_DestroyTexture(game->greetings[i]);
  }
  SDL_DestroyTexture(game->oink);
  SDL_DestroyTexture(game->help);
  SDL_free(game->drawables);
  combat_free(&game->combat);
  npcs_free(&game->npcs);
  world_free(game->world);
  atlas_free(&game->atlas);
  engine_shutdown(&game->engine);
  SDL_free(game);
}
