/*
  Copyright (C) 2013 batiste.bieler@gmail.com

  Villagers and animals
 */

#ifndef GAME_NPC_H
#define GAME_NPC_H

#include "atlas.h"
#include "world.h"

typedef enum NpcKind { NPC_VILLAGER, NPC_PIG } NpcKind;

typedef enum NpcState {
  NPC_IDLE,
  NPC_WALKING,
  NPC_ENTERING,  // walking through a door
  NPC_INSIDE,
  NPC_EXITING,
} NpcState;

typedef struct Npc {
  NpcKind kind;
  NpcState state;
  int look;     // villager shirt colour
  int home;     // house index, villagers only
  int village;  // village they currently belong to
  int house;    // house they are in or heading to, -1 if none
  int timer;    // ticks left in the current state

  float x, y;          // feet position in world pixels
  float prevX, prevY;  // at the previous tick, for interpolation
  float targetX, targetY;
  Direction dir;
  bool faceLeft;  // for side-only sprites
  bool moving;

  int *path;
  int pathLen, pathPos;
  Uint64 rng;
} Npc;

typedef struct Npcs {
  Npc *items;
  int count;
} Npcs;

bool npcs_spawn(Npcs *npcs, const World *world, Uint64 seed);
void npcs_free(Npcs *npcs);
void npcs_update(Npcs *npcs, const World *world);

// Frame to draw, and the sprite offset from the feet position.
const Sprite *npc_sprite(const Npc *npc, const Atlas *atlas, Uint64 tick, bool *flip, SDL_FPoint *offset);

static inline bool npc_visible(const Npc *npc) {
  return npc->state != NPC_INSIDE;
}

#endif
