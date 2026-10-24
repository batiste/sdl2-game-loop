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

bool engine_init(Engine *engine, const char *title, int width, int height, int viewHeight) {
  *engine = (Engine){.width = width, .height = height, .viewHeight = viewHeight, .scale = 1, .vsync = true};

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

  SDL_SetDefaultTextureScaleMode(engine->renderer, SDL_SCALEMODE_PIXELART);
  engine_fit_view(engine);
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

void engine_fit_view(Engine *engine) {
  int pw = 0, ph = 0;
  if (!SDL_GetWindowSizeInPixels(engine->window, &pw, &ph) || pw <= 0 || ph <= 0) {
    return;
  }
  // a whole scale keeps the pixel art crisp; pick the one that gets the view
  // closest to the wanted height, and use the whole width of the window
  engine->scale = SDL_max(1, (int)SDL_roundf((float)ph / (float)engine->viewHeight));
  engine->width = pw / engine->scale;
  engine->height = ph / engine->scale;
  SDL_SetRenderLogicalPresentation(engine->renderer, engine->width, engine->height,
                                   SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
}

void engine_toggle_fullscreen(Engine *engine) {
  bool fullscreen = (SDL_GetWindowFlags(engine->window) & SDL_WINDOW_FULLSCREEN) != 0;
  // desktop fullscreen: the display's own resolution, no mode switch
  SDL_SetWindowFullscreenMode(engine->window, NULL);
  SDL_SetWindowFullscreen(engine->window, !fullscreen);
}

void engine_set_vsync(Engine *engine, bool enabled) {
  if (!SDL_SetRenderVSync(engine->renderer, enabled ? 1 : SDL_RENDERER_VSYNC_DISABLED)) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Could not change vsync: %s", SDL_GetError());
    return;
  }
  engine->vsync = enabled;
  // On high refresh displays (120 Hz ProMotion...) about 60 fps is plenty for
  // pixel art and halves the drawing work. The renderers can't present every
  // other refresh, so engine_present() waits instead.
  engine->framePeriod = 0;
  const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(engine->window));
  if (enabled && mode && mode->refresh_rate > 90) {
    float refreshes = SDL_roundf(mode->refresh_rate / 60.0f);
    engine->framePeriod = (Uint64)((double)refreshes * 1e9 / (double)mode->refresh_rate);
  }
}

void engine_present(Engine *engine) {
  if (engine->framePeriod > 0) {
    // frames on a fixed schedule: the present itself may return early when
    // the display has buffers to spare
    Uint64 now = SDL_GetTicksNS();
    if (engine->nextFrame < now || engine->nextFrame > now + engine->framePeriod) {
      engine->nextFrame = now;  // first frame, or fell behind
    } else {
      SDL_DelayPrecise(engine->nextFrame - now);
    }
    engine->nextFrame += engine->framePeriod;
  }
  SDL_RenderPresent(engine->renderer);
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
  return engine_render_text_wrapped(engine, font, text, 0);
}

SDL_Texture *engine_render_outlined_text(Engine *engine, TTF_Font *font, const char *text, SDL_Color color,
                                         SDL_Color outline) {
  SDL_Surface *back = TTF_RenderText_Solid(font, text, 0, outline);
  SDL_Surface *front = TTF_RenderText_Solid(font, text, 0, color);
  SDL_Surface *surface = back && front ? SDL_CreateSurface(front->w + 4, front->h + 4, SDL_PIXELFORMAT_RGBA32) : NULL;
  SDL_Texture *texture = NULL;
  if (surface) {
    for (int dy = -2; dy <= 2; dy += 2) {
      for (int dx = -2; dx <= 2; dx += 2) {
        SDL_BlitSurface(back, NULL, surface, &(SDL_Rect){2 + dx, 2 + dy, back->w, back->h});
      }
    }
    SDL_BlitSurface(front, NULL, surface, &(SDL_Rect){2, 2, front->w, front->h});
    texture = SDL_CreateTextureFromSurface(engine->renderer, surface);
  }
  SDL_DestroySurface(back);
  SDL_DestroySurface(front);
  SDL_DestroySurface(surface);
  return texture;
}

SDL_Texture *engine_render_text_wrapped(Engine *engine, TTF_Font *font, const char *text, int wrapWidth) {
  const SDL_Color black = {0, 0, 0, 255};
  const SDL_Color white = {255, 255, 255, 255};

  SDL_Surface *shadow = TTF_RenderText_Solid_Wrapped(font, text, 0, white, wrapWidth);
  SDL_Surface *front = TTF_RenderText_Solid_Wrapped(font, text, 0, black, wrapWidth);
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
