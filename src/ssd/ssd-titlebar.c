// SPDX-License-Identifier: GPL-2.0-only

#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <string.h>
#include <wlr/render/pixman.h>
#include <wlr/types/wlr_scene.h>
#include "buffer.h"
#include "common/mem.h"
#include "common/scene-helpers.h"
#include "config/rcxml.h"
#include "labwc.h"
#include "node.h"
#include "scaled-buffer/scaled-buffer.h"
#include "scaled-buffer/scaled-font-buffer.h"
#include "scaled-buffer/scaled-icon-buffer.h"
#include "scaled-buffer/scaled-img-buffer.h"
#include "ssd.h"
#include "ssd-internal.h"
#include "theme.h"
#include "view.h"

static void set_squared_corners(struct ssd *ssd, bool enable);
static void set_alt_button_icon(struct ssd *ssd, enum lab_node_type type, bool enable);
static void update_visible_buttons(struct ssd *ssd);

/* Room kept free next to @corner, unless the corners are squared */
static int
corner_inset(struct ssd *ssd, enum lab_corner corner)
{
	if (ssd->state.was_maximized || ssd->state.was_squared) {
		return 0;
	}
	return ssd_get_corner_inset(corner);
}

/*
 * The titlebar is laid out as if it were at the top of the view, whatever
 * edge it sits on: "along" the bar is x and "across" it is y below. A side
 * titlebar is the top one turned outwards, so its top edge stays against the
 * outer border: on the left the left end of the bar goes to the bottom and
 * the text reads upwards, on the right the left end goes to the top and the
 * text reads downwards.
 */

/* Length of the titlebar along the edge it sits on */
static int
titlebar_length(struct ssd *ssd)
{
	struct view *view = ssd->view;
	if (ssd->titlebar.position == LAB_TITLEBAR_TOP) {
		return view->current.width;
	}
	return view_effective_height(view, /* use_pending */ false);
}

static enum wl_output_transform
titlebar_transform(struct ssd *ssd)
{
	switch (ssd->titlebar.position) {
	case LAB_TITLEBAR_LEFT:
		return WL_OUTPUT_TRANSFORM_270;
	case LAB_TITLEBAR_RIGHT:
		return WL_OUTPUT_TRANSFORM_90;
	default:
		return WL_OUTPUT_TRANSFORM_NORMAL;
	}
}

static void
set_subtree_position(struct ssd *ssd, struct wlr_scene_tree *tree)
{
	int titlebar_height = rc.theme->titlebar_height;
	switch (ssd->titlebar.position) {
	case LAB_TITLEBAR_LEFT:
		wlr_scene_node_set_position(&tree->node, -titlebar_height, 0);
		break;
	case LAB_TITLEBAR_RIGHT:
		wlr_scene_node_set_position(&tree->node,
			view_effective_width(ssd->view, /* use_pending */ false), 0);
		break;
	default:
		wlr_scene_node_set_position(&tree->node, 0, -titlebar_height);
		break;
	}
}

/*
 * Position @node, laid out at (@x, @y) with size @width x @height in the
 * top-titlebar layout. An @upright node (a button icon) isn't rotated with
 * the bar, so it gets centered in its rotated slot instead.
 */
static void
place(struct ssd *ssd, struct wlr_scene_node *node, int x, int y,
		int width, int height, bool upright)
{
	int titlebar_height = rc.theme->titlebar_height;
	int length = titlebar_length(ssd);
	int dx = upright ? (height - width) / 2 : 0;
	int dy = upright ? (width - height) / 2 : 0;

	switch (ssd->titlebar.position) {
	case LAB_TITLEBAR_LEFT:
		wlr_scene_node_set_position(node,
			y + dx, length - x - width + dy);
		break;
	case LAB_TITLEBAR_RIGHT:
		wlr_scene_node_set_position(node,
			titlebar_height - y - height + dx, x + dy);
		break;
	default:
		wlr_scene_node_set_position(node, x, y);
		break;
	}
}

