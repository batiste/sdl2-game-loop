/*
  Copyright (C) 2013 batiste.bieler@gmail.com

  The invasion of the evil lords: waves of demons attack the villages.

  Waves alternate with calm periods. Each wave targets the village nearest
  to the hero: demons gather in the countryside, march along the roads and
  go after the villagers, who run for the houses. A villager killed by a
  demon rises again as a skeleton. Every tenth wave brings an evil lord, and
  every fourth from the third, a swarm of bats.
 */

#include "combat.h"

#include <float.h>

#define TICKS_PER_SECOND 50
#define MAX_MONSTERS 400

#define HERO_HP 40
#define FIELD_TICKS 25  // how often the way to the hero is worked out again
#define HERO_INVULNERABLE 28
#define FIRST_WAVE_DELAY (20 * TICKS_PER_SECOND)
#define CALM_DELAY (35 * TICKS_PER_SECOND)

// difficulty
#define WAVE_BASE 12            // demons in a wave: WAVE_BASE + WAVE_GROWTH * wave
#define WAVE_GROWTH 10
#define BOSS_EVERY 10           // waves
#define WAVE_TOUGHNESS 0.2f     // extra health per wave
#define LORDS_PER_WAVE 0.04f    // share of lords grows with each wave after the first...
#define MAX_LORDS 0.25f         // ...up to this
#define BATS_PER_WAVE 0.06f     // share of bats grows with each wave after the first...
#define MAX_BATS 0.3f           // ...up to this
#define BAT_WAVE_EVERY 4        // waves 3, 7, 11... are swarms of bats...
#define BAT_WAVE_SHARE 0.75f    // ...this many of them
#define BAT_SPACING 30.0f       // bats keep this far from each other...
#define BAT_ORBIT 100.0f        // ...and circle round to their own side this far out, then dive
#define LORDS_ALIVE 2           // lords about at the same time: LORDS_ALIVE + wave / 2...
#define MAX_LORDS_ALIVE 8       // ...up to this
#define SWORD_RANGE 44          // reach at level 1, in pixels...
#define SWORD_RANGE_LEVEL 6     // ...and per level above
#define SWORD_ARC 0.6f          // half of the swing at level 1, in radians...
#define SWORD_ARC_LEVEL 0.1f    // ...and per level above
#define SWORD_PUSH 9.0f         // knockback of a sword hit (about 4 times this in pixels)
#define SWORD_SHOVE 4.0f        // and of the demons just out of reach, unhurt
#define SWORD_SHOVE_REACH 22.0f // how far out of reach the shove goes
#define STAGGER_TICKS 8         // a demon hit by the sword stops walking this long
#define REGEN_TICKS 300         // one health point every so many ticks
#define HEART_CHANCE 0.006f
#define HEART_HEAL 10
#define COIN_CHANCE 0.3f        // an underling or skeleton drops a coin
#define LORD_GOLD 3
#define BOSS_GOLD 40
#define WAVE_GOLD 10            // for clearing a wave: WAVE_GOLD + WAVE_GOLD_GROWTH * wave
#define WAVE_GOLD_GROWTH 2
#define POTION_HEAL 20
#define AMULET_HP 10
#define BOMB_FUSE 26
#define BOMB_RANGE 240.0f       // thrown at the nearest demon this close
#define BOMB_RADIUS 84.0f
#define BOMB_DAMAGE 30.0f
#define MOLASSES_RANGE 280.0f   // jars are thrown at the nearest demon this close
#define MOLASSES_COOLDOWN 240   // ticks between jars, less 20 per level
#define MOLASSES_SECONDS 5      // a puddle lasts this long, and 1 more per level
#define WISDOM_XP 0.2f          // more experience per wisdom amulet
#define LEVEL_XP 8              // experience for a level: LEVEL_XP more for each one
#define BOSS_XP 30
#define REPEL_TICKS 70          // the beat of the repulsion spell, 8 faster per level...
#define REPEL_PUSH 7.0f         // ...its knockback, and 2 more per level...
#define REPEL_STAGGER 10        // ...and the demons reel this long
#define REPEL_LATE 30           // ready this long and no sword swing: it pushes anyway
#define SHIELD_BLOCK 0.15f      // share of a blob's damage each shield blocks
#define MAX_SHIELDS 5
#define RICOCHET_CHANCE 0.2f    // a blob bounces off the shield...
#define RICOCHET_PER_SHIELD 0.15f  // ...and this much more per extra shield
#define RICOCHET_SPEED 1.3f     // faster on the way back
#define STUCK_TICKS 100         // getting no closer to the hero this long...
#define DETOUR_TICKS 40         // ...a demon in sight walks sideways this long
#define SCARE_RADIUS 260.0f
#define VILLAGER_SENSE 90.0f  // demons bite villagers this close on their way
#define CHASE_DIRECT 220.0f   // closer than this, demons go straight for their prey
#define SURROUND_RANGE 170.0f // closer than this, underlings spread out around the hero...
#define SURROUND_TURN 1.4f    // ...swinging up to this far round him (radians)...
#define SURROUND_BOOST 1.35f  // ...with a burst of speed
#define BITE_RANGE 34.0f      // close enough to go straight in

typedef struct MonsterInfo {
  float hp, speed, damage;
  int xp;
  float radius;  // body, for hits
} MonsterInfo;

static const MonsterInfo INFO[MON_KINDS] = {
    [MON_UNDERLING] = {5, 0.94f, 3, 1, 10},
    [MON_LORD] = {5, 0.65f, 4, 3, 11},
    [MON_BOSS] = {150, 0.77f, 12, 25, 24},
    [MON_SKELETON] = {5, 0.65f, 4, 2, 10},
    [MON_BAT] = {2, 1.5f, 2, 1, 8},
};

// ---------------------------------------------------------------- helpers

static float rndf(Combat *c) {
  return SDL_randf_r(&c->rng);
}

static int rnd(Combat *c, int n) {
  return SDL_rand_r(&c->rng, n);
}

static float dist2(float ax, float ay, float bx, float by) {
  return (ax - bx) * (ax - bx) + (ay - by) * (ay - by);
}

#define MESSAGE(c, ...)                                             \
  do {                                                              \
    SDL_snprintf((c)->message, sizeof((c)->message), __VA_ARGS__); \
    (c)->events |= EV_MESSAGE;                                      \
  } while (0)

static float might(const Combat *c) {
  return 1 + 0.2f * (float)c->hero.levels[UP_MIGHT];
}

static float haste(const Combat *c) {
  return 1 - 0.08f * (float)c->hero.levels[UP_HASTE];
}

float combat_speed_factor(const Combat *c) {
  return 1 + 0.1f * (float)c->hero.levels[UP_SPEED];
}

float combat_pickup_radius(const Combat *c) {
  (void)c;
  return 48;
}

float combat_aura_radius(const Combat *c) {
  return 44 + 8 * (float)c->hero.levels[UP_AURA];
}

int combat_flame_count(const Combat *c) {
  int level = c->hero.levels[UP_FLAMES];
  return level ? level + 1 : 0;
}

float combat_flame_radius(void) {
  return 64;
}

int combat_alive_monsters(const Combat *c) {
  return c->numMonsters;
}

void combat_next_wave(Combat *c) {
  if (c->waveState == WAVE_CALM) {
    c->waveTimer = 1;
  }
}

// ---------------------------------------------------------------- setup

