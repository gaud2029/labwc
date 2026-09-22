// SPDX-License-Identifier: GPL-2.0-only
#include "snap-grid.h"
#include <assert.h>
#include <wlr/types/wlr_output_layout.h>
#include "common/border.h"
#include "common/edge.h"
#include "config/rcxml.h"
#include "labwc.h"
#include "output.h"
#include "ssd.h"
#include "view.h"

/* Round value to the nearest multiple of size, counting from origin */
static int
round_to_grid(int value, int origin, int size)
{
	int rel = value - origin;
	int rem = ((rel % size) + size) % size;
	rel -= rem;
	if (2 * rem >= size) {
		rel += size;
	}
	return origin + rel;
}

static struct wlr_box
grid_origin(struct view *view)
{
	struct wlr_box box = {0};
	if (output_is_usable(view->output)) {
		wlr_output_layout_get_box(server.output_layout,
			view->output->wlr_output, &box);
	}
	return box;
}

void
snap_grid_move_apply(struct view *view, int *x, int *y)
{
	assert(view);
	int size = rc.snap_grid_size;
	if (size <= 1) {
		return;
	}

	struct wlr_box origin = grid_origin(view);
	struct border border = ssd_get_margin(view->ssd);

	*x = round_to_grid(*x - border.left, origin.x, size) + border.left;
	*y = round_to_grid(*y - border.top, origin.y, size) + border.top;
}

void
snap_grid_resize_apply(struct view *view, struct wlr_box *new_geom)
{
	assert(view);
	int size = rc.snap_grid_size;
	if (size <= 1) {
		return;
	}

	struct wlr_box origin = grid_origin(view);
	struct border border = ssd_get_margin(view->ssd);
	enum lab_edge edges = server.resize_edges;

	/* Only move the grabbed edges; the opposite edges stay anchored */
	if (edges & LAB_EDGE_TOP) {
		int bottom = new_geom->y + new_geom->height;
		new_geom->y = round_to_grid(new_geom->y - border.top,
			origin.y, size) + border.top;
		new_geom->height = bottom - new_geom->y;
	} else if (edges & LAB_EDGE_BOTTOM) {
		int bottom = new_geom->y + new_geom->height + border.bottom;
		bottom = round_to_grid(bottom, origin.y, size);
		new_geom->height = bottom - border.bottom - new_geom->y;
	}

	if (edges & LAB_EDGE_LEFT) {
		int right = new_geom->x + new_geom->width;
		new_geom->x = round_to_grid(new_geom->x - border.left,
			origin.x, size) + border.left;
		new_geom->width = right - new_geom->x;
	} else if (edges & LAB_EDGE_RIGHT) {
		int right = new_geom->x + new_geom->width + border.right;
		right = round_to_grid(right, origin.x, size);
		new_geom->width = right - border.right - new_geom->x;
	}
}
