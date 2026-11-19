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
semantic theme tokens. Startup logs the linked SDL version, the renderer SDL selected,
the active theme and font size, the preferences path, and frame timing for the first
rendered frame.

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
Vendored yyjson (0.13.0) powers typed settings and semantic light/dark theme tokens
loaded from `config/` with strict parse errors and compiled-in fallbacks.
See [`docs/ROADMAP.md`](docs/ROADMAP.md) for phase status and exit evidence.

## Architecture

- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — module boundaries and design.
- [`docs/ROADMAP.md`](docs/ROADMAP.md) — phased delivery plan and exit criteria.
- [`docs/architecture/`](docs/architecture/) — approved architectural decisions (ADRs).

## Licence

Vellum is free software licensed under **GPL-2.0-only**. See [LICENSE](LICENSE) for the
full text. Third-party components keep their own licences; their notices are preserved
alongside any vendored sources.
