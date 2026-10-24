/*
  Copyright (C) 2013 batiste.bieler@gmail.com

  A* on a 4-connected grid
 */

#ifndef GAME_PATHFIND_H
#define GAME_PATHFIND_H

#include <stdbool.h>

// Cost of stepping from cell (fx, fy) onto the adjacent cell (x, y);
// a negative value means blocked.
typedef float (*PathCostFn)(void *ctx, int fx, int fy, int x, int y);

typedef struct PathQuery {
  int width, height;
  int startX, startY;
  int goalX, goalY;
  PathCostFn cost;
  void *ctx;
  float minCost;   // lowest value cost() can return, keeps the heuristic admissible
  float turnCost;  // extra cost for changing direction, favours straight lines
} PathQuery;

// Writes the cells from start to goal (inclusive) as y * width + x into `out`.
// Returns the number of cells, or -1 if there is no path or it is longer than maxOut.
int path_find(const PathQuery *query, int *out, int maxOut);

// Cost of the cheapest way from every cell to (goalX, goalY), FLT_MAX where
// there is none. `out` holds width * height values.
bool path_distance_field(int width, int height, int goalX, int goalY, PathCostFn cost, void *ctx, float *out);

#endif
