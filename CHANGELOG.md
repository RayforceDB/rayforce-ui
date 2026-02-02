# Changelog

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
