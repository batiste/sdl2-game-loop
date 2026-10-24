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

typedef struct CharacterSheet {
  Animation *walk[DIR_COUNT];
  Animation *swordRight;
  Animation *swordLeft;
} CharacterSheet;

typedef struct Atlas {
  Sprite tiles[GID_COUNT];
  CharacterSheet player;
  CharacterSheet villagers[NUM_VILLAGER_LOOKS];
  Animation *pig;  // faces right
} Atlas;

bool atlas_load(Atlas *atlas, Engine *engine);
void atlas_free(Atlas *atlas);

#endif
