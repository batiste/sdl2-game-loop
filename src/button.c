/*
  Copyright (C) 2013 batiste.bieler@gmail.com

  Buttons in the style of the pixel art.
 */

#include "button.h"

#define PIXEL 2.0f  // one pixel of the art
#define DENT 4.0f   // size of the square cut in the corners

static SDL_Color mix(SDL_Color a, SDL_Color b, float t) {
  return (SDL_Color){(Uint8)((float)a.r + ((float)b.r - (float)a.r) * t),
                     (Uint8)((float)a.g + ((float)b.g - (float)a.g) * t),
                     (Uint8)((float)a.b + ((float)b.b - (float)a.b) * t), a.a};
}

static void rect(SDL_Renderer *renderer, float x, float y, float w, float h, SDL_Color c) {
  if (w > 0 && h > 0) {
    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(renderer, &(SDL_FRect){x, y, w, h});
  }
}

static void dented(SDL_Renderer *renderer, float x, float y, float w, float h, SDL_Color c) {
  rect(renderer, x + DENT, y, w - 2 * DENT, h, c);
  rect(renderer, x, y + DENT, DENT, h - 2 * DENT, c);
  rect(renderer, x + w - DENT, y + DENT, DENT, h - 2 * DENT, c);
}

ButtonStyle button_style(SDL_Color face) {
  const SDL_Color white = {255, 255, 255, 255}, black = {0, 0, 0, 255};
  return (ButtonStyle){
      .face = face,
      .light = mix(face, white, 0.5f),
      .dark = mix(face, black, 0.22f),
      .side = mix(face, black, 0.5f),
      .outline = {58, 41, 41, 255},
  };
}

bool button_contains(SDL_FRect r, SDL_FPoint point) {
  SDL_FRect bounds = {r.x, r.y, r.w, r.h + BUTTON_DEPTH};
  return SDL_PointInRectFloat(&point, &bounds);
}

ButtonState button_state(SDL_FRect r, SDL_FPoint mouse, bool mouseDown) {
  if (!button_contains(r, mouse)) {
    return BUTTON_IDLE;
  }
  return mouseDown ? BUTTON_PRESSED : BUTTON_HOVER;
}

void button_shape(SDL_Renderer *renderer, SDL_FRect r, SDL_Color color) {
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  dented(renderer, r.x, r.y, r.w, r.h, color);
}

SDL_FRect button_draw(SDL_Renderer *renderer, SDL_FRect r, const ButtonStyle *style, ButtonState state) {
  // on the art pixel grid
  float x = SDL_floorf(r.x / PIXEL) * PIXEL, y = SDL_floorf(r.y / PIXEL) * PIXEL;
  float w = SDL_floorf(r.w / PIXEL) * PIXEL, h = SDL_floorf(r.h / PIXEL) * PIXEL;
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

  // a soft shadow on the ground, then the base
  dented(renderer, x + PIXEL, y + BUTTON_DEPTH + PIXEL, w, h, (SDL_Color){0, 0, 0, 70});
  dented(renderer, x, y + BUTTON_DEPTH, w, h, style->outline);
  dented(renderer, x + PIXEL, y + BUTTON_DEPTH + PIXEL, w - 2 * PIXEL, h - 2 * PIXEL, style->side);

  if (state == BUTTON_PRESSED) {
    y += BUTTON_DEPTH;
  }
  SDL_Color face = state == BUTTON_HOVER ? mix(style->face, (SDL_Color){255, 255, 255, 255}, 0.2f) : style->face;
  SDL_Color light = state == BUTTON_PRESSED ? style->dark : style->light;
  dented(renderer, x, y, w, h, style->outline);
  dented(renderer, x + PIXEL, y + PIXEL, w - 2 * PIXEL, h - 2 * PIXEL, face);

  // the bevel follows the dents
  float in = x + PIXEL, top = y + PIXEL, bottom = y + h - 2 * PIXEL;
  rect(renderer, in + DENT, top, w - 2 * PIXEL - 2 * DENT, PIXEL, light);
  rect(renderer, in, top + DENT, DENT, PIXEL, light);
  rect(renderer, x + w - PIXEL - DENT, top + DENT, DENT, PIXEL, light);
  if (state != BUTTON_PRESSED) {
    rect(renderer, in + DENT, bottom, w - 2 * PIXEL - 2 * DENT, PIXEL, style->dark);
    rect(renderer, in, bottom - DENT, DENT, PIXEL, style->dark);
    rect(renderer, x + w - PIXEL - DENT, bottom - DENT, DENT, PIXEL, style->dark);
  }
  return (SDL_FRect){x + PIXEL, y + PIXEL, w - 2 * PIXEL, h - 2 * PIXEL};
}