/* Rotate a (plain) scene buffer with the bar and set its unrotated size */
static void
set_buffer_size(struct ssd *ssd, struct wlr_scene_buffer *buffer,
		int width, int height)
{
	if (ssd->titlebar.position == LAB_TITLEBAR_TOP) {
		wlr_scene_buffer_set_dest_size(buffer, width, height);
	} else {
		wlr_scene_buffer_set_dest_size(buffer, height, width);
	}
}

static void
place_buttons(struct ssd *ssd, struct ssd_titlebar_subtree *subtree)
{
	struct theme *theme = rc.theme;
	int length = titlebar_length(ssd);
	int width = theme->window_button_width;
	int height = theme->window_button_height;

	/* Center vertically within titlebar */
	int y = (theme->titlebar_height - height) / 2;

	int x = theme->window_titlebar_padding_width
		+ corner_inset(ssd, LAB_CORNER_TOP_LEFT);
	struct ssd_button *button;
	wl_list_for_each(button, &subtree->buttons_left, link) {
		place(ssd, button->node, x, y, width, height, true);
		x += width + theme->window_button_spacing;
	}

	x = length - theme->window_titlebar_padding_width
		- corner_inset(ssd, LAB_CORNER_TOP_RIGHT)
		+ theme->window_button_spacing;
	wl_list_for_each(button, &subtree->buttons_right, link) {
		x -= width + theme->window_button_spacing;
		place(ssd, button->node, x, y, width, height, true);
	}
}

/* Background and corners, which change with the length and squaring */
static void
place_background(struct ssd *ssd, struct ssd_titlebar_subtree *subtree,
		bool squared)
{
	struct theme *theme = rc.theme;
	int length = titlebar_length(ssd);
	int corner_width = ssd_get_corner_width();
	int bg_offset = squared ? 0 : corner_width;
	int bar_length = MAX(length - 2 * bg_offset, 0);

	set_buffer_size(ssd, subtree->bar, bar_length, theme->titlebar_height);
	place(ssd, &subtree->bar->node, bg_offset, 0,
		bar_length, theme->titlebar_height, false);

	/* The corner buffers include the outer border */
	int corner_buffer_width = corner_width + theme->border_width;
	int corner_buffer_height = theme->titlebar_height + theme->border_width;
	place(ssd, &subtree->corner_left->node,
		-theme->border_width, -theme->border_width,
		corner_buffer_width, corner_buffer_height, false);
	place(ssd, &subtree->corner_right->node,
		length - corner_width, -theme->border_width,
		corner_buffer_width, corner_buffer_height, false);
}

