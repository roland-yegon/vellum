# vellum

A beautiful, lightweight, open-source native code editor built for everyone, from legacy
hardware to modern machines. Minimal, fast, cross-platform, and designed to stay out of
your way.

## Build

Vellum builds with Clang, CMake, and Ninja. SDL3 3.2.0 or newer, ICU (`icu-uc`),
FreeType (`freetype2`), and HarfBuzz (`harfbuzz`) are required. CJK and Arabic fonts
are bundled under `assets/fonts/`, so no host fonts are needed for the multilingual
demo.

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Other presets: `release` (optimized) and `bench` (optimized with debug info, used for
the rendering benchmark). Each preset writes to its own directory under `build/`.

The debug preset enables AddressSanitizer and UBSan and treats warnings as errors.

## Run

```sh
./build/debug/vellum
```

The window closes with `Esc`, the window manager's close button, or `SIGINT`/`SIGTERM`.
Press `T` to switch between the dark and light themes; all panels are recoloured from
semantic theme tokens and the choice is persisted to your user settings file. Settings
and theme overrides in the preferences directory are watched on a worker and reloaded
live when they change. The window now uses a retained widget tree: an activity bar, a
sidebar, a draggable splitter, a tab strip, an editor area, and a status bar. Click the
activity bar to toggle the sidebar, drag the splitter (or focus it and use the arrow
keys) to resize, switch tabs by clicking or with the left/right arrow keys, and press
`Tab` to move focus between panels. The editor area shows shaped Latin (including `!=`
and `=>` ligatures), CJK, an Arabic phrase laid out right-to-left, and mixed-script
fallback using the bundled fonts. Startup logs the linked SDL version, the renderer SDL
selected, the active theme and font size, the preferences path, and frame timing for the
first rendered frame.

Tests and benchmarks:

```sh
ctest --preset debug --output-on-failure
./build/bench/vellum_bench_draw_list
```

The current benchmark measures construction of a 200,000-quad draw list. It is a
**skeleton**, not the full 200,000-glyph rendering benchmark specified in
[ADR-004](docs/architecture/ADR-004-rendering-sdl3-first-gpu-conditional.md); no
performance conclusion should be drawn from it yet.

## Current state

Phase 1 (Foundation) is implemented: C17/CMake/Ninja build, core utilities, SDL3
window and event loop, draw list plus a batched SDL Render backend, unit tests,
integration tests, and the benchmark skeleton. Phase 2 is in progress: ICU-backed
UTF-8 validation and grapheme boundaries, a FreeType glyph atlas with pinned
multilingual fonts, HarfBuzz run shaping with fallback and a cache, renderer-owned
textures with per-texture geometry batching, and glyph-atlas upload are in place.
`vellum_text_layout_line()` turns shaped runs into positioned atlas quads with ICU
base-direction detection, right-to-left placement, and per-face fallback glyphs; the
demo screen renders Latin, CJK, Arabic, and mixed-script text through it. A retained
widget tree in `src/ui/` provides constraint layout, dirty propagation, focus/tab order,
hit-testing, pointer capture for drags, and theme-token panel painting; `src/ui/panels.c`
builds the activity bar, sidebar, splitter, tab strip, editor area, and status bar, and
the app dispatches input to the tree and relayouts on resize or config changes. Vendored
yyjson (0.13.0) powers typed settings and semantic light/dark theme tokens loaded from
`config/` with strict parse errors and compiled-in fallbacks. User overrides in the
preferences directory layer over the shipped defaults and reload live via a file
watcher on a fixed worker thread.
See [`docs/ROADMAP.md`](docs/ROADMAP.md) for phase status and exit evidence.

## Architecture

- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — module boundaries and design.
- [`docs/ROADMAP.md`](docs/ROADMAP.md) — phased delivery plan and exit criteria.
- [`docs/architecture/`](docs/architecture/) — approved architectural decisions (ADRs).

## Licence

Vellum is free software licensed under **GPL-2.0-only**. See [LICENSE](LICENSE) for the
full text. Third-party components keep their own licences; their notices are preserved
alongside any vendored sources.
