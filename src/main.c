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
#include "button.h"
#include "combat.h"
#include "engine.h"
#include "icons.h"
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
#define HURT_GAIN 0.8f
#define UI_GAIN 0.6f
#define CARD_WIDTH 200  // level up and shop choices, at most
#define CARD_GAP 16
#define LEVEL_UP_GRACE_NS SDL_MS_TO_NS(450)  // keys mashed in the fight don't pick an upgrade
#define CHOICE_FLASH_NS SDL_MS_TO_NS(260)    // the chosen card flashes before the game goes on
#define DENIED_NS SDL_MS_TO_NS(250)          // a card that can't be bought shakes this long
#define MAX_CARDS 6
#define COMPACT_CARD_WIDTH 340
#define COMPACT_TEXT_X 64  // where the text starts on a compact card, right of the icon
#define BOMB_SHAKE 10.0f  // pixels the view shakes when a bomb goes off
#define DEATH_HOP 0.35f   // seconds the hero jolts up, flashing...
#define DEATH_FALL 0.9f   // ...until he has fallen over...
#define DEATH_SCENE_NS SDL_MS_TO_NS(2800)  // ...and after a silence, the game over
#define DEATH_SHAKE 8.0f

static const char *const GREETINGS[] = {"Hello!", "Nice day!", "Hi there!", "Welcome!", "Lovely weather.", "Oh, hi."};
#define NUM_GREETINGS ((int)SDL_arraysize(GREETINGS))

typedef enum {
  SCENE_SPLASH,
  SCENE_INTRO,
  SCENE_PLAYING,
  SCENE_LEVEL_UP,
  SCENE_SHOP,
  SCENE_DYING,
  SCENE_MENU,
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
  int icon;  // drawn when there is no sprite
} Drawable;

typedef enum MenuItem {
  MENU_RESUME,
  MENU_NEW_WORLD,
  MENU_MUSIC,
  MENU_FULLSCREEN,
  MENU_VSYNC,
  MENU_MAP,
  MENU_LEVEL_UP,  // for testing
  MENU_QUIT,
  MENU_COUNT,
} MenuItem;

// a line of text that is only rendered again when it changes. The pixel font
// has no $ % ( ) / @ \ ^ _ characters: keep them out of texts.
typedef struct TextSlot {
  char text[160];
  SDL_Texture *texture;
  bool outlined;  // white with a dark outline, for dark backgrounds
} TextSlot;

// what goes on a card of the level up or shop screens
typedef struct CardSpec {
  const char *name, *tag, *text, *footer;
  SDL_Color color, tagColor;
  int icon;  // drawn above the name, -1 for none
  int pips, pipsFilled;  // levels of an upgrade, 0 pips for none
  bool disabled;  // can't be bought
} CardSpec;

typedef struct Card {
  SDL_Texture *name, *tag, *text, *footer;
  SDL_Color color;
  int icon;
  int pips, pipsFilled;
  bool disabled;
} Card;

typedef struct Game {
  Engine engine;
  Atlas atlas;
  Scene scene;
  Uint64 sceneStart;

  SDL_Texture *splash;
  TTF_Font *font;
  TTF_Font *smallFont;
  TTF_Font *bigFont;
  MIX_Audio *swish;
  MIX_Track *sfxTrack;
  MIX_Audio *deathSounds[3];
  MIX_Audio *bossDeath;
  MIX_Track *deathTracks[DEATH_VOICES];
  MIX_Track *hurtTrack, *heroDeathTrack;
  MIX_Track *levelUpTrack, *selectTrack, *tickTrack;
  MIX_Track *explosionTrack, *coinTrack, *potionTrack, *deniedTrack, *splatTrack, *repelTrack, *blockTrack;
  MIX_Track *ricochetTrack;
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
  SDL_FRect nextWaveButton, shopButton, leaveButton, skipButton;  // empty when not shown
  SDL_Texture *nextWaveLabel, *shopLabel, *leaveLabel, *skipLabel;
  float shake;  // how far the view shakes, in pixels
  float xpShown;  // the experience bar fills up smoothly
  int xpLevel;
  Uint64 lastHud;

  // pause menu, over the scene it was opened from
  Scene menuReturn;
  int menuSelected;
  int menuArmed;  // asks for a second press to confirm, -1 if none
  Uint64 menuOpenedAt;
  SDL_FRect menuRects[MENU_COUNT];
  TextSlot menuText[MENU_COUNT], menuKeys[MENU_COUNT];
  SDL_Texture *menuTitle;
  bool heroDown;  // dead, lying there until a new world
  Uint64 diedAt;
  SDL_FPoint mouse;  // in logical coordinates
  bool mouseDown;
  Uint64 lastTime;
  Uint64 accumulator;

  int frames;
  int fps;
  Uint64 fpsTimer;
  TextSlot infoText, hpText, levelText, statusText, villagersText;
  TextSlot goldText, potionText, bombText, shopGoldText;
  TextSlot amuletText, wisdomText, shieldText, tipTitle, tipText;
  SDL_FRect upgradeRects[UP_COUNT];  // the upgrade icons at the top left, for the mouse
  SDL_Texture *bannerText;
  Uint64 bannerShownAt;
  int currentVillage;
  SDL_Texture *greetings[NUM_GREETINGS];
  SDL_Texture *oink;
  SDL_Texture *help;
  SDL_Texture *digits[3][10];  // damage numbers: dealt, taken, healed

  // modal panel: intro, level up, game over, victory
  SDL_Texture *panelTitle;
  SDL_Texture *panelBody;
  Uint64 panelOpenedAt;

  // level up and shop: the choices as cards
  Upgrade choices[UPGRADE_CHOICES];
  Card cards[MAX_CARDS];
  int numCards;
  int cardColumns;
  bool cardCompact;
  float cardWidth, cardHeight;
  SDL_FRect cardRects[MAX_CARDS];  // where they rest, for the mouse
  SDL_Texture *levelUpHint;
  int selected;
  int chosen;  // upgrade picked, -1 until then
  int flashCard, deniedCard;  // bought, or too dear, -1 for none
  Uint64 chosenAt, flashAt, deniedAt;

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
    slot->texture = slot->outlined ? engine_render_outlined_text(&game->engine, font, text, (SDL_Color){255, 255, 255, 255},
                                                                 (SDL_Color){58, 41, 41, 255})
                                   : engine_render_text(&game->engine, font, text);
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

static SDL_FPoint texture_size(SDL_Texture *texture) {
  SDL_FPoint size = {0, 0};
  if (texture) {
    SDL_GetTextureSize(texture, &size.x, &size.y);
  }
  return size;
}

// Text width in pixels, as draw_text() renders it.
static float text_width(const TextSlot *slot, TTF_Font *font, const char *text) {
  int w = 0;
  TTF_GetStringSize(font, text, 0, &w, NULL);
  // the outline or shadow, a pixel of the font (see engine.c)
  int t = SDL_max(1, (int)SDL_lroundf(TTF_GetFontSize(font) / ENGINE_FONT_GRID));
  return (float)(w + (slot->outlined ? 2 * SDL_min(2, t) : t));
}

// Where text goes to sit in the middle of a box h pixels tall at y.
static float text_middle(const TextSlot *slot, TTF_Font *font, float y, float h) {
  int t = SDL_max(1, (int)SDL_lroundf(TTF_GetFontSize(font) / ENGINE_FONT_GRID));
  float height = (float)(TTF_GetFontAscent(font) + (slot->outlined ? 2 * SDL_min(2, t) : t));
  return SDL_roundf(y + (h - height) / 2);
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
  game->panelOpenedAt = SDL_GetTicksNS();
}

static void play_sound(MIX_Track *track) {
  if (track) {
    MIX_PlayTrack(track, 0);
  }
}

// ---------------------------------------------------------------- world

enum { MUSIC_VILLAGE, MUSIC_COMBAT, MUSIC_GAME_OVER, NUM_MUSIC };
static void play_music(Game *game, int which, bool abrupt);
static void stop_music(Game *game, int fadeMs);

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
  game->heroDown = false;
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
             "Move with WASD or the arrow keys, your weapons strike on their own. Every demon you "
             "slay makes you stronger. Demons cannot enter houses: villagers hide "
             "there, and time stands still while you are inside.\n\n"
             "Between waves, spend the gold of the demons at the merchant: potions, bombs and amulets.\n\n"
             "Press Esc for the menu, Enter to begin.");
}

static void free_cards(Game *game) {
  for (int i = 0; i < MAX_CARDS; i++) {
    Card *card = &game->cards[i];
    SDL_DestroyTexture(card->name);
    SDL_DestroyTexture(card->tag);
    SDL_DestroyTexture(card->text);
    SDL_DestroyTexture(card->footer);
    *card = (Card){.icon = -1};
  }
  game->numCards = 0;
}

// Compact cards (the shop) are wide and short, in two columns: the icon on
// the left, the text on the right. The others stand side by side.
static void build_cards(Game *game, const CardSpec *specs, int count, bool compact) {
  Engine *engine = &game->engine;
  const SDL_Color outline = {58, 41, 41, 255};
  TTF_Font *footerFont = compact ? game->smallFont : game->font;
  free_cards(game);
  game->cardCompact = compact;
  game->cardColumns = compact ? 2 : SDL_min(count, UPGRADE_CHOICES);
  // narrower cards when the view is
  float room = ((float)engine->width - 24 - CARD_GAP * (float)(game->cardColumns - 1)) / (float)game->cardColumns;
  game->cardWidth = SDL_min(compact ? (float)COMPACT_CARD_WIDTH : (float)CARD_WIDTH, SDL_floorf(room / 2) * 2);
  int wrap = (int)game->cardWidth - (compact ? COMPACT_TEXT_X + 40 : 28);
  float height = 0;
  for (int i = 0; i < count; i++) {
    const CardSpec *spec = &specs[i];
    Card *card = &game->cards[i];
    *card = (Card){.color = spec->color, .icon = spec->icon, .pips = spec->pips, .pipsFilled = spec->pipsFilled,
                   .disabled = spec->disabled};
    if (spec->tag) {
      card->tag = engine_render_outlined_text(engine, game->smallFont, spec->tag, spec->tagColor, outline);
    }
    card->name = compact ? engine_render_text(engine, game->font, spec->name)
                         : engine_render_text_wrapped(engine, game->font, spec->name, wrap);
    card->text = engine_render_text_wrapped(engine, game->smallFont, spec->text, wrap);
    if (spec->footer) {
      card->footer = engine_render_outlined_text(engine, footerFont, spec->footer,
                                                 spec->disabled ? (SDL_Color){214, 206, 196, 255}
                                                                : (SDL_Color){255, 214, 72, 255},
                                                 outline);
    }
    SDL_FPoint name = texture_size(card->name), text = texture_size(card->text);
    SDL_FPoint footer = texture_size(card->footer), tag = texture_size(card->tag);
    if (compact) {
      // name, description, then tag and price on the last line
      float icon = spec->icon >= 0 ? icon_size((IconKind)spec->icon, 4).y + 20 : 0;
      height = SDL_max(height, SDL_max(icon, 8 + name.y + 2 + text.y + 4 + SDL_max(footer.y, tag.y) + 6));
    } else {
      float icon = spec->icon >= 0 ? icon_size((IconKind)spec->icon, 4).y + 8 : 0;
      // key and tag, icon, name, description, pips or price
      height = SDL_max(height, 44 + icon + name.y + 8 + text.y + (spec->footer ? 50 : 36));
    }
  }
  game->numCards = count;
  game->cardHeight = SDL_max(compact ? 80.0f : 160.0f, SDL_ceilf(height / 2) * 2);
}

