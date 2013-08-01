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
        if(draw_mode == 1) { 
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

void showSplashScreen(void) {

  SDL_Rect rect;
  SDL_Texture * splashTexture = getTexture("assets/splash.png");
  int w, h;
  SDL_QueryTexture(splashTexture, NULL, NULL, &w, &h);
  rect.w = w;
  rect.h = h;
  rect.x = viewport.w / 2 - w / 2;
  rect.y = viewport.h / 2 - h / 2;

  SDL_RendererInfo info;
  SDL_GetRendererInfo(renderer, &info);

  SDL_Delay(100);
  int alpha = 1;
  while(alpha < 255) {
    if(SDL_SetTextureAlphaMod(splashTexture, alpha) != 0) {
      quit(1);
    }

    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, splashTexture, NULL, &rect);
    SDL_RenderPresent(renderer);

    SDL_Delay(TICK_INTERVAL);
    alpha = alpha + 6;
  }
}

int
main(int argc, char *argv[])
{

  int i, j, done;
  SDL_Event event;

  init();

  SDL_SetRenderDrawColor(renderer, 0xff, 0xff, 0xff, 0xff);
  SDL_RenderClear(renderer);
  SDL_RenderPresent(renderer);

  // Load assets
  // Mix_Music * music = getMusic("assets/heroic.ogg"); 
  Mix_Music * swish = getMusic("assets/swish.ogg");
  TTF_Font * font = getFont("assets/yoster.ttf", 26);
  SDL_Texture * groundTexture = getTexture("assets/ground.png");
  SDL_Texture * characterTexture = getTexture("assets/character.png");

  /*Mix_PlayMusic(music, -1);
  if(Mix_PlayMusic(music, -1)) {
    fprintf(stderr, "Unable to play ogg file: %s\n", Mix_GetError());
    quit(1);
  }*/

  // Table of sprites, ready to use
  SpriteTable * groundTable = splitTextureTable(groundTexture, 48, 48);
  // SpriteTable * characterTable = splitTextureTable(characterTexture, 48, 48);

  // Animations of the character
  SDL_Rect rect;
  rect.x = 0;
  rect.y = 0;
  rect.w = 48;
  rect.h = 48;

  // Stand up
  Animation * stand = createAnimation(characterTexture, &rect, 4, 1);

  // Move
  Animation * goDown = createAnimation(characterTexture, &rect, 4, 4);
  rect.y = 48;
  Animation * goRight = createAnimation(characterTexture, &rect, 4, 4);
  rect.y = 2 * 48;
  Animation * goUp = createAnimation(characterTexture, &rect, 4, 4);
  rect.y = 3 * 48;
  Animation * goLeft = createAnimation(characterTexture, &rect, 4, 4);

  // Sword
  rect.y = 4 * 48;
  Animation * swordRight = createAnimation(characterTexture, &rect, 2, 6);
  rect.y = 5 * 48;
  Animation * swordLeft = createAnimation(characterTexture, &rect, 2, 6);

  // All sorts of variable for the game loop
  done = 0;

  // the current sprite respresenting the character
  Sprite * characterSprite = getSpriteFromAnimation(goUp, 0);

  int scroll_x = 0, scroll_y = 0;


  int lines = viewport.h / 48;
  int columns = viewport.w / 48;
  // int x_offset = (2 + columns) * 48;
  // int y_offset = (2 + lines) * 48;
  int x, y;

  SDL_Texture * text_texture1 = NULL;

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

  SDL_Delay(100);
  showSplashScreen();
  SDL_Delay(1500);
  SDL_SetRenderDrawColor(renderer, 0x70, 0xc8, 0x40, 0xff);

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
      if(controls[0]) {
        Mix_PlayMusic(swish, 0);
      }

      characterSprite = getSpriteFromAnimation(swordLeft, frameNum);
    }

    drawSpriteAt(renderer, characterSprite, viewport.w / 2, viewport.h / 2);


    // do this every second on a physical frame
    if(frameNum % framesBySecond == 0 && physical_frame) {

        if(text_texture1) {
            SDL_DestroyTexture(text_texture1);
        }

        char buffer[50];
        if(draw_mode == 1) {
          sprintf(buffer, "Press c to maximize FPS. Current FPS: %d", renderedFrames);
        } else {
          sprintf(buffer, "Press c to cap to 50FPS. Current FPS: %d", renderedFrames);
        }

        text_texture1 = renderFontToTexture(font, buffer);

        renderedFrames = 0;
    }

    if(text_texture1) {
        SDL_Rect text_rect;
        text_rect.x = 15;
        text_rect.y = 10;
        SDL_QueryTexture(text_texture1, NULL, NULL, &text_rect.w, &text_rect.h);
        SDL_RenderCopy(renderer, text_texture1, NULL, &text_rect);
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

  // seems to create more problems with valgrind
  destroyAnimation(stand);
  destroyAnimation(goUp);
  destroyAnimation(goDown);
  destroyAnimation(goLeft);
  destroyAnimation(goRight);  
  destroyAnimation(swordRight);
  destroyAnimation(swordLeft);
  destroySpriteTable(groundTable);
  SDL_DestroyTexture(text_texture1);

  quit(0);

  // to prevent compiler warning
  return 0; 
}