void
ssd_titlebar_create(struct ssd *ssd)
{
	struct view *view = ssd->view;
	struct theme *theme = rc.theme;
	enum wl_output_transform transform = titlebar_transform(ssd);

	ssd->titlebar.tree = lab_wlr_scene_tree_create(ssd->tree);
	node_descriptor_create(&ssd->titlebar.tree->node,
		LAB_NODE_TITLEBAR, view, /*data*/ NULL);

	enum ssd_active_state active;
	FOR_EACH_ACTIVE_STATE(active) {
		struct ssd_titlebar_subtree *subtree = &ssd->titlebar.subtrees[active];
		subtree->tree = lab_wlr_scene_tree_create(ssd->titlebar.tree);
		struct wlr_scene_tree *parent = subtree->tree;
		wlr_scene_node_set_enabled(&parent->node, active);
		set_subtree_position(ssd, parent);

		struct wlr_buffer *titlebar_fill =
			&theme->window[active].titlebar_fill->base;
		struct wlr_buffer *corner_top_left =
			&theme->window[active].corner_top_left_normal->base;
		struct wlr_buffer *corner_top_right =
			&theme->window[active].corner_top_right_normal->base;

		/* Background */
		subtree->bar = lab_wlr_scene_buffer_create(parent, titlebar_fill);
		/*
		 * Work around the wlroots/pixman bug that widened 1px buffer
		 * becomes translucent when bilinear filtering is used.
		 * TODO: remove once https://gitlab.freedesktop.org/wlroots/wlroots/-/issues/3990
		 * is solved
		 */
		if (wlr_renderer_is_pixman(server.renderer)) {
			wlr_scene_buffer_set_filter_mode(
				subtree->bar, WLR_SCALE_FILTER_NEAREST);
		}
		wlr_scene_buffer_set_transform(subtree->bar, transform);

		subtree->corner_left = lab_wlr_scene_buffer_create(parent, corner_top_left);
		wlr_scene_buffer_set_transform(subtree->corner_left, transform);

		subtree->corner_right = lab_wlr_scene_buffer_create(parent, corner_top_right);
		wlr_scene_buffer_set_transform(subtree->corner_right, transform);

		/* Title */
		subtree->title = scaled_font_buffer_create_for_titlebar(
			subtree->tree, theme->titlebar_height,
			theme->window[active].titlebar_pattern);
		assert(subtree->title);
		scaled_buffer_set_transform(subtree->title->scaled_buffer,
			transform);
		node_descriptor_create(&subtree->title->scene_buffer->node,
			LAB_NODE_TITLE, view, /*data*/ NULL);

		/* Buttons, placed by set_squared_corners() below */
		wl_list_init(&subtree->buttons_left);
		wl_list_init(&subtree->buttons_right);

		for (int b = 0; b < rc.nr_title_buttons_left; b++) {
			enum lab_node_type type = rc.title_buttons_left[b];
			struct lab_img **imgs =
				theme->window[active].button_imgs[type];
			attach_ssd_button(&subtree->buttons_left, type, parent,
				imgs, 0, 0, view);
		}

		for (int b = rc.nr_title_buttons_right - 1; b >= 0; b--) {
			enum lab_node_type type = rc.title_buttons_right[b];
			struct lab_img **imgs =
				theme->window[active].button_imgs[type];
			attach_ssd_button(&subtree->buttons_right, type, parent,
				imgs, 0, 0, view);
		}
	}

	bool maximized = view->maximized == VIEW_AXIS_BOTH;
	bool squared = ssd_should_be_squared(ssd);
	if (maximized) {
		set_alt_button_icon(ssd, LAB_NODE_BUTTON_MAXIMIZE, true);
		ssd->state.was_maximized = true;
	}
	if (squared) {
		ssd->state.was_squared = true;
	}
	set_squared_corners(ssd, maximized || squared);

	update_visible_buttons(ssd);

	ssd_update_title(ssd);

	if (view->shaded) {
		set_alt_button_icon(ssd, LAB_NODE_BUTTON_SHADE, true);
	}

	if (view->visible_on_all_workspaces) {
		set_alt_button_icon(ssd, LAB_NODE_BUTTON_OMNIPRESENT, true);
	}
}

static void
update_button_state(struct ssd_button *button, enum lab_button_state state,
		bool enable)
{
	if (enable) {
		button->state_set |= state;
	} else {
		button->state_set &= ~state;
	}
	/* Switch the displayed icon buffer to the new one */
	for (uint8_t state_set = LAB_BS_DEFAULT;
			state_set <= LAB_BS_ALL; state_set++) {
		struct scaled_img_buffer *buffer = button->img_buffers[state_set];
		if (!buffer) {
			continue;
		}
		wlr_scene_node_set_enabled(&buffer->scene_buffer->node,
			state_set == button->state_set);
	}
}

/*
 * Also lays out the background, corners and buttons again, since squaring
 * the corners drops the room kept free next to them.
 */
