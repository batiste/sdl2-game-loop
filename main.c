/*
  Copyright (C) 2013 batiste.bieler@gmail.com 
    
  Based on a SDL example from Sam Lantinga <slouken@libsdl.org>
 */

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <math.h>

#include "SDL.h"
#include "SDL_image.h"
#include "SDL_ttf.h"
#include "SDL_audio.h"
#include "SDL_mixer.h"

#include "list.c"
// this seems necessary to do this: SDL_Texture->w
#include "SDL2/SDL/src/render/SDL_sysrender.h"

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
#define TICK_INTERVAL   20

inline int mod(a, b) {
  int c = a % b;
  return (c < 0) ? c + b : c;
}

static Uint32 next_time;
static SDL_Rect viewport;
SDL_Window *window;
SDL_Renderer *renderer;

struct Sprite {
    SDL_Texture * texture;
    struct SDL_Rect source;
    struct SDL_Rect destination;
};
typedef struct Sprite Sprite;

struct SpriteTable {
    // a table of pointer of sprite
    Sprite ** table;
    int length;
};
typedef struct SpriteTable SpriteTable;

struct Animation {
    SpriteTable * sprites;
    Uint32 startTick;
    Uint32 currentTick;
    int ticksBySprite;
};
typedef struct Animation Animation;

Sprite * 
createSprite(SDL_Texture * texture, int w, int h) {
    Sprite * sp = (Sprite *)malloc(sizeof(Sprite));
    sp->texture = texture;
    sp->source.x = 0;
    sp->source.y = 0;
    sp->source.w = w;
    sp->source.h = h;

    sp->destination.x = 0;
    sp->destination.y = 0;
    sp->destination.w = w;
    sp->destination.h = h;
    return sp;
}

Animation * createAnimation(SDL_Texture * texture, SDL_Rect * sprites_start, int ticksBySprite, int numberSprite) {

    Sprite * sprite;
    int j;
    int columns = (texture->w - sprites_start->x) / sprites_start->w;
    if(numberSprite != 0) {
      columns = MIN(numberSprite, columns);
    }

    Sprite ** table = (Sprite **) malloc(columns * sizeof(Sprite *));

    for(j=0; j<columns; j++) {
        sprite = createSprite(texture, sprites_start->w, sprites_start->h);
        sprite->source.x = j * sprites_start->w + sprites_start->x;
        sprite->source.y = sprites_start->y;
        table[j] = sprite;
    }

    SpriteTable * spritetable = (SpriteTable *) malloc(sizeof(SpriteTable));
    spritetable->table = table;
    spritetable->length = columns;
  
    Animation * anim = (Animation *) malloc(sizeof(Animation));
    anim->sprites = spritetable;
    anim->ticksBySprite = ticksBySprite;

    anim->startTick = 0;
    anim->currentTick = 0;

    return anim;
}

Sprite * 
getSpriteFromAnimation(Animation * anim, int frame) {
    int spriteIndex = (frame / TICK_INTERVAL) % anim->sprites->length;
    return anim->sprites->table[spriteIndex];
}


// 0: full FPS, 1: cap the framerate
int draw_mode = 0;

Uint32 
time_left(void) {
    Uint32 now;
    now = SDL_GetTicks();
    if(next_time <= now)
        return 0;
    else
        return next_time - now;
}

