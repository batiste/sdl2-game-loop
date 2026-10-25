/*
  Copyright (C) 2013 batiste.bieler@gmail.com

  The invasion of the evil lords: waves of demons attack the villages.

  The hero's weapons fire on their own at the nearest demons; each kill
  gives experience, and some leave gold. Each level offers a choice of
  upgrades; between waves, gold buys potions, bombs and amulets at the shop.
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
  UP_MOLASSES,
  UP_REPEL,
  // passives
  UP_SPEED,
  UP_VITALITY,
  UP_MIGHT,
  UP_HASTE,
  UP_COUNT,
  UP_HEAL = UP_COUNT,  // offered when everything is maxed
} Upgrade;

// sold at the shop
typedef enum Item {
  ITEM_POTION,
  ITEM_BOMB,
  ITEM_AMULET,
  ITEM_WISDOM,
  ITEM_SHIELD,
  ITEM_COUNT,
} Item;

typedef enum CombatEvent {
  EV_LEVEL_UP = 1 << 0,
  EV_PLAYER_DIED = 1 << 1,
  EV_BOSS_KILLED = 1 << 2,
  EV_MESSAGE = 1 << 3,  // see Combat.message
  EV_SLASH = 1 << 4,
  EV_WAVE_STARTED = 1 << 5,
  EV_WAVE_CLEARED = 1 << 6,
  EV_MONSTER_DIED = 1 << 7,
  EV_HERO_HURT = 1 << 8,
  EV_COIN = 1 << 9,
  EV_EXPLOSION = 1 << 10,
  EV_SPLAT = 1 << 11,  // a jar of molasses landed
  EV_REPEL = 1 << 12,  // the repulsion spell pushed
  EV_BLOCK = 1 << 13,  // the shield took some of a blob
  EV_RICOCHET = 1 << 14,  // the shield sent a blob back
} CombatEvent;

typedef struct Hero {
  int hp, maxHp;
  int xp, level, nextLevel;
  int levels[UP_COUNT];
  int pendingLevels;
  int invulnerable;  // ticks
  int regenTimer;

  int swordTimer, stoneTimer, auraTimer, molassesTimer;
  // repulsion: ticks until it is ready (below 0 once ready), until the
  // pulse waiting for the middle of a sword swing (0 if none), and since the last one
  int repelCooldown, repelTimer, repelAge;
  float flameAngle;
  int slashTimer;  // the sword animation is playing
  bool slashLeft;

  int gold;
  int potions, bombs, amulets;
  int wisdom, shields;  // amulets and shields bought
  float xpCarry;  // fractions of experience from the wisdom amulets
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
  float flank;  // -1..1: which way and how far round the hero it swings
  int stagger;  // ticks left reeling from a blow, not walking
  bool slowed;  // wading through molasses
  // getting no closer to the hero: a detour, or a jump nearer off screen
  float bestDist;
  int noProgress;
  int detour;  // ticks left walking sideways round an obstacle
  float detourX, detourY;
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
  bool reflected;  // a blob sent back by the shield, now hurting demons
} Shot;

typedef enum SoulKind { SOUL_HEART, SOUL_GOLD } SoulKind;

// something lying on the ground, picked up by walking near it
typedef struct Soul {
  float x, y;
  SoulKind kind;
  int value;  // gold
} Soul;

// thrown towards (x, y): flies, lands, and goes off when the fuse is out,
// or a jar of molasses that spills where it lands
typedef struct Bomb {
  float fromX, fromY, x, y;
  int flight;  // ticks left in the air
  int fuse;    // ticks left on the ground
  bool molasses;
} Bomb;

// spilled molasses: demons in it are slowed down
typedef struct Puddle {
  float x, y, radius;
  float slow;  // speed factor inside
  int life, maxLife;
} Puddle;

typedef struct Explosion {
  float x, y;
  int life;
} Explosion;

#define BOMB_FLIGHT 22
#define EXPLOSION_TICKS 24

// half of a killed monster drifting away
typedef struct Debris {
  MonsterKind kind;
  bool right;
  float x, y, vx, vy;  // the halves fly off with the blow that killed it
  int life;
} Debris;

typedef struct Slash {
  float x, y, angle, range;
  float arc;  // half of the swing, in radians
  int life;
} Slash;

// a damage number floating up and fading
typedef struct Popup {
  float x, y;
  int amount;
  int life;
  bool hero;  // damage taken by the hero
  bool heal;  // health given back to the hero
} Popup;

#define POPUP_TICKS 30

typedef enum WaveState { WAVE_CALM, WAVE_ATTACK } WaveState;

#define MAX_SHOTS 256
#define MAX_SOULS 1024
#define MAX_DEBRIS 128
#define MAX_SLASHES 8
#define MAX_POPUPS 192
#define MAX_BOMBS 8
#define MAX_EXPLOSIONS 8
#define MAX_PUDDLES 12

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
  Popup popups[MAX_POPUPS];
  int numPopups;
  Bomb bombs[MAX_BOMBS];
  int numBombs;
  Explosion explosions[MAX_EXPLOSIONS];
  int numExplosions;
  Puddle puddles[MAX_PUDDLES];
  int numPuddles;

  int wave;
  WaveState waveState;
  int waveTimer;  // ticks until the next wave while calm
  int toSpawn;
  int spawnTimer;
  int targetVillage;
  bool bossPending;
  bool batWave;  // a swarm: mostly bats
  bool bossAlive;

  // walking cost from every cell to the hero, shared by all the demons
  float *field;
  int fieldTimer;
  int fieldCell;

  // the view around the hero, in pixels: demons stuck out of it may jump
  // closer, staying out of sight. Set by the caller, 0 for never.
  float viewW, viewH;

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
// Let a pending level go without an upgrade.
void combat_skip_upgrade(Combat *combat);
// A level for free, for testing.
void combat_level_up(Combat *combat);
const char *upgrade_name(Upgrade upgrade);
const char *upgrade_description(Upgrade upgrade);

int combat_alive_monsters(const Combat *combat);

// Skip the rest of the calm and start the next wave now.
void combat_next_wave(Combat *combat);

// The shop. Items can't be bought past combat_item_max() of each.
#define MAX_ITEMS 5
int combat_item_max(Item item);
const char *item_name(Item item);
const char *item_description(Item item);
int combat_item_price(const Combat *combat, Item item);
int combat_item_count(const Combat *combat, Item item);
bool combat_buy(Combat *combat, Item item);  // false if too poor or full

// Use what was bought; false when there is none, or no need.
bool combat_drink_potion(Combat *combat);
bool combat_throw_bomb(Combat *combat, float heroX, float heroY);
float combat_bomb_radius(void);

// The repulsion spell pushes between sword slashes; 0 radius without it.
float combat_repel_radius(const Combat *combat);

#endif
