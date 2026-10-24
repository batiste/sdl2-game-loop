/*
  Copyright (C) 2013 batiste.bieler@gmail.com

  TMX map reader (Tiled editor format) using mxml.
  Format documentation: https://doc.mapeditor.org/en/stable/reference/tmx-map-format/
 */

#ifndef GAME_TMX_H
#define GAME_TMX_H

#include <stdint.h>

// High bits of a gid encode flipping; mask them to get the tile id.
#define TMX_GID_MASK 0x0FFFFFFFu

typedef struct TmxProperty {
  char *name;
  char *value;
} TmxProperty;

typedef struct TmxTileset {
  char *name;
  char *image;  // path relative to the map file
  int firstgid;
  int tilewidth;
  int tileheight;
  int spacing;
  int margin;
  int columns;
  int numTiles;
} TmxTileset;

typedef struct TmxLayer {
  char *name;
  int width;
  int height;
  uint32_t *tiles;  // width * height gids, 0 means empty
} TmxLayer;

typedef struct TmxObject {
  char *name;
  float x;
  float y;
  float width;
  float height;
  int numProperties;
  TmxProperty *properties;
} TmxObject;

typedef struct TmxObjectGroup {
  char *name;
  int numObjects;
  TmxObject *objects;
} TmxObjectGroup;

typedef struct TmxMap {
  int width;
  int height;
  int tilewidth;
  int tileheight;
  char *orientation;  // "orthogonal", "isometric", "staggered" or "hexagonal"

  int numTilesets;
  TmxTileset *tilesets;

  int numLayers;
  TmxLayer *layers;

  int numObjectGroups;
  TmxObjectGroup *objectGroups;

  // highest gid any tileset can produce, for sizing lookup tables
  int maxGid;
} TmxMap;

TmxMap *tmx_load(const char *path);
void tmx_free(TmxMap *map);

const TmxObjectGroup *tmx_find_object_group(const TmxMap *map, const char *name);

#endif
