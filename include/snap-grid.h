/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_SNAP_GRID_H
#define LABWC_SNAP_GRID_H

#include <wlr/util/box.h>

struct view;

/*
 * Align the outer frame of a view (including SSD borders) to a grid of
 * rc.snap_grid_size pixels, anchored at the origin of the view's output.
 * Both functions are no-ops when the grid size is 0 or 1.
 */
void snap_grid_move_apply(struct view *view, int *x, int *y);
void snap_grid_resize_apply(struct view *view, struct wlr_box *new_geom);

#endif /* LABWC_SNAP_GRID_H */
