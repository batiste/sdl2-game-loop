/*
  Copyright (C) 2013 batiste.bieler@gmail.com

  The invasion of the evil lords: waves of demons attack the villages.

  The hero's weapons fire on their own at the nearest demons; killed demons
  leave souls (experience). Each level offers a choice of upgrades.
 */

#ifndef GAME_COMBAT_H
#define GAME_COMBAT_H

#include "atlas.h"
#include "npc.h"
#include "world.h"

#define MAX_UPGRADE_LEVEL 5
#define UPGRADE_CHOICES 3

typedef enum Upgrade {
  // weapons
  UP_SWORD,
  UP_STONES,
  UP_FLAMES,
  UP_AURA,
  // passives
  UP_SPEED,
  UP_VITALITY,
  UP_MIGHT,
  UP_HASTE,
  UP_MAGNET,
  UP_COUNT,
  UP_HEAL = UP_COUNT,  // offered when everything is maxed
} Upgrade;

typedef enum CombatEvent {
  EV_LEVEL_UP = 1 << 0,
  EV_PLAYER_DIED = 1 << 1,
  EV_BOSS_KILLED = 1 << 2,
  EV_MESSAGE = 1 << 3,  // see Combat.message
  EV_SLASH = 1 << 4,
  EV_WAVE_STARTED = 1 << 5,
  EV_WAVE_CLEARED = 1 << 6,
} CombatEvent;

typedef struct Hero {
  int hp, maxHp;
  int xp, level, nextLevel;
  int levels[UP_COUNT];
  int pendingLevels;
  int invulnerable;  // ticks
  int regenTimer;

  int swordTimer, stoneTimer, auraTimer;
  float flameAngle;
  int slashTimer;  // the sword animation is playing
  bool slashLeft;
} Hero;

typedef struct Monster {
  MonsterKind kind;
  float x, y;  // feet
  float prevX, prevY;
  float kickX, kickY;  // knockback velocity
  float hp, maxHp;
  int hurtTimer;
  int attackTimer;
  int shootTimer;
  int retarget;
  int target;  // -1 the hero, >= 0 a villager, -2 none
  int flameCooldown, auraCooldown;
  bool faceLeft;
  bool moving;
  float wanderX, wanderY;
  int wanderTimer;
  int stuck;  // ticks without progress towards the hero
} Monster;

typedef struct Shot {
  float x, y, vx, vy;
  float prevX, prevY;
  int life;
  float damage;
  int pierce;
  bool hostile;
  bool boss;
  int lastHit;  // monster index, so piercing stones hit each one once
} Shot;

typedef struct Soul {
  float x, y;
  int value;  // xp, 0 for a heart
} Soul;

// half of a killed monster drifting away
typedef struct Debris {
  MonsterKind kind;
  bool right;
  float x, y, vx;
  int life;
} Debris;

typedef struct Slash {
  float x, y, angle, range;
  int life;
} Slash;

typedef enum WaveState { WAVE_CALM, WAVE_ATTACK } WaveState;

#define MAX_SHOTS 256
#define MAX_SOULS 512
#define MAX_DEBRIS 128
#define MAX_SLASHES 8

typedef struct Combat {
  Uint64 rng;
  Hero hero;

  Monster *monsters;
  int numMonsters, capMonsters;
  Shot shots[MAX_SHOTS];
  int numShots;
  Soul souls[MAX_SOULS];
  int numSouls;
  Debris debris[MAX_DEBRIS];
  int numDebris;
  Slash slashes[MAX_SLASHES];
  int numSlashes;

  int wave;
  WaveState waveState;
  int waveTimer;  // ticks until the next wave while calm
  int toSpawn;
  int spawnTimer;
  int targetVillage;
  bool bossPending;
  bool bossAlive;

  // walking cost from every cell to the hero, shared by all the demons
  float *field;
  int fieldTimer;
  int fieldCell;

  int kills;
  int villagersLost;
  int events;
  char message[128];
} Combat;

void combat_init(Combat *combat, Uint64 seed);
void combat_free(Combat *combat);

// One tick. Returns CombatEvent flags.
int combat_update(Combat *combat, const World *world, Npcs *npcs, float heroX, float heroY);

float combat_speed_factor(const Combat *combat);
float combat_pickup_radius(const Combat *combat);
float combat_aura_radius(const Combat *combat);
int combat_flame_count(const Combat *combat);
float combat_flame_radius(void);

// Level up: pick UPGRADE_CHOICES options, then apply the chosen one.
int combat_upgrade_options(Combat *combat, Upgrade out[UPGRADE_CHOICES]);
void combat_apply_upgrade(Combat *combat, Upgrade upgrade);
const char *upgrade_name(Upgrade upgrade);
const char *upgrade_description(Upgrade upgrade);

int combat_alive_monsters(const Combat *combat);

#endif
