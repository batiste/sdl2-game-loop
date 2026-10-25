/*
  Copyright (C) 2013 batiste.bieler@gmail.com

  Small pixel art icons drawn from text: one character per art pixel, '.'
  is transparent, the others are colours from palette().
 */

#include "icons.h"

#define MAX_ROWS 12

typedef struct Icon {
  const char *rows[MAX_ROWS];
} Icon;

static const Icon ICONS[ICON_COUNT] = {
    [ICON_POTION] = {{
        "...kkkk...",
        "...kttk...",
        "...kkkk...",
        "...kggk...",
        "..kggggk..",
        ".kggggggk.",
        ".kwrrrrrk.",
        ".kwrrrrrk.",
        ".krrrrrrk.",
        "..krrrrk..",
        "...kkkk...",
    }},
    [ICON_BOMB] = {{
        ".......y..",
        "......yoy.",
        ".....k.y..",
        "....k.....",
        "..kkkkkk..",
        ".kbbbbbbk.",
        "kbwwbbbbbk",
        "kbwbbbbbbk",
        "kbbbbbbbbk",
        ".kbbbbbbk.",
        "..kkkkkk..",
    }},
    [ICON_AMULET] = {{
        "..k...k..",
        "...k.k...",
        "....y....",
        ".kk...kk.",
        "krrk.krrk",
        "krwrkrrrk",
        "krrrrrrrk",
        ".krrrrrk.",
        "..krrrk..",
        "...krk...",
        "....k....",
    }},
    [ICON_JAR] = {{
        "..kkkk..",
        ".kttttk.",
        "..kkkk..",
        ".kooook.",
        "kowyoook",
        "kowooook",
        "kooooook",
        ".kooook.",
        "..kkkk..",
    }},
    [ICON_WISDOM] = {{
        "k.......k",
        ".k.....k.",
        "..k...k..",
        "...kyk...",
        "..kyyyk..",
        ".kyuuuyk.",
        "kyuwluuyk",
        "kyulluuyk",
        "kyuuuuuyk",
        ".kyuuuyk.",
        "..kyyyk..",
        "...kkk...",
    }},
    [ICON_SHIELD] = {{
        "...kkkk...",
        ".kkddddkk.",
        ".kdmddmdk.",
        "kddmddmddk",
        "kddmbbmddk",
        "kddmbbmddk",
        "kddmddmddk",
        ".kdmddmdk.",
        ".kkddddkk.",
        "...kkkk...",
    }},
    [ICON_BAT_UP] = {{
        "k...........k",
        "kk.........kk",
        "kbk..k.k..kbk",
        ".kbk.kbk.kbk.",
        "..kbkrbrkbk..",
        "...kkbbbkk...",
        "....kkkkk....",
    }},
    [ICON_BAT_DOWN] = {{
        ".....k.k.....",
        "....kbkbk....",
        "..kkbrbrbkk..",
        ".kbbbbbbbbbk.",
        "kbbk.kbk.kbbk",
        "kbk..kkk..kbk",
        "kk.........kk",
    }},
    [ICON_SHIELD_SMALL] = {{
        ".kkkk.",
        "kdmmdk",
        "kdbbdk",
        "kdbbdk",
        "kdmmdk",
        ".kddk.",
        "..kk..",
    }},
    [ICON_SWORD] = {{
        ".......kk",
        "......ksk",
        ".....ksk.",
        "....ksk..",
        "k..ksk...",
        ".kysk....",
        "..yk.....",
        ".kdy.....",
        "kdk.k....",
    }},
    [ICON_STONES] = {{
        ".........",
        "..kkkk...",
        ".ksseek..",
        ".keeeek..",
        "..kkkkkk.",
        "....ksek.",
        "....keeek",
        "....kkkk.",
        ".........",
    }},
    [ICON_FLAMES] = {{
        "....k....",
        "...kok...",
        "...koyk..",
        "..koyyk..",
        ".koyyyok.",
        ".koyhyok.",
        ".koyyyok.",
        "..koook..",
        "...kkk...",
    }},
    [ICON_AURA] = {{
        "....y....",
        ".y.....y.",
        "...kkk...",
        "..kyhyk..",
        "y.khhhk.y",
        "..kyhyk..",
        "...kkk...",
        ".y.....y.",
        "....y....",
    }},
    [ICON_REPULSION] = {{
        "..qqqqq..",
        ".q.....q.",
        "q..ppp..q",
        "q.p...p.q",
        "q.p.k.p.q",
        "q.p...p.q",
        "q..ppp..q",
        ".q.....q.",
        "..qqqqq..",
    }},
    [ICON_BOOTS] = {{
        "..kkkk...",
        "..kddk...",
        "..kddk...",
        "..kddk...",
        "..kdddkk.",
        ".kddddddk",
        ".kmmmmmmk",
        "..kkkkkk.",
        ".........",
    }},
    [ICON_HEART] = {{
        ".kk...kk.",
        "krrk.krrk",
        "krhrkrrrk",
        "krrrrrrrk",
        ".krrrrrk.",
        "..krrrk..",
        "...krk...",
        "....k....",
    }},
    [ICON_HAMMER] = {{
        "kkkkkkk..",
        "keeeeek..",
        "keseeek..",
        "kkkkkkk..",
        "...kdk...",
        "...kdk...",
        "...kdk...",
        "...kdk...",
        "...kkk...",
    }},
    [ICON_HASTE] = {{
        ".....kkk.",
        "....kyyk.",
        "...kyyk..",
        "..kyyykkk",
        ".kkkyyyk.",
        "...kyyk..",
        "..kyyk...",
        "..kyk....",
        "..kk.....",
    }},
    [ICON_COIN] = {{
        ".kkkk.",
        "kywyyk",
        "kwyyyk",
        "kyyyok",
        "kyyook",
        ".kkkk.",
    }},
};