static void
set_squared_corners(struct ssd *ssd, bool enable)
{
	/*
	 * The rounded button images are cut for a top titlebar, while the
	 * button icons stay upright on a side one
	 */
	bool rounded = !enable && ssd->titlebar.position == LAB_TITLEBAR_TOP;

	enum ssd_active_state active;
	FOR_EACH_ACTIVE_STATE(active) {
		struct ssd_titlebar_subtree *subtree = &ssd->titlebar.subtrees[active];

		place_background(ssd, subtree, enable);
		place_buttons(ssd, subtree);

		wlr_scene_node_set_enabled(&subtree->corner_left->node, !enable);

		wlr_scene_node_set_enabled(&subtree->corner_right->node, !enable);

		/* (Un)round the corner buttons */
		struct ssd_button *button;
		wl_list_for_each(button, &subtree->buttons_left, link) {
			update_button_state(button, LAB_BS_ROUNDED, rounded);
			break;
		}
		wl_list_for_each(button, &subtree->buttons_right, link) {
			update_button_state(button, LAB_BS_ROUNDED, rounded);
			break;
		}
	}
}

static void
set_alt_button_icon(struct ssd *ssd, enum lab_node_type type, bool enable)
{
	enum ssd_active_state active;
	FOR_EACH_ACTIVE_STATE(active) {
		struct ssd_titlebar_subtree *subtree = &ssd->titlebar.subtrees[active];

		struct ssd_button *button;
		wl_list_for_each(button, &subtree->buttons_left, link) {
			if (button->type == type) {
				update_button_state(button,
					LAB_BS_TOGGLED, enable);
			}
		}
		wl_list_for_each(button, &subtree->buttons_right, link) {
			if (button->type == type) {
				update_button_state(button,
					LAB_BS_TOGGLED, enable);
			}
		}
	}
}

/*
 * Usually this function just enables all the nodes for buttons, but some
 * buttons can be hidden for small windows (e.g. xterm -geometry 1x1).
 */
static void
update_visible_buttons(struct ssd *ssd)
{
	struct theme *theme = rc.theme;
	int width = MAX(titlebar_length(ssd) - 2 * theme->window_titlebar_padding_width
		- corner_inset(ssd, LAB_CORNER_TOP_LEFT)
		- corner_inset(ssd, LAB_CORNER_TOP_RIGHT), 0);
	int button_width = theme->window_button_width;
	int button_spacing = theme->window_button_spacing;
	int button_count_left = rc.nr_title_buttons_left;
	int button_count_right = rc.nr_title_buttons_right;

	/* Make sure infinite loop never occurs */
	assert(button_width > 0);

	/*
	 * The corner-left button is lastly removed as it's usually a window
	 * menu button (or an app icon button in the future).
	 *
	 * There is spacing to the inside of each button, including between the
	 * innermost buttons and the window title. See also get_title_offsets().
	 */
	while (width < ((button_width + button_spacing)
			* (button_count_left + button_count_right))) {
		if (button_count_left > button_count_right) {
			button_count_left--;
		} else {
			button_count_right--;
		}
	}

	enum ssd_active_state active;
	FOR_EACH_ACTIVE_STATE(active) {
		struct ssd_titlebar_subtree *subtree = &ssd->titlebar.subtrees[active];
		int button_count = 0;

		struct ssd_button *button;
		wl_list_for_each(button, &subtree->buttons_left, link) {
			wlr_scene_node_set_enabled(button->node,
				button_count < button_count_left);
			button_count++;
		}

		button_count = 0;
		wl_list_for_each(button, &subtree->buttons_right, link) {
			wlr_scene_node_set_enabled(button->node,
				button_count < button_count_right);
			button_count++;
		}
	}
}

