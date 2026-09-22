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