void combat_init(Combat *c, Uint64 seed) {
  combat_free(c);
  *c = (Combat){.rng = seed ^ 0xd1b54a32d192ed03ull};
  c->hero = (Hero){.hp = HERO_HP, .maxHp = HERO_HP, .nextLevel = LEVEL_XP, .repelAge = 1000};
  c->hero.levels[UP_SWORD] = 1;
  c->waveState = WAVE_CALM;
  c->waveTimer = FIRST_WAVE_DELAY;
  c->targetVillage = -1;
}

void combat_free(Combat *c) {
  SDL_free(c->monsters);
  c->monsters = NULL;
  c->numMonsters = c->capMonsters = 0;
  SDL_free(c->field);
  c->field = NULL;
}

static Monster *add_monster(Combat *c, MonsterKind kind, float x, float y) {
  if (c->numMonsters >= MAX_MONSTERS) {
    return NULL;
  }
  if (c->numMonsters == c->capMonsters) {
    int cap = c->capMonsters ? c->capMonsters * 2 : 32;
    Monster *grown = SDL_realloc(c->monsters, (size_t)cap * sizeof(*grown));
    if (!grown) {
      return NULL;
    }
    c->monsters = grown;
    c->capMonsters = cap;
  }
  Monster *m = &c->monsters[c->numMonsters++];
  // later waves are tougher, the evil lord only half as fast
  float toughness = WAVE_TOUGHNESS * (kind == MON_BOSS ? 0.5f : 1);
  float hp = INFO[kind].hp * (1 + toughness * (float)SDL_max(0, c->wave - 1));
  // each one has its own side to come from: half go left, half right
  float flank = (rndf(c) < 0.5f ? -1 : 1) * (0.35f + 0.65f * rndf(c));
  *m = (Monster){.kind = kind, .x = x, .y = y, .prevX = x, .prevY = y, .hp = hp, .maxHp = hp,
                 .target = -2, .retarget = rnd(c, 15), .shootTimer = 60 + rnd(c, 60), .flank = flank,
                 .bestDist = FLT_MAX};
  return m;
}

static void remove_monster(Combat *c, int i) {
  c->monsters[i] = c->monsters[--c->numMonsters];
}

// ---------------------------------------------------------------- effects

static void add_soul(Combat *c, float x, float y, SoulKind kind, int value) {
  if (c->numSouls < MAX_SOULS) {
    c->souls[c->numSouls++] = (Soul){x + (rndf(c) - 0.5f) * 10, y + (rndf(c) - 0.5f) * 6, kind, value};
  }
}

// Experience straight from a kill, more with the wisdom amulets.
static void gain_xp(Combat *c, int value) {
  Hero *h = &c->hero;
  float gain = (float)value * (1 + WISDOM_XP * (float)h->wisdom) + h->xpCarry;
  h->xp += (int)gain;
  h->xpCarry = gain - SDL_floorf(gain);
}

static void kill_monster(Combat *c, int i) {
  Monster *m = &c->monsters[i];
  for (int side = 0; side < 2 && c->numDebris < MAX_DEBRIS && m->kind != MON_BAT; side++) {
    // apart from each other, and carried by the knockback of the killing blow
    float split = side ? 1.2f : -1.2f;
    c->debris[c->numDebris++] = (Debris){m->kind, side == 1, m->x, m->y, split + m->kickX * 0.8f, m->kickY * 0.8f, 30};
  }
  if (m->kind == MON_BOSS) {
    gain_xp(c, BOSS_XP);
    for (int k = 0; k < 10; k++) {
      add_soul(c, m->x + (rndf(c) - 0.5f) * 50, m->y + (rndf(c) - 0.5f) * 36, SOUL_GOLD, BOSS_GOLD / 10);
    }
    c->bossAlive = false;
    c->events |= EV_BOSS_KILLED;
  } else {
    gain_xp(c, INFO[m->kind].xp);
    if (m->kind == MON_LORD) {
      add_soul(c, m->x - 8, m->y + 4, SOUL_GOLD, LORD_GOLD);
    } else if (rndf(c) < COIN_CHANCE) {
      add_soul(c, m->x - 8, m->y + 4, SOUL_GOLD, 1);
    }
  }
  if (rndf(c) < HEART_CHANCE) {
    add_soul(c, m->x + 10, m->y, SOUL_HEART, 0);
  }
  c->kills++;
  c->events |= EV_MONSTER_DIED;
  remove_monster(c, i);
}

static void popup(Combat *c, float x, float y, float amount, bool hero) {
  if (c->numPopups < MAX_POPUPS) {
    int shown = SDL_max(1, (int)SDL_roundf(amount));
    c->popups[c->numPopups++] = (Popup){x + (rndf(c) - 0.5f) * 12, y, shown, POPUP_TICKS, hero, false};
  }
}

static void heal_hero(Combat *c, int amount) {
  Hero *h = &c->hero;
  int healed = SDL_min(amount, h->maxHp - h->hp);
  h->hp += healed;
  if (healed > 0 && c->numPopups < MAX_POPUPS) {
    c->popups[c->numPopups++] = (Popup){0, -46, healed, POPUP_TICKS, true, true};
  }
}

// Returns true if the monster died (and was removed).
static bool hit_monster(Combat *c, int i, float damage, float fromX, float fromY, float kick) {
  Monster *m = &c->monsters[i];
  popup(c, m->x, m->y - (m->kind == MON_BOSS ? 70 : 34), SDL_min(damage, m->hp), false);
  m->hp -= damage;
  m->hurtTimer = 8;
  float dx = m->x - fromX, dy = m->y - fromY, d = SDL_sqrtf(dx * dx + dy * dy);
  if (d > 0.01f && m->kind != MON_BOSS) {
    m->kickX += dx / d * kick;
    m->kickY += dy / d * kick;
  }
  if (m->hp <= 0) {
    kill_monster(c, i);
    return true;
  }
  return false;
}

static void hurt_hero(Combat *c, float damage) {
  Hero *h = &c->hero;
  if (h->invulnerable > 0 || h->hp <= 0) {
    return;
  }
  popup(c, 0, -46, SDL_ceilf(damage), true);  // follows the hero, see main.c
  c->events |= EV_HERO_HURT;
  h->hp = SDL_max(0, h->hp - (int)SDL_ceilf(damage));
  h->invulnerable = HERO_INVULNERABLE;
  if (h->hp == 0) {
    c->events |= EV_PLAYER_DIED;
  }
}

static void fire(Combat *c, float x, float y, float tx, float ty, float speed, float damage, bool hostile, int pierce,
                 int life) {
  if (c->numShots >= MAX_SHOTS) {
    return;
  }
  float dx = tx - x, dy = ty - y, d = SDL_sqrtf(dx * dx + dy * dy);
  if (d < 0.01f) {
    return;
  }
  c->shots[c->numShots++] = (Shot){x, y, dx / d * speed, dy / d * speed, x, y, life, damage, pierce,
                                   hostile, false, -1, false};
}

// ---------------------------------------------------------------- waves

static bool monster_blocked(const World *world, float x, float y);

static bool spawn_spot(Combat *c, const World *world, const Village *v, float *ox, float *oy) {
  for (int attempt = 0; attempt < 60; attempt++) {
    float angle = rndf(c) * 2 * SDL_PI_F, dist = 15 + rndf(c) * 7;
    int x = v->centerX + (int)(SDL_cosf(angle) * dist), y = v->centerY + (int)(SDL_sinf(angle) * dist);
    if (world_walkable(world, x, y) && world->houseAt[y * world->width + x] < 0 &&
        world->kind[y * world->width + x] != CELL_WATER) {
      *ox = world_cell_feet_x(x);
      *oy = world_cell_feet_y(y);
      return true;
    }
  }
  return false;
}