static SDL_Color palette(char c, IconKind icon) {
  switch (c) {
    case 'k': return (SDL_Color){58, 41, 41, 255};     // outline
    case 'r': return (SDL_Color){222, 44, 64, 255};    // red
    case 'g': return (SDL_Color){206, 232, 240, 255};  // glass
    case 't': return (SDL_Color){176, 122, 70, 255};   // cork
    case 'b': return (SDL_Color){62, 62, 76, 255};     // iron
    case 'y': return (SDL_Color){250, 212, 64, 255};   // gold
    case 'o': return (SDL_Color){214, 140, 30, 255};   // dark gold, flame
    case 'u': return (SDL_Color){64, 132, 232, 255};   // blue
    case 'l': return (SDL_Color){168, 210, 255, 255};  // light blue
    case 'd': return (SDL_Color){178, 116, 60, 255};   // wood
    case 'p': return (SDL_Color){156, 84, 214, 255};   // purple
    case 'q': return (SDL_Color){214, 168, 255, 255};  // light purple
    case 's': return (SDL_Color){214, 218, 230, 255};  // steel
    case 'e': return (SDL_Color){150, 146, 138, 255};  // stone
    case 'h': return (SDL_Color){255, 250, 220, 255};  // white hot
    case 'm': return (SDL_Color){118, 72, 36, 255};    // dark wood
    case 'w':
      return icon == ICON_BOMB ? (SDL_Color){150, 150, 172, 255}
             : icon == ICON_COIN || icon == ICON_JAR ? (SDL_Color){255, 248, 200, 255}
             : icon == ICON_WISDOM ? (SDL_Color){255, 255, 255, 255}
                                 : (SDL_Color){255, 196, 204, 255};  // shine
    default: return (SDL_Color){255, 0, 255, 255};
  }
}

SDL_FPoint icon_size(IconKind icon, float unit) {
  const Icon *ic = &ICONS[icon];
  int rows = 0;
  while (rows < MAX_ROWS && ic->rows[rows]) {
    rows++;
  }
  return (SDL_FPoint){(float)SDL_strlen(ic->rows[0]) * unit, (float)rows * unit};
}

void icon_draw(SDL_Renderer *renderer, IconKind icon, float x, float y, float unit) {
  const Icon *ic = &ICONS[icon];
  x = SDL_floorf(x / unit) * unit;
  y = SDL_floorf(y / unit) * unit;
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  for (int row = 0; row < MAX_ROWS && ic->rows[row]; row++) {
    for (int col = 0; ic->rows[row][col]; col++) {
      char c = ic->rows[row][col];
      if (c != '.') {
        SDL_Color color = palette(c, icon);
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
        SDL_RenderFillRect(renderer, &(SDL_FRect){x + (float)col * unit, y + (float)row * unit, unit, unit});
      }
    }
  }
}
