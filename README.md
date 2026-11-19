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
./build/bench/vellum_bench_draw_list          # draw-list microbenchmark
cmake --preset release && cmake --build --preset release
./build/release/vellum_bench_text_frame       # real text-pipeline benchmark
```

`vellum_bench_draw_list` is a skeleton microbenchmark for draw-list construction.
`vellum_bench_text_frame` is the repeatable benchmark required by
[ADR-004](docs/architecture/ADR-004-rendering-sdl3-first-gpu-conditional.md): it
runs the real shaping, line-cache, atlas, draw-list, and SDL render pipeline over
a 1,000-glyph editor scene and a 200,000-glyph stress scene, with a fixed
configuration printed in its output. Set `VELLUM_BENCH_HEADLESS=1` for a CI smoke
run using SDL's dummy/software drivers; those numbers are not performance
evidence. Recorded release results and interpretation are in
[`docs/benchmarks/2026-11-19-text-frame.md`](docs/benchmarks/2026-11-19-text-frame.md).

## Current state

Phase 1 (Foundation) is implemented: C17/CMake/Ninja build, core utilities, SDL3
window and event loop, draw list plus a batched SDL Render backend, unit tests,
integration tests, and the benchmark skeleton. Phase 2 is complete: ICU-backed
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
watcher on a fixed worker thread. A bounded line cache stores fully laid-out
single lines keyed by text, pixel size, direction, and box width, so unchanged
lines reuse their geometry. `SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED` is translated
into app input; the app re-queries the window scale, recomputes the effective text
pixel size, and re-rasterizes glyphs at the new size (verified in a real window:
scale 1.00 → 2.00, pixel size 14 → 28, atlas re-upload before the first frame).
The release text benchmark measures 1.12–2.22 ms mean per frame for 1,000 glyphs
and documents the 200,000-glyph stress result and the no-GPU-change conclusion;
per-frame arena usage is stable with one reset per frame.
See [`docs/ROADMAP.md`](docs/ROADMAP.md) for phase status and exit evidence and
[`docs/benchmarks/2026-11-19-text-frame.md`](docs/benchmarks/2026-11-19-text-frame.md)
for the recorded benchmark data.

## Architecture

- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — module boundaries and design.
- [`docs/ROADMAP.md`](docs/ROADMAP.md) — phased delivery plan and exit criteria.
- [`docs/architecture/`](docs/architecture/) — approved architectural decisions (ADRs).

## Licence

Vellum is free software licensed under **GPL-2.0-only**. See [LICENSE](LICENSE) for the
full text. Third-party components keep their own licences; their notices are preserved
alongside any vendored sources.
