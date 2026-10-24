/*
  Copyright (C) 2013 batiste.bieler@gmail.com

  TMX map reader (Tiled editor format) using mxml.
  Supports embedded tilesets and layer data encoded as XML or CSV.
 */

#include "tmx.h"

#include <mxml.h>
#include <SDL3/SDL.h>

static int attr_int(mxml_node_t *node, const char *name, int fallback) {
  const char *value = mxmlElementGetAttr(node, name);
  return value ? SDL_atoi(value) : fallback;
}

static float attr_float(mxml_node_t *node, const char *name, float fallback) {
  const char *value = mxmlElementGetAttr(node, name);
  return value ? (float)SDL_atof(value) : fallback;
}

static char *attr_dup(mxml_node_t *node, const char *name) {
  const char *value = mxmlElementGetAttr(node, name);
  return SDL_strdup(value ? value : "");
}

static bool is_element(mxml_node_t *node, const char *name) {
  const char *element = mxmlGetType(node) == MXML_TYPE_ELEMENT ? mxmlGetElement(node) : NULL;
  return element && SDL_strcmp(element, name) == 0;
}

static mxml_node_t *first_child(mxml_node_t *parent, const char *name) {
  for (mxml_node_t *n = mxmlGetFirstChild(parent); n; n = mxmlGetNextSibling(n)) {
    if (is_element(n, name)) {
      return n;
    }
  }
  return NULL;
}

static int count_children(mxml_node_t *parent, const char *name) {
  int count = 0;
  for (mxml_node_t *n = mxmlGetFirstChild(parent); n; n = mxmlGetNextSibling(n)) {
    count += is_element(n, name);
  }
  return count;
}

#define FOR_EACH_CHILD(var, parent, name)                                \
  for (mxml_node_t *var = mxmlGetFirstChild(parent); var;                \
       var = mxmlGetNextSibling(var))                                    \
    if (is_element(var, name))

static void parse_properties(mxml_node_t *node, TmxProperty **out, int *count) {
  mxml_node_t *props = first_child(node, "properties");
  *out = NULL;
  *count = props ? count_children(props, "property") : 0;
  if (*count == 0) {
    return;
  }
  *out = SDL_calloc((size_t)*count, sizeof(**out));
  int i = 0;
  FOR_EACH_CHILD(prop, props, "property") {
    (*out)[i].name = attr_dup(prop, "name");
    (*out)[i].value = attr_dup(prop, "value");
    i++;
  }
}

static bool parse_tileset(mxml_node_t *node, TmxTileset *set) {
  if (mxmlElementGetAttr(node, "source")) {
    SDL_SetError("External tilesets (.tsx) are not supported, embed them in the map");
    return false;
  }
  mxml_node_t *image = first_child(node, "image");
  if (!image) {
    SDL_SetError("Tileset without an image");
    return false;
  }

  set->name = attr_dup(node, "name");
  set->image = attr_dup(image, "source");
  set->firstgid = attr_int(node, "firstgid", 1);
  set->tilewidth = attr_int(node, "tilewidth", 0);
  set->tileheight = attr_int(node, "tileheight", 0);
  set->spacing = attr_int(node, "spacing", 0);
  set->margin = attr_int(node, "margin", 0);
  if (set->tilewidth <= 0 || set->tileheight <= 0) {
    SDL_SetError("Tileset '%s' has an invalid tile size", set->name);
    return false;
  }

  // Older maps lack columns/tilecount, derive them from the image size.
  int imageW = attr_int(image, "width", 0);
  int imageH = attr_int(image, "height", 0);
  int stepX = set->tilewidth + set->spacing;
  int stepY = set->tileheight + set->spacing;
  int rows = (imageH - 2 * set->margin + set->spacing) / stepY;
  set->columns = attr_int(node, "columns", (imageW - 2 * set->margin + set->spacing) / stepX);
  set->numTiles = attr_int(node, "tilecount", set->columns * rows);
  return true;
}

static bool parse_layer(mxml_node_t *node, TmxLayer *layer) {
  layer->name = attr_dup(node, "name");
  layer->width = attr_int(node, "width", 0);
  layer->height = attr_int(node, "height", 0);
  int numTiles = layer->width * layer->height;
  layer->tiles = SDL_calloc((size_t)SDL_max(numTiles, 1), sizeof(*layer->tiles));
  if (!layer->tiles) {
    return false;
  }

  mxml_node_t *data = first_child(node, "data");
  if (!data) {
    return true;
  }

  const char *encoding = mxmlElementGetAttr(data, "encoding");
  if (!encoding) {
    int i = 0;
    FOR_EACH_CHILD(tile, data, "tile") {
      if (i < numTiles) {
        const char *gid = mxmlElementGetAttr(tile, "gid");
        layer->tiles[i++] = gid ? (uint32_t)SDL_strtoul(gid, NULL, 10) : 0;
      }
    }
  } else if (SDL_strcmp(encoding, "csv") == 0) {
    int i = 0;
    for (mxml_node_t *n = mxmlGetFirstChild(data); n && i < numTiles; n = mxmlGetNextSibling(n)) {
      const char *text = mxmlGetOpaque(n);
      while (text && *text && i < numTiles) {
        char *end;
        unsigned long gid = SDL_strtoul(text, &end, 10);
        if (end == text) {
          text++;  // skip separators and whitespace
        } else {
          layer->tiles[i++] = (uint32_t)gid;
          text = end;
        }
      }
    }
  } else {
    SDL_SetError("Layer '%s': encoding '%s' is not supported, use XML or CSV", layer->name, encoding);
    return false;
  }
  return true;
}

