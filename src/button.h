/*
  Copyright (C) 2013 batiste.bieler@gmail.com

  Buttons in the style of the pixel art: an outline with a dent cut in every
  corner, a bevel, and a base that shows how far they stand up. Used for
  buttons, the level up cards and the panels.
 */

#ifndef GAME_BUTTON_H
#define GAME_BUTTON_H

#include <SDL3/SDL.h>

#define BUTTON_DEPTH 4.0f  // the base shows this much below the face

typedef struct ButtonStyle {
  SDL_Color face;
  SDL_Color light;  // highlight along the top
  SDL_Color dark;   // shade along the bottom
  SDL_Color side;   // the base
  SDL_Color outline;
} ButtonStyle;

typedef enum ButtonState { BUTTON_IDLE, BUTTON_HOVER, BUTTON_PRESSED } ButtonState;

// A style from a face colour: lighter and darker edges, a dark brown outline.
ButtonStyle button_style(SDL_Color face);

// Whether the mouse is over the button drawn at rect, and holding it down.
bool button_contains(SDL_FRect rect, SDL_FPoint point);
ButtonState button_state(SDL_FRect rect, SDL_FPoint mouse, bool mouseDown);

// Draw the button raised at rect; it sinks onto its base when pressed and
// takes BUTTON_DEPTH more pixels below. Returns the face, inside the outline,
// where the contents go.
SDL_FRect button_draw(SDL_Renderer *renderer, SDL_FRect rect, const ButtonStyle *style, ButtonState state);

// Fill the shape of a button: a rectangle with a dent in every corner. The
// parts don't overlap, so translucent colours blend evenly.
void button_shape(SDL_Renderer *renderer, SDL_FRect rect, SDL_Color color);

#endif
