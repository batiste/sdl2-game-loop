/*
  Copyright (C) 2013 batiste.bieler@gmail.com

  The invasion of the evil lords: waves of demons attack the villages.

  Waves alternate with calm periods. Each wave targets the village nearest
  to the hero: demons gather in the countryside, march along the roads and
  go after the villagers, who run for the houses. A villager killed by a
  demon rises again as a skeleton. Every fifth wave brings an evil lord.
 */

#include "combat.h"

#include <float.h>

#define TICKS_PER_SECOND 50
#define MAX_MONSTERS 400

#define HERO_HP 50
#define FIELD_TICKS 25  // how often the way to the hero is worked out again
#define HERO_INVULNERABLE 28
#define FIRST_WAVE_DELAY (20 * TICKS_PER_SECOND)
#define CALM_DELAY (15 * TICKS_PER_SECOND)

// difficulty
#define WAVE_BASE 35            // demons in a wave: WAVE_BASE + WAVE_GROWTH * wave
#define WAVE_GROWTH 15
#define WAVE_TOUGHNESS 0.2f     // extra health per wave
#define LORDS_PER_WAVE 0.1f     // share of lords grows with each wave...
#define MAX_LORDS 0.4f          // ...up to this
#define SWORD_RANGE 44          // reach at level 1, in pixels...
#define SWORD_RANGE_LEVEL 6     // ...and per level above
#define SWORD_ARC 0.6f          // half of the swing at level 1, in radians...
#define SWORD_ARC_LEVEL 0.1f    // ...and per level above
#define REGEN_TICKS 300         // one health point every so many ticks
#define HEART_CHANCE 0.02f
#define HEART_HEAL 10
#define SCARE_RADIUS 260.0f
#define VILLAGER_SENSE 90.0f  // demons bite villagers this close on their way
#define CHASE_DIRECT 220.0f   // closer than this, demons go straight for their prey

typedef struct MonsterInfo {
  float hp, speed, damage;
  int xp;
  float radius;  // body, for hits
} MonsterInfo;

static const MonsterInfo INFO[MON_KINDS] = {
    [MON_UNDERLING] = {3, 1.5f, 3, 1, 10},
    [MON_LORD] = {8, 1.05f, 4, 3, 11},
    [MON_BOSS] = {200, 1.2f, 12, 25, 24},
    [MON_SKELETON] = {5, 1.0f, 4, 2, 10},
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
  return 48 * (1 + 0.4f * (float)c->hero.levels[UP_MAGNET]);
}