static bool parse_object_group(mxml_node_t *node, TmxObjectGroup *group) {
  group->name = attr_dup(node, "name");
  group->numObjects = count_children(node, "object");
  if (group->numObjects == 0) {
    return true;
  }
  group->objects = SDL_calloc((size_t)group->numObjects, sizeof(*group->objects));
  if (!group->objects) {
    return false;
  }

  int i = 0;
  FOR_EACH_CHILD(object, node, "object") {
    TmxObject *obj = &group->objects[i++];
    obj->name = attr_dup(object, "name");
    obj->x = attr_float(object, "x", 0);
    obj->y = attr_float(object, "y", 0);
    obj->width = attr_float(object, "width", 0);
    obj->height = attr_float(object, "height", 0);
    parse_properties(object, &obj->properties, &obj->numProperties);
  }
  return true;
}

TmxMap *tmx_load(const char *path) {
  mxml_options_t *options = mxmlOptionsNew();
  mxmlOptionsSetTypeValue(options, MXML_TYPE_OPAQUE);
  mxml_node_t *tree = mxmlLoadFilename(NULL, options, path);
  mxmlOptionsDelete(options);
  if (!tree) {
    SDL_SetError("Could not parse %s", path);
    return NULL;
  }

  // The <map> element sits below the <?xml?> declaration node.
  mxml_node_t *root = is_element(tree, "map") ? tree
                    : mxmlFindElement(tree, tree, "map", NULL, NULL, MXML_DESCEND_ALL);
  if (!root) {
    SDL_SetError("%s has no <map> element", path);
    mxmlDelete(tree);
    return NULL;
  }

  TmxMap *map = SDL_calloc(1, sizeof(*map));
  map->width = attr_int(root, "width", 0);
  map->height = attr_int(root, "height", 0);
  map->tilewidth = attr_int(root, "tilewidth", 0);
  map->tileheight = attr_int(root, "tileheight", 0);
  map->orientation = attr_dup(root, "orientation");

  map->numTilesets = count_children(root, "tileset");
  map->numLayers = count_children(root, "layer");
  map->numObjectGroups = count_children(root, "objectgroup");
  map->tilesets = SDL_calloc((size_t)SDL_max(map->numTilesets, 1), sizeof(*map->tilesets));
  map->layers = SDL_calloc((size_t)SDL_max(map->numLayers, 1), sizeof(*map->layers));
  map->objectGroups = SDL_calloc((size_t)SDL_max(map->numObjectGroups, 1), sizeof(*map->objectGroups));

  bool ok = map->tilesets && map->layers && map->objectGroups;
  int tilesets = 0, layers = 0, groups = 0;
  for (mxml_node_t *n = mxmlGetFirstChild(root); ok && n; n = mxmlGetNextSibling(n)) {
    if (is_element(n, "tileset")) {
      TmxTileset *set = &map->tilesets[tilesets++];
      ok = parse_tileset(n, set);
      map->maxGid = SDL_max(map->maxGid, set->firstgid + set->numTiles - 1);
    } else if (is_element(n, "layer")) {
      ok = parse_layer(n, &map->layers[layers++]);
    } else if (is_element(n, "objectgroup")) {
      ok = parse_object_group(n, &map->objectGroups[groups++]);
    }
  }

  mxmlDelete(tree);
  if (!ok) {
    tmx_free(map);
    return NULL;
  }
  return map;
}

void tmx_free(TmxMap *map) {
  if (!map) {
    return;
  }
  for (int i = 0; i < map->numTilesets; i++) {
    SDL_free(map->tilesets[i].name);
    SDL_free(map->tilesets[i].image);
  }
  for (int i = 0; i < map->numLayers; i++) {
    SDL_free(map->layers[i].name);
    SDL_free(map->layers[i].tiles);
  }
  for (int i = 0; i < map->numObjectGroups; i++) {
    TmxObjectGroup *group = &map->objectGroups[i];
    for (int j = 0; j < group->numObjects; j++) {
      TmxObject *obj = &group->objects[j];
      for (int k = 0; k < obj->numProperties; k++) {
        SDL_free(obj->properties[k].name);
        SDL_free(obj->properties[k].value);
      }
      SDL_free(obj->properties);
      SDL_free(obj->name);
    }
    SDL_free(group->objects);
    SDL_free(group->name);
  }
  SDL_free(map->tilesets);
  SDL_free(map->layers);
  SDL_free(map->objectGroups);
  SDL_free(map->orientation);
  SDL_free(map);
}

const TmxObjectGroup *tmx_find_object_group(const TmxMap *map, const char *name) {
  for (int i = 0; i < map->numObjectGroups; i++) {
    if (SDL_strcmp(map->objectGroups[i].name, name) == 0) {
      return &map->objectGroups[i];
    }
  }
  return NULL;
}