static bool reaches_hero(const Combat *c, const World *world, float x, float y) {
  int cx = (int)(x / TILE_SIZE), cy = (int)(y / TILE_SIZE);
  return c->field && world_in_bounds(world, cx, cy) && c->field[cy * world->width + cx] < FLT_MAX;
}

// A pack of monsters gathering at the same spot. Returns false when no spot
// with a way to the hero was found.
static bool spawn_pack(Combat *c, const World *world, int count, MonsterKind kind) {
  const Village *v = &world->villages[c->targetVillage];
  float x = 0, y = 0;
  bool found = false;
  for (int attempt = 0; attempt < 5 && !found; attempt++) {
    found = spawn_spot(c, world, v, &x, &y) && reaches_hero(c, world, x, y);
  }
  if (!found) {
    return false;
  }
  for (int k = 0; k < count; k++) {
    float spread = kind == MON_BAT ? 80 : 24;  // a flock is loose
    float mx = x + (rndf(c) - 0.5f) * spread, my = y + (rndf(c) - 0.5f) * spread * 0.7f;
    if (monster_blocked(world, mx, my)) {
      mx = x, my = y;
    }
    add_monster(c, kind, mx, my);
  }
  return true;
}

static void update_waves(Combat *c, const World *world, float heroX, float heroY) {
  if (world->numVillages == 0) {
    return;
  }
  if (c->waveState == WAVE_CALM) {
    if (--c->waveTimer > 0) {
      return;
    }
    c->wave++;
    c->waveState = WAVE_ATTACK;
    c->events |= EV_WAVE_STARTED;
    c->toSpawn = WAVE_BASE + WAVE_GROWTH * c->wave;
    c->spawnTimer = 0;
    c->bossPending = c->wave % BOSS_EVERY == 0;
    c->batWave = !c->bossPending && c->wave % BAT_WAVE_EVERY == BAT_WAVE_EVERY - 1;
    // the demons go for the village nearest to the hero
    float best = FLT_MAX;
    for (int i = 0; i < world->numVillages; i++) {
      const Village *v = &world->villages[i];
      float d = dist2(heroX, heroY, world_cell_feet_x(v->centerX), world_cell_feet_y(v->centerY));
      if (d < best) {
        best = d;
        c->targetVillage = i;
      }
    }
    const char *name = world->villages[c->targetVillage].name;
    if (c->bossPending) {
      MESSAGE(c, "Wave %d: the evil lord marches on %s!", c->wave, name);
    } else if (c->batWave) {
      MESSAGE(c, "Wave %d: a swarm of bats descends on %s!", c->wave, name);
    } else {
      MESSAGE(c, "Wave %d: demons attack %s!", c->wave, name);
    }
    return;
  }

  if (c->toSpawn > 0 && --c->spawnTimer <= 0) {
    c->spawnTimer = (int)(0.4f * TICKS_PER_SECOND);
    // lords come alone or in pairs, and only so many at once
    float share = SDL_min(MAX_LORDS, LORDS_PER_WAVE * (float)(c->wave - 1));
    int lordsAlive = 0;
    for (int i = 0; i < c->numMonsters; i++) {
      lordsAlive += c->monsters[i].kind == MON_LORD;
    }
    bool lord = rndf(c) < share && lordsAlive < SDL_min(MAX_LORDS_ALIVE, LORDS_ALIVE + c->wave / 2);
    float batShare = c->batWave ? BAT_WAVE_SHARE : SDL_min(MAX_BATS, BATS_PER_WAVE * (float)(c->wave - 1));
    bool bats = !lord && rndf(c) < batShare;
    int flock = c->batWave ? 5 + rnd(c, 4) : 3 + rnd(c, 3);
    int pack = SDL_min(c->toSpawn, lord ? 1 + rnd(c, 2) : bats ? flock : 3 + rnd(c, 4));
    if (spawn_pack(c, world, pack, lord ? MON_LORD : bats ? MON_BAT : MON_UNDERLING)) {
      c->toSpawn -= pack;
    }
    if (c->bossPending && c->toSpawn <= WAVE_GROWTH * c->wave / 2 && spawn_pack(c, world, 1, MON_BOSS)) {
      c->bossPending = false;
      c->bossAlive = true;
    }
  }
  if (c->toSpawn <= 0 && !c->bossPending && c->numMonsters == 0) {
    c->waveState = WAVE_CALM;
    c->waveTimer = CALM_DELAY;
    c->events |= EV_WAVE_CLEARED | EV_COIN;
    int gold = WAVE_GOLD + WAVE_GOLD_GROWTH * c->wave;
    c->hero.gold += gold;
    MESSAGE(c, "Wave %d cleared! %d gold", c->wave, gold);
  }
}

// ---------------------------------------------------------------- monsters

static bool monster_blocked(const World *world, float x, float y) {
  return world_blocked(world, x - 6, y - 4, x + 6, y + 2) || world_house_at(world, x, y) >= 0;
}

static void monster_step(const World *world, Monster *m, float dx, float dy) {
  if (dx != 0 && !monster_blocked(world, m->x + dx, m->y)) {
    m->x += dx;
  }
  if (dy != 0 && !monster_blocked(world, m->x, m->y + dy)) {
    m->y += dy;
  }
  if (SDL_fabsf(dx) > 0.2f) {
    m->faceLeft = dx < 0;
  }
}

// Demons go for the hero, but bite any villager who comes too close.
static void pick_target(const World *world, Npcs *npcs, Monster *m, float heroX, float heroY, bool heroExposed) {
  float best = VILLAGER_SENSE * VILLAGER_SENSE;
  m->target = heroExposed ? -1 : -2;
  float heroD = heroExposed ? dist2(m->x, m->y, heroX, heroY) : FLT_MAX;
  for (int i = 0; i < npcs->count; i++) {
    const Npc *npc = &npcs->items[i];
    if (npc_exposed(npc, world)) {
      float d = dist2(m->x, m->y, npc->x, npc->y);
      if (d < best && d < heroD) {
        best = d;
        m->target = i;
      }
    }
  }
}

// Step towards (gx, gy); returns false when nothing moved.
static bool walk_towards(const World *world, Monster *m, float gx, float gy, float speed) {
  float dx = gx - m->x, dy = gy - m->y, d = SDL_sqrtf(dx * dx + dy * dy);
  if (d < 1) {
    return true;
  }
  float oldX = m->x, oldY = m->y;
  monster_step(world, m, dx / d * speed, dy / d * speed);
  m->moving = true;
  return SDL_fabsf(m->x - oldX) + SDL_fabsf(m->y - oldY) > 0.2f;
}

// Walk downhill on the distance field; false when it doesn't help here.
static bool follow_field(Combat *c, const World *world, Monster *m, float speed) {
  int cx = (int)(m->x / TILE_SIZE), cy = (int)(m->y / TILE_SIZE);
  if (!c->field || !world_in_bounds(world, cx, cy)) {
    return false;
  }
  static const int DX[4] = {1, -1, 0, 0}, DY[4] = {0, 0, 1, -1};
  float best = c->field[cy * world->width + cx];
  int bx = -1, by = -1;
  for (int d = 0; d < 4; d++) {
    int nx = cx + DX[d], ny = cy + DY[d];
    if (world_in_bounds(world, nx, ny) && c->field[ny * world->width + nx] < best) {
      best = c->field[ny * world->width + nx];
      bx = nx, by = ny;
    }
  }
  if (bx < 0) {
    return false;  // in the hero's cell, or nowhere to go
  }
  if (walk_towards(world, m, world_cell_feet_x(bx), world_cell_feet_y(by), speed)) {
    m->stuck = 0;
  } else if (++m->stuck % 20 == 0) {
    // caught on a tree or a corner: jostle
    m->kickX += (rndf(c) - 0.5f) * 6;
    m->kickY += (rndf(c) - 0.5f) * 6;
  }
  return true;
}

