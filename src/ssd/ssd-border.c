// SPDX-License-Identifier: GPL-2.0-only

#include <assert.h>
#include <wlr/types/wlr_scene.h>
#include "common/macros.h"
#include "common/scene-helpers.h"
#include "config/rcxml.h"
#include "ssd.h"
#include "ssd-internal.h"
#include "theme.h"
#include "view.h"

/*
 * Size and place the border rects around the view and its titlebar.
 *
 * Non-tiled (partial border, rounded corners), titlebar at the top:
 *    _____________
 *   o           oox
 *  |---------------|
 *  |_______________|
 *
 * With the titlebar on a side, the corners sit at both ends of the bar and
 * it's the side border that stops short of them, e.g. on the left:
 *    ______________
 *   /x|            |
 *  |o |            |
 *  |  |            |
 *   \_|____________|
 *
 * Tiled (full border, squared corners):
 *   _______________
 *  |o           oox|
 *  |---------------|
 *  |_______________|
 *
 * Tiled or non-tiled with zero title height (full boarder, no title):
 *   _______________
 *  |_______________|
 */
static void
set_border_geometry(struct ssd *ssd)
{
	struct view *view = ssd->view;
	struct theme *theme = rc.theme;

	int bw = theme->border_width;
	int width = view_effective_width(view, /* use_pending */ false);
	int height = view_effective_height(view, /* use_pending */ false);
	int corner_width = ssd_get_corner_width();
	struct border titlebar = ssd_titlebar_thickness(ssd);

	/* The view plus its titlebar, in view coordinates */
	struct wlr_box inner = {
		.x = -titlebar.left,
		.y = -titlebar.top,
		.width = width + titlebar.left + titlebar.right,
		.height = height + titlebar.top,
	};

	/* Full border by default */
	struct wlr_box left = { inner.x - bw, inner.y, bw, inner.height };
	struct wlr_box right = { inner.x + inner.width, inner.y, bw, inner.height };
	struct wlr_box top = { inner.x - bw, inner.y - bw, inner.width + 2 * bw, bw };
	struct wlr_box bottom = { inner.x - bw, height, inner.width + 2 * bw, bw };

	/* Leave room for the corner buffers at both ends of the titlebar */
	if (ssd->titlebar.height > 0 && !ssd->state.was_squared) {
		switch (ssd->titlebar.position) {
		case LAB_TITLEBAR_LEFT:
			left.y = corner_width;
			left.height = height - 2 * corner_width;
			top.x = 0;
			bottom.x = 0;
			top.width = width + bw;
			bottom.width = width + bw;
			break;
		case LAB_TITLEBAR_RIGHT:
			right.y = corner_width;
			right.height = height - 2 * corner_width;
			top.x = -bw;
			bottom.x = -bw;
			top.width = width + bw;
			bottom.width = width + bw;
			break;
		default:
			top.x = corner_width;
			top.width = width - 2 * corner_width;
			left.y = 0;
			right.y = 0;
			left.height = height;
			right.height = height;
			break;
		}
	}

	/* The border tree is offset by -border_width */
	struct wlr_box *boxes[] = { &left, &right, &top, &bottom };
	for (size_t i = 0; i < ARRAY_SIZE(boxes); i++) {
		boxes[i]->x += bw;
		boxes[i]->width = MAX(boxes[i]->width, 0);
		boxes[i]->height = MAX(boxes[i]->height, 0);
	}

	enum ssd_active_state active;
	FOR_EACH_ACTIVE_STATE(active) {
		struct ssd_border_subtree *subtree = &ssd->border.subtrees[active];
		struct wlr_scene_rect *rects[] = {
			subtree->left, subtree->right, subtree->top, subtree->bottom
		};
		for (size_t i = 0; i < ARRAY_SIZE(rects); i++) {
			wlr_scene_rect_set_size(rects[i],
				boxes[i]->width, boxes[i]->height);
			wlr_scene_node_set_position(&rects[i]->node,
				boxes[i]->x, boxes[i]->y);
		}
	}
}

void
ssd_border_create(struct ssd *ssd)
{
	assert(ssd);
	assert(!ssd->border.tree);

	struct view *view = ssd->view;
	struct theme *theme = rc.theme;

	ssd->border.tree = lab_wlr_scene_tree_create(ssd->tree);
	wlr_scene_node_set_position(&ssd->border.tree->node, -theme->border_width, 0);

	enum ssd_active_state active;
	FOR_EACH_ACTIVE_STATE(active) {
		struct ssd_border_subtree *subtree = &ssd->border.subtrees[active];
		subtree->tree = lab_wlr_scene_tree_create(ssd->border.tree);
		struct wlr_scene_tree *parent = subtree->tree;
		wlr_scene_node_set_enabled(&parent->node, active);
		float *color = theme->window[active].border_color;

		subtree->left = lab_wlr_scene_rect_create(parent, 0, 0, color);
		subtree->right = lab_wlr_scene_rect_create(parent, 0, 0, color);
		subtree->bottom = lab_wlr_scene_rect_create(parent, 0, 0, color);
		subtree->top = lab_wlr_scene_rect_create(parent, 0, 0, color);
	}

	if (view->maximized == VIEW_AXIS_BOTH) {
		wlr_scene_node_set_enabled(&ssd->border.tree->node, false);
	}

	/*
	 * The SSD is also recreated by a Reconfigure request, so the
	 * corners may be squared already.
	 */
	set_border_geometry(ssd);
}

void
ssd_border_update(struct ssd *ssd)
{
	assert(ssd);
	assert(ssd->border.tree);

	struct view *view = ssd->view;
	if (view->maximized == VIEW_AXIS_BOTH
			&& ssd->border.tree->node.enabled) {
		/* Disable borders on maximize */
		wlr_scene_node_set_enabled(&ssd->border.tree->node, false);
		ssd->margin = ssd_thickness(ssd->view);
	}

	if (view->maximized == VIEW_AXIS_BOTH) {
		return;
	} else if (!ssd->border.tree->node.enabled) {
		/* And re-enabled them when unmaximized */
		wlr_scene_node_set_enabled(&ssd->border.tree->node, true);
		ssd->margin = ssd_thickness(ssd->view);
	}

	set_border_geometry(ssd);
}

void
ssd_border_destroy(struct ssd *ssd)
{
	assert(ssd);
	assert(ssd->border.tree);

	wlr_scene_node_destroy(&ssd->border.tree->node);
	ssd->border = (struct ssd_border_scene){0};
}