void
ssd_titlebar_update(struct ssd *ssd)
{
	struct view *view = ssd->view;

	bool maximized = view->maximized == VIEW_AXIS_BOTH;
	bool squared = ssd_should_be_squared(ssd);

	/* Squaring the corners drops the room kept free next to them */
	bool corners_changed = ssd->state.was_maximized != maximized
		|| ssd->state.was_squared != squared;
	if (corners_changed) {
		if (ssd->state.was_maximized != maximized) {
			set_alt_button_icon(ssd, LAB_NODE_BUTTON_MAXIMIZE, maximized);
		}
		ssd->state.was_maximized = maximized;
		ssd->state.was_squared = squared;
	}

	/* A right titlebar moves in when the view rolls up sideways */
	bool shade_changed = ssd->state.was_shaded != view->shaded;
	if (shade_changed) {
		set_alt_button_icon(ssd, LAB_NODE_BUTTON_SHADE, view->shaded);
		ssd->state.was_shaded = view->shaded;
	}

	if (ssd->state.was_omnipresent != view->visible_on_all_workspaces) {
		set_alt_button_icon(ssd, LAB_NODE_BUTTON_OMNIPRESENT,
			view->visible_on_all_workspaces);
		ssd->state.was_omnipresent = view->visible_on_all_workspaces;
	}

	/* A side titlebar follows the height, and the right one the width */
	bool resized = view->current.width != ssd->state.geometry.width
		|| (ssd->titlebar.position != LAB_TITLEBAR_TOP
			&& (view->current.height != ssd->state.geometry.height
				|| shade_changed));
	if (!resized && !corners_changed) {
		return;
	}

	enum ssd_active_state active;
	FOR_EACH_ACTIVE_STATE(active) {
		set_subtree_position(ssd, ssd->titlebar.subtrees[active].tree);
	}
	set_squared_corners(ssd, maximized || squared);
	update_visible_buttons(ssd);

	ssd_update_title(ssd);
}

void
ssd_titlebar_destroy(struct ssd *ssd)
{
	if (!ssd->titlebar.tree) {
		return;
	}

	zfree(ssd->state.title.text);
	wlr_scene_node_destroy(&ssd->titlebar.tree->node);
	ssd->titlebar = (struct ssd_titlebar_scene){0};
}

/*
 * For ssd_update_title* we do not early out because
 * .active and .inactive may result in different sizes
 * of the title (font family/size) or background of
 * the title (different button/border width).
 *
 * Both, wlr_scene_node_set_enabled() and wlr_scene_node_set_position()
 * check for actual changes and return early if there is no change in state.
 * Always using wlr_scene_node_set_enabled(node, true) will thus not cause
 * any unnecessary screen damage and makes the code easier to follow.
 */

static void
ssd_update_title_positions(struct ssd *ssd, int offset_left, int offset_right)
{
	struct theme *theme = rc.theme;
	int width = titlebar_length(ssd);
	int title_bg_width = width - offset_left - offset_right;

	enum ssd_active_state active;
	FOR_EACH_ACTIVE_STATE(active) {
		struct ssd_titlebar_subtree *subtree = &ssd->titlebar.subtrees[active];
		struct scaled_font_buffer *title = subtree->title;
		int x, y;

		x = offset_left;
		y = (theme->titlebar_height - title->height) / 2;

		if (title_bg_width <= 0) {
			wlr_scene_node_set_enabled(&title->scene_buffer->node, false);
			continue;
		}
		wlr_scene_node_set_enabled(&title->scene_buffer->node, true);

		if (theme->window_label_text_justify == LAB_JUSTIFY_CENTER) {
			if (title->width + MAX(offset_left, offset_right) * 2 <= width) {
				/* Center based on the full width */
				x = (width - title->width) / 2;
			} else {
				/*
				 * Center based on the width between the buttons.
				 * Title jumps around once this is hit but its still
				 * better than to hide behind the buttons on the right.
				 */
				x += (title_bg_width - title->width) / 2;
			}
		} else if (theme->window_label_text_justify == LAB_JUSTIFY_RIGHT) {
			x += title_bg_width - title->width;
		} else if (theme->window_label_text_justify == LAB_JUSTIFY_LEFT) {
			/* TODO: maybe add some theme x padding here? */
		}
		place(ssd, &title->scene_buffer->node, x, y,
			title->width, title->height, false);
	}
}