static void attack(Combat *c, Npcs *npcs, int i) {
  Monster *m = &c->monsters[i];
  m->attackTimer = 35;
  if (m->target == -1) {
    hurt_hero(c, INFO[m->kind].damage);
    return;
  }
  Npc *npc = &npcs->items[m->target];
  if (npc_hurt(npc, 1)) {
    // the villager rises again, on the wrong side
    c->villagersLost++;
    add_monster(c, MON_SKELETON, npc->x, npc->y);
  }
}

static void shoot(Combat *c, Monster *m, float tx, float ty) {
  float dx = tx - m->x, dy = ty - m->y;
  if (m->kind == MON_BOSS) {
    m->shootTimer = 60;
    float a = SDL_atan2f(dy, dx);
    for (int k = -1; k <= 1; k++) {
      float ak = a + (float)k * 0.25f;
      fire(c, m->x, m->y - 30, m->x + SDL_cosf(ak), m->y - 30 + SDL_sinf(ak), 3.6f, 6, true, 0, 100);
      c->shots[c->numShots - 1].boss = true;
    }
  } else {
    m->shootTimer = 95 + rnd(c, 40);
    fire(c, m->x, m->y - 16, tx, ty - 16, 2.8f, 8, true, 0, 110);
  }
}

// A demon stuck out of sight jumps to a spot nearer the hero, still out of
// sight, from where it can reach him. False when it is in sight, or no spot
// was found.
static bool jump_closer(Combat *c, const World *world, Monster *m, float hx, float hy, float d) {
  float halfW = c->viewW / 2, halfH = c->viewH / 2;
  if (c->viewW <= 0 || m->kind == MON_BOSS || (SDL_fabsf(m->x - hx) < halfW + 32 && SDL_fabsf(m->y - hy) < halfH + 64)) {
    return false;
  }
  for (int attempt = 0; attempt < 24; attempt++) {
    // just outside one of the sides of the view
    float margin = 48 + rndf(c) * 96, along = (rndf(c) - 0.5f) * 2, x, y;
    int side = rnd(c, 4);
    if (side < 2) {
      x = hx + (side ? 1.0f : -1.0f) * (halfW + margin);
      y = hy + along * (halfH + margin);
    } else {
      x = hx + along * (halfW + margin);
      y = hy + (side == 3 ? 1.0f : -1.0f) * (halfH + margin);
    }
    int cx = (int)(x / TILE_SIZE), cy = (int)(y / TILE_SIZE);
    if (dist2(x, y, hx, hy) < d * d && world_in_bounds(world, cx, cy) && world_walkable(world, cx, cy) &&
        world->kind[cy * world->width + cx] != CELL_WATER && !monster_blocked(world, x, y) &&
        reaches_hero(c, world, x, y)) {
      m->x = m->prevX = x;
      m->y = m->prevY = y;
      m->kickX = m->kickY = 0;
      return true;
    }
  }
  return false;
}

// How much the molasses holds a demon back: 1 when it is out of it.
static float molasses_factor(const Combat *c, Monster *m) {
  float factor = 1;
  for (int i = 0; i < c->numPuddles; i++) {
    const Puddle *p = &c->puddles[i];
    float dx = m->x - p->x, dy = (m->y - p->y) / 0.7f;  // puddles are squashed by the perspective
    if (dx * dx + dy * dy < p->radius * p->radius) {
      factor = SDL_min(factor, p->slow);
    }
  }
  m->slowed = factor < 1;
  return m->kind == MON_BOSS ? (1 + factor) / 2 : factor;  // the evil lord wades through
}

// Bats fly straight over everything, zigzagging, and go for the hero only:
// a bite, and they flutter off to come again.
static void update_bat(Combat *c, const World *world, Npcs *npcs, int i, float heroX, float heroY,
                       bool heroExposed) {
  Monster *m = &c->monsters[i];
  float phase = (float)(++m->wanderTimer) * 0.15f + m->flank * 10;
  float tx = heroX, ty = heroY - 10;
  m->target = heroExposed ? -1 : -2;
  if (heroExposed) {
    // each circles round to its own side of the hero before diving in
    float bearing = SDL_atan2f(m->y - ty, m->x - tx), side = m->flank * SDL_PI_F;
    float turn = SDL_fmodf(side - bearing + 3 * SDL_PI_F, 2 * SDL_PI_F) - SDL_PI_F;
    if (SDL_fabsf(turn) > 0.35f || dist2(m->x, m->y, tx, ty) > (BAT_ORBIT + 40) * (BAT_ORBIT + 40)) {
      float a = bearing + SDL_clamp(turn, -0.6f, 0.6f);
      tx += SDL_cosf(a) * BAT_ORBIT;
      ty += SDL_sinf(a) * BAT_ORBIT * 0.75f;
    }
  }
  if (!heroExposed) {
    // circling over the house the hero hides in
    tx = heroX + SDL_cosf(phase * 0.2f) * 90;
    ty = heroY + SDL_sinf(phase * 0.2f) * 60;
  }
  float dx = tx - m->x, dy = ty - m->y, d = SDL_sqrtf(dx * dx + dy * dy);
  if (d > 1) {
    float a = SDL_atan2f(dy, dx) + SDL_sinf(phase) * 0.7f;
    m->x += SDL_cosf(a) * INFO[MON_BAT].speed;
    m->y += SDL_sinf(a) * INFO[MON_BAT].speed;
    m->faceLeft = SDL_cosf(a) < 0;
    m->moving = true;
  }
  float maxX = (float)(world->width * TILE_SIZE), maxY = (float)(world->height * TILE_SIZE);
  m->x = SDL_clamp(m->x, 8.0f, maxX - 8);
  m->y = SDL_clamp(m->y, 8.0f, maxY - 8);
  if (heroExposed && d < 16 + INFO[MON_BAT].radius && m->attackTimer == 0) {
    attack(c, npcs, i);
    m->kickX -= dx / SDL_max(d, 1.0f) * 5;
    m->kickY -= dy / SDL_max(d, 1.0f) * 5;
  }
}

