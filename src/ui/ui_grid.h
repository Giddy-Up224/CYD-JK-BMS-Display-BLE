#pragma once
// A page of per-cell values: four summary chips on top, then a grid of cell
// tiles sized to the cell count (8 cells: 4 x 2 big tiles, 16: 4 x 4,
// 24: 6 x 4, 32: 8 x 4). Each tile has the cell number, the value and a thin
// level bar; the lowest and highest cells are colored. Used by the Cells and
// Wire resistance pages.

#include "jk/jk_proto.h"
#include "lvgl.h"

struct GridTile {
    lv_obj_t *box, *num, *value, *bar;
};

struct CellGrid {
    lv_obj_t *chipCaption[4], *chipValue[4];
    lv_obj_t *area;
    GridTile tiles[JK_MAX_CELLS];
    int count;  // tiles laid out, -1 before the first update
};

void gridBuild(CellGrid &g, lv_obj_t *page);
// values[n] already in display units; lo/hi: indexes to color (or -1);
// decimals for the tiles. Lays the tiles out again when n changes.
void gridUpdate(CellGrid &g, int n, const float *values, int lo, int hi, int decimals);
// No data: keeps the tiles, shows dashes
void gridEmpty(CellGrid &g);
void gridChip(CellGrid &g, int i, const char *caption, const char *value, lv_color_t color);
