# labwc fork: snap-to-grid

Personal fork of labwc, branched from the `0.20.2` tag (branch `snap-to-grid`)
so it builds against the system wlroots 0.20.2. Don't rebase on `master`
unless the system wlroots gets upgraded to match.

The feature: `<snapping><grid><size>N</size></grid>` aligns the outer window
frame to an N-pixel grid during interactive move/resize.
- Code: `src/snap-grid.c`, `include/snap-grid.h`
- Hooks: `process_cursor_move()` / `process_cursor_resize()` in
  `src/input/cursor.c`, applied just before `resistance_*_apply()`
- Config: `snap_grid_size` in `include/config/rcxml.h`, parsed in
  `src/config/rcxml.c` (`size.grid.snapping`)
- Docs: `docs/labwc-config.5.scd`, `docs/rc.xml.all`

Keep the feature itself in a single commit so it rebases cleanly onto new
labwc releases (and could be sent upstream).

## angled-corner (branch `angled-corner`, on top of `snap-to-grid`)

`<theme><cornerStyle>angled</cornerStyle>` cuts the titlebar's top corners at
45 degrees, `cornerRadius` pixels along each edge, instead of rounding them;
`<corners><topLeft style="" radius=""/><topRight .../></corners>` sets each
corner on its own. The title and outermost buttons start past the cut
(`ssd_get_corner_inset()`), except when the corners are squared.
- Code: `rounded_rect()` (corner buffers) and `round_corner_button()`
  (clipping the outermost title buttons) in `src/theme.c`;
  `ssd_get_corner_inset()` in `src/ssd/ssd.c`, used by the layout in
  `src/ssd/ssd-titlebar.c`
- Config: `rc.corners[]` (by `enum lab_corner`) in `include/config/rcxml.h`,
  resolved from the `cornerRadius`/`cornerStyle` shorthands in
  `post_processing()` of `src/config/rcxml.c`

## vertical-titlebar (branch `vertical-titlebar`, on top of `angled-corner`)

`<action name="SetTitlebarPosition" position="top|left|right"/>` moves one
window's titlebar to a side (`view->titlebar_position`, copied into
`ssd->titlebar.position` when the SSD is created; changing it recreates the
SSD). The client-menu gets a "Titlebar" sub-menu (`client-titlebar-menu`).
- The titlebar is still laid out as if it were at the top; `place()` in
  `src/ssd/ssd-titlebar.c` maps that onto the side, and the bar, corners
  and title are rotated with `wlr_scene_buffer_set_transform()` (left:
  270, text reads upwards; right: 90, downwards). Button icons stay
  upright. `scaled_buffer_set_transform()` keeps the rotated title's size
  right across re-renders.
- `ssd_thickness()` / `ssd_titlebar_thickness()` in `src/ssd/ssd.c` give the
  titlebar's room per side; borders, extents and shadow use the latter.
- A shaded view with a side titlebar rolls up sideways:
  `view_effective_width()` is 0 instead of `view_effective_height()`
  (see `src/view.c`); use both wherever the view's size matters.

Headless test (no nested window needed), with window rules running the
action on map and grim for screenshots:

    env -u WAYLAND_DISPLAY WLR_BACKENDS=headless WLR_RENDERER=pixman \
        WLR_HEADLESS_OUTPUTS=1 WLR_LIBINPUT_NO_DEVICES=1 \
        ./build/labwc -d -C <dir> 2>log
    WAYLAND_DISPLAY=wayland-N grim shot.png

## Build
    meson setup build        # once
    ninja -C build
    meson test -C build

## Test (never in the real session)
Run a nested labwc as a window inside the current Wayland session:

    ./build/labwc -d -C dev/nested -s foot 2>/tmp/labwc-nested.log

`dev/nested/rc.xml` sets a 20 px grid. The user drags/resizes the foot window;
check the result in the log or with a temporary `wlr_log(WLR_DEBUG, ...)`.
Also check that edge resistance and half-screen edge snapping still work.

## Install
Build with the PKGBUILD in `dev/pkg/` (package `labwc-snapgrid`,
provides/conflicts `labwc`). To revert: `sudo pacman -S labwc`.