static void update_monster(Combat *c, const World *world, Npcs *npcs, int i, float heroX, float heroY,
                           bool heroExposed) {
  Monster *m = &c->monsters[i];
  const MonsterInfo *info = &INFO[m->kind];
  m->prevX = m->x;
  m->prevY = m->y;
  m->hurtTimer = SDL_max(0, m->hurtTimer - 1);
  m->attackTimer = SDL_max(0, m->attackTimer - 1);
  m->flameCooldown = SDL_max(0, m->flameCooldown - 1);
  m->auraCooldown = SDL_max(0, m->auraCooldown - 1);
  m->moving = false;

  // knockback, through walls for a bat
  if (SDL_fabsf(m->kickX) + SDL_fabsf(m->kickY) > 0.1f) {
    if (m->kind == MON_BAT) {
      m->x += m->kickX;
      m->y += m->kickY;
    } else {
      monster_step(world, m, m->kickX, m->kickY);
    }
    m->kickX *= 0.75f;
    m->kickY *= 0.75f;
  }
  if (m->stagger > 0) {
    m->stagger--;
    return;  // reeling from a blow
  }
  if (m->kind == MON_BAT) {
    update_bat(c, world, npcs, i, heroX, heroY, heroExposed);
    return;
  }

  if (--m->retarget <= 0) {
    m->retarget = 15;
    pick_target(world, npcs, m, heroX, heroY, heroExposed);
  }
  float tx = heroX, ty = heroY;
  if (m->target >= 0) {
    if (npc_exposed(&npcs->items[m->target], world)) {
      tx = npcs->items[m->target].x, ty = npcs->items[m->target].y;
    } else {
      m->target = heroExposed ? -1 : -2;
    }
  } else if (m->target == -1 && !heroExposed) {
    m->target = -2;
  }

  float speed = info->speed * world_ground_speed(world, m->x, m->y) * molasses_factor(c, m);
  if (m->target == -2) {
    // the hero hides in a house: lurk around it
    if (--m->wanderTimer <= 0) {
      m->wanderTimer = 2 * TICKS_PER_SECOND + rnd(c, 2 * TICKS_PER_SECOND);
      m->wanderX = heroX + (rndf(c) - 0.5f) * 6 * TILE_SIZE;
      m->wanderY = heroY + (rndf(c) - 0.5f) * 6 * TILE_SIZE;
    }
    if (dist2(m->x, m->y, m->wanderX, m->wanderY) > 8 * 8) {
      walk_towards(world, m, m->wanderX, m->wanderY, speed * 0.6f);
    }
    return;
  }

  float dx = tx - m->x, dy = ty - m->y, d = SDL_sqrtf(dx * dx + dy * dy);

  // far from the hero and getting no closer: caught on something
  if (m->target == -1 && d > CHASE_DIRECT) {
    if (d < m->bestDist - 4) {
      m->bestDist = d;
      m->noProgress = 0;
    } else if (++m->noProgress > STUCK_TICKS) {
      m->noProgress = 0;
      m->bestDist = FLT_MAX;
      if (!jump_closer(c, world, m, tx, ty, d)) {
        // in sight: walk sideways for a while, on its own side
        float side = m->flank < 0 ? -1.0f : 1.0f;
        m->detour = DETOUR_TICKS;
        m->detourX = -dy / d * side;
        m->detourY = dx / d * side;
      }
      return;
    }
  } else {
    m->bestDist = FLT_MAX;
    m->noProgress = 0;
  }
  if (m->detour > 0) {
    m->detour--;
    if (!walk_towards(world, m, m->x + m->detourX * 10, m->y + m->detourY * 10, speed)) {
      m->detourX = -m->detourX;  // blocked that way too: the other way
      m->detourY = -m->detourY;
    }
    return;
  }
  bool direct = d < CHASE_DIRECT;
  bool flanker = m->kind == MON_UNDERLING || m->kind == MON_SKELETON;
  if (direct && flanker && m->target == -1 && d < SURROUND_RANGE && d > BITE_RANGE) {
    // spiral in round the hero towards its own side, so a group closes in
    // from all around instead of queueing up in front of him
    float bearing = SDL_atan2f(m->y - ty, m->x - tx) + m->flank * SURROUND_TURN * (d / SURROUND_RANGE);
    float ring = SDL_max(BITE_RANGE - 8, d - 30);
    float gx = tx + SDL_cosf(bearing) * ring, gy = ty + SDL_sinf(bearing) * ring;
    if (!walk_towards(world, m, gx, gy, speed * SURROUND_BOOST)) {
      direct = false;  // blocked: go around
    }
  } else if (direct) {
    // close enough to go straight at the prey
    float dir = 1;
    if (m->kind == MON_LORD && m->target == -1) {
      dir = d < 150 ? -0.7f : (d < 210 ? 0 : 1);  // keep at a distance
    }
    if (dir != 0 && !walk_towards(world, m, m->x + dx * dir, m->y + dy * dir, speed)) {
      direct = false;  // blocked: go around
    }
  }
  if (!direct && (m->target != -1 || !follow_field(c, world, m, speed))) {
    walk_towards(world, m, tx, ty, speed);
  }

  if ((m->kind == MON_LORD || m->kind == MON_BOSS) && m->target == -1 && d < 320 && --m->shootTimer <= 0) {
    shoot(c, m, tx, ty);
  }
  if (d < 20 + info->radius && m->attackTimer == 0) {
    attack(c, npcs, i);
  }
}

// Keep monsters from piling up on each other.
static void separate_monsters(Combat *c, const World *world) {
  for (int i = 0; i < c->numMonsters; i++) {
    for (int j = i + 1; j < c->numMonsters; j++) {
      Monster *a = &c->monsters[i], *b = &c->monsters[j];
      if ((a->kind == MON_BAT) != (b->kind == MON_BAT)) {
        continue;  // bats fly over the walkers
      }
      float dx = b->x - a->x, dy = b->y - a->y, d2 = dx * dx + dy * dy;
      if (a->kind == MON_BAT) {
        // bats keep their distance from each other, through walls
        if (d2 < BAT_SPACING * BAT_SPACING && d2 > 0.01f) {
          float d = SDL_sqrtf(d2), push = (BAT_SPACING - d) * 0.15f;
          a->x -= dx / d * push, a->y -= dy / d * push;
          b->x += dx / d * push, b->y += dy / d * push;
        }
        continue;
      }
      float min = INFO[a->kind].radius + INFO[b->kind].radius;
      if (d2 < min * min && d2 > 0.01f) {
        float d = SDL_sqrtf(d2), push = (min - d) * 0.25f;
        monster_step(world, a, -dx / d * push, -dy / d * push);
        monster_step(world, b, dx / d * push, dy / d * push);
      }
    }
  }
}

// ---------------------------------------------------------------- weapons

static int nearest_monster(const Combat *c, float x, float y, float range, int skip) {
  int best = -1;
  float bestD = range * range;
  for (int i = 0; i < c->numMonsters; i++) {
    float d = dist2(x, y, c->monsters[i].x, c->monsters[i].y);
    if (i != skip && d < bestD) {
      bestD = d;
      best = i;
    }
  }
  return best;
}

static void use_sword(Combat *c, float hx, float hy) {
  Hero *h = &c->hero;
  int level = h->levels[UP_SWORD];
  if (--h->swordTimer > 0 || level == 0) {
    return;
  }
  float range = SWORD_RANGE + SWORD_RANGE_LEVEL * (float)(level - 1);
  float arc = SWORD_ARC + SWORD_ARC_LEVEL * (float)(level - 1);
  int target = nearest_monster(c, hx, hy - 12, range + 16, -1);
  if (target < 0) {
    return;  // no swinging at nothing
  }
  h->swordTimer = (int)((float)(55 - 5 * level) * haste(c));
  if (h->levels[UP_REPEL] > 0 && h->repelCooldown <= 0 && h->repelTimer == 0) {
    h->repelTimer = SDL_max(1, h->swordTimer / 2);  // repulsion, between this slash and the next
  }
  Monster *t = &c->monsters[target];
  float angle = SDL_atan2f(t->y - 12 - (hy - 12), t->x - hx);
  float damage = (3 + (float)level) * might(c);
  int directions = level >= 3 ? 2 : 1;
  for (int k = 0; k < directions; k++) {
    float a = angle + (float)k * SDL_PI_F;
    if (c->numSlashes < MAX_SLASHES) {
      c->slashes[c->numSlashes++] = (Slash){hx, hy - 14, a, range, arc, 10};
    }
    for (int i = c->numMonsters - 1; i >= 0; i--) {
      Monster *m = &c->monsters[i];
      float dx = m->x - hx, dy = m->y - 12 - (hy - 12), d = SDL_sqrtf(dx * dx + dy * dy);
      float diff = SDL_fabsf(SDL_fmodf(SDL_atan2f(dy, dx) - a + 3 * SDL_PI_F, 2 * SDL_PI_F) - SDL_PI_F);
      if (d < range + INFO[m->kind].radius && diff < arc) {
        m->stagger = STAGGER_TICKS;  // set first: hit_monster may remove it
        hit_monster(c, i, damage, hx, hy, SWORD_PUSH);
      } else if (d < range + SWORD_SHOVE_REACH + INFO[m->kind].radius && diff < arc && m->kind != MON_BOSS) {
        // the front of the crowd is pushed back too
        m->kickX += dx / SDL_max(d, 1.0f) * SWORD_SHOVE;
        m->kickY += dy / SDL_max(d, 1.0f) * SWORD_SHOVE;
      }
    }
  }
  h->slashTimer = 12;
  h->slashLeft = SDL_cosf(angle) < 0;
  c->events |= EV_SLASH;
}

