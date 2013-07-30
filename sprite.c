/*
  Copyright (C) 2013 batiste.bieler@gmail.com 
    
  Sprite functions
 */

#include "common.c"

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
    // a table of sprites used for animation
    SpriteTable * sprites;
    //Uint32 startTick;
    //Uint32 currentTick;
    int ticksByFrame;
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

Animation * createAnimation(SDL_Texture * texture, SDL_Rect * sprites_start, int ticksByFrame, int numberSprite) {

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
    anim->ticksByFrame = ticksByFrame;

    //anim->startTick = 0;
    //anim->currentTick = 0;

    return anim;
}

void destroyAnimation(Animation * anim) {
  free(anim->sprites->table);
  free(anim->sprites);
  free(anim);
}

Sprite * 
getSpriteFromAnimation(Animation * anim, int frame) {
    int spriteIndex = (frame / anim->ticksByFrame) % anim->sprites->length;
    return anim->sprites->table[spriteIndex];
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

void destroySpriteTable(SpriteTable * table) {
  free(table->table);
  free(table);
}


