/*
  Copyright (C) 2013 batiste.bieler@gmail.com 
    
  SDL game loop
 */



#include "common.c"
#include "sprite.c"



// Keyboard variables and functions

int wasd[4] = {0, 0, 0, 0};
int controls[1] = {0};

void handleKeyboard(int key, int down_or_up) {

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
    case SDLK_SPACE:
      controls[0] = down_or_up;
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
  Mix_Music * music = getMusic("assets/heroic.ogg"); 
  Mix_Music * swish = getMusic("assets/swish.ogg");

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

  TTF_Font * font = TTF_OpenFont("assets/calvin.ttf", 25);

  // Grey color
  SDL_SetRenderDrawColor(renderer, 0xA0, 0xA0, 0xA0, 0xFF);
  SDL_RenderClear(renderer);

  SDL_Texture *groundTexture = getTexture("assets/ground.png");
  if (!groundTexture) {
      fprintf(stderr, "Couldn't load %s: %s\n", argv[i], SDL_GetError());
      quit(1);
  }

  // Table of sprites, ready to use
  SpriteTable * groundTable = splitTextureTable(groundTexture, 48, 48);
  SDL_Texture * characterTexture = getTexture("assets/character.png");
  SpriteTable * characterTable = splitTextureTable(characterTexture, 48, 48);

  // animations of the character
  SDL_Rect rect;
  rect.x = 0;
  rect.y = 0;
  rect.w = 48;
  rect.h = 48;
  Animation * stand = createAnimation(characterTexture, &rect, 4, 1);

  Animation * goDown = createAnimation(characterTexture, &rect, 4, 4);
  rect.y = 48;
  Animation * goRight = createAnimation(characterTexture, &rect, 4, 4);
  rect.y = 2 * 48;
  Animation * goUp = createAnimation(characterTexture, &rect, 4, 4);
  rect.y = 3 * 48;
  Animation * goLeft = createAnimation(characterTexture, &rect, 4, 4);

  rect.y = 4 * 48;
  Animation * swordRight = createAnimation(characterTexture, &rect, 2, 6);
  rect.y = 5 * 48;
  Animation * swordLeft = createAnimation(characterTexture, &rect, 2, 6);

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

    // this is a "real" frame where we responde to events and apply physic
    if(physical_frame) {

      // Check for events
      while (SDL_PollEvent(&event)) {
          // printf("Event type: %d\n", event.type);
          if (event.type == SDL_QUIT || event.type == SDL_WINDOWEVENT_CLOSE) {
              done = 1;
          }

          if (event.type == SDL_KEYDOWN) {
            handleKeyboard(event.key.keysym.sym, 1);
          }

          if (event.type == SDL_KEYUP) {
            handleKeyboard(event.key.keysym.sym, 0);
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

      if(controls[0]) {
        Mix_PlayMusic(swish, 0);
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

    if(controls[0]) {
      characterSprite = getSpriteFromAnimation(swordRight, frameNum);
    }

    if(controls[0] && wasd[1]) {
      characterSprite = getSpriteFromAnimation(swordLeft, frameNum);
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
        capFramerate();
    }

    renderedFrames++;

  }

  // cleanup
  // TODO: free the structure that need to be
  
  
  quit(0);

  // to prevent compiler warning
  return 0; 
}