static void use_stones(Combat *c, float hx, float hy) {
  Hero *h = &c->hero;
  int level = h->levels[UP_STONES];
  if (level == 0 || --h->stoneTimer > 0) {
    return;
  }
  int count = 1 + (level - 1) / 2;
  int first = nearest_monster(c, hx, hy, 360, -1);
  if (first < 0) {
    return;
  }
  h->stoneTimer = (int)((float)(60 - 5 * level) * haste(c));
  int target = first;
  for (int k = 0; k < count; k++) {
    if (target < 0) {
      target = first;
    }
    const Monster *m = &c->monsters[target];
    // each stone stops at the first demon it hits
    fire(c, hx, hy - 20, m->x, m->y - 14, 7, (1.5f + 0.5f * (float)level) * might(c), false, 0, 60);
    target = nearest_monster(c, m->x, m->y, 200, target);
  }
}

static void use_flames(Combat *c, float hx, float hy) {
  Hero *h = &c->hero;
  int count = combat_flame_count(c);
  if (count == 0) {
    return;
  }
  h->flameAngle = SDL_fmodf(h->flameAngle + 0.07f, 2 * SDL_PI_F);
  float damage = (1 + 0.4f * (float)h->levels[UP_FLAMES]) * might(c);
  for (int k = 0; k < count; k++) {
    float a = h->flameAngle + (float)k * 2 * SDL_PI_F / (float)count;
    float fx = hx + SDL_cosf(a) * combat_flame_radius(), fy = hy - 14 + SDL_sinf(a) * combat_flame_radius() * 0.8f;
    for (int i = c->numMonsters - 1; i >= 0; i--) {
      Monster *m = &c->monsters[i];
      float r = INFO[m->kind].radius + 10;
      if (m->flameCooldown == 0 && dist2(fx, fy, m->x, m->y - 12) < r * r) {
        m->flameCooldown = 25;
        hit_monster(c, i, damage, fx, fy, 3);
      }
    }
  }
}

static void use_aura(Combat *c, float hx, float hy) {
  Hero *h = &c->hero;
  int level = h->levels[UP_AURA];
  if (level == 0 || --h->auraTimer > 0) {
    return;
  }
  h->auraTimer = (int)(40 * haste(c));
  float r = combat_aura_radius(c), damage = (0.8f + 0.4f * (float)level) * might(c);
  for (int i = c->numMonsters - 1; i >= 0; i--) {
    Monster *m = &c->monsters[i];
    if (dist2(hx, hy, m->x, m->y) < r * r) {
      hit_monster(c, i, damage, hx, hy, 1.5f);
    }
  }
}

float combat_repel_radius(const Combat *c) {
  int level = c->hero.levels[UP_REPEL];
  return level ? 64 + 10 * (float)level : 0;
}

// Pushes every demon in reach away; false when there was none.
static bool repel_pulse(Combat *c, float hx, float hy) {
  int level = c->hero.levels[UP_REPEL];
  float r = combat_repel_radius(c), push = REPEL_PUSH + 2 * (float)(level - 1);
  bool pushed = false;
  for (int i = 0; i < c->numMonsters; i++) {
    Monster *m = &c->monsters[i];
    float dx = m->x - hx, dy = m->y - hy, d = SDL_sqrtf(dx * dx + dy * dy);
    if (m->kind != MON_BOSS && d < r && d > 0.01f) {
      float strength = push * (1 - 0.4f * d / r);  // stronger close in
      m->kickX += dx / d * strength;
      m->kickY += dy / d * strength;
      m->stagger = SDL_max(m->stagger, REPEL_STAGGER);
      pushed = true;
    }
  }
  return pushed;
}

// On the beat between two sword slashes (see use_sword()), or on its own
// when the sword has nothing in reach. Only when there are demons to push.
static void use_repel(Combat *c, float hx, float hy) {
  Hero *h = &c->hero;
  int level = h->levels[UP_REPEL];
  h->repelAge++;
  if (level == 0) {
    return;
  }
  h->repelCooldown--;
  bool due = h->repelTimer > 0 ? --h->repelTimer == 0 : h->repelCooldown <= -REPEL_LATE;
  if (due && repel_pulse(c, hx, hy)) {
    h->repelCooldown = (int)((float)(REPEL_TICKS - 8 * (level - 1)) * haste(c));
    h->repelAge = 0;
    c->events |= EV_REPEL;
  }
}

static void use_molasses(Combat *c, float hx, float hy) {
  Hero *h = &c->hero;
  int level = h->levels[UP_MOLASSES];
  if (level == 0 || --h->molassesTimer > 0 || c->numBombs >= MAX_BOMBS) {
    return;
  }
  int target = nearest_monster(c, hx, hy, MOLASSES_RANGE, -1);
  if (target < 0) {
    return;
  }
  h->molassesTimer = (int)((float)(MOLASSES_COOLDOWN - 20 * level) * haste(c));
  const Monster *m = &c->monsters[target];
  c->bombs[c->numBombs++] = (Bomb){hx, hy - 20, m->x, m->y, BOMB_FLIGHT, 0, true};
}

static void spill(Combat *c, const Bomb *b) {
  int level = SDL_max(1, c->hero.levels[UP_MOLASSES]);
  int life = (MOLASSES_SECONDS + level - 1) * TICKS_PER_SECOND;
  if (c->numPuddles == MAX_PUDDLES) {
    // the oldest one dries up
    c->puddles[0] = c->puddles[--c->numPuddles];
  }
  c->puddles[c->numPuddles++] = (Puddle){b->x, b->y, 36 + 6 * (float)level, 0.4f - 0.04f * (float)(level - 1), life, life};
  c->events |= EV_SPLAT;
}

static void explode(Combat *c, const Bomb *b) {
  for (int i = c->numMonsters - 1; i >= 0; i--) {
    Monster *m = &c->monsters[i];
    if (dist2(b->x, b->y, m->x, m->y) < (BOMB_RADIUS + INFO[m->kind].radius) * (BOMB_RADIUS + INFO[m->kind].radius)) {
      m->stagger = STAGGER_TICKS * 2;
      hit_monster(c, i, BOMB_DAMAGE, b->x, b->y, 14);
    }
  }
  // it blows the demons' blobs away too
  for (int s = c->numShots - 1; s >= 0; s--) {
    if (c->shots[s].hostile && dist2(b->x, b->y, c->shots[s].x, c->shots[s].y + 14) < BOMB_RADIUS * BOMB_RADIUS) {
      c->shots[s] = c->shots[--c->numShots];
    }
  }
  if (c->numExplosions < MAX_EXPLOSIONS) {
    c->explosions[c->numExplosions++] = (Explosion){b->x, b->y, EXPLOSION_TICKS};
  }
  c->events |= EV_EXPLOSION;
}

