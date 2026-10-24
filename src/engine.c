/*
  Copyright (C) 2013 batiste.bieler@gmail.com

  Engine: window, renderer, audio and asset management.
 */

#include "engine.h"

#include <SDL3_image/SDL_image.h>

typedef enum { RESOURCE_TEXTURE, RESOURCE_FONT, RESOURCE_AUDIO } ResourceKind;

struct EngineResource {
  ResourceKind kind;
  void *ptr;
};

static void track(Engine *engine, ResourceKind kind, void *ptr) {
  if (engine->numResources == engine->capResources) {
    int cap = engine->capResources ? engine->capResources * 2 : 16;
    struct EngineResource *grown =
        SDL_realloc(engine->resources, (size_t)cap * sizeof(*grown));
    if (!grown) {
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Out of memory");
      return;
    }
    engine->resources = grown;
    engine->capResources = cap;
  }
  engine->resources[engine->numResources++] = (struct EngineResource){kind, ptr};
}

bool engine_init(Engine *engine, const char *title, int width, int height) {
  *engine = (Engine){.width = width, .height = height, .vsync = true};

  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Unable to initialize SDL: %s", SDL_GetError());
    return false;
  }

  if (!SDL_CreateWindowAndRenderer(title, width, height,
                                   SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY,
                                   &engine->window, &engine->renderer)) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not create window: %s", SDL_GetError());
    return false;
  }
  SDL_Log("Renderer: %s", SDL_GetRendererName(engine->renderer));

  // Render at a fixed logical size, scaled to the window (and to Retina displays).
  SDL_SetRenderLogicalPresentation(engine->renderer, width, height,
                                   SDL_LOGICAL_PRESENTATION_LETTERBOX);
  SDL_SetDefaultTextureScaleMode(engine->renderer, SDL_SCALEMODE_PIXELART);
  engine_set_vsync(engine, true);

  if (!TTF_Init()) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Unable to initialize SDL_ttf: %s", SDL_GetError());
    return false;
  }

  if (!MIX_Init()) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Unable to initialize SDL_mixer: %s", SDL_GetError());
    return false;
  }
  engine->mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);
  if (!engine->mixer) {
    // Not fatal: the game is playable without sound.
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "No audio: %s", SDL_GetError());
  }

  return true;
}

void engine_shutdown(Engine *engine) {
  for (int i = engine->numResources - 1; i >= 0; i--) {
    struct EngineResource *res = &engine->resources[i];
    switch (res->kind) {
      case RESOURCE_TEXTURE: SDL_DestroyTexture(res->ptr); break;
      case RESOURCE_FONT: TTF_CloseFont(res->ptr); break;
      case RESOURCE_AUDIO: MIX_DestroyAudio(res->ptr); break;
    }
  }
  SDL_free(engine->resources);

  if (engine->mixer) {
    MIX_DestroyMixer(engine->mixer);
  }
  MIX_Quit();
  TTF_Quit();
  if (engine->renderer) {
    SDL_DestroyRenderer(engine->renderer);
  }
  if (engine->window) {
    SDL_DestroyWindow(engine->window);
  }
  *engine = (Engine){0};
}

void engine_set_vsync(Engine *engine, bool enabled) {
  if (!SDL_SetRenderVSync(engine->renderer, enabled ? 1 : SDL_RENDERER_VSYNC_DISABLED)) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Could not change vsync: %s", SDL_GetError());
    return;
  }
  engine->vsync = enabled;
}

char *engine_asset_path(const char *relative) {
  char *path = NULL;
  const char *base = SDL_GetBasePath();
  SDL_asprintf(&path, "%sassets/%s", base ? base : "", relative);
  return path;
}

SDL_Texture *engine_load_texture(Engine *engine, const char *relative) {
  char *path = engine_asset_path(relative);
  SDL_Texture *texture = IMG_LoadTexture(engine->renderer, path);
  if (texture) {
    track(engine, RESOURCE_TEXTURE, texture);
  } else {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't load %s: %s", path, SDL_GetError());
  }
  SDL_free(path);
  return texture;
}

SDL_Surface *engine_load_surface(const char *relative) {
  char *path = engine_asset_path(relative);
  SDL_Surface *loaded = IMG_Load(path);
  SDL_Surface *surface = loaded ? SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32) : NULL;
  if (!surface) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't load %s: %s", path, SDL_GetError());
  }
  SDL_DestroySurface(loaded);
  SDL_free(path);
  return surface;
}

SDL_Texture *engine_texture_from_surface(Engine *engine, SDL_Surface *surface) {
  SDL_Texture *texture = SDL_CreateTextureFromSurface(engine->renderer, surface);
  if (texture) {
    track(engine, RESOURCE_TEXTURE, texture);
  } else {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't create texture: %s", SDL_GetError());
  }
  return texture;
}

TTF_Font *engine_load_font(Engine *engine, const char *relative, float size) {
  char *path = engine_asset_path(relative);
  TTF_Font *font = TTF_OpenFont(path, size);
  if (font) {
    track(engine, RESOURCE_FONT, font);
  } else {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't load %s: %s", path, SDL_GetError());
  }
  SDL_free(path);
  return font;
}

MIX_Audio *engine_load_audio(Engine *engine, const char *relative, bool predecode) {
  if (!engine->mixer) {
    return NULL;
  }
  char *path = engine_asset_path(relative);
  MIX_Audio *audio = MIX_LoadAudio(engine->mixer, path, predecode);
  if (audio) {
    track(engine, RESOURCE_AUDIO, audio);
  } else {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't load %s: %s", path, SDL_GetError());
  }
  SDL_free(path);
  return audio;
}

SDL_Texture *engine_render_text(Engine *engine, TTF_Font *font, const char *text) {
  const SDL_Color black = {0, 0, 0, 255};
  const SDL_Color white = {255, 255, 255, 255};

  SDL_Surface *shadow = TTF_RenderText_Solid(font, text, 0, white);
  SDL_Surface *front = TTF_RenderText_Solid(font, text, 0, black);
  SDL_Surface *surface = NULL;
  SDL_Texture *texture = NULL;

  if (shadow && front) {
    surface = SDL_CreateSurface(front->w + 2, front->h + 1, SDL_PIXELFORMAT_RGBA32);
  }
  if (surface) {
    SDL_BlitSurface(shadow, NULL, surface, &(SDL_Rect){2, 1, shadow->w, shadow->h});
    SDL_BlitSurface(front, NULL, surface, &(SDL_Rect){0, 0, front->w, front->h});
    texture = SDL_CreateTextureFromSurface(engine->renderer, surface);
  }
  if (!texture) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't render text: %s", SDL_GetError());
  }

  SDL_DestroySurface(shadow);
  SDL_DestroySurface(front);
  SDL_DestroySurface(surface);
  return texture;
}
