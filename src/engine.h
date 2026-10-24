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

  // logical resolution: the window in whole multiples of art pixels, about
  // viewHeight tall, with the window's aspect ratio
  int width;
  int height;
  int scale;  // screen pixels per logical pixel
  int viewHeight;
  bool vsync;

  // every loaded asset, released by engine_shutdown()
  struct EngineResource *resources;
  int numResources;
  int capResources;
};

// width and height: initial window size; viewHeight: about how tall the view
// should be, in logical pixels, whatever the window.
bool engine_init(Engine *engine, const char *title, int width, int height, int viewHeight);
void engine_shutdown(Engine *engine);

void engine_set_vsync(Engine *engine, bool enabled);
void engine_toggle_fullscreen(Engine *engine);

// Adapt the logical resolution to the window; call when its size changes.
void engine_fit_view(Engine *engine);

// Resolve a path relative to the assets directory. Caller must SDL_free() it.
char *engine_asset_path(const char *relative);

// Loaders return NULL and log on failure. Engine owns the returned objects.
SDL_Texture *engine_load_texture(Engine *engine, const char *relative);
TTF_Font *engine_load_font(Engine *engine, const char *relative, float size);
// Short sounds are decoded up front; music is streamed from the file.
MIX_Audio *engine_load_audio(Engine *engine, const char *relative, bool predecode);

// Load an image as an RGBA32 surface for pixel edits. Caller owns it.
SDL_Surface *engine_load_surface(const char *relative);
// Upload a surface; the engine owns the texture, the caller keeps the surface.
SDL_Texture *engine_texture_from_surface(Engine *engine, SDL_Surface *surface);

// Render black text with a white drop shadow. Caller owns the texture.
SDL_Texture *engine_render_text(Engine *engine, TTF_Font *font, const char *text);
// Text in `color` with a one pixel `outline` all around. Caller owns the texture.
SDL_Texture *engine_render_outlined_text(Engine *engine, TTF_Font *font, const char *text, SDL_Color color,
                                         SDL_Color outline);
// Same, breaking lines at newlines and to fit wrapWidth pixels (0: newlines only).
SDL_Texture *engine_render_text_wrapped(Engine *engine, TTF_Font *font, const char *text, int wrapWidth);

#endif