static void update_bombs(Combat *c) {
  for (int i = c->numBombs - 1; i >= 0; i--) {
    Bomb *b = &c->bombs[i];
    if (b->flight > 0) {
      b->flight--;
    } else if (b->molasses) {
      spill(c, b);
      c->bombs[i] = c->bombs[--c->numBombs];
    } else if (--b->fuse <= 0) {
      explode(c, b);
      c->bombs[i] = c->bombs[--c->numBombs];
    }
  }
  for (int i = c->numExplosions - 1; i >= 0; i--) {
    if (--c->explosions[i].life <= 0) {
      c->explosions[i] = c->explosions[--c->numExplosions];
    }
  }
  for (int i = c->numPuddles - 1; i >= 0; i--) {
    if (--c->puddles[i].life <= 0) {
      c->puddles[i] = c->puddles[--c->numPuddles];
    }
  }
}

// ---------------------------------------------------------------- update

// The shield sends a blob back at the nearest demon, or the way it came.
static void ricochet(Combat *c, Shot *shot) {
  float speed = SDL_sqrtf(shot->vx * shot->vx + shot->vy * shot->vy) * RICOCHET_SPEED;
  float dx = -shot->vx, dy = -shot->vy;
  int target = nearest_monster(c, shot->x, shot->y + 14, 320, -1);
  if (target >= 0) {
    dx = c->monsters[target].x - shot->x;
    dy = c->monsters[target].y - 14 - shot->y;
  }
  float d = SDL_max(0.01f, SDL_sqrtf(dx * dx + dy * dy));
  shot->vx = dx / d * speed;
  shot->vy = dy / d * speed;
  shot->hostile = false;
  shot->reflected = true;
  shot->damage *= might(c);
  shot->pierce = 0;
  shot->lastHit = -1;
  shot->life = 80;
  c->events |= EV_RICOCHET;
}

static void update_shots(Combat *c, const World *world, float hx, float hy) {
  for (int s = c->numShots - 1; s >= 0; s--) {
    Shot *shot = &c->shots[s];
    shot->prevX = shot->x;
    shot->prevY = shot->y;
    shot->x += shot->vx;
    shot->y += shot->vy;
    // they fly over houses, but not through trees and fences
    int cx = (int)(shot->x / TILE_SIZE), cy = (int)((shot->y + 14) / TILE_SIZE);
    bool overHouse = world_in_bounds(world, cx, cy) && world->houseAt[cy * world->width + cx] >= 0;
    bool gone = --shot->life <= 0 ||
                (!overHouse && world_blocked(world, shot->x - 1, shot->y + 13, shot->x + 1, shot->y + 15));
    if (!gone && shot->hostile) {
      if (dist2(shot->x, shot->y, hx, hy - 16) < 14 * 14) {
        const Hero *h = &c->hero;
        float chance = RICOCHET_CHANCE + RICOCHET_PER_SHIELD * (float)(h->shields - 1);
        if (h->shields > 0 && rndf(c) < chance) {
          ricochet(c, shot);
          continue;
        }
        if (h->shields > 0 && h->invulnerable == 0) {
          c->events |= EV_BLOCK;
        }
        hurt_hero(c, shot->damage * (1 - SHIELD_BLOCK * (float)h->shields));
        gone = true;
      }
    } else if (!gone) {
      for (int i = c->numMonsters - 1; i >= 0; i--) {
        Monster *m = &c->monsters[i];
        float r = INFO[m->kind].radius + 4;
        if (i != shot->lastHit && dist2(shot->x, shot->y, m->x, m->y - 14) < r * r) {
          shot->lastHit = i;
          hit_monster(c, i, shot->damage, shot->x - shot->vx, shot->y - shot->vy, 4);
          if (shot->pierce-- <= 0) {
            gone = true;
          }
          break;
        }
      }
    }
    if (gone) {
      c->shots[s] = c->shots[--c->numShots];
    }
  }
}

static void update_souls(Combat *c, float hx, float hy) {
  Hero *h = &c->hero;
  float pickup = combat_pickup_radius(c);
  for (int i = c->numSouls - 1; i >= 0; i--) {
    Soul *soul = &c->souls[i];
    float dx = hx - soul->x, dy = hy - 12 - soul->y, d = SDL_sqrtf(dx * dx + dy * dy);
    if (d < 14) {
      if (soul->kind == SOUL_HEART) {
        heal_hero(c, HEART_HEAL);
      } else {
        h->gold += soul->value;
        c->events |= EV_COIN;
      }
      c->souls[i] = c->souls[--c->numSouls];
    } else if (d < pickup) {
      float pull = SDL_min(d, 6.0f);
      soul->x += dx / d * pull;
      soul->y += dy / d * pull;
    }
  }
  while (h->xp >= h->nextLevel) {
    h->xp -= h->nextLevel;
    h->level++;
    h->nextLevel = LEVEL_XP + h->level * LEVEL_XP;
    h->pendingLevels++;
  }
  if (h->pendingLevels > 0) {
    c->events |= EV_LEVEL_UP;
  }
}

int combat_update(Combat *c, const World *world, Npcs *npcs, float hx, float hy) {
  Hero *h = &c->hero;
  bool heroInside = world_house_at(world, hx, hy) >= 0;

  // the hero only gets his breath back between waves
  int regenEvery = REGEN_TICKS - 35 * h->levels[UP_VITALITY];
  if (c->waveState == WAVE_CALM && ++h->regenTimer >= regenEvery) {
    h->regenTimer = 0;
    h->hp = SDL_min(h->maxHp, h->hp + 1);
  }
  h->invulnerable = SDL_max(0, h->invulnerable - 1);
  h->slashTimer = SDL_max(0, h->slashTimer - 1);

  // the way to the hero, worked out again when he changes cell
  int heroCell = (int)(hy / TILE_SIZE) * world->width + (int)(hx / TILE_SIZE);
  if (!c->field) {
    c->field = SDL_malloc((size_t)(world->width * world->height) * sizeof(float));
    c->fieldTimer = 0;
  }
  if (c->field && (--c->fieldTimer <= 0 || (heroCell != c->fieldCell && c->fieldTimer < FIELD_TICKS - 5))) {
    c->fieldTimer = FIELD_TICKS;
    c->fieldCell = heroCell;
    world_distance_field(world, (int)(hx / TILE_SIZE), (int)(hy / TILE_SIZE), c->field);
  }

  update_waves(c, world, hx, hy);

  for (int i = 0; i < c->numMonsters; i++) {
    update_monster(c, world, npcs, i, hx, hy, !heroInside);
  }
  separate_monsters(c, world);
  // monsters in sight frighten the villagers
  if (c->numMonsters > 0 && SDL_rand_r(&c->rng, 10) == 0) {
    for (int i = 0; i < c->numMonsters; i++) {
      npcs_scare(npcs, world, c->monsters[i].x, c->monsters[i].y, SCARE_RADIUS);
    }
  }

  use_sword(c, hx, hy);
  use_stones(c, hx, hy);
  use_flames(c, hx, hy);
  use_aura(c, hx, hy);
  use_molasses(c, hx, hy);
  use_repel(c, hx, hy);
  update_shots(c, world, hx, hy);
  update_bombs(c);
  update_souls(c, hx, hy);

  for (int i = c->numDebris - 1; i >= 0; i--) {
    Debris *d = &c->debris[i];
    // slide to a stop, not through walls
    if (!world_blocked(world, d->x + d->vx - 4, d->y - 2, d->x + d->vx + 4, d->y)) {
      d->x += d->vx;
    }
    if (!world_blocked(world, d->x - 4, d->y + d->vy - 2, d->x + 4, d->y + d->vy)) {
      d->y += d->vy;
    }
    d->vx *= 0.88f;
    d->vy *= 0.88f;
    if (--d->life <= 0) {
      c->debris[i] = c->debris[--c->numDebris];
    }
  }
  for (int i = c->numPopups - 1; i >= 0; i--) {
    Popup *p = &c->popups[i];
    p->y -= 0.8f;
    if (--p->life <= 0) {
      c->popups[i] = c->popups[--c->numPopups];
    }
  }
  for (int i = c->numSlashes - 1; i >= 0; i--) {
    if (--c->slashes[i].life <= 0) {
      c->slashes[i] = c->slashes[--c->numSlashes];
    }
  }

  int events = c->events;
  c->events = 0;
  return events;
}