static const IconKind UPGRADE_ICONS[] = {
    [UP_SWORD] = ICON_SWORD,  [UP_STONES] = ICON_STONES, [UP_FLAMES] = ICON_FLAMES,  [UP_AURA] = ICON_AURA,
    [UP_MOLASSES] = ICON_JAR, [UP_REPEL] = ICON_REPULSION, [UP_SPEED] = ICON_BOOTS, [UP_VITALITY] = ICON_HEART,
    [UP_MIGHT] = ICON_HAMMER, [UP_HASTE] = ICON_HASTE,  [UP_HEAL] = ICON_HEART,
};

// The colour of each card: weapons warm, passives cool.
static SDL_Color upgrade_color(Upgrade u) {
  static const SDL_Color COLORS[] = {
      [UP_SWORD] = {238, 122, 98, 255},   [UP_STONES] = {196, 184, 160, 255}, [UP_FLAMES] = {248, 158, 70, 255},
      [UP_AURA] = {250, 214, 100, 255},   [UP_MOLASSES] = {214, 150, 76, 255}, [UP_REPEL] = {190, 140, 230, 255},
      [UP_SPEED] = {128, 204, 110, 255},  [UP_VITALITY] = {236, 116, 140, 255},
      [UP_MIGHT] = {196, 136, 222, 255},  [UP_HASTE] = {104, 200, 222, 255},
      [UP_HEAL] = {236, 196, 140, 255},
  };
  return COLORS[u];
}

static void open_level_up(Game *game) {
  Combat *c = &game->combat;
  int count = combat_upgrade_options(c, game->choices);
  CardSpec specs[MAX_CARDS];
  char tags[MAX_CARDS][32];
  for (int i = 0; i < count; i++) {
    Upgrade u = game->choices[i];
    int level = u < UP_COUNT ? c->hero.levels[u] : 0;
    if (u == UP_HEAL) {
      SDL_strlcpy(tags[i], "Heal", sizeof(tags[i]));
    } else if (level == 0) {
      SDL_strlcpy(tags[i], "New!", sizeof(tags[i]));
    } else {
      SDL_snprintf(tags[i], sizeof(tags[i]), "Level %d", level + 1);
    }
    bool fresh = level == 0 && u != UP_HEAL;
    specs[i] = (CardSpec){
        .name = upgrade_name(u), .tag = tags[i], .text = upgrade_description(u), .color = upgrade_color(u),
        .tagColor = fresh ? (SDL_Color){255, 232, 96, 255} : (SDL_Color){255, 255, 255, 255},
        .icon = (int)UPGRADE_ICONS[u], .pips = u < UP_COUNT ? MAX_UPGRADE_LEVEL : 0, .pipsFilled = level,
    };
  }
  build_cards(game, specs, count, false);
  game->selected = 0;
  game->chosen = game->flashCard = game->deniedCard = -1;

  // the level reached with this choice, when several are pending
  char title[64];
  SDL_snprintf(title, sizeof(title), "Level %d!", c->hero.level + 2 - c->hero.pendingLevels);
  SDL_DestroyTexture(game->panelTitle);
  SDL_DestroyTexture(game->panelBody);
  game->panelTitle = engine_render_outlined_text(&game->engine, game->bigFont, title, (SDL_Color){255, 214, 72, 255},
                                                 (SDL_Color){58, 41, 41, 255});
  game->panelBody = NULL;
  game->scene = SCENE_LEVEL_UP;
  game->panelOpenedAt = SDL_GetTicksNS();
  play_sound(game->levelUpTrack);
}

// The shop's cards, made again after each purchase.
static void build_shop_cards(Game *game) {
  static const SDL_Color COLORS[ITEM_COUNT] = {
      [ITEM_POTION] = {236, 116, 140, 255}, [ITEM_BOMB] = {164, 168, 196, 255},  [ITEM_AMULET] = {250, 206, 96, 255},
      [ITEM_WISDOM] = {120, 170, 240, 255}, [ITEM_SHIELD] = {204, 150, 96, 255},
  };
  static const IconKind ICONS[ITEM_COUNT] = {
      [ITEM_POTION] = ICON_POTION, [ITEM_BOMB] = ICON_BOMB,   [ITEM_AMULET] = ICON_AMULET,
      [ITEM_WISDOM] = ICON_WISDOM, [ITEM_SHIELD] = ICON_SHIELD,
  };
  const Combat *c = &game->combat;
  CardSpec specs[ITEM_COUNT];
  char tags[ITEM_COUNT][32], prices[ITEM_COUNT][32];
  for (int i = 0; i < ITEM_COUNT; i++) {
    int owned = combat_item_count(c, (Item)i), price = combat_item_price(c, (Item)i);
    bool full = owned >= combat_item_max((Item)i);
    SDL_snprintf(tags[i], sizeof(tags[i]), "Have %d", owned);
    if (full) {
      SDL_strlcpy(prices[i], "Full", sizeof(prices[i]));
    } else {
      SDL_snprintf(prices[i], sizeof(prices[i]), "%d gold", price);
    }
    specs[i] = (CardSpec){
        .name = item_name((Item)i), .tag = tags[i], .text = item_description((Item)i), .footer = prices[i],
        .color = COLORS[i], .tagColor = {255, 255, 255, 255}, .icon = (int)ICONS[i],
        .disabled = full || c->hero.gold < price,
    };
  }
  build_cards(game, specs, ITEM_COUNT, true);
}

static void open_shop(Game *game) {
  build_shop_cards(game);
  game->selected = 0;
  game->chosen = game->flashCard = game->deniedCard = -1;
  SDL_DestroyTexture(game->panelTitle);
  SDL_DestroyTexture(game->panelBody);
  game->panelTitle = engine_render_outlined_text(&game->engine, game->bigFont, "The merchant",
                                                 (SDL_Color){255, 214, 72, 255}, (SDL_Color){58, 41, 41, 255});
  game->panelBody = NULL;
  game->scene = SCENE_SHOP;
  game->panelOpenedAt = SDL_GetTicksNS();
  play_sound(game->selectTrack);
}

static void buy_item(Game *game, int i) {
  if (i < 0 || i >= game->numCards) {
    return;
  }
  game->selected = i;
  if (combat_buy(&game->combat, (Item)i)) {
    build_shop_cards(game);
    game->flashCard = i;
    game->flashAt = SDL_GetTicksNS();
    play_sound(game->coinTrack);
  } else {
    game->deniedCard = i;
    game->deniedAt = SDL_GetTicksNS();
    play_sound(game->deniedTrack);
  }
}

static void select_choice(Game *game, int choice) {
  if (choice != game->selected) {
    game->selected = choice;
    play_sound(game->tickTrack);
  }
}

// The card flashes, then apply_choice() goes on with the game.
static void pick_choice(Game *game, int choice) {
  if (game->chosen >= 0 || choice < 0 || choice >= game->numCards ||
      SDL_GetTicksNS() - game->panelOpenedAt < LEVEL_UP_GRACE_NS) {
    return;
  }
  game->selected = game->chosen = game->flashCard = choice;
  game->chosenAt = game->flashAt = SDL_GetTicksNS();
  play_sound(game->selectTrack);
}

static void resume_play(Game *game);

static void apply_choice(Game *game) {
  combat_apply_upgrade(&game->combat, game->choices[game->chosen]);
  game->chosen = -1;
  if (game->combat.hero.pendingLevels > 0) {
    open_level_up(game);
  } else {
    resume_play(game);
  }
}

static void try_open_shop(Game *game) {
  if (game->combat.waveState == WAVE_CALM) {
    open_shop(game);
  } else {
    show_banner(game, "The merchant only comes between waves");
    play_sound(game->deniedTrack);
  }
}

// For testing: a level now, and its upgrades to choose from.
static void free_level(Game *game) {
  combat_level_up(&game->combat);
  open_level_up(game);
}

// No upgrade this time.
static void skip_level(Game *game) {
  if (game->chosen >= 0 || SDL_GetTicksNS() - game->panelOpenedAt < LEVEL_UP_GRACE_NS) {
    return;
  }
  play_sound(game->tickTrack);
  combat_skip_upgrade(&game->combat);
  if (game->combat.hero.pendingLevels > 0) {
    open_level_up(game);
  } else {
    resume_play(game);
  }
}

