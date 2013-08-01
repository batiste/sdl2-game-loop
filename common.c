/*
  Copyright (C) 2013 batiste.bieler@gmail.com 
    
  Common function and globals,
  Abstract away what is not central to the game logic
 */

#ifndef GAME_LOOP_COMMON
#define GAME_LOOP_COMMON 1

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <math.h>

#include "list.c"

#include "SDL.h"
#include "SDL_image.h"
#include "SDL_ttf.h"
#include "SDL_audio.h"
#include "SDL_mixer.h"
#define TICK_INTERVAL   20

// this seems necessary to do this: SDL_Texture->w
// use SDL_QueryTexture(img, NULL, NULL, &w, &h); to get the size
// #include "SDL2/SDL/src/render/SDL_sysrender.h"

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))


// globals
SDL_Window * window = NULL;
SDL_Renderer * renderer = NULL;
SDL_DisplayMode displaymode;
SDL_Rect viewport;

GenericList * texturesList;
GenericList * musicList;
GenericList * fontList;

SDL_Color black;
SDL_Color white;

// 0: full FPS, 1: cap the framerate
int draw_mode = 1;
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

    printf("Cleanup\n");

    ListElement *el;
    
    for(el = texturesList->first; el != NULL; el=el->next) {
      printf("Destroy texture\n");
      SDL_DestroyTexture((SDL_Texture *)el->data);
    }
    destroyList(texturesList);

    for(el = musicList->first; el != NULL; el=el->next) {
      printf("Destroy music\n");
      Mix_FreeMusic((Mix_Music *)el->data);
    }
    destroyList(musicList);

    for(el = fontList->first; el != NULL; el=el->next) {
      printf("Destroy font\n");
      TTF_CloseFont((TTF_Font *)el->data);
    }
    destroyList(fontList);

    if(renderer) {
        SDL_DestroyRenderer(renderer);
        //printf("%s\n", SDL_GetError());
    }
    if(window) {
        SDL_DestroyWindow(window);
        //printf("%s\n", SDL_GetError());
    }
    Mix_CloseAudio();
    SDL_Quit();
    printf("Bye!\n");
    exit(0);
}



void init(void) {

  texturesList = createList();
  musicList = createList();
  fontList = createList();
  black.r = 0; black.g = 0; black.b = 0;
  white.r = 255; white.g = 255; white.b = 255;

 // Initialize SDL2
  if( SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
    fprintf(stderr, "Unable to initialize SDL: %s \n", SDL_GetError());
    quit(1);
  }

  // Display available audio device
  int count = SDL_GetNumAudioDevices(0), i;
  for (i = 0; i < count; ++i ) {
    fprintf(stderr, "Audio device %d: %s\n", i, SDL_GetAudioDeviceName(i, 0));
  }

  // init sound
  int audio_rate = 22050;
  Uint16 audio_format = AUDIO_S16SYS;
  int audio_channels = 4;
  int audio_buffers = 4096;

  if(Mix_OpenAudio(audio_rate, audio_format, audio_channels, audio_buffers) != 0) {
    fprintf(stderr, "Unable to initialize audio: %s\n", Mix_GetError());
    quit(1);
  }

  // Get desktop information
  if (SDL_GetDesktopDisplayMode(0, &displaymode) < 0) {
    fprintf(stderr, "Could not get display mode: %s\n", SDL_GetError());
    quit(1);
  }

  viewport.x = 0;
  viewport.y = 0;
  viewport.w = MAX(displaymode.w, 800) - 150;
  viewport.h = MAX(displaymode.h, 600) - 150;

  // Create an application window with the following settings:
  window = SDL_CreateWindow(
      "Game example",                    //    window title
      SDL_WINDOWPOS_UNDEFINED,           //    initial x destination
      SDL_WINDOWPOS_UNDEFINED,           //    initial y destination
      viewport.w,                        //    width, in pixels
      viewport.h,                        //    height, in pixels
      SDL_WINDOW_SHOWN                   //    flags
  );

  // Check that the window was successfully made
  if(window==NULL){   
      // In the event that the window could not be made...
      fprintf(stderr, "Could not create window: %s\n", SDL_GetError());
      quit(1);
  }
  
  renderer = SDL_CreateRenderer(window, -1, 0); // SDL_RENDERER_PRESENTVSYNC
  if (renderer < 0) {
      fprintf(stderr, "Could not create renderer: %s\n", SDL_GetError());
      quit(1);
  }

  SDL_RenderGetViewport(renderer, &viewport);

  if (TTF_Init() == -1) {
      fprintf(stderr, "Unable to initialize SDL_ttf: %s \n", TTF_GetError());
      quit(1);
  }

}


SDL_Texture * renderFontToTexture(TTF_Font * font, char * text) {
    // Create a text texture with a shadow effect
    SDL_Surface * text1 = TTF_RenderText_Blended(font, text, black);
    SDL_Surface * text2 = TTF_RenderText_Blended(font, text, white);

    /* Create a 32-bit surface with the bytes of each pixel in R,G,B,A order,
       as expected by OpenGL for textures */
    SDL_Surface *surface;
    Uint32 rmask, gmask, bmask, amask;

    /* SDL interprets each pixel as a 32-bit number, so our masks must depend
       on the endianness (byte order) of the machine */
    #if SDL_BYTEORDER == SDL_BIG_ENDIAN
        rmask = 0xff000000;
        gmask = 0x00ff0000;
        bmask = 0x0000ff00;
        amask = 0x000000ff;
    #else
        rmask = 0x000000ff;
        gmask = 0x0000ff00;
        bmask = 0x00ff0000;
        amask = 0xff000000;
    #endif

    // with amask = 0, I see the image, with amask = 0xff000000, I see nothing
    surface = SDL_CreateRGBSurface(0, text1->w + 8, text1->h + 8, 32,
                                   rmask, gmask, bmask, amask);

    SDL_ConvertSurface(surface, text1->format, 0);
    //SDL_PixelFormat *fmt;
    //surface->format;

    SDL_Rect text_rect;
    text_rect.x = 4;
    text_rect.y = 4;
    text_rect.w = text2->w;
    text_rect.h = text2->h;

    SDL_BlitSurface(text2, NULL, surface, &text_rect);
    text_rect.x = 2;
    text_rect.y = 2;
    SDL_BlitSurface(text1, NULL, surface, &text_rect);

    SDL_Texture * texture = SDL_CreateTextureFromSurface(renderer, surface);

    // This important to free the surface to avoid leaks
    SDL_FreeSurface(text1);
    SDL_FreeSurface(text2);
    SDL_FreeSurface(surface);
    return texture;
}



// Helper fonction to load assets

SDL_Texture * getTexture(char * filename) {
  SDL_Texture * texture = IMG_LoadTexture(renderer, filename);
  if (texture == NULL) {
      fprintf(stderr, "Couldn't load %s: %s\n", filename, SDL_GetError());
      quit(1);
  }
  addToList(texturesList, texture);
  return texture;
}

Mix_Music * getMusic(char *  filename) {
  Mix_Music * music = Mix_LoadMUS(filename); 
  if(music == NULL) {
    fprintf(stderr, "Unable to load sound file: %s\n", Mix_GetError()); 
    quit(1); 
  }
  addToList(musicList, music);
  return music;
}

TTF_Font * getFont(char *  filename, int size) {
  TTF_Font * font = TTF_OpenFont(filename, size);
  if(font == NULL) {
    fprintf(stderr, "Unable to load ttf file: %s\n", SDL_GetError()); 
    quit(1); 
  }
  addToList(fontList, font);
  return font;
}




#endif
