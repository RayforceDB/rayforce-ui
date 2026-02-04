# Changelog

## v0.2.0 — 2026-02-04

- Windows extension build: raykx.dll now built and packaged on Windows
- Grid toolbar with flash toggle button and row count display
- Flash highlight cells on value change (green/red for increase/decrease)
- IBM Plex Sans font with increased cell padding
- Fix null display using native Rayforce format
- Fix startup freeze when loading scripts

## v0.1.8 — 2026-02-02

- Toast notifications: auto-dismiss popups replace scrolling alert panel
- Zero-copy obj_p transfer for all cross-thread data (no more malloc'd strings)
- Rule callbacks compiled once via lambda_call with cell value from at_idx
- Remove dead fn_expr/fn_dirty fields and msg->text field
- Widget rules/trigger system with color overlays and fn callbacks

## v0.1.7 — 2026-02-02

- Increase logo watermark brightness

## v0.1.6 — 2026-02-02

- Switch to Fira Code Bold font (full box-drawing character support)
- Fix multi-line error rendering in REPL (ANSI newline handling)
- Export rayforce symbols for plugin loading (-rdynamic)
- Auto-clone rayforce dependency on make
- Guard clean target against missing deps

## v0.1.5 — 2026-02-02

- Professional dark theme with brand gold (#E9A033) accents
- Zed-style flat UI: zero rounding, thin borders
- Bump fonts to 36/80px
- Remove window control button borders
- Fix bolt color to match logo

## v0.1.3 — 2026-01-31

- Only link dbghelp in debug builds on Windows

## v0.1.2 — 2026-01-31

- Embed fonts, logo, and icon into binary — no more runtime dependency on `assets/` directory
- Switch from Iosevka-Bold (9.4MB) to JetBrainsMono-Regular (268KB)
- Suppress console window on Windows (GUI subsystem)
- Remove assets from release packages (now embedded)

## v0.1.1 — 2026-01-29

- Static link Windows binary to avoid missing DLL errors
- Add `clifeed` example
- Add demo page with live feed walkthrough
- Fix GitHub org URL and social links

## v0.1.0 — 2026-01-28

- Initial release of rayforce-ui
- REPL widget with file chooser for loading scripts
- Grid, Chart, Text widget types
- Zero-copy data flow between Rayforce runtime and UI
- Dear ImGui with docking support
- CI build and release pipeline