static void call_next_wave(Game *game) {
  if (game->combat.waveState == WAVE_CALM) {
    play_sound(game->selectTrack);
  }
  combat_next_wave(&game->combat);
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
    float speed = PLAYER_SPEED * combat_speed_factor(&game->combat) *
                  world_ground_speed(game->world, game->x, game->y) / length;
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
    game->combat.viewW = (float)game->engine.width;
    game->combat.viewH = (float)game->engine.height;
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
  if ((events & EV_HERO_HURT) && game->hurtTrack) {
    MIX_PlayTrack(game->hurtTrack, 0);
  }
  if (events & EV_BOSS_KILLED) {
    play_death(game, game->bossDeath);
  } else if ((events & EV_MONSTER_DIED) && game->tick - game->lastDeathSound >= DEATH_SOUND_TICKS) {
    play_death(game, game->deathSounds[SDL_rand(3)]);
  }
  if ((events & EV_COIN) && game->coinTrack && !MIX_TrackPlaying(game->coinTrack)) {
    MIX_PlayTrack(game->coinTrack, 0);
  }
  if (events & EV_SPLAT) {
    play_sound(game->splatTrack);
  }
  if (events & EV_REPEL) {
    play_sound(game->repelTrack);
  }
  if (events & EV_BLOCK) {
    play_sound(game->blockTrack);
  }
  if (events & EV_RICOCHET) {
    play_sound(game->ricochetTrack);
  }
  if (events & EV_EXPLOSION) {
    play_sound(game->explosionTrack);
    game->shake = BOMB_SHAKE;
  }
  game->shake = game->shake > 0.5f ? game->shake * 0.86f : 0;
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
    // the death scene plays, then the game over: see SDL_AppIterate()
    game->scene = SCENE_DYING;
    game->heroDown = true;
    game->diedAt = SDL_GetTicksNS();
    stop_music(game, 150);
    play_sound(game->heroDeathTrack);
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
  if (!d->sprite) {
    icon_draw(renderer, (IconKind)d->icon, d->x, d->y, 2);
    return;
  }
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

static float ease_out_cubic(float t) {
  float u = 1 - SDL_clamp(t, 0.0f, 1.0f);
  return 1 - u * u * u;
}

// Overshoots a little and settles: things pop into place.
static float ease_out_back(float t) {
  float u = SDL_clamp(t, 0.0f, 1.0f) - 1;
  return 1 + 2.7f * u * u * u + 1.7f * u * u;
}

// The same pseudo random number in 0..1 for the same n, every frame.
static float hash01(Uint32 n) {
  n = (n ^ 61u) ^ (n >> 16);
  n *= 9u;
  n ^= n >> 4;
  n *= 0x27d4eb2du;
  n ^= n >> 15;
  return (float)(n & 0xffffffu) / 16777216.0f;
}

static void draw_texture(SDL_Renderer *renderer, SDL_Texture *texture, float x, float y) {
  float w, h;
  if (texture && SDL_GetTextureSize(texture, &w, &h)) {
    SDL_RenderTexture(renderer, texture, NULL, &(SDL_FRect){SDL_roundf(x), SDL_roundf(y), w, h});
  }
}

// A disc squashed for the ground's perspective, from one colour in the
// middle to another at the edge.
static void render_disc(SDL_Renderer *renderer, float x, float y, float r, SDL_FColor inner, SDL_FColor outer) {
#define SIDES 24
  SDL_Vertex v[SIDES * 3];
  for (int k = 0; k < SIDES; k++) {
    float a0 = (float)k * 2 * SDL_PI_F / SIDES, a1 = (float)(k + 1) * 2 * SDL_PI_F / SIDES;
    v[k * 3 + 0] = (SDL_Vertex){{x, y}, inner, {0, 0}};
    v[k * 3 + 1] = (SDL_Vertex){{x + SDL_cosf(a0) * r, y + SDL_sinf(a0) * r * 0.7f}, outer, {0, 0}};
    v[k * 3 + 2] = (SDL_Vertex){{x + SDL_cosf(a1) * r, y + SDL_sinf(a1) * r * 0.7f}, outer, {0, 0}};
  }
  SDL_RenderGeometry(renderer, NULL, v, SIDES * 3, NULL, 0);
#undef SIDES
}

// A dot that grows arms `arm` art pixels long.
static void draw_star(SDL_Renderer *renderer, float x, float y, int arm, SDL_Color c) {
  x = SDL_floorf(x / 2) * 2, y = SDL_floorf(y / 2) * 2;
  float a = (float)arm * 2;
  fill(renderer, x - a, y, 2 * a + 2, 2, c);
  fill(renderer, x, y - a, 2, a, c);
  fill(renderer, x, y + 2, 2, a, c);
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

// Gold and hearts lying on the ground.
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
    if (s->kind == SOUL_GOLD) {
      icon_draw(renderer, ICON_COIN, x - 6, y - 6, 2);
    } else {
      SDL_Color red = {220, 40, 40, 255}, dark = {110, 16, 16, 255};
      fill(renderer, x - 6, y - 5, 12, 10, dark);
      fill(renderer, x - 4, y - 4, 4, 2, red);
      fill(renderer, x + 2, y - 4, 4, 2, red);
      fill(renderer, x - 4, y - 2, 10, 2, red);
      fill(renderer, x - 2, y, 6, 2, red);
      fill(renderer, x, y + 2, 2, 2, red);
    }
  }
}