void 
cap_framerate(void) {
  SDL_Delay(time_left());
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

void
drawSprite(SDL_Renderer * renderer, Sprite * sp) {
    SDL_RenderCopy(renderer, sp->texture, &sp->source, &sp->destination);
}



void
drawSpriteAt(SDL_Renderer * renderer, Sprite * sp, int x, int y) {
  SDL_Rect _rect;
  _rect.x = x;
  _rect.y = y;
  _rect.w = sp->destination.w;
  _rect.h = sp->destination.h;
  SDL_RenderCopy(renderer, sp->texture, &sp->source, &_rect);
}


SpriteTable *
splitTextureTable(SDL_Texture * texture, int w, int h) {
    Sprite * sprite;
    int i, j;
    int columns = texture->w / w;
    int lines = texture->h / h;

    Sprite ** table = (Sprite **) malloc(columns * lines * sizeof(Sprite *));
   
    printf("Texture has lines %d and columns %d\n", lines, columns);

    for(i=0; i<lines; i++) {
        for(j=0; j<columns; j++) {
            sprite = createSprite(texture, w, h);
            sprite->source.x = j * w;
            sprite->source.y = i * h;
            table[i * columns + j] = sprite;
        }
    }

    SpriteTable * spritetable = (SpriteTable *) malloc(sizeof(SpriteTable));
    spritetable->table = table;
    spritetable->length = lines * columns;

    return spritetable;
}

SDL_Texture * getTexture(SDL_Renderer * renderer, char *  filename) {
  SDL_Texture * texture = IMG_LoadTexture(renderer, filename);
  if (!texture) {
      fprintf(stderr, "Couldn't load %s: %s\n", filename, SDL_GetError());
      quit(1);
  }
  return texture;
}

// Keyboard variables and functions

int wasd[4] = {0, 0, 0, 0};

void handle_keyboard(int key, int down_or_up) {

    // down
    if(down_or_up) {
      switch(key)
      {
        case SDLK_c:
          if(draw_mode) { 
              draw_mode = 0;        
          } else {
              draw_mode = 1;
          }
          break;
      }
    }

    // down and up
    switch(key)
    {
        case SDLK_w:
          wasd[0] = down_or_up;
          break;
        case SDLK_a:
          wasd[1] = down_or_up;
          break;
        case SDLK_s:
          wasd[2] = down_or_up;
          break;
        case SDLK_d:
          wasd[3] = down_or_up;
          break;
    }
    printf("Keyboard event wasd %d, %d, %d, %d\n", wasd[0], wasd[1], wasd[2], wasd[3]);
}


int
main(int argc, char *argv[])
{

  int i, j, k, done;
  SDL_Event event;
  SDL_DisplayMode mode;
  ListElement *el;

  // Initialize SDL2
  if( SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
    printf("Unable to initialize SDL: %s \n", SDL_GetError());
    quit(1);
  }

  // Sound INIT
  int count = SDL_GetNumAudioDevices(0);
  for ( i = 0; i < count; ++i ) {
      printf("Audio device %d: %s\n", i, SDL_GetAudioDeviceName(i, 0));
  }

  int audio_rate = 22050;
  Uint16 audio_format = AUDIO_S16SYS;
  int audio_channels = 2;
  int audio_buffers = 4096;

  if(Mix_OpenAudio(audio_rate, audio_format, audio_channels, audio_buffers) != 0) {
    fprintf(stderr, "Unable to initialize audio: %s\n", Mix_GetError());
    exit(1);
  }

  // Get desktop information
  if (SDL_GetDesktopDisplayMode(0, &mode) < 0) {
      printf("Could not get display mode: %s\n", SDL_GetError());
      quit(1);
  }

  // Play sound
  Mix_Music * music; 
  music = Mix_LoadMUS("heroic.ogg"); 
  if(music == NULL) { 
    printf("Unable to load sound file: %s\n", Mix_GetError()); 
    quit(1); 
  }

  /*Mix_PlayMusic(music, -1);
  if(Mix_PlayMusic(music, -1)) {
    fprintf(stderr, "Unable to play ogg file: %s\n", Mix_GetError());
    quit(1);
  }*/

  viewport.x = 0;
  viewport.y = 0;
  viewport.w = MAX(mode.w, 800) - 150;
  viewport.h = MAX(mode.h, 600) - 150;

  printf("Window width : %d\n", viewport.w);

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
      printf("Could not create window: %s\n", SDL_GetError());
      quit(1);
  }
  
  renderer = SDL_CreateRenderer(window, -1, 0);
  if (renderer < 0) {
      printf("Could not create renderer: %s\n", SDL_GetError());
      quit(1);
  }

  if (TTF_Init() == -1) {
      printf("Unable to initialize SDL_ttf: %s \n", TTF_GetError());
      quit(1);
  }

  TTF_Font * font = TTF_OpenFont("calvin.ttf", 25);

  // Grey color
  SDL_SetRenderDrawColor(renderer, 0xA0, 0xA0, 0xA0, 0xFF);
  SDL_RenderClear(renderer);

  SDL_Texture *groundTexture = getTexture(renderer, "ground.png");
  if (!groundTexture) {
      fprintf(stderr, "Couldn't load %s: %s\n", argv[i], SDL_GetError());
      quit(1);
  }

  // Table of sprites, ready to use
  SpriteTable * groundTable = splitTextureTable(groundTexture, 48, 48);
  SDL_Texture * characterTexture = getTexture(renderer, "character.png");
  SpriteTable * characterTable = splitTextureTable(characterTexture, 48, 48);

  // animations of the character
  SDL_Rect rect;
  rect.x = 0;
  rect.y = 0;
  rect.w = 48;
  rect.h = 48;
  Animation * stand = createAnimation(characterTexture, &rect, 5, 1);

  Animation * goDown = createAnimation(characterTexture, &rect, 5, 4);
  rect.y = 48;
  Animation * goRight = createAnimation(characterTexture, &rect, 5, 4);
  rect.y = 2 * 48;
  Animation * goUp = createAnimation(characterTexture, &rect, 5, 4);
  rect.y = 3 * 48;
  Animation * goLeft = createAnimation(characterTexture, &rect, 5, 4);


  // All sorts of variable for the game loop
  done = 0;

  Sprite * characterSprite = getSpriteFromAnimation(goUp, 0);

  int scroll_x = 0, scroll_y = 0;
  SDL_Color black;
  black.r = 0; black.g = 0; black.b = 0;

  next_time = SDL_GetTicks() + TICK_INTERVAL;

  int lines = viewport.h / 48;
  int columns = viewport.w / 48;
  int x_offset = (2 + columns) * 48;
  int y_offset = (2 + lines) * 48;
  int x, y;

  Uint32 startTime = SDL_GetTicks();
  SDL_Texture * text_texture = NULL;

  // number of the current frame
  int frameNum = SDL_GetTicks() / TICK_INTERVAL;
  // amount of rendered frames
  int renderedFrames = 0;

  // desired frames by second
  int framesBySecond = 1000 / TICK_INTERVAL;

  // indicate if the current frame is a "real" frame
  int physical_frame = 1;

  printf("Desired fps %d\n", framesBySecond);
  printf("Start the game loop\n");

  // The game loop
  while (!done) {

    // slow down the physical stuff by being sure it runs
    // only once every TICK_INTERVAL, or physical frame
    if(SDL_GetTicks() / TICK_INTERVAL > frameNum) {
      frameNum = SDL_GetTicks() / TICK_INTERVAL;
      physical_frame = 1;
    } else {
      // no physical simulation should happen in this frame
      physical_frame = 0;
    }

    // ---- Physic and events

    // move the map
    if(physical_frame) {

      // Check for events
      while (SDL_PollEvent(&event)) {
          // printf("Event type: %d\n", event.type);
          if (event.type == SDL_QUIT || event.type == SDL_WINDOWEVENT_CLOSE) {
              done = 1;
          }

          if (event.type == SDL_KEYDOWN) {
            handle_keyboard(event.key.keysym.sym, 1);
          }

          if (event.type == SDL_KEYUP) {
            handle_keyboard(event.key.keysym.sym, 0);
          }
      }

      // apply events to the world

      int speed = 7;
      if(wasd[0]) {
        scroll_y = scroll_y + speed;
      }
      if(wasd[1]) {
         scroll_x = scroll_x + speed;
      }
      if(wasd[2]) {
         scroll_y = scroll_y - speed;
      }
      if(wasd[3]) {
         scroll_x = scroll_x - speed;
      }

    }


    // ---- Graphic rendering

    // this is not free        
    SDL_RenderClear(renderer);

    // render the ground
    for(i = 0; i < lines + 2; i++) {
        y = mod(i * 48 + scroll_y, viewport.h + 48) - 48;
        for(j = 0;j < columns + 2; j++) {
            x = mod(j * 48 + scroll_x, viewport.w + 48) - 48;
            drawSpriteAt(renderer, 
                groundTable->table[abs(i + j) % 8], 
                x, y);
        }
    }

    // render the character
    characterSprite = getSpriteFromAnimation(stand, frameNum);

    if(wasd[0]) {
      characterSprite = getSpriteFromAnimation(goUp, frameNum);
    }
    if(wasd[1]) {
      characterSprite = getSpriteFromAnimation(goLeft, frameNum);
    }
    if(wasd[2]) {
      characterSprite = getSpriteFromAnimation(goDown, frameNum);
    }
    if(wasd[3]) {
      characterSprite = getSpriteFromAnimation(goRight, frameNum);
    }

    drawSpriteAt(renderer, characterSprite, viewport.w / 2, viewport.h / 2);


    // do this every second on a physical frame
    if(frameNum % framesBySecond == 0 && physical_frame) {

        if(text_texture) {
            SDL_DestroyTexture(text_texture);   
        }

        char buffer[50];
        sprintf(buffer, "Press c to cap to 50fps. Current fps: %d", renderedFrames);
        SDL_Surface * text = TTF_RenderText_Blended(font, buffer, black);
        text_texture = SDL_CreateTextureFromSurface(renderer, text);

        // without this the program will take all the memory very fast
        SDL_FreeSurface(text);
        renderedFrames = 0;
    }

    if(text_texture) {
        SDL_Rect text_rect;
        text_rect.x = 15;
        text_rect.y = 10;
        text_rect.w = text_texture->w;
        text_rect.h = text_texture->h;
        SDL_RenderCopy(renderer, text_texture, NULL, &text_rect);
    }

    // Update the screen
    SDL_RenderPresent(renderer);

    // Cap to ~ 50 fps
    if(draw_mode == 1) {
        cap_framerate();
    }

    renderedFrames++;

  }

  // cleanup
  // TODO: free the structure that need to be
  
  
  quit(0);

  // to prevent compiler warning
  return 0; 
}

