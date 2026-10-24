/*
  Copyright (C) 2013 batiste.bieler@gmail.com

  All the game graphics: tiles indexed by id, character and animal animations
 */

#ifndef GAME_ATLAS_H
#define GAME_ATLAS_H

#include "engine.h"
#include "sprite.h"
#include "tiles.h"

#define NUM_VILLAGER_LOOKS 6

// rows of character.png
typedef enum { DIR_DOWN, DIR_RIGHT, DIR_UP, DIR_LEFT, DIR_COUNT } Direction;

typedef enum MonsterKind { MON_UNDERLING, MON_LORD, MON_BOSS, MON_SKELETON, MON_KINDS } MonsterKind;

typedef struct CharacterSheet {
  Animation *walk[DIR_COUNT];
  Animation *swordRight;
  Animation *swordLeft;
} CharacterSheet;

typedef struct Atlas {
  Sprite tiles[GID_COUNT];
  SDL_Color roofColors[NUM_HOUSE_STYLES];  // for the minimap
  CharacterSheet player;
  CharacterSheet villagers[NUM_VILLAGER_LOOKS];
  Animation *pig;  // faces right
  Sprite monsters[MON_KINDS];  // one frame each, feet at the bottom centre
} Atlas;

bool atlas_load(Atlas *atlas, Engine *engine);
void atlas_free(Atlas *atlas);

#endif
