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
- Next: the bottom corners, with the part of the window outside the cut
  filled in the border color (bottom borders are plain rects today, in
  `src/ssd/ssd-border.c`).

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