/*
 * Get left/right offsets of the title area based on visible/hidden states of
 * buttons set in update_visible_buttons().
 */
static void
get_title_offsets(struct ssd *ssd, int *offset_left, int *offset_right)
{
	struct ssd_titlebar_subtree *subtree = &ssd->titlebar.subtrees[SSD_ACTIVE];
	int button_width = rc.theme->window_button_width;
	int button_spacing = rc.theme->window_button_spacing;
	int padding_width = rc.theme->window_titlebar_padding_width;
	*offset_left = padding_width + corner_inset(ssd, LAB_CORNER_TOP_LEFT);
	*offset_right = padding_width + corner_inset(ssd, LAB_CORNER_TOP_RIGHT);

	struct ssd_button *button;
	wl_list_for_each(button, &subtree->buttons_left, link) {
		if (button->node->enabled) {
			*offset_left += button_width + button_spacing;
		}
	}
	wl_list_for_each(button, &subtree->buttons_right, link) {
		if (button->node->enabled) {
			*offset_right += button_width + button_spacing;
		}
	}
}

void
ssd_update_title(struct ssd *ssd)
{
	if (!ssd || !rc.show_title) {
		return;
	}

	struct view *view = ssd->view;
	/* view->title is never NULL (instead it can be an empty string) */
	assert(view->title);

	struct theme *theme = rc.theme;
	struct ssd_state_title *state = &ssd->state.title;
	bool title_unchanged = state->text && !strcmp(view->title, state->text);

	int offset_left, offset_right;
	get_title_offsets(ssd, &offset_left, &offset_right);
	int title_bg_width = titlebar_length(ssd) - offset_left - offset_right;

	enum ssd_active_state active;
	FOR_EACH_ACTIVE_STATE(active) {
		struct ssd_titlebar_subtree *subtree = &ssd->titlebar.subtrees[active];
		struct ssd_state_title_width *dstate = &state->dstates[active];
		const float *text_color = theme->window[active].label_text_color;
		struct font *font = active ?
			&rc.font_activewindow : &rc.font_inactivewindow;

		if (title_bg_width <= 0) {
			dstate->truncated = true;
			continue;
		}

		if (title_unchanged
				&& !dstate->truncated && dstate->width < title_bg_width) {
			/* title the same + we don't need to resize title */
			continue;
		}

		const float bg_color[4] = {0, 0, 0, 0}; /* ignored */
		scaled_font_buffer_update(subtree->title, view->title,
			title_bg_width, font,
			text_color, bg_color);

		/* And finally update the cache */
		dstate->width = subtree->title->width;
		dstate->truncated = title_bg_width <= dstate->width;
	}

	if (!title_unchanged) {
		xstrdup_replace(state->text, view->title);
	}
	ssd_update_title_positions(ssd, offset_left, offset_right);
}

void
ssd_update_hovered_button(struct wlr_scene_node *node)
{
	struct ssd_button *button = NULL;

	if (node && node->data) {
		button = node_try_ssd_button_from_node(node);
		if (button == server.hovered_button) {
			/* Cursor is still on the same button */
			return;
		}
	}

	/* Disable old hover */
	if (server.hovered_button) {
		update_button_state(server.hovered_button, LAB_BS_HOVERED, false);
	}
	server.hovered_button = button;
	if (button) {
		update_button_state(button, LAB_BS_HOVERED, true);
	}
}

bool
ssd_should_be_squared(struct ssd *ssd)
{
	struct view *view = ssd->view;
	int corner_width = ssd_get_corner_width();

	return (view_is_tiled_and_notify_tiled(view)
			|| titlebar_length(ssd) < corner_width * 2)
		&& view->maximized != VIEW_AXIS_BOTH;
}