float combat_aura_radius(const Combat *c) {
  return 56 + 12 * (float)c->hero.levels[UP_AURA];
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

// ---------------------------------------------------------------- setup

void combat_init(Combat *c, Uint64 seed) {
  combat_free(c);
  *c = (Combat){.rng = seed ^ 0xd1b54a32d192ed03ull};
  c->hero = (Hero){.hp = HERO_HP, .maxHp = HERO_HP, .nextLevel = 10};
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
  // later waves are tougher
  float hp = INFO[kind].hp * (1 + WAVE_TOUGHNESS * (float)SDL_max(0, c->wave - 1));
  *m = (Monster){.kind = kind, .x = x, .y = y, .prevX = x, .prevY = y, .hp = hp, .maxHp = hp,
                 .target = -2, .retarget = rnd(c, 15), .shootTimer = 60 + rnd(c, 60)};
  return m;
}

static void remove_monster(Combat *c, int i) {
  c->monsters[i] = c->monsters[--c->numMonsters];
}

// ---------------------------------------------------------------- effects

static void add_soul(Combat *c, float x, float y, int value) {
  if (c->numSouls < MAX_SOULS) {
    c->souls[c->numSouls++] = (Soul){x + (rndf(c) - 0.5f) * 10, y + (rndf(c) - 0.5f) * 6, value};
  }
}

static void kill_monster(Combat *c, int i) {
  Monster *m = &c->monsters[i];
  for (int side = 0; side < 2 && c->numDebris < MAX_DEBRIS; side++) {
    c->debris[c->numDebris++] = (Debris){m->kind, side == 1, m->x, m->y, side ? 1.2f : -1.2f, 30};
  }
  int xp = INFO[m->kind].xp;
  if (m->kind == MON_BOSS) {
    for (int k = 0; k < 10; k++) {
      add_soul(c, m->x + (rndf(c) - 0.5f) * 40, m->y + (rndf(c) - 0.5f) * 30, 3);
    }
    c->bossAlive = false;
    c->events |= EV_BOSS_KILLED;
  } else {
    add_soul(c, m->x, m->y, xp);
  }
  if (rndf(c) < HEART_CHANCE) {
    add_soul(c, m->x + 10, m->y, 0);  // a heart
  }
  c->kills++;
  c->events |= EV_MONSTER_DIED;
  remove_monster(c, i);
}

// Returns true if the monster died (and was removed).
static bool hit_monster(Combat *c, int i, float damage, float fromX, float fromY, float kick) {
  Monster *m = &c->monsters[i];
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
                                   hostile, false, -1};
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
    float mx = x + (rndf(c) - 0.5f) * 24, my = y + (rndf(c) - 0.5f) * 16;
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
    c->bossPending = c->wave % 5 == 0;
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
    } else {
      MESSAGE(c, "Wave %d: demons attack %s!", c->wave, name);
    }
    return;
  }

  if (c->toSpawn > 0 && --c->spawnTimer <= 0) {
    c->spawnTimer = (int)(0.4f * TICKS_PER_SECOND);
    int pack = SDL_min(c->toSpawn, 3 + rnd(c, 4));
    float lords = SDL_min(MAX_LORDS, LORDS_PER_WAVE * (float)c->wave);
    if (spawn_pack(c, world, pack, rndf(c) < lords ? MON_LORD : MON_UNDERLING)) {
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
    c->events |= EV_WAVE_CLEARED;
    MESSAGE(c, "Wave %d cleared! Next one in %d seconds", c->wave, CALM_DELAY / TICKS_PER_SECOND);
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
    m->shootTimer = 35;
    float a = SDL_atan2f(dy, dx);
    for (int k = -1; k <= 1; k++) {
      float ak = a + (float)k * 0.25f;
      fire(c, m->x, m->y - 30, m->x + SDL_cosf(ak), m->y - 30 + SDL_sinf(ak), 3.6f, 8, true, 0, 100);
      c->shots[c->numShots - 1].boss = true;
    }
  } else {
    m->shootTimer = 80 + rnd(c, 30);
    fire(c, m->x, m->y - 16, tx, ty - 16, 2.8f, 8, true, 0, 110);
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

  // knockback
  if (SDL_fabsf(m->kickX) + SDL_fabsf(m->kickY) > 0.1f) {
    monster_step(world, m, m->kickX, m->kickY);
    m->kickX *= 0.75f;
    m->kickY *= 0.75f;
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

  float speed = info->speed;
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
  bool direct = d < CHASE_DIRECT;
  if (direct) {
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
      float dx = b->x - a->x, dy = b->y - a->y, d2 = dx * dx + dy * dy;
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
        hit_monster(c, i, damage, hx, hy, 6);
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
    fire(c, hx, hy - 20, m->x, m->y - 14, 7, (1.5f + 0.5f * (float)level) * might(c), false,
         level >= 4 ? 2 : (level >= 2 ? 1 : 0), 60);
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

// ---------------------------------------------------------------- update

static void update_shots(Combat *c, const World *world, float hx, float hy) {
  for (int s = c->numShots - 1; s >= 0; s--) {
    Shot *shot = &c->shots[s];
    shot->prevX = shot->x;
    shot->prevY = shot->y;
    shot->x += shot->vx;
    shot->y += shot->vy;
    bool gone = --shot->life <= 0 || world_house_at(world, shot->x, shot->y + 14) >= 0 ||
                world_blocked(world, shot->x - 1, shot->y + 13, shot->x + 1, shot->y + 15);
    if (!gone && shot->hostile) {
      if (dist2(shot->x, shot->y, hx, hy - 16) < 14 * 14) {
        hurt_hero(c, shot->damage);
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
      if (soul->value == 0) {
        h->hp = SDL_min(h->maxHp, h->hp + HEART_HEAL);
      } else {
        h->xp += soul->value;
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
    h->nextLevel = 10 + h->level * 10;
    h->pendingLevels++;
  }
  if (h->pendingLevels > 0) {
    c->events |= EV_LEVEL_UP;
  }
}

int combat_update(Combat *c, const World *world, Npcs *npcs, float hx, float hy) {
  Hero *h = &c->hero;
  bool heroInside = world_house_at(world, hx, hy) >= 0;

  // healing, faster when resting in a house
  int regenEvery = REGEN_TICKS - 35 * h->levels[UP_VITALITY];
  if (++h->regenTimer >= (heroInside ? regenEvery / 3 : regenEvery)) {
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
  update_shots(c, world, hx, hy);
  update_souls(c, hx, hy);

  for (int i = c->numDebris - 1; i >= 0; i--) {
    Debris *d = &c->debris[i];
    d->x += d->vx;
    if (--d->life <= 0) {
      c->debris[i] = c->debris[--c->numDebris];
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
      [UP_AURA] = "Holy ground", [UP_SPEED] = "Swift boots", [UP_VITALITY] = "Vitality",
      [UP_MIGHT] = "Might", [UP_HASTE] = "Haste", [UP_MAGNET] = "Soul magnet",
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
      [UP_SPEED] = "Move a tenth faster.",
      [UP_VITALITY] = "15 more health, and heal faster.",
      [UP_MIGHT] = "All weapons deal a fifth more damage.",
      [UP_HASTE] = "All weapons attack a little more often.",
      [UP_MAGNET] = "Collect souls from further away.",
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
