/*
  Copyright (C) 2013 batiste.bieler@gmail.com

  Engine: window, renderer, audio and asset management.
  Abstracts away what is not central to the game logic.
 */

#ifndef GAME_ENGINE_H
#define GAME_ENGINE_H

#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <SDL3_ttf/SDL_ttf.h>

typedef struct Engine Engine;

struct Engine {
  SDL_Window *window;
  SDL_Renderer *renderer;
  MIX_Mixer *mixer;

  // logical resolution, independent of window size and pixel density
  int width;
  int height;
  bool vsync;

  // every loaded asset, released by engine_shutdown()
  struct EngineResource *resources;
  int numResources;
  int capResources;
};

bool engine_init(Engine *engine, const char *title, int width, int height);
void engine_shutdown(Engine *engine);

void engine_set_vsync(Engine *engine, bool enabled);

// Resolve a path relative to the assets directory. Caller must SDL_free() it.
char *engine_asset_path(const char *relative);

// Loaders return NULL and log on failure. Engine owns the returned objects.
SDL_Texture *engine_load_texture(Engine *engine, const char *relative);
TTF_Font *engine_load_font(Engine *engine, const char *relative, float size);
MIX_Audio *engine_load_audio(Engine *engine, const char *relative);

// Render black text with a white drop shadow. Caller owns the texture.
SDL_Texture *engine_render_text(Engine *engine, TTF_Font *font, const char *text);

#endif
