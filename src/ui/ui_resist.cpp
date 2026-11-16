// Wire resistance page: the balance lead resistance of each cell in
// milliohms. A high one usually means a loose or corroded balance lead.

#include <stdio.h>
#include "ui_grid.h"
#include "ui_internal.h"

static CellGrid grid_;

void resistBuild(lv_obj_t *page) {
    gridBuild(grid_, page);
}

void resistUpdate(const BmsSnapshot &d) {
    if(!uiFresh(d)) {
        gridChip(grid_, 0, "Low", "--", UI_FAINT);
        gridChip(grid_, 1, "High", "--", UI_FAINT);
        gridChip(grid_, 2, "Average", "--", UI_FAINT);
        gridChip(grid_, 3, "Unit", "--", UI_FAINT);
        gridEmpty(grid_);
        return;
    }
    const JkCells &c = d.cells;
    int n = c.cellCount;
    float mohm[JK_MAX_CELLS];
    int lo = -1, hi = -1;
    float total = 0;
    for(int i = 0; i < n; i++) {
        mohm[i] = c.resistOhm[i] * 1000;
        total += mohm[i];
        if(lo < 0 || mohm[i] < mohm[lo]) lo = i;
        if(hi < 0 || mohm[i] > mohm[hi]) hi = i;
    }
    bool spread = n && mohm[hi] - mohm[lo] >= 1.0f;
    gridUpdate(grid_, n, mohm, spread ? lo : -1, spread ? hi : -1, 0);

    char cap[24], val[24];
    snprintf(cap, sizeof(cap), "Low  #%d", lo + 1);
    snprintf(val, sizeof(val), "%.0f", n ? mohm[lo] : 0.0f);
    gridChip(grid_, 0, cap, val, spread ? UI_COOL : UI_TEXT);
    snprintf(cap, sizeof(cap), "High  #%d", hi + 1);
    snprintf(val, sizeof(val), "%.0f", n ? mohm[hi] : 0.0f);
    gridChip(grid_, 1, cap, val, spread ? UI_BAD : UI_TEXT);
    snprintf(val, sizeof(val), "%.1f", n ? total / n : 0.0f);
    gridChip(grid_, 2, "Average", val, UI_TEXT);
    gridChip(grid_, 3, "Unit", "mOhm", UI_DIM);
}
