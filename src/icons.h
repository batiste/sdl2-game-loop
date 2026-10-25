/*
  Copyright (C) 2013 batiste.bieler@gmail.com

  Small pixel art icons drawn from text, for the shop and the HUD.
 */

#ifndef GAME_ICONS_H
#define GAME_ICONS_H

#include <SDL3/SDL.h>

typedef enum IconKind {
  ICON_POTION,
  ICON_BOMB,
  ICON_AMULET,
  ICON_COIN,
  ICON_JAR,
  ICON_WISDOM,
  ICON_SHIELD,
  ICON_BAT_UP,
  ICON_BAT_DOWN,
  ICON_SHIELD_SMALL,  // on the hero's arm
  // upgrades
  ICON_SWORD,
  ICON_STONES,
  ICON_FLAMES,
  ICON_AURA,
  ICON_REPULSION,
  ICON_BOOTS,
  ICON_HEART,
  ICON_HAMMER,
  ICON_HASTE,
  ICON_COUNT,
} IconKind;

// Size in pixels when drawn with art pixels `unit` pixels wide.
SDL_FPoint icon_size(IconKind icon, float unit);
// Top left at (x, y), on the grid of the unit.
void icon_draw(SDL_Renderer *renderer, IconKind icon, float x, float y, float unit);

#endif