// ---------------------------------------------------------------- upgrades

const char *upgrade_name(Upgrade u) {
  static const char *const NAMES[] = {
      [UP_SWORD] = "Sword", [UP_STONES] = "Throwing stones", [UP_FLAMES] = "Spirit flames",
      [UP_AURA] = "Holy ground", [UP_MOLASSES] = "Molasses", [UP_REPEL] = "Repulsion", [UP_SPEED] = "Swift boots", [UP_VITALITY] = "Vitality",
      [UP_MIGHT] = "Might", [UP_HASTE] = "Haste",
      [UP_HEAL] = "Bread and cheese",
  };
  return NAMES[u];
}

// The pixel font has no $ % ( ) / @ \ ^ _ characters: keep them out of texts.
const char *upgrade_description(Upgrade u) {
  static const char *const TEXT[] = {
      [UP_SWORD] = "Stronger, wider slashes. Slashes both ways from level 3.",
      [UP_STONES] = "Throws stones at the nearest demons.",
      [UP_FLAMES] = "Flames circle around you and burn demons.",
      [UP_AURA] = "Blessed ground hurts demons around you.",
      [UP_MOLASSES] = "Throws jars of molasses. Demons wade through the puddles.",
      [UP_REPEL] = "A wave of force pushes nearby demons away, on a beat.",
      [UP_SPEED] = "Move a tenth faster.",
      [UP_VITALITY] = "15 more health, and heal faster between waves.",
      [UP_MIGHT] = "All weapons deal a fifth more damage.",
      [UP_HASTE] = "All weapons attack a little more often.",
      [UP_HEAL] = "Restore 20 health.",
  };
  return TEXT[u];
}

int combat_upgrade_options(Combat *c, Upgrade out[UPGRADE_CHOICES]) {
  Upgrade pool[UP_COUNT];
  int n = 0;
  for (int u = 0; u < UP_COUNT; u++) {
    if (c->hero.levels[u] < MAX_UPGRADE_LEVEL) {
      pool[n++] = (Upgrade)u;
    }
  }
  int count = 0;
  while (count < UPGRADE_CHOICES && n > 0) {
    int k = rnd(c, n);
    out[count++] = pool[k];
    pool[k] = pool[--n];
  }
  if (count < UPGRADE_CHOICES) {
    out[count++] = UP_HEAL;
  }
  return count;
}

void combat_skip_upgrade(Combat *c) {
  c->hero.pendingLevels = SDL_max(0, c->hero.pendingLevels - 1);
}

void combat_level_up(Combat *c) {
  Hero *h = &c->hero;
  h->level++;
  h->nextLevel = LEVEL_XP + h->level * LEVEL_XP;
  h->pendingLevels++;
}

void combat_apply_upgrade(Combat *c, Upgrade u) {
  Hero *h = &c->hero;
  if (u == UP_HEAL) {
    h->hp = SDL_min(h->maxHp, h->hp + 20);
  } else {
    h->levels[u]++;
    if (u == UP_VITALITY) {
      h->maxHp += 15;
      h->hp += 15;
    }
  }
  h->pendingLevels = SDL_max(0, h->pendingLevels - 1);
}

// ---------------------------------------------------------------- shop

// The pixel font has no $ % ( ) / @ \ ^ _ characters: keep them out of texts.
const char *item_name(Item item) {
  static const char *const NAMES[] = {
      [ITEM_POTION] = "Health potion", [ITEM_BOMB] = "Bomb",         [ITEM_AMULET] = "Heart amulet",
      [ITEM_WISDOM] = "Wisdom amulet", [ITEM_SHIELD] = "Wooden shield",
  };
  return NAMES[item];
}

const char *item_description(Item item) {
  static const char *const TEXT[] = {
      [ITEM_POTION] = "Restores 20 health. Drink it with Q.",
      [ITEM_BOMB] = "Blows up the nearest demons. Throw it with E.",
      [ITEM_AMULET] = "10 more health, for good.",
      [ITEM_WISDOM] = "A fifth more experience from each kill.",
      [ITEM_SHIELD] = "Blocks some blob damage, may bounce them back.",
  };
  return TEXT[item];
}

int combat_item_price(const Combat *c, Item item) {
  switch (item) {
    case ITEM_POTION: return 15;
    case ITEM_BOMB: return 20;
    // dearer each time
    case ITEM_AMULET: return 40 + 20 * c->hero.amulets;
    case ITEM_WISDOM: return 45 + 30 * c->hero.wisdom;
    case ITEM_SHIELD: return 25 + 10 * c->hero.shields;
    case ITEM_COUNT: break;
  }
  return 0;
}

int combat_item_max(Item item) {
  return item == ITEM_SHIELD ? MAX_SHIELDS : MAX_ITEMS;
}

int combat_item_count(const Combat *c, Item item) {
  switch (item) {
    case ITEM_POTION: return c->hero.potions;
    case ITEM_BOMB: return c->hero.bombs;
    case ITEM_AMULET: return c->hero.amulets;
    case ITEM_WISDOM: return c->hero.wisdom;
    case ITEM_SHIELD: return c->hero.shields;
    case ITEM_COUNT: break;
  }
  return 0;
}

bool combat_buy(Combat *c, Item item) {
  Hero *h = &c->hero;
  int price = combat_item_price(c, item);
  if (h->gold < price || combat_item_count(c, item) >= combat_item_max(item)) {
    return false;
  }
  h->gold -= price;
  switch (item) {
    case ITEM_POTION: h->potions++; break;
    case ITEM_BOMB: h->bombs++; break;
    case ITEM_AMULET:
      h->amulets++;
      h->maxHp += AMULET_HP;
      h->hp += AMULET_HP;
      break;
    case ITEM_WISDOM: h->wisdom++; break;
    case ITEM_SHIELD: h->shields++; break;
    case ITEM_COUNT: break;
  }
  return true;
}

bool combat_drink_potion(Combat *c) {
  Hero *h = &c->hero;
  if (h->potions == 0 || h->hp >= h->maxHp || h->hp <= 0) {
    return false;
  }
  h->potions--;
  heal_hero(c, POTION_HEAL);
  return true;
}

bool combat_throw_bomb(Combat *c, float hx, float hy) {
  Hero *h = &c->hero;
  if (h->bombs == 0 || h->hp <= 0 || c->numBombs >= MAX_BOMBS) {
    return false;
  }
  h->bombs--;
  // at the nearest demon, or dropped at the hero's feet
  int target = nearest_monster(c, hx, hy, BOMB_RANGE, -1);
  float tx = target >= 0 ? c->monsters[target].x : hx, ty = target >= 0 ? c->monsters[target].y : hy;
  c->bombs[c->numBombs++] = (Bomb){hx, hy - 20, tx, ty, BOMB_FLIGHT, BOMB_FUSE, false};
  return true;
}

float combat_bomb_radius(void) {
  return BOMB_RADIUS;
}
