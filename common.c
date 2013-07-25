/*
  Copyright (C) 2013 batiste.bieler@gmail.com 
    
  Common function and globals
 */

#ifndef GAME_LOOP_COMMON
#define GAME_LOOP_COMMON 1

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <math.h>

#include "SDL.h"
#include "SDL_image.h"
#include "SDL_ttf.h"
#include "SDL_audio.h"
#include "SDL_mixer.h"
#define TICK_INTERVAL   20

// this seems necessary to do this: SDL_Texture->w
#include "SDL2/SDL/src/render/SDL_sysrender.h"

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

// globals
SDL_Window *window;
SDL_Renderer *renderer;
SDL_Rect viewport;
// 0: full FPS, 1: cap the framerate
int draw_mode = 0;
static Uint32 next_time;


inline int mod(a, b) {
  int c = a % b;
  return (c < 0) ? c + b : c;
}

Uint32 
timeLeft(void) {
    Uint32 now;
    now = SDL_GetTicks();
    if(next_time <= now)
        return 0;
    else
        return next_time - now;
}

void 
capFramerate(void) {
  SDL_Delay(timeLeft());
  next_time += TICK_INTERVAL;
}


// Call this instead of exit(), so we can clean up SDL
static void
quit(int rc) {
    if(renderer) {
        SDL_DestroyRenderer(renderer);
        printf("%s\n", SDL_GetError());
    }
    if(window) {
        SDL_DestroyWindow(window);
        printf("%s\n", SDL_GetError());
    }
    SDL_Quit();
    printf("End of the program\n");
    exit(0);
}


SDL_Texture * getTexture(char *  filename) {
  SDL_Texture * texture = IMG_LoadTexture(renderer, filename);
  if (!texture) {
      fprintf(stderr, "Couldn't load %s: %s\n", filename, SDL_GetError());
      quit(1);
  }
  return texture;
}

Mix_Music * getMusic(char *  filename) {
  Mix_Music * music = Mix_LoadMUS(filename); 
  if(music == NULL) {
    printf("Unable to load sound file: %s\n", Mix_GetError()); 
    quit(1); 
  }
  return music;
}


#endif
