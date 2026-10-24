/*
  Copyright (C) 2013 batiste.bieler@gmail.com

  A* on a 4-connected grid
 */

#include "pathfind.h"

#include <SDL3/SDL.h>
#include <float.h>

// Search states are (cell, arrival direction) so turns can be priced.
#define DIRS 5  // 4 directions + "start"

static const int DX[4] = {1, -1, 0, 0};
static const int DY[4] = {0, 0, 1, -1};

typedef struct HeapNode {
  float f;
  int state;
} HeapNode;

typedef struct Heap {
  HeapNode *nodes;
  int count, cap;
} Heap;

static bool heap_push(Heap *h, float f, int state) {
  if (h->count == h->cap) {
    int cap = h->cap ? h->cap * 2 : 1024;
    HeapNode *grown = SDL_realloc(h->nodes, (size_t)cap * sizeof(*grown));
    if (!grown) {
      return false;
    }
    h->nodes = grown;
    h->cap = cap;
  }
  int i = h->count++;
  while (i > 0) {
    int parent = (i - 1) / 2;
    if (h->nodes[parent].f <= f) {
      break;
    }
    h->nodes[i] = h->nodes[parent];
    i = parent;
  }
  h->nodes[i] = (HeapNode){f, state};
  return true;
}

static HeapNode heap_pop(Heap *h) {
  HeapNode top = h->nodes[0];
  HeapNode last = h->nodes[--h->count];
  int i = 0;
  for (;;) {
    int child = 2 * i + 1;
    if (child >= h->count) {
      break;
    }
    if (child + 1 < h->count && h->nodes[child + 1].f < h->nodes[child].f) {
      child++;
    }
    if (h->nodes[child].f >= last.f) {
      break;
    }
    h->nodes[i] = h->nodes[child];
    i = child;
  }
  if (h->count > 0) {
    h->nodes[i] = last;
  }
  return top;
}

int path_find(const PathQuery *q, int *out, int maxOut) {
  int w = q->width, h = q->height;
  if (q->startX < 0 || q->startY < 0 || q->startX >= w || q->startY >= h ||
      q->goalX < 0 || q->goalY < 0 || q->goalX >= w || q->goalY >= h) {
    return -1;
  }

  int numStates = w * h * DIRS;
  float *g = SDL_malloc((size_t)numStates * sizeof(*g));
  int *parent = SDL_malloc((size_t)numStates * sizeof(*parent));
  Heap heap = {0};
  int result = -1;
  if (!g || !parent) {
    goto done;
  }
  for (int i = 0; i < numStates; i++) {
    g[i] = FLT_MAX;
  }

  int start = (q->startY * w + q->startX) * DIRS + 4;
  int goalCell = q->goalY * w + q->goalX;
  g[start] = 0;
  parent[start] = -1;
  heap_push(&heap, 0, start);

  int found = -1;
  while (heap.count > 0) {
    HeapNode node = heap_pop(&heap);
    int state = node.state;
    int cell = state / DIRS, dir = state % DIRS;
    int x = cell % w, y = cell / w;
    float gs = g[state];
    float hs = q->minCost * (float)(SDL_abs(x - q->goalX) + SDL_abs(y - q->goalY));
    if (node.f > gs + hs + 1e-4f) {
      continue;  // stale heap entry
    }
    if (cell == goalCell) {
      found = state;
      break;
    }
    for (int d = 0; d < 4; d++) {
      int nx = x + DX[d], ny = y + DY[d];
      if (nx < 0 || ny < 0 || nx >= w || ny >= h) {
        continue;
      }
      float step = q->cost(q->ctx, nx, ny);
      if (step < 0) {
        continue;
      }
      if (dir != 4 && dir != d) {
        step += q->turnCost;
      }
      int next = (ny * w + nx) * DIRS + d;
      float ng = gs + step;
      if (ng < g[next]) {
        g[next] = ng;
        parent[next] = state;
        float nh = q->minCost * (float)(SDL_abs(nx - q->goalX) + SDL_abs(ny - q->goalY));
        if (!heap_push(&heap, ng + nh, next)) {
          goto done;
        }
      }
    }
  }

  if (found >= 0) {
    int length = 0;
    for (int s = found; s >= 0; s = parent[s]) {
      length++;
    }
    if (length <= maxOut) {
      int i = length;
      for (int s = found; s >= 0; s = parent[s]) {
        out[--i] = s / DIRS;
      }
      result = length;
    }
  }

done:
  SDL_free(heap.nodes);
  SDL_free(g);
  SDL_free(parent);
  return result;
}
