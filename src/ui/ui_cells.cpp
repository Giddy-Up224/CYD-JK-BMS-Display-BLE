// Cells page: lowest / highest cell, delta and average on top, a tile per
// cell below. The lowest cell is blue, the highest red, once they are
// HIGHLIGHT_V apart.

#include <stdio.h>
#include "ui_grid.h"
#include "ui_internal.h"

static const float HIGHLIGHT_V = 0.005f;

static CellGrid grid_;

void cellsBuild(lv_obj_t *page) {
    gridBuild(grid_, page);
}

void cellsUpdate(const BmsSnapshot &d) {
    if(!uiFresh(d)) {
        gridChip(grid_, 0, "Low", "--", UI_FAINT);
        gridChip(grid_, 1, "High", "--", UI_FAINT);
        gridChip(grid_, 2, "Delta", "--", UI_FAINT);
        gridChip(grid_, 3, "Average", "--", UI_FAINT);
        gridEmpty(grid_);
        return;
    }
    const JkCells &c = d.cells;
    bool spread = c.deltaV >= HIGHLIGHT_V;
    gridUpdate(grid_, c.cellCount, c.cellV, spread ? c.minCell : -1, spread ? c.maxCell : -1, 3);

    char cap[24], val[24];
    snprintf(cap, sizeof(cap), "Low  #%d", c.minCell + 1);
    snprintf(val, sizeof(val), "%.3f V", c.minV);
    gridChip(grid_, 0, cap, val, spread ? UI_COOL : UI_TEXT);
    snprintf(cap, sizeof(cap), "High  #%d", c.maxCell + 1);
    snprintf(val, sizeof(val), "%.3f V", c.maxV);
    gridChip(grid_, 1, cap, val, spread ? UI_BAD : UI_TEXT);
    snprintf(val, sizeof(val), "%.0f mV", c.deltaV * 1000);
    const JkSettings &s = d.settings;
    bool over = s.valid && s.triggerDeltaV > 0 && c.deltaV >= s.triggerDeltaV;
    gridChip(grid_, 2, "Delta", val, over ? UI_WARN : UI_TEXT);
    snprintf(val, sizeof(val), "%.3f V", c.avgV);
    gridChip(grid_, 3, "Average", val, UI_TEXT);
}
