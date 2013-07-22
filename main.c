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

#define MAX_SPEED       1
#define TICK_INTERVAL   20

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
    Sprite ** table;
    int length;
};
typedef struct SpriteTable SpriteTable;


Uint32 
time_left(void) {
    Uint32 now;
    now = SDL_GetTicks();
    if(next_time <= now)
        return 0;
    else
        return next_time - now;
}


/* Call this instead of exit(), so we can clean up SDL: atexit() is evil. */
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


SDL_Rect _rect;
void
drawSpriteAt(SDL_Renderer * renderer, Sprite * sp, int x, int y) {
    _rect.x = x;
    _rect.y = y;
    _rect.w = sp->destination.w;
    _rect.h = sp->destination.h;
    SDL_RenderCopy(renderer, sp->texture, &sp->source, &_rect);
}

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


GenericList *
splitTexture(SDL_Texture * texture, int w, int h) {
    Sprite * sprite;
    int i, j;
    GenericList * spriteList = createList();
    int columns = texture->w / w;
    int lines = texture->h / h;
   
    printf("Texture has lines %d and columns %d\n", lines, columns);

    for(i=0; i<lines; i++) {
        for(j=0; j<columns; j++) {
            sprite = createSprite(texture, w, h);
            sprite->source.x = j * w;
            sprite->source.y = i * h;
            addToList(spriteList, sprite);
        }
    }
    return spriteList;
}

SpriteTable *
splitTextureTable(SDL_Texture * texture, int w, int h) {
    Sprite * sprite;
    int i, j;
    GenericList * spriteList = createList();
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

void mixaudio(void *unused, Uint8 *stream, int len) { } 

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

    // get desktop information
    if (SDL_GetDesktopDisplayMode(0, &mode) < 0) {
        printf("Could not get display mode: %s\n", SDL_GetError());
        quit(1);
    }

    // play sound
    Mix_Music *music; 
    music = Mix_LoadMUS("heroic.ogg"); 
    if(music == NULL) { 
      printf("Unable to load sound file: %s\n", Mix_GetError()); 
      quit(1); 
    }

    Mix_PlayMusic(music, -1);
    if(Mix_PlayMusic(music, -1)) {
      fprintf(stderr, "Unable to play ogg file: %s\n", Mix_GetError());
      quit(1);
    }

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
        quit(2);
    }

    if (TTF_Init() == -1) {
        printf("Unable to initialize SDL_ttf: %s \n", TTF_GetError());
        quit(1);
    }

    TTF_Font * font = TTF_OpenFont("calvin.ttf", 25);

    // grey color
    SDL_SetRenderDrawColor(renderer, 0xA0, 0xA0, 0xA0, 0xFF);
    SDL_RenderClear(renderer);

    SDL_Texture *texture = IMG_LoadTexture(renderer, "ground.png");
    if (!texture) {
        fprintf(stderr, "Couldn't load %s: %s\n", argv[i], SDL_GetError());
        quit(2);
    }

    // a table of sprites, ready to use
    SpriteTable * spriteTable = splitTextureTable(texture, 48, 48);


    SDL_Texture *texture2 = IMG_LoadTexture(renderer, "character.png");
    SpriteTable * characterTable = splitTextureTable(texture2, 48, 48);

    next_time = SDL_GetTicks() + TICK_INTERVAL;
    
    // all sorts of variable for the game loop
    done = 0;

    int scroll_x = 0, scroll_y = 0;
    SDL_Color black;
    black.r = 0; black.g = 0; black.b = 0;

    int lines = viewport.h / 48;
    int columns = viewport.w / 48;
    int x_offset = (2 + columns) * 48;
    int y_offset = (2 + lines) * 48;
    int x, y;

    Uint32 startTime = SDL_GetTicks();
    int numFrames = 0;
    int draw_mode = 0;
    SDL_Texture * text_texture = NULL;

    printf("Start the game loop\n");
    // The game loop
    while (!done) {

        // Check for events
        while (SDL_PollEvent(&event)) {
            // printf("Event type: %d\n", event.type);
            if (event.type == SDL_QUIT || event.type == SDL_WINDOWEVENT_CLOSE) {
                done = 1;
            }

            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_c) {
                if(draw_mode) { 
                    draw_mode = 0;        
                } else {
                    draw_mode = 1;
                }
            }
        }

        // this is not free        
        SDL_RenderClear(renderer);

        for(i = 0; i < lines + 2; i++) {
            y = (i * 48 + scroll_y) % y_offset - 48;
            for(j = 0;j < columns + 2; j++) {
                x = (j * 48 + scroll_x) % x_offset - 48;
                drawSpriteAt(renderer, 
                    spriteTable->table[(i + j) % 8], 
                    x, y);
            }
        }

        drawSpriteAt(renderer, 
            characterTable->table[(scroll_x / 10) % characterTable->length], 
            500, 500);

      
        drawSpriteAt(renderer, 
            characterTable->table[(scroll_x / 10) % characterTable->length], 
            600, 500);

        drawSpriteAt(renderer, 
            characterTable->table[(scroll_x / 10) % characterTable->length], 
            700, 500);


        if(numFrames > 60) {
            if(text_texture) {
                SDL_DestroyTexture(text_texture);   
            }

            float fps = (numFrames / (float)(SDL_GetTicks() - startTime)) * 1000;
            startTime = SDL_GetTicks();
            char buffer[50];
            sprintf(buffer, "Press c to cap to 50fps. Current fps: %f", ceil(fps));
            SDL_Surface * text = TTF_RenderText_Blended(font, buffer, black);
            text_texture = SDL_CreateTextureFromSurface(renderer, text);

            // without this the program will take all the memory very fast
            SDL_FreeSurface(text);
            numFrames = 0;
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

        // Cap to ~ 60 fps
        if(draw_mode == 1) {
            SDL_Delay(time_left());
            next_time += TICK_INTERVAL;
        }

        scroll_x = scroll_x + 1;
        scroll_y = scroll_y + 1;
        ++numFrames;

    }

    // cleanup
    // TODO: free the structure that need to be
    
    
    quit(0);

    // to prevent compiler warning
    return 0; 
}

