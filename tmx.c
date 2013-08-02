/*
  Copyright (C) 2013 batiste.bieler@gmail.com 
    
  TMX map reader in C using mxml
 */

#include "mxml.h"
#include "string.h"
#include <stdlib.h>
#ifndef WIN32
#  include <unistd.h>
#endif /* !WIN32 */
#include <fcntl.h>
#ifndef O_BINARY
#  define O_BINARY 0
#endif /* !O_BINARY */

// TMX Format documentation : https://github.com/bjorn/tiled/wiki/TMX-Map-Format

struct TmxTileset {
  int tilewidth;
  int tileheight;
  char name[50];
  int firstgid;
  // 1 ortho
  int orientation;

};
typedef struct TmxTileset TmxTileset;

struct TmxLayer {
  int width;
  int height;
  int numTiles;
  int * tiles;
  char name[50];
};
typedef struct TmxLayer TmxLayer;

struct TmxObject {
  int width;
  int height;
  int x;
  int y;
};
typedef struct TmxObject TmxObject;

struct TmxObjectGroup {
  int width;
  int height;
  int numObjects;
  TmxObject * objects;
  char name[50];
};
typedef struct TmxObjectGroup TmxObjectGroup;

struct TmxMap {
  int width;
  int height;
  int tilewidth;
  int tileheight;
  // 1 ortho
  char orientation[15];

  TmxTileset * tilesets;
  int numTilesets;

  TmxObjectGroup * objectGroups;
  int numObjectGroups;

  TmxLayer * layers;
  int numLayers;
};
typedef struct TmxMap TmxMap;

/*TmxTileset * createTileset(mxml_node_t * node) {
  
}*/


// mxmlLoadString
TmxMap * TMX_LoadFile(char * filename) {
  FILE *fp;
  mxml_node_t * tree;

  fp = fopen(filename, "r");
  tree = mxmlLoadFile(NULL, fp, MXML_TEXT_CALLBACK);
  fclose(fp);

  mxml_node_t * node;
  mxml_node_t * mapNode;

  /* Find the first "map" element */
  mapNode = mxmlFindElement(tree, tree, "map",
                         NULL, NULL,
                         MXML_DESCEND);



  // create and fillup the map
  TmxMap * map = malloc(sizeof(TmxMap));
  map->width = atoi(mxmlElementGetAttr(mapNode, "width"));
  map->height = atoi(mxmlElementGetAttr(mapNode, "height"));
  map->tilewidth = atoi(mxmlElementGetAttr(mapNode, "tilewidth"));
  map->tileheight = atoi(mxmlElementGetAttr(mapNode, "tileheight"));
  strncpy(map->orientation, mxmlElementGetAttr(mapNode, "orientation"), 12);
  map->numTilesets = 0;
  map->numObjectGroups = 0;
  map->numLayers = 0;
  map->tilesets = NULL;
  map->objectGroups = NULL;
  map->layers = NULL;

  node = mxmlFindElement(tree, tree, "tileset",
                         NULL, NULL,
                         MXML_DESCEND);

  if(node == NULL) {
    printf("Cannot go further without at least one tileset");
    mxmlDelete(tree);
    return map;
  }

  const char * name;  

  // First pass we count the tilesets, layers and object groups
  while(node != NULL) {
    name = mxmlGetElement(node);
    if(name) {
      if(strcmp(name, "tileset") == 0) {
        map->numTilesets += 1;
      }
      if(strcmp(name, "layer") == 0) {
        map->numLayers += 1;
      }
      if(strcmp(name, "objectgroup") == 0) {
        map->numObjectGroups += 1;
      }
    }
    node = mxmlWalkNext(node, mapNode,
                        MXML_NO_DESCEND);
  }

  // create the map object in memory
  map->tilesets = malloc(map->numTilesets * sizeof(TmxTileset));
  map->layers = malloc(map->numLayers * sizeof(TmxLayer));
  map->objectGroups = malloc(map->numObjectGroups * sizeof(TmxObjectGroup));
  
  node = mxmlFindElement(tree, tree, "tileset",
                         NULL, NULL,
                         MXML_DESCEND);

  int i = 0, j = 0, k = 0;
  // Second pass we fill the tilesets, layers and object groups
  while(node != NULL) {
    name = mxmlGetElement(node);
    if(name) {
      if(strcmp(name, "tileset") == 0) {
        TmxTileset * set = &map->tilesets[i];
        set->firstgid = atoi(mxmlElementGetAttr(node, "firstgid"));
        set->tilewidth = atoi(mxmlElementGetAttr(node, "tilewidth"));
        set->tileheight = atoi(mxmlElementGetAttr(node, "tileheight"));
        strncpy(set->name, mxmlElementGetAttr(node, "name"), 50);
        i++;
      }

      if(strcmp(name, "layer") == 0) {
        TmxLayer * layer = &map->layers[j];
        layer->width = atoi(mxmlElementGetAttr(node, "width"));
        layer->height = atoi(mxmlElementGetAttr(node, "height"));
        strncpy(layer->name, mxmlElementGetAttr(node, "name"), 50);
        j++;

      }
      if(strcmp(name, "objectgroup") == 0) {
        TmxObjectGroup * group = &map->objectGroups[k];
        group->width = atoi(mxmlElementGetAttr(node, "width"));
        group->height = atoi(mxmlElementGetAttr(node, "height"));
        strncpy(group->name, mxmlElementGetAttr(node, "name"), 50);
        k++;
      }
    }
    node = mxmlWalkNext(node, mapNode,
                        MXML_NO_DESCEND);
  }

  mxmlDelete(tree);
  return map;
}
