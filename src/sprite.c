/*
  Copyright (C) 2013 batiste.bieler@gmail.com

  Sprites and frame based animations
 */

#include "sprite.h"

void sprite_draw(SDL_Renderer *renderer, const Sprite *sprite, float x, float y) {
  SDL_FRect dest = {x, y, sprite->source.w, sprite->source.h};
  SDL_RenderTexture(renderer, sprite->texture, &sprite->source, &dest);
}

Animation *animation_create(SDL_Texture *texture, SDL_Rect first, int ticksPerFrame, int numFrames) {
  float texW, texH;
  SDL_GetTextureSize(texture, &texW, &texH);
  int columns = ((int)texW - first.x) / first.w;
  if (numFrames > 0) {
    columns = SDL_min(numFrames, columns);
  }
  if (columns <= 0 || ticksPerFrame <= 0) {
    return NULL;
  }

  Animation *anim = SDL_malloc(sizeof(*anim));
  Sprite *frames = SDL_malloc((size_t)columns * sizeof(*frames));
  if (!anim || !frames) {
    SDL_free(anim);
    SDL_free(frames);
    return NULL;
  }

  for (int i = 0; i < columns; i++) {
    frames[i] = (Sprite){
        .texture = texture,
        .source = {(float)(first.x + i * first.w), (float)first.y, (float)first.w, (float)first.h},
    };
  }
  *anim = (Animation){.frames = frames, .numFrames = columns, .ticksPerFrame = ticksPerFrame};
  return anim;
}

void animation_destroy(Animation *anim) {
  if (anim) {
    SDL_free(anim->frames);
    SDL_free(anim);
  }
}

const Sprite *animation_frame(const Animation *anim, Uint64 tick) {
  Uint64 index = (tick / (Uint64)anim->ticksPerFrame) % (Uint64)anim->numFrames;
  return &anim->frames[index];
}