// Molasses on the ground: it spreads out, bubbles, and dries up.
static void render_puddles(Game *game, float camX, float camY) {
  SDL_Renderer *renderer = game->engine.renderer;
  const Combat *c = &game->combat;
  for (int i = 0; i < c->numPuddles; i++) {
    const Puddle *p = &c->puddles[i];
    float x = p->x - camX, y = p->y - camY;
    float spread = ease_out_back((float)(p->maxLife - p->life) / 8);
    float fade = SDL_min(1.0f, (float)p->life / 25);
    float r = p->radius * spread;
    render_disc(renderer, x, y, r, (SDL_FColor){0.62f, 0.3f, 0.02f, 0.88f * fade},
                (SDL_FColor){0.86f, 0.52f, 0.08f, 0.7f * fade});
    // a glossy shine
    render_disc(renderer, x - r * 0.25f, y - r * 0.15f, r * 0.35f, (SDL_FColor){1, 0.86f, 0.5f, 0.6f * fade},
                (SDL_FColor){1, 0.8f, 0.4f, 0});
    // bubbles swelling and popping
    for (Uint32 k = 0; k < 5; k++) {
      float phase = SDL_fmodf((float)(game->worldTick % 10000) / 40 + hash01(k + (Uint32)i * 7), 1.0f);
      float a = hash01(k * 3 + 1 + (Uint32)i * 11) * 2 * SDL_PI_F, d = hash01(k * 3 + 2) * r * 0.7f;
      float size = phase < 0.8f ? 2 + SDL_floorf(phase * 3) * 2 : 0;
      fill(renderer, x + SDL_cosf(a) * d, y + SDL_sinf(a) * d * 0.7f, size, size,
           (SDL_Color){255, 214, 140, (Uint8)(200 * fade)});
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
    if (s->reflected) {
      // gold on the way back, with a trail
      float len = SDL_max(0.01f, SDL_sqrtf(s->vx * s->vx + s->vy * s->vy));
      for (int k = 2; k >= 1; k--) {
        float tx = x - s->vx / len * 7 * (float)k, ty = y - s->vy / len * 7 * (float)k;
        fill(renderer, tx - 2, ty - 2, 4, 4, (SDL_Color){255, 220, 110, (Uint8)(200 - 70 * k)});
      }
      fill(renderer, x - 5, y - 5, 10, 10, (SDL_Color){120, 80, 10, 255});
      fill(renderer, x - 3, y - 3, 6, 6, (SDL_Color){255, 226, 100, 255});
      fill(renderer, x - 3, y - 3, 2, 2, (SDL_Color){255, 255, 230, 255});
    } else if (!s->hostile) {
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

  // bombs flying in an arc, then fizzing on the ground
  SDL_FPoint bombSize = icon_size(ICON_BOMB, 2);
  for (int i = 0; i < c->numBombs; i++) {
    const Bomb *b = &c->bombs[i];
    float p = b->flight > 0 ? SDL_min(1.0f, ((float)(BOMB_FLIGHT - b->flight) + t) / BOMB_FLIGHT) : 1;
    float gx = lerp(b->fromX, b->x, p) - camX, gy = lerp(b->fromY + 20, b->y, p) - camY;
    float y = gy - SDL_sinf(p * SDL_PI_F) * 60;
    fill(renderer, gx - 8, gy - 2, 16, 4, (SDL_Color){0, 0, 0, 60});
    icon_draw(renderer, b->molasses ? ICON_JAR : ICON_BOMB, gx - bombSize.x / 2, y - bombSize.y, 2);
    if (b->flight == 0 && (game->worldTick / 3) % 2) {
      draw_star(renderer, gx + 4, y - bombSize.y, 1, (SDL_Color){255, 255, 255, 255});
    }
  }

  // explosions: a flash, sparks flying out, smoke rising
  for (int i = 0; i < c->numExplosions; i++) {
    const Explosion *e = &c->explosions[i];
    float p = SDL_clamp(1 - ((float)e->life - t) / EXPLOSION_TICKS, 0.0f, 1.0f);
    float x = e->x - camX, y = e->y - 10 - camY, r = combat_bomb_radius();
    if (p < 0.6f) {
      float a = 1 - p / 0.6f;
      render_disc(renderer, x, y, r * (0.5f + ease_out_cubic(p * 2) * 0.5f), (SDL_FColor){1, 0.95f, 0.6f, a},
                  (SDL_FColor){1, 0.4f, 0.05f, a * 0.8f});
    }
    if (p < 0.25f) {
      // the white hot core
      render_disc(renderer, x, y, r * 0.45f * (1 - p * 2), (SDL_FColor){1, 1, 1, 1}, (SDL_FColor){1, 1, 0.8f, 0.6f});
    }
    for (Uint32 k = 0; k < 8; k++) {
      float a = (float)k * SDL_PI_F / 4 + hash01(k + (Uint32)i * 31) * 0.6f;
      float d = r * 0.45f * ease_out_cubic(p) * (0.6f + hash01(k + 50) * 0.6f);
      float size = SDL_floorf((10 + p * 14) / 2) * 2;
      fill(renderer, x + SDL_cosf(a) * d - size / 2, y + SDL_sinf(a) * d * 0.6f - p * 30 - size / 2, size, size,
           (SDL_Color){90, 84, 80, (Uint8)(150 * (1 - p))});
    }
    for (Uint32 k = 0; k < 18; k++) {
      float a = (float)k * 2 * SDL_PI_F / 18 + hash01(k + (Uint32)i * 17) * 0.4f;
      float d = r * ease_out_cubic(p) * (0.5f + hash01(k + 90) * 0.7f);
      float size = p < 0.3f ? 6 : p < 0.7f ? 4 : 2;
      SDL_Color spark = k % 3 ? (SDL_Color){255, 200, 60, 255} : (SDL_Color){255, 250, 220, 255};
      spark.a = (Uint8)(255 * (1 - p));
      fill(renderer, x + SDL_cosf(a) * d, y + SDL_sinf(a) * d * 0.7f, size, size, spark);
    }
  }

  // bats flap over everything, their shadows on the ground
  for (int i = 0; i < c->numMonsters; i++) {
    const Monster *m = &c->monsters[i];
    if (m->kind != MON_BAT) {
      continue;
    }
    float x = lerp(m->prevX, m->x, t) - camX, y = lerp(m->prevY, m->y, t) - camY;
    bool up = (game->worldTick / 4 + (Uint64)i) % 2;
    IconKind icon = up ? ICON_BAT_UP : ICON_BAT_DOWN;
    SDL_FPoint size = icon_size(icon, 2);
    fill(renderer, x - 6, y - 2, 12, 4, (SDL_Color){0, 0, 0, 50});
    if (m->hurtTimer == 0 || (game->worldTick / 2) % 2) {  // blinking when hit
      icon_draw(renderer, icon, x - size.x / 2, y - 26 - size.y / 2 - (up ? 2 : 0), 2);
    }
  }

  // the repulsion spell: a flash and a double ring going out
  float repelled = (float)c->hero.repelAge + t;
  if (c->hero.levels[UP_REPEL] > 0 && repelled < 18) {
    float p = repelled / 18, r = combat_repel_radius(c) * ease_out_cubic(p);
    if (p < 0.35f) {
      float a = 1 - p / 0.35f;
      render_disc(renderer, px - camX, py - 10 - camY, r, (SDL_FColor){0.85f, 0.7f, 1, 0.35f * a},
                  (SDL_FColor){0.7f, 0.5f, 1, 0.1f * a});
    }
    for (int ring = 0; ring < 2; ring++) {
      float rr = r * (ring ? 0.82f : 1);
      Uint8 alpha = (Uint8)((ring ? 150 : 240) * (1 - p));
      for (int k = 0; k < 40; k++) {
        float a = (float)k * 2 * SDL_PI_F / 40 + (float)ring * 0.08f;
        fill(renderer, px - camX + SDL_cosf(a) * rr - 2, py - 10 - camY + SDL_sinf(a) * rr * 0.7f - 2, 4, 4,
             ring ? (SDL_Color){180, 130, 255, alpha} : (SDL_Color){236, 214, 255, alpha});
      }
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

  // damage numbers, built from pre-rendered digits
  for (int i = 0; i < c->numPopups; i++) {
    const Popup *p = &c->popups[i];
    char text[12];
    SDL_snprintf(text, sizeof(text), "%d", p->amount);
    SDL_Texture *const *set = game->digits[p->heal ? 2 : p->hero ? 1 : 0];
    float w = 0, h = 0;
    for (const char *ch = text; *ch; ch++) {
      float dw, dh;
      if (set[*ch - '0'] && SDL_GetTextureSize(set[*ch - '0'], &dw, &dh)) {
        w += dw - 4, h = dh;
      }
    }
    float x = SDL_roundf((p->hero ? px : p->x) - camX - w / 2);
    float y = SDL_roundf((p->hero ? py : 0) + p->y - camY - h / 2);
    float alpha = SDL_min(1.0f, (float)p->life / 10);
    for (const char *ch = text; *ch; ch++) {
      SDL_Texture *digit = set[*ch - '0'];
      float dw, dh;
      if (digit && SDL_GetTextureSize(digit, &dw, &dh)) {
        SDL_SetTextureAlphaModFloat(digit, alpha);
        SDL_RenderTexture(renderer, digit, NULL, &(SDL_FRect){x, y, dw, dh});
        x += dw - 4;
      }
    }
  }

  // health bars of the big demons
  for (int i = 0; i < c->numMonsters; i++) {
    const Monster *m = &c->monsters[i];
    if (m->hp >= m->maxHp || m->kind == MON_UNDERLING || m->kind == MON_BAT) {
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
  render_puddles(game, camX, camY);
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
    if (m->kind == MON_BAT || x < camX - 60 || y < camY - 20 || x > camX + viewW + 60 || y > camY + viewH + 90) {
      continue;
    }
    const Sprite *sprite = &game->atlas.monsters[m->kind];
    float scale = m->kind == MON_BOSS ? 2 : 1;
    float bob = m->moving && (game->worldTick / 8 + (Uint64)i) % 2 ? -2 : 0;
    push_drawable(game, &count, (Drawable){
        .key = y, .sprite = sprite, .flip = m->faceLeft, .scale = scale,
        .x = SDL_roundf(x - sprite->source.w * scale / 2 - camX),
        .y = SDL_roundf(y - sprite->source.h * scale - camY + bob),
        .tinted = m->hurtTimer > 0 || m->slowed,
        .tint = m->hurtTimer > 0 ? (SDL_Color){255, 110, 110, 255} : (SDL_Color){214, 160, 96, 255},
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

  // a dead hero is drawn by render_death(), over the darkness
  const Hero *hero = &combat->hero;
  bool blink = hero->invulnerable > 0 && (game->tick / 3) % 2;
  if (!game->heroDown) {
    push_drawable(game, &count, (Drawable){
        .key = py, .sprite = game->sprite,
        .x = SDL_roundf(px - CHARACTER_OFFSET_X - camX), .y = SDL_roundf(py - CHARACTER_OFFSET_Y - camY),
        .tinted = hero->invulnerable > 0, .tint = {255, 120, 120, 255}, .fade = blink ? 0.5f : 0,
    });
    if (hero->shields > 0) {
      // in the left hand, or slung on the back when walking away
      static const float SIDE[DIR_COUNT] = {[DIR_DOWN] = 2, [DIR_RIGHT] = 2, [DIR_UP] = -6, [DIR_LEFT] = -14};
      push_drawable(game, &count, (Drawable){
          .key = py + 0.5f, .icon = ICON_SHIELD_SMALL,
          .x = SDL_roundf(px + SIDE[game->facing] - camX), .y = SDL_roundf(py - 20 - camY),
      });
    }
  }

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

// The name, level and description of an upgrade, by the mouse.
static void render_tooltip(Game *game, Upgrade u) {
  char title[64];
  SDL_snprintf(title, sizeof(title), "%s, level %d", upgrade_name(u), game->combat.hero.levels[u]);
  const char *text = upgrade_description(u);
  float w = SDL_max(text_width(&game->tipTitle, game->font, title), text_width(&game->tipText, game->smallFont, text)) + 28;
  float h = 66;
  float x = SDL_min(game->mouse.x + 16, (float)game->engine.width - w - 8), y = game->mouse.y + 18;
  ButtonStyle style = button_style((SDL_Color){246, 239, 158, 255});
  SDL_FRect face = button_draw(game->engine.renderer, (SDL_FRect){x, y, w, h}, &style, BUTTON_IDLE);
  draw_text(game, &game->tipTitle, game->font, face.x + 12, face.y + 4, "%s", title);
  draw_text(game, &game->tipText, game->smallFont, face.x + 12, face.y + 36, "%s", text);
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
  const SDL_Color outline = {58, 41, 41, 255};
  float dt = SDL_min(0.1f, (float)(now - game->lastHud) / (float)SDL_NS_PER_SECOND);
  game->lastHud = now;

  // health, with the numbers on the bar
  SDL_FRect bar = {12, 12, 220, 22};
  float health = (bar.w - 4) * (float)hero->hp / (float)hero->maxHp;
  button_shape(renderer, bar, outline);
  button_shape(renderer, (SDL_FRect){bar.x + 2, bar.y + 2, bar.w - 4, bar.h - 4}, (SDL_Color){96, 24, 24, 255});
  if (hero->hp > 0) {
    button_shape(renderer, (SDL_FRect){bar.x + 2, bar.y + 2, SDL_max(10.0f, health), bar.h - 4},
                 (SDL_Color){222, 52, 42, 255});
    fill(renderer, bar.x + 6, bar.y + 4, SDL_max(2.0f, health - 8), 2, (SDL_Color){255, 140, 120, 255});
  }
  char text[64];
  SDL_snprintf(text, sizeof(text), "%d of %d", hero->hp, hero->maxHp);
  draw_text(game, &game->hpText, game->smallFont, SDL_roundf(bar.x + (bar.w - text_width(&game->hpText, game->smallFont, text)) / 2),
            text_middle(&game->hpText, game->smallFont, bar.y, bar.h), "%s", text);

  // progress to the next level, filling up smoothly
  if (hero->level != game->xpLevel) {
    game->xpLevel = hero->level;
    game->xpShown = 0;
  }
  float xp = (float)hero->xp / (float)hero->nextLevel;
  game->xpShown += (xp - game->xpShown) * SDL_min(1.0f, dt * 8);
  SDL_snprintf(text, sizeof(text), "Level %d", hero->level + 1);
  float left = 12 + text_width(&game->levelText, game->smallFont, text) + 8;
  SDL_FRect xpBar = {SDL_floorf(left / 2) * 2, 42, bar.x + bar.w - SDL_floorf(left / 2) * 2, 14};
  draw_text(game, &game->levelText, game->smallFont, 12, text_middle(&game->levelText, game->smallFont, xpBar.y, xpBar.h),
            "%s", text);
  float filled = (xpBar.w - 4) * SDL_clamp(game->xpShown, 0.0f, 1.0f);
  button_shape(renderer, xpBar, outline);
  button_shape(renderer, (SDL_FRect){xpBar.x + 2, xpBar.y + 2, xpBar.w - 4, xpBar.h - 4}, (SDL_Color){28, 44, 84, 255});
  if (filled >= 1) {
    bool gaining = xp - game->xpShown > 0.01f;  // glows while it fills
    button_shape(renderer, (SDL_FRect){xpBar.x + 2, xpBar.y + 2, SDL_max(10.0f, filled), xpBar.h - 4},
                 gaining ? (SDL_Color){170, 220, 255, 255} : (SDL_Color){100, 170, 255, 255});
    fill(renderer, xpBar.x + 6, xpBar.y + 4, SDL_max(2.0f, filled - 8), 2, (SDL_Color){210, 236, 255, 255});
  }

  game->nextWaveButton = game->shopButton = (SDL_FRect){0};
  if (game->frozen) {
    draw_text(game, &game->statusText, game->smallFont, 12, 62, "Time stands still");
  } else if (c->waveState == WAVE_ATTACK) {
    int remaining = c->numMonsters + c->toSpawn;
    if (c->batWave) {
      draw_text(game, &game->statusText, game->smallFont, 12, 62, "Bat swarm %d: %d left", c->wave, remaining);
    } else {
      draw_text(game, &game->statusText, game->smallFont, 12, 62, "Wave %d: %d demon%s", c->wave, remaining,
                remaining == 1 ? "" : "s");
    }
  } else {
    draw_text(game, &game->statusText, game->smallFont, 12, 62, "Next wave in %d s",
              (c->waveTimer + 49) / 50);
  }
  if (c->waveState == WAVE_CALM && game->scene == SCENE_PLAYING) {
    // the button to call the next wave right away (or press Enter)
    SDL_FRect b = {12, 146, 128, 28};
    ButtonStyle style = button_style((SDL_Color){246, 239, 158, 255});
    SDL_FRect face = button_draw(renderer, b, &style, button_state(b, game->mouse, game->mouseDown));
    if (game->nextWaveLabel) {
      float lw, lh;
      SDL_GetTextureSize(game->nextWaveLabel, &lw, &lh);
      SDL_RenderTexture(renderer, game->nextWaveLabel, NULL,
                        &(SDL_FRect){SDL_roundf(face.x + (face.w - lw) / 2), SDL_roundf(face.y + (face.h - lh) / 2), lw, lh});
    }
    game->nextWaveButton = b;

    // and the one to the merchant (or press B)
    SDL_FRect shop = {b.x + b.w + 8, b.y, 76, b.h};
    style = button_style((SDL_Color){250, 206, 96, 255});
    face = button_draw(renderer, shop, &style, button_state(shop, game->mouse, game->mouseDown));
    if (game->shopLabel) {
      float lw, lh;
      SDL_GetTextureSize(game->shopLabel, &lw, &lh);
      SDL_RenderTexture(renderer, game->shopLabel, NULL,
                        &(SDL_FRect){SDL_roundf(face.x + (face.w - lw) / 2), SDL_roundf(face.y + (face.h - lh) / 2), lw, lh});
    }
    game->shopButton = shop;
  }

  // gold and what was bought, while playing: the shop shows its own
  if (game->scene == SCENE_PLAYING) {
    float bag = (float)game->engine.height - 34;
    icon_draw(renderer, ICON_COIN, 12, bag + 6, 2);
    draw_text(game, &game->goldText, game->smallFont, 30, bag, "%d", hero->gold);
    icon_draw(renderer, ICON_POTION, 96, bag - 2, 2);
    draw_text(game, &game->potionText, game->smallFont, 120, bag, "%d  Q", hero->potions);
    icon_draw(renderer, ICON_BOMB, 184, bag - 2, 2);
    draw_text(game, &game->bombText, game->smallFont, 208, bag, "%d  E", hero->bombs);
    // and what is worn
    const IconKind worn[] = {ICON_AMULET, ICON_WISDOM, ICON_SHIELD};
    const int counts[] = {hero->amulets, hero->wisdom, hero->shields};
    TextSlot *texts[] = {&game->amuletText, &game->wisdomText, &game->shieldText};
    float x = 272;
    for (int k = 0; k < 3; k++) {
      if (counts[k] > 0) {
        SDL_FPoint size = icon_size(worn[k], 2);
        icon_draw(renderer, worn[k], x, bag + 10 - size.y / 2, 2);
        draw_text(game, texts[k], game->smallFont, x + size.x + 6, bag, "%d", counts[k]);
        x += size.x + 40;
      }
    }
  }
  int alive = 0, villagers = 0;
  for (int i = 0; i < game->npcs.count; i++) {
    if (game->npcs.items[i].kind == NPC_VILLAGER) {
      villagers++;
      alive += !game->npcs.items[i].dead;
    }
  }
  draw_text(game, &game->villagersText, game->smallFont, 12, 82, "Villagers %d of %d", alive, villagers);

  // the upgrades so far, with their levels: hover one to read about it
  float ix = 12;
  for (int u = 0; u < UP_COUNT; u++) {
    game->upgradeRects[u] = (SDL_FRect){0};
    if (hero->levels[u] == 0) {
      continue;
    }
    SDL_FRect r = {ix, 108, 26, 26};
    button_shape(renderer, r, (SDL_Color){58, 41, 41, 170});
    SDL_FPoint size = icon_size(UPGRADE_ICONS[u], 2);
    icon_draw(renderer, UPGRADE_ICONS[u], r.x + (r.w - size.x) / 2, r.y + (r.h - size.y) / 2, 2);
    SDL_Texture *digit = game->digits[0][SDL_min(hero->levels[u], 9)];
    SDL_FPoint ds = texture_size(digit);
    draw_texture(renderer, digit, r.x + r.w - ds.x + 6, r.y + r.h - ds.y + 6);
    game->upgradeRects[u] = r;
    ix += 34;
  }

  for (int u = 0; u < UP_COUNT && game->scene == SCENE_PLAYING; u++) {
    if (game->upgradeRects[u].w > 0 && SDL_PointInRectFloat(&game->mouse, &game->upgradeRects[u])) {
      render_tooltip(game, (Upgrade)u);
    }
  }

  Uint64 shown = now - game->bannerShownAt;
  if (game->bannerText && shown < BANNER_NS && game->scene == SCENE_PLAYING) {
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

// Light rays turning slowly around a point, fading out towards the edges.
static void render_rays(SDL_Renderer *renderer, float cx, float cy, float radius, float angle) {
#define RAYS 14
  SDL_Vertex v[RAYS * 3];
  SDL_FColor center = {1.0f, 0.86f, 0.45f, 0.32f}, edge = {1.0f, 0.86f, 0.45f, 0.0f};
  for (int k = 0; k < RAYS; k++) {
    float a0 = angle + (float)k * 2 * SDL_PI_F / RAYS, a1 = a0 + SDL_PI_F / RAYS;
    v[k * 3 + 0] = (SDL_Vertex){{cx, cy}, center, {0, 0}};
    v[k * 3 + 1] = (SDL_Vertex){{cx + SDL_cosf(a0) * radius, cy + SDL_sinf(a0) * radius}, edge, {0, 0}};
    v[k * 3 + 2] = (SDL_Vertex){{cx + SDL_cosf(a1) * radius, cy + SDL_sinf(a1) * radius}, edge, {0, 0}};
  }
  SDL_RenderGeometry(renderer, NULL, v, RAYS * 3, NULL, 0);
#undef RAYS
}

// The key to press for a card, on a key cap.
static void render_key(Game *game, int i, SDL_FRect key) {
  SDL_Renderer *renderer = game->engine.renderer;
  button_shape(renderer, key, (SDL_Color){58, 41, 41, 255});
  SDL_FPoint size = texture_size(game->digits[0][i + 1]);
  draw_texture(renderer, game->digits[0][i + 1], key.x + (key.w - size.x) / 2 + 1, key.y + (key.h - size.y) / 2);
}

// A card: the key to press, a tag, maybe an icon, the name, the
// description, and pips for the levels of an upgrade or the price.
static void render_card(Game *game, int i, SDL_FRect face, float time) {
  SDL_Renderer *renderer = game->engine.renderer;
  const Card *card = &game->cards[i];
  const SDL_Color outline = {58, 41, 41, 255};
  float bob = SDL_roundf(SDL_sinf(time * 4 + (float)i) * 1) * 2;
  SDL_FPoint name = texture_size(card->name), tag = texture_size(card->tag), footer = texture_size(card->footer);

  if (game->cardCompact) {
    if (card->icon >= 0) {
      SDL_FPoint icon = icon_size((IconKind)card->icon, 4);
      icon_draw(renderer, (IconKind)card->icon, SDL_roundf(face.x + (COMPACT_TEXT_X - icon.x) / 2 + 2),
                SDL_roundf(face.y + (face.h - icon.y) / 2) + bob, 4);
    }
    render_key(game, i, (SDL_FRect){face.x + face.w - 28, face.y + 6, 22, 22});
    float x = face.x + COMPACT_TEXT_X;
    draw_texture(renderer, card->name, x, face.y + 6);
    draw_texture(renderer, card->text, x, face.y + 8 + name.y);
    float bottom = face.y + face.h - 6;
    draw_texture(renderer, card->tag, x, bottom - tag.y);
    draw_texture(renderer, card->footer, face.x + face.w - 8 - footer.x, bottom - footer.y);
    return;
  }

  render_key(game, i, (SDL_FRect){face.x + 6, face.y + 6, 26, 26});
  draw_texture(renderer, card->tag, face.x + face.w - 8 - tag.x, face.y + 8);
  float y = face.y + 44;
  if (card->icon >= 0) {
    SDL_FPoint icon = icon_size((IconKind)card->icon, 4);
    icon_draw(renderer, (IconKind)card->icon, SDL_roundf(face.x + (face.w - icon.x) / 2), y + bob, 4);
    y += icon.y + 8;
  }
  draw_texture(renderer, card->name, face.x + (face.w - name.x) / 2, y);
  draw_texture(renderer, card->text, face.x + 12, y + name.y + 8);

  if (card->pips > 0) {
    float pip = 10, gap = 6, w = (float)card->pips * pip + (float)(card->pips - 1) * gap;
    float x = SDL_roundf(face.x + (face.w - w) / 2), py = face.y + face.h - 22;
    bool blink = SDL_fmodf(time, 0.5f) < 0.3f;
    for (int k = 0; k < card->pips; k++, x += pip + gap) {
      fill(renderer, x, py, pip, pip, outline);
      SDL_Color c = k < card->pipsFilled ? (SDL_Color){255, 255, 255, 255}
                    : k == card->pipsFilled
                        ? (blink ? (SDL_Color){255, 232, 96, 255} : (SDL_Color){255, 255, 255, 255})
                        : (SDL_Color){0, 0, 0, 70};
      fill(renderer, x + 2, py + 2, pip - 4, pip - 4, c);
    }
  }
  if (card->footer) {
    draw_texture(renderer, card->footer, face.x + (face.w - footer.x) / 2, face.y + face.h - footer.y - 8);
  }
}

// Height of the rows of cards.
static float cards_height(const Game *game) {
  int rows = (game->numCards + game->cardColumns - 1) / SDL_max(1, game->cardColumns);
  return (float)rows * (game->cardHeight + BUTTON_DEPTH) + (float)(rows - 1) * CARD_GAP;
}

// The cards popping up one after the other, from `time` seconds after the
// screen opened.
static void render_cards(Game *game, Uint64 now, float cardsY, float time) {
  SDL_Renderer *renderer = game->engine.renderer;
  float cardW = game->cardWidth, cardH = game->cardHeight;
  int columns = SDL_max(1, game->cardColumns);
  float rowW = cardW * (float)columns + CARD_GAP * (float)(columns - 1);
  float flash = (float)(now - game->flashAt) / (float)SDL_NS_PER_SECOND;
  float denied = (float)(now - game->deniedAt) / (float)SDL_NS_PER_SECOND;
  for (int i = 0; i < game->numCards; i++) {
    const Card *card = &game->cards[i];
    float x = SDL_roundf((float)game->engine.width / 2 - rowW / 2) + (float)(i % columns) * (cardW + CARD_GAP);
    float y = cardsY + (float)(i / columns) * (cardH + BUTTON_DEPTH + CARD_GAP);
    game->cardRects[i] = (SDL_FRect){x, y, cardW, cardH};
    float appear = (time - 0.12f - (float)i * (game->cardCompact ? 0.05f : 0.08f)) / 0.35f;
    if (appear <= 0 || (game->chosen >= 0 && i != game->chosen)) {
      continue;
    }
    bool selected = i == game->selected;
    if (i == game->deniedCard && now - game->deniedAt < DENIED_NS) {
      x += SDL_roundf(SDL_sinf(denied * 70) * 4 * (1 - denied / ((float)DENIED_NS / SDL_NS_PER_SECOND)));
    }
    float lift = selected ? (game->cardCompact ? 4.0f : 6.0f) : 0;
    SDL_FRect r = {x, y + (1 - ease_out_back(appear)) * 70 - lift, cardW, cardH};
    if (selected) {
      float pulse = 0.5f + 0.5f * SDL_sinf(time * 8);
      button_shape(renderer, (SDL_FRect){r.x - 6, r.y - 6, r.w + 12, r.h + BUTTON_DEPTH + 12},
                   (SDL_Color){255, 232, 120, (Uint8)(70 + 90 * pulse)});
    }
    bool pressed = i == game->chosen || (game->mouseDown && button_contains(game->cardRects[i], game->mouse));
    ButtonState state = !selected ? BUTTON_IDLE : pressed ? BUTTON_PRESSED : BUTTON_HOVER;
    SDL_Color color = card->color;
    if (card->disabled) {
      // greyed out
      Uint8 grey = (Uint8)((color.r + color.g + color.b) / 3);
      color = (SDL_Color){(Uint8)((color.r + grey * 2) / 3 * 0.8f), (Uint8)((color.g + grey * 2) / 3 * 0.8f),
                          (Uint8)((color.b + grey * 2) / 3 * 0.8f), 255};
    }
    ButtonStyle style = button_style(color);
    SDL_FRect face = button_draw(renderer, r, &style, state);
    render_card(game, i, face, time);
    if (i == game->flashCard && now - game->flashAt < CHOICE_FLASH_NS && SDL_fmodf(flash, 0.1f) < 0.05f) {
      button_shape(renderer, face, (SDL_Color){255, 255, 255, 200});
    }
  }
}

// Twinkling stars all over, and sparkles rising.
static void render_stars(Game *game, float time, int stars, int sparkles) {
  SDL_Renderer *renderer = game->engine.renderer;
  float W = (float)game->engine.width, H = (float)game->engine.height;
  for (Uint32 i = 0; i < (Uint32)stars; i++) {
    float glow = SDL_sinf(time * (2 + hash01(i * 3 + 2) * 3) + (float)i);
    if (glow > 0.2f) {
      draw_star(renderer, hash01(i * 3) * W, hash01(i * 3 + 1) * H, glow > 0.85f ? 2 : glow > 0.55f ? 1 : 0,
                (SDL_Color){255, 240, 180, (Uint8)(255 * glow)});
    }
  }
  for (Uint32 i = 0; i < (Uint32)sparkles; i++) {
    float speed = 30 + hash01(i + 100) * 60;
    float y = H + 10 - SDL_fmodf(time * speed + hash01(i + 200) * (H + 20), H + 20);
    float x = hash01(i + 300) * W + SDL_sinf(time * 2 + (float)i) * 8;
    fill(renderer, x, y, 2, 2, (SDL_Color){255, 226, 130, 200});
  }
}

static void render_level_up(Game *game, Uint64 now) {
  SDL_Renderer *renderer = game->engine.renderer;
  float W = (float)game->engine.width, H = (float)game->engine.height;
  float time = (float)(now - game->panelOpenedAt) / (float)SDL_NS_PER_SECOND;

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, 22, 14, 40, 175);
  SDL_RenderFillRect(renderer, NULL);

  SDL_FPoint title = texture_size(game->panelTitle), hint = texture_size(game->levelUpHint);
  float cardH = game->cardHeight;
  float skipH = 26;
  float top = SDL_roundf((H - (title.y + 28 + cardH + BUTTON_DEPTH + 22 + skipH + BUTTON_DEPTH + 14 + hint.y)) / 2);
  float cardsY = top + title.y + 28;
  float cx = W / 2, cy = cardsY + cardH / 2;

  render_rays(renderer, cx, cy, ease_out_cubic(time * 2) * SDL_max(W, H), time * 0.25f);
  render_stars(game, time, 48, 32);
  // a burst when it opens
  if (time < 0.9f) {
    float p = time / 0.9f;
    for (Uint32 i = 0; i < 24; i++) {
      float a = (float)i * 2 * SDL_PI_F / 24 + hash01(i + 400) * 0.3f;
      float d = ease_out_cubic(p) * 320 * (0.5f + hash01(i + 500) * 0.7f);
      draw_star(renderer, cx + SDL_cosf(a) * d, cy + SDL_sinf(a) * d * 0.7f, p < 0.5f ? 2 : 1,
                (SDL_Color){255, 244, 160, (Uint8)(255 * (1 - p))});
    }
  }

  // the title drops in and keeps bobbing
  float drop = (1 - ease_out_back(time / 0.45f)) * -60;
  float bob = SDL_roundf(SDL_sinf(time * 3) * 1.5f) * 2;
  draw_texture(renderer, game->panelTitle, cx - title.x / 2, top + drop + bob);

  render_cards(game, now, cardsY, time);
  // no upgrade this time
  game->skipButton = (SDL_FRect){0};
  if (time > 0.5f && game->chosen < 0) {
    SDL_FPoint label = texture_size(game->skipLabel);
    SDL_FRect b = {SDL_roundf(cx - (label.x + 36) / 2), cardsY + cardH + BUTTON_DEPTH + 22, label.x + 36, skipH};
    ButtonStyle style = button_style((SDL_Color){214, 206, 196, 255});
    SDL_FRect face = button_draw(renderer, b, &style, button_state(b, game->mouse, game->mouseDown));
    draw_texture(renderer, game->skipLabel, face.x + (face.w - label.x) / 2, face.y + (face.h - label.y) / 2);
    game->skipButton = b;
  }
  if (time > 0.5f) {
    draw_texture(renderer, game->levelUpHint, cx - hint.x / 2, cardsY + cardH + BUTTON_DEPTH + 22 + skipH + BUTTON_DEPTH + 14);
  }
  // a white flash as it opens
  if (time < 0.15f) {
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, (Uint8)(200 * (1 - time / 0.15f)));
    SDL_RenderFillRect(renderer, NULL);
  }
}

static void render_shop(Game *game, Uint64 now) {
  SDL_Renderer *renderer = game->engine.renderer;
  float W = (float)game->engine.width, H = (float)game->engine.height;
  float time = (float)(now - game->panelOpenedAt) / (float)SDL_NS_PER_SECOND;

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, 34, 20, 14, 190);
  SDL_RenderFillRect(renderer, NULL);
  render_stars(game, time, 28, 0);

  SDL_FPoint title = texture_size(game->panelTitle);
  float cx = W / 2, grid = cards_height(game), bottom = 32;
  float top = SDL_roundf((H - (title.y + 12 + grid + 18 + bottom)) / 2);
  float drop = (1 - ease_out_back(time / 0.4f)) * -40;
  draw_texture(renderer, game->panelTitle, cx - title.x / 2, top + drop);

  float cardsY = top + title.y + 12;
  render_cards(game, now, cardsY, time);

  // the purse on the left, the way out on the right
  float rowW = game->cardWidth * 2 + CARD_GAP;
  float left = SDL_roundf(cx - rowW / 2), right = left + rowW;
  float y = cardsY + grid + 18;
  SDL_FPoint coin = icon_size(ICON_COIN, 4);
  icon_draw(renderer, ICON_COIN, left, y + 2, 4);
  draw_text(game, &game->shopGoldText, game->font, left + coin.x + 8, y - 4, "%d gold", game->combat.hero.gold);

  SDL_FPoint label = texture_size(game->leaveLabel);
  SDL_FRect b = {right - label.x - 40, y, label.x + 40, bottom - BUTTON_DEPTH};
  ButtonStyle style = button_style((SDL_Color){246, 239, 158, 255});
  SDL_FRect face = button_draw(renderer, b, &style, button_state(b, game->mouse, game->mouseDown));
  draw_texture(renderer, game->leaveLabel, face.x + (face.w - label.x) / 2, face.y + (face.h - label.y) / 2);
  game->leaveButton = b;
}

// The hero struck down: a red flash and a spray, a jolt, the fall, then a
// soul rising from the body while the world goes dark around it.
static void render_death(Game *game, Uint64 now, float camX, float camY) {
  SDL_Renderer *renderer = game->engine.renderer;
  float time = (float)(now - game->diedAt) / (float)SDL_NS_PER_SECOND;
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, 12, 4, 8, (Uint8)(205 * ease_out_cubic(time / 1.4f)));
  SDL_RenderFillRect(renderer, NULL);

  const Sprite *sprite = animation_frame(game->atlas.player.walk[game->facing], 0);
  float fx = SDL_roundf(game->x - camX), fy = SDL_roundf(game->y - camY);  // feet
  float hop = 0, angle = 0;
  if (time < DEATH_HOP) {
    hop = SDL_sinf(time / DEATH_HOP * SDL_PI_F) * 14;
  } else {
    float p = SDL_min(1.0f, (time - DEATH_HOP) / (DEATH_FALL - DEATH_HOP));
    angle = 90 * p * p;  // faster and faster
    if (time > DEATH_FALL && time < DEATH_FALL + 0.15f) {
      hop = SDL_sinf((time - DEATH_FALL) / 0.15f * SDL_PI_F) * 4;  // a little bounce
    }
  }
  if (game->facing == DIR_LEFT) {
    angle = -angle;
  }
  SDL_FRect dest = {fx - CHARACTER_OFFSET_X, SDL_roundf(fy - CHARACTER_OFFSET_Y - hop), sprite->source.w,
                    sprite->source.h};
  SDL_FPoint feet = {CHARACTER_OFFSET_X, CHARACTER_OFFSET_Y};
  // greying once down
  float grey = SDL_clamp((time - DEATH_FALL) / 1.0f, 0.0f, 1.0f);
  Uint8 mod = (Uint8)(255 - 110 * grey);
  SDL_SetTextureColorMod(sprite->texture, mod, (Uint8)((float)mod * 0.85f), (Uint8)((float)mod * 0.85f));
  SDL_RenderTextureRotated(renderer, sprite->texture, &sprite->source, &dest, angle, &feet, SDL_FLIP_NONE);
  SDL_SetTextureColorMod(sprite->texture, 255, 255, 255);
  if (time < DEATH_HOP && (int)(time * 20) % 2 == 0) {
    // flashing white
    SDL_SetTextureBlendMode(sprite->texture, SDL_BLENDMODE_ADD);
    SDL_RenderTextureRotated(renderer, sprite->texture, &sprite->source, &dest, angle, &feet, SDL_FLIP_NONE);
    SDL_RenderTextureRotated(renderer, sprite->texture, &sprite->source, &dest, angle, &feet, SDL_FLIP_NONE);
    SDL_SetTextureBlendMode(sprite->texture, SDL_BLENDMODE_BLEND);
  }

  // the spray of the blow, falling
  if (time < 0.8f) {
    for (Uint32 i = 0; i < 18; i++) {
      float a = hash01(i + 700) * 2 * SDL_PI_F, speed = 60 + hash01(i + 800) * 140;
      float x = fx + SDL_cosf(a) * speed * time;
      float y = fy - 22 + SDL_sinf(a) * speed * time * 0.7f - 80 * time + 420 * time * time;
      SDL_Color c = i % 3 ? (SDL_Color){210, 30, 44, 255} : (SDL_Color){255, 240, 230, 255};
      c.a = (Uint8)(255 * (1 - time / 0.8f));
      fill(renderer, x, y, 4, 4, c);
    }
  }

  // the soul leaving
  float p = (time - DEATH_FALL - 0.1f) / 1.6f;
  if (p > 0 && p < 1) {
    float x = fx + SDL_sinf(p * 9) * 6, y = fy - 20 - p * 100;
    Uint8 a = (Uint8)(255 * SDL_sinf(p * SDL_PI_F));
    SDL_Color glow = {170, 210, 255, a}, light = {240, 248, 255, a};
    fill(renderer, x - 2, y - 6, 4, 2, glow);
    fill(renderer, x - 4, y - 4, 8, 6, glow);
    fill(renderer, x - 2, y + 2, 4, 4, glow);
    fill(renderer, x - 2, y - 4, 4, 6, light);
    if ((int)(time * 10) % 2) {
      draw_star(renderer, x + 8, y + 10, 1, (SDL_Color){240, 248, 255, (Uint8)(a / 2)});
    }
  }

  if (time < 0.15f) {
    SDL_SetRenderDrawColor(renderer, 220, 20, 30, (Uint8)(140 * (1 - time / 0.15f)));
    SDL_RenderFillRect(renderer, NULL);
  }
}

static void render_panel(Game *game, Uint64 now) {
  Scene scene = game->scene == SCENE_MENU ? game->menuReturn : game->scene;
  if (scene == SCENE_PLAYING || scene == SCENE_DYING || !game->panelTitle) {
    return;
  }
  if (scene == SCENE_LEVEL_UP) {
    render_level_up(game, now);
    return;
  }
  if (scene == SCENE_SHOP) {
    render_shop(game, now);
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
  ButtonStyle style = button_style((SDL_Color){246, 239, 158, 255});
  SDL_FRect face = button_draw(renderer, (SDL_FRect){x - 4, y - 4, w + 8, h + 8}, &style, BUTTON_IDLE);
  SDL_RenderTexture(renderer, game->panelTitle, NULL,
                    &(SDL_FRect){SDL_roundf(face.x + (face.w - tw) / 2), face.y + 22, tw, th});
  if (game->panelBody) {
    SDL_RenderTexture(renderer, game->panelBody, NULL, &(SDL_FRect){face.x + 26, face.y + 46 + th, bw, bh});
  }
}

static const char *menu_label(const Game *game, int item, char *buf, size_t size) {
  bool armed = game->menuArmed == item;
  switch ((MenuItem)item) {
    case MENU_RESUME: return "Resume";
    case MENU_NEW_WORLD: return armed ? "Sure? Lose this run" : "New world";
    case MENU_MUSIC: SDL_snprintf(buf, size, "Music: %s", game->musicMuted ? "off" : "on"); return buf;
    case MENU_FULLSCREEN: return "Fullscreen";
    case MENU_VSYNC: SDL_snprintf(buf, size, "VSync: %s", game->engine.vsync ? "on" : "off"); return buf;
    case MENU_MAP: SDL_snprintf(buf, size, "Map: %s", game->showMap ? "shown" : "hidden"); return buf;
    case MENU_LEVEL_UP: return "Level up now";
    case MENU_QUIT: return armed ? "Sure? Quit" : "Quit";
    case MENU_COUNT: break;
  }
  return "";
}

static const char *const MENU_KEYS[MENU_COUNT] = {"Esc", "", "N", "F", "C", "M", "L", ""};

#define MENU_WIDTH 300
#define MENU_ITEM_HEIGHT 28

static void render_menu(Game *game, Uint64 now) {
  SDL_Renderer *renderer = game->engine.renderer;
  float time = (float)(now - game->menuOpenedAt) / (float)SDL_NS_PER_SECOND;
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 130);
  SDL_RenderFillRect(renderer, NULL);

  SDL_FPoint title = texture_size(game->menuTitle);
  float step = MENU_ITEM_HEIGHT + BUTTON_DEPTH + 6;
  float w = MENU_WIDTH, h = 22 + title.y + 18 + step * (float)MENU_COUNT + 8 + 20 + 18;
  float x = SDL_roundf(((float)game->engine.width - w) / 2);
  float y = SDL_roundf(((float)game->engine.height - h) / 2 + (1 - ease_out_back(time / 0.3f)) * 30);
  ButtonStyle panel = button_style((SDL_Color){246, 239, 158, 255});
  SDL_FRect face = button_draw(renderer, (SDL_FRect){x, y, w, h}, &panel, BUTTON_IDLE);
  draw_texture(renderer, game->menuTitle, face.x + (face.w - title.x) / 2, face.y + 20);

  float itemY = face.y + 22 + title.y + 18;
  for (int i = 0; i < MENU_COUNT; i++, itemY += step) {
    SDL_FRect r = {face.x + 22, itemY, face.w - 44, MENU_ITEM_HEIGHT};
    game->menuRects[i] = r;
    bool selected = i == game->menuSelected;
    ButtonState state = !selected ? BUTTON_IDLE
                        : game->mouseDown && button_contains(r, game->mouse) ? BUTTON_PRESSED
                                                                             : BUTTON_HOVER;
    SDL_Color color = game->menuArmed == i ? (SDL_Color){236, 112, 96, 255} : (SDL_Color){250, 206, 96, 255};
    if (!selected) {
      color = (SDL_Color){(Uint8)(color.r * 0.9f), (Uint8)(color.g * 0.9f), (Uint8)(color.b * 0.9f), 255};
    }
    ButtonStyle style = button_style(color);
    SDL_FRect b = button_draw(renderer, r, &style, state);
    char buf[64];
    draw_text(game, &game->menuText[i], game->smallFont, b.x + 10, text_middle(&game->menuText[i], game->smallFont, b.y, b.h), "%s",
              menu_label(game, i, buf, sizeof(buf)));
    if (MENU_KEYS[i][0]) {
      // the shortcut, on a key cap
      float kw = text_width(&game->menuKeys[i], game->smallFont, MENU_KEYS[i]) + 8;
      SDL_FRect cap = {SDL_floorf((b.x + b.w - 6 - kw) / 2) * 2, b.y + 2, SDL_ceilf(kw / 2) * 2, b.h - 4};
      button_shape(renderer, cap, (SDL_Color){58, 41, 41, 200});
      draw_text(game, &game->menuKeys[i], game->smallFont, cap.x + 4, text_middle(&game->menuKeys[i], game->smallFont, cap.y, cap.h),
                "%s", MENU_KEYS[i]);
    }
  }

  char info[96];
  SDL_snprintf(info, sizeof(info), "World %" SDL_PRIu64 "   %d fps%s", game->seed, game->fps,
               game->engine.vsync ? "" : ", uncapped");
  draw_text(game, &game->infoText, game->smallFont,
            SDL_roundf(face.x + (face.w - text_width(&game->infoText, game->smallFont, info)) / 2), itemY + 6, "%s", info);
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
  float shake = game->shake;
  float sinceDeath = (float)(now - game->diedAt) / (float)SDL_NS_PER_SECOND;
  if (game->heroDown && sinceDeath < 0.45f) {
    shake = DEATH_SHAKE * (1 - sinceDeath / 0.45f);
  }
  if (shake > 0) {
    camX += SDL_roundf((SDL_randf() - 0.5f) * 2 * shake);
    camY += SDL_roundf((SDL_randf() - 0.5f) * 2 * shake);
  }

  update_roof(game, now);
  SDL_SetRenderDrawColor(game->engine.renderer, 0x70, 0xc8, 0x40, 0xff);
  SDL_RenderClear(game->engine.renderer);
  render_world(game, camX, camY, t);
  render_minimap(game, t);
  render_hud(game, now);
  if (game->heroDown) {
    render_death(game, now, camX, camY);
  }
  render_panel(game, now);
  if (game->scene == SCENE_MENU) {
    render_menu(game, now);
  }
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

static void stop_music(Game *game, int fadeMs) {
  if (game->musicPlaying >= 0 && game->musicTrack[game->musicPlaying]) {
    MIX_Track *track = game->musicTrack[game->musicPlaying];
    MIX_StopTrack(track, MIX_TrackMSToFrames(track, fadeMs));
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

// A track playing one short sound, NULL without it. The engine owns the audio.
static MIX_Track *sound_track(Engine *engine, const char *file, float gain) {
  MIX_Audio *audio = engine_load_audio(engine, file, true);
  MIX_Track *track = audio ? MIX_CreateTrack(engine->mixer) : NULL;
  if (track) {
    MIX_SetTrackAudio(track, audio);
    MIX_SetTrackGain(track, gain);
  }
  return track;
}

static void open_menu(Game *game) {
  game->menuReturn = game->scene;
  game->scene = SCENE_MENU;
  game->menuSelected = 0;
  game->menuArmed = -1;
  game->menuOpenedAt = SDL_GetTicksNS();
  play_sound(game->selectTrack);
}

static void close_menu(Game *game) {
  if (game->menuReturn == SCENE_PLAYING) {
    resume_play(game);
  } else {
    game->scene = game->menuReturn;
  }
  play_sound(game->tickTrack);
}

static void select_menu(Game *game, int item) {
  if (item != game->menuSelected) {
    game->menuSelected = item;
    game->menuArmed = -1;
    play_sound(game->tickTrack);
  }
}

static SDL_AppResult activate_menu(Game *game, int item) {
  play_sound(game->selectTrack);
  switch ((MenuItem)item) {
    case MENU_RESUME: close_menu(game); break;
    case MENU_NEW_WORLD:
      if (game->menuArmed != item) {
        game->menuArmed = item;
        break;
      }
      if (!new_world(game, random_seed())) {
        return SDL_APP_FAILURE;
      }
      resume_play(game);
      break;
    case MENU_MUSIC: toggle_music(game); break;
    case MENU_FULLSCREEN: engine_toggle_fullscreen(&game->engine); break;
    case MENU_VSYNC: engine_set_vsync(&game->engine, !game->engine.vsync); break;
    case MENU_MAP: game->showMap = !game->showMap; break;
    case MENU_LEVEL_UP:
      if (game->menuReturn != SCENE_PLAYING) {
        play_sound(game->deniedTrack);
        break;
      }
      free_level(game);
      break;
    case MENU_QUIT:
      if (game->menuArmed != item) {
        game->menuArmed = item;
        break;
      }
      return SDL_APP_SUCCESS;
    case MENU_COUNT: break;
  }
  return SDL_APP_CONTINUE;
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
  // the pixel font is drawn on a grid of 12 to the em: multiples of 12 keep
  // every one of its pixels the same size
  game->font = engine_load_font(engine, "yoster.ttf", 24);
  game->smallFont = engine_load_font(engine, "yoster.ttf", 12);
  game->bigFont = engine_load_font(engine, "yoster.ttf", 48);
  if (!game->splash || !game->font || !game->smallFont || !game->bigFont || !atlas_load(&game->atlas, engine)) {
    return SDL_APP_FAILURE;
  }
  // upgrade names on two lines stay centred on their card
  TTF_SetFontWrapAlignment(game->font, TTF_HORIZONTAL_ALIGN_CENTER);
  for (int i = 0; i < NUM_GREETINGS; i++) {
    game->greetings[i] = engine_render_text(engine, game->smallFont, GREETINGS[i]);
  }
  game->oink = engine_render_text(engine, game->smallFont, "Oink!");
  game->help = engine_render_text(engine, game->smallFont, "Help!");
  game->nextWaveLabel = engine_render_text(engine, game->smallFont, "Next wave");
  game->shopLabel = engine_render_text(engine, game->smallFont, "Shop");
  game->leaveLabel = engine_render_text(engine, game->smallFont, "Leave");
  game->skipLabel = engine_render_text(engine, game->smallFont, "Skip  X");
  game->shopGoldText.outlined = true;
  game->hpText.outlined = game->levelText.outlined = true;
  for (int i = 0; i < MENU_COUNT; i++) {
    game->menuKeys[i].outlined = true;
  }
  game->menuTitle = engine_render_text(engine, game->font, "Paused");
  game->chosen = game->flashCard = game->deniedCard = -1;
  game->levelUpHint = engine_render_outlined_text(engine, game->smallFont, "Press 1, 2 or 3, the arrows and Enter, or click",
                                                  (SDL_Color){255, 255, 255, 255}, (SDL_Color){58, 41, 41, 255});
  for (int i = 0; i < 10; i++) {
    char digit[2] = {(char)('0' + i), 0};
    SDL_Color outline = {40, 24, 24, 255};
    game->digits[0][i] = engine_render_outlined_text(engine, game->smallFont, digit, (SDL_Color){255, 255, 255, 255}, outline);
    game->digits[1][i] = engine_render_outlined_text(engine, game->smallFont, digit, (SDL_Color){255, 70, 60, 255}, outline);
    game->digits[2][i] = engine_render_outlined_text(engine, game->smallFont, digit, (SDL_Color){120, 232, 90, 255}, outline);
  }

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
  game->hurtTrack = sound_track(engine, "hero-hurt.wav", HURT_GAIN);
  game->heroDeathTrack = sound_track(engine, "hero-death.wav", HURT_GAIN);
  game->levelUpTrack = sound_track(engine, "level-up.wav", UI_GAIN);
  game->selectTrack = sound_track(engine, "select.wav", UI_GAIN);
  game->tickTrack = sound_track(engine, "tick.wav", UI_GAIN);
  game->explosionTrack = sound_track(engine, "explosion.wav", 0.9f);
  game->coinTrack = sound_track(engine, "coin.wav", UI_GAIN);
  game->potionTrack = sound_track(engine, "potion.wav", UI_GAIN);
  game->deniedTrack = sound_track(engine, "denied.wav", UI_GAIN);
  game->splatTrack = sound_track(engine, "splat.wav", 0.7f);
  game->repelTrack = sound_track(engine, "repel.wav", 0.45f);
  game->blockTrack = sound_track(engine, "block.wav", 0.8f);
  game->ricochetTrack = sound_track(engine, "ricochet.wav", 0.7f);
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
  if (event->type == SDL_EVENT_MOUSE_MOTION || event->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
      event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
    // buttons are in logical coordinates, like everything drawn
    SDL_ConvertEventToRenderCoordinates(game->engine.renderer, event);
    bool motion = event->type == SDL_EVENT_MOUSE_MOTION;
    game->mouse = motion ? (SDL_FPoint){event->motion.x, event->motion.y} : (SDL_FPoint){event->button.x, event->button.y};
    if (!motion && event->button.button == SDL_BUTTON_LEFT) {
      game->mouseDown = event->type == SDL_EVENT_MOUSE_BUTTON_DOWN;
    }
    bool clicked = event->type == SDL_EVENT_MOUSE_BUTTON_UP && event->button.button == SDL_BUTTON_LEFT;
    if (game->scene == SCENE_PLAYING && clicked) {
      if (button_contains(game->nextWaveButton, game->mouse)) {
        call_next_wave(game);
      } else if (button_contains(game->shopButton, game->mouse)) {
        try_open_shop(game);
      }
      return SDL_APP_CONTINUE;
    }
    if (game->scene == SCENE_MENU) {
      for (int i = 0; i < MENU_COUNT; i++) {
        if (button_contains(game->menuRects[i], game->mouse)) {
          select_menu(game, i);
          if (clicked) {
            return activate_menu(game, i);
          }
        }
      }
      return SDL_APP_CONTINUE;
    }
    if (game->scene == SCENE_LEVEL_UP && clicked && button_contains(game->skipButton, game->mouse)) {
      skip_level(game);
      return SDL_APP_CONTINUE;
    }
    if (game->scene == SCENE_SHOP && clicked && button_contains(game->leaveButton, game->mouse)) {
      resume_play(game);
      return SDL_APP_CONTINUE;
    }
    bool cards = (game->scene == SCENE_LEVEL_UP && game->chosen < 0) || game->scene == SCENE_SHOP;
    for (int i = 0; cards && i < game->numCards; i++) {
      if (button_contains(game->cardRects[i], game->mouse)) {
        select_choice(game, i);
        if (clicked && game->scene == SCENE_SHOP) {
          buy_item(game, i);
        } else if (clicked) {
          pick_choice(game, i);
        }
      }
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
    if (game->scene == SCENE_MENU) {
      close_menu(game);
    } else if (game->scene == SCENE_SHOP) {
      resume_play(game);
    } else if (game->scene == SCENE_SPLASH) {
      open_intro(game);
    } else if (game->scene != SCENE_DYING) {
      open_menu(game);
    }
    return SDL_APP_CONTINUE;
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
    case SCENE_SHOP: {
      int n = game->numCards, choice = (int)key - (int)SDL_SCANCODE_1;
      if (choice >= 0 && choice < n) {
        buy_item(game, choice);
      } else if (key == SDL_SCANCODE_LEFT || key == SDL_SCANCODE_A) {
        select_choice(game, (game->selected + n - 1) % n);
      } else if (key == SDL_SCANCODE_RIGHT || key == SDL_SCANCODE_D) {
        select_choice(game, (game->selected + 1) % n);
      } else if ((key == SDL_SCANCODE_UP || key == SDL_SCANCODE_W) && game->selected >= game->cardColumns) {
        select_choice(game, game->selected - game->cardColumns);
      } else if ((key == SDL_SCANCODE_DOWN || key == SDL_SCANCODE_S) && game->selected + game->cardColumns < n) {
        select_choice(game, game->selected + game->cardColumns);
      } else if (enter) {
        buy_item(game, game->selected);
      } else if (key == SDL_SCANCODE_B) {
        resume_play(game);
      }
      return SDL_APP_CONTINUE;
    }
    case SCENE_LEVEL_UP: {
      int n = game->numCards;
      if (game->chosen >= 0 || n == 0) {
        return SDL_APP_CONTINUE;
      }
      int choice = (int)key - (int)SDL_SCANCODE_1;
      if (choice >= 0 && choice < n) {
        pick_choice(game, choice);
      } else if (key == SDL_SCANCODE_LEFT || key == SDL_SCANCODE_A) {
        select_choice(game, (game->selected + n - 1) % n);
      } else if (key == SDL_SCANCODE_RIGHT || key == SDL_SCANCODE_D) {
        select_choice(game, (game->selected + 1) % n);
      } else if (enter) {
        pick_choice(game, game->selected);
      } else if (key == SDL_SCANCODE_X) {
        skip_level(game);
      }
      return SDL_APP_CONTINUE;
    }
    case SCENE_DYING:
      return SDL_APP_CONTINUE;
    case SCENE_MENU:
      if (key == SDL_SCANCODE_UP || key == SDL_SCANCODE_W) {
        select_menu(game, (game->menuSelected + MENU_COUNT - 1) % MENU_COUNT);
      } else if (key == SDL_SCANCODE_DOWN || key == SDL_SCANCODE_S) {
        select_menu(game, (game->menuSelected + 1) % MENU_COUNT);
      } else if (enter) {
        return activate_menu(game, game->menuSelected);
      }
      return SDL_APP_CONTINUE;
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
      } else if (key == SDL_SCANCODE_R) {
        if (!new_world(game, random_seed())) {
          return SDL_APP_FAILURE;
        }
        resume_play(game);
      }
      return SDL_APP_CONTINUE;
    case SCENE_PLAYING:
      break;
  }

  switch (key) {
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
      call_next_wave(game);
      break;
    case SDL_SCANCODE_C:
      engine_set_vsync(&game->engine, !game->engine.vsync);
      break;
    case SDL_SCANCODE_M:
      game->showMap = !game->showMap;
      break;
    case SDL_SCANCODE_Q:
      play_sound(combat_drink_potion(&game->combat) ? game->potionTrack : game->deniedTrack);
      break;
    case SDL_SCANCODE_E:
      // not from inside a house, where time stands still
      if (!game->frozen && combat_throw_bomb(&game->combat, game->x, game->y)) {
        play_sound(game->sfxTrack);
      } else {
        play_sound(game->deniedTrack);
      }
      break;
    case SDL_SCANCODE_B:
      try_open_shop(game);
      break;
    case SDL_SCANCODE_L:
      free_level(game);
      break;
    case SDL_SCANCODE_N:
      toggle_music(game);
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

  if (game->scene == SCENE_LEVEL_UP && game->chosen >= 0 && now - game->chosenAt >= CHOICE_FLASH_NS) {
    apply_choice(game);
  }
  if (game->scene == SCENE_DYING && now - game->diedAt >= DEATH_SCENE_NS) {
    open_game_over(game);
    play_music(game, MUSIC_GAME_OVER, true);
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

  engine_present(&game->engine);
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
  MIX_Track *tracks[] = {game->hurtTrack,      game->heroDeathTrack, game->levelUpTrack, game->selectTrack,
                         game->tickTrack,      game->explosionTrack, game->coinTrack,    game->potionTrack,
                         game->deniedTrack,    game->splatTrack,     game->repelTrack, game->blockTrack,
                         game->ricochetTrack};
  for (int i = 0; i < (int)SDL_arraysize(tracks); i++) {
    if (tracks[i]) {
      MIX_DestroyTrack(tracks[i]);
    }
  }
  TextSlot *slots[] = {&game->infoText, &game->hpText,     &game->levelText, &game->statusText,   &game->villagersText,
                       &game->goldText, &game->potionText, &game->bombText,  &game->shopGoldText,
                       &game->amuletText, &game->wisdomText, &game->shieldText, &game->tipTitle, &game->tipText};
  for (int i = 0; i < (int)SDL_arraysize(slots); i++) {
    SDL_DestroyTexture(slots[i]->texture);
  }
  for (int i = 0; i < MENU_COUNT; i++) {
    SDL_DestroyTexture(game->menuText[i].texture);
    SDL_DestroyTexture(game->menuKeys[i].texture);
  }
  SDL_DestroyTexture(game->menuTitle);
  SDL_DestroyTexture(game->bannerText);
  SDL_DestroyTexture(game->panelTitle);
  SDL_DestroyTexture(game->panelBody);
  SDL_DestroyTexture(game->minimap);
  for (int i = 0; i < NUM_GREETINGS; i++) {
    SDL_DestroyTexture(game->greetings[i]);
  }
  SDL_DestroyTexture(game->oink);
  SDL_DestroyTexture(game->help);
  SDL_DestroyTexture(game->nextWaveLabel);
  SDL_DestroyTexture(game->levelUpHint);
  SDL_DestroyTexture(game->shopLabel);
  SDL_DestroyTexture(game->leaveLabel);
  SDL_DestroyTexture(game->skipLabel);
  free_cards(game);
  for (int i = 0; i < 10; i++) {
    SDL_DestroyTexture(game->digits[0][i]);
    SDL_DestroyTexture(game->digits[1][i]);
    SDL_DestroyTexture(game->digits[2][i]);
  }
  SDL_free(game->drawables);
  combat_free(&game->combat);
  npcs_free(&game->npcs);
  world_free(game->world);
  atlas_free(&game->atlas);
  engine_shutdown(&game->engine);
  SDL_free(game);
}
