/*
  Copyright (C) 2013 batiste.bieler@gmail.com

  Sprites and frame based animations
 */

#ifndef GAME_SPRITE_H
#define GAME_SPRITE_H

#include <SDL3/SDL.h>

typedef struct Sprite {
  SDL_Texture *texture;
  SDL_FRect source;
} Sprite;

typedef struct Animation {
  Sprite *frames;
  int numFrames;
  int ticksPerFrame;
} Animation;

void sprite_draw(SDL_Renderer *renderer, const Sprite *sprite, float x, float y);

// Build an animation from `numFrames` consecutive cells of a sprite sheet row,
// starting at `first`. numFrames == 0 uses every cell until the texture edge.
Animation *animation_create(SDL_Texture *texture, SDL_Rect first, int ticksPerFrame, int numFrames);
void animation_destroy(Animation *anim);

const Sprite *animation_frame(const Animation *anim, Uint64 tick);

#endif
