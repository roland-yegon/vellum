# ADR-004: SDL3 Rendering First, GPU Backend Only When Justified

- **Status:** Accepted
- **Date:** 2026-11-19
- **Supersedes:** open questions "GPU backend trigger" in `ARCHITECTURE.md` §11 and the
  summary entries for the renderer in `ARCHITECTURE.md` §12

## Decision

Build the first rendering backend on the **SDL3 Render API**, using batched geometry
via `SDL_RenderGeometry` for glyphs and rectangles. Do **not** introduce a custom GPU
rendering architecture, shaders, or a second rendering framework prematurely.

Define and maintain a repeatable benchmark that measures the rendering backend under
these fixed conditions:

- **Workload:** 200,000 visible glyphs in a representative code-editor scene (mixed
  syntax styles, selections, current-line highlight, sidebar, status bar).
- **Configuration:** fixed viewport, fixed font family/size, fixed UI scale, fixed
  theme; all recorded with the result.
- **Build:** release configuration only, with tracing counters available.
- **Conditions:** consistent hardware, driver, and desktop session; record SDL3
  version, GPU driver reported by SDL, and OS. Repeat runs and report a distribution,
  not a single number.
- **Measurements:** CPU time per frame, GPU time where the API/tooling exposes it, and
  end-to-end frame latency.

**Investigation threshold:** approximately **8 ms per frame**. If the backend exceeds
this threshold under the defined benchmark, the response is first to **profile** the
pipeline, not to change architecture. Investigate at minimum:

- glyph caching and atlas hit rate;
- texture atlas efficiency and atlas resize churn;
- draw-call count and batching;
- dirty-region rendering;
- text layout / shaped-run caching;
- unnecessary redraws and layout passes;
- CPU-side bottlenecks (allocation, shaping, command construction).

Only if profiling demonstrates that the existing backend cannot meet the performance
requirements may a custom SDL3 GPU backend be introduced. Any such proposal must
document the measured evidence, the additional complexity and maintenance cost, and a
superseding ADR. The internal draw-list interface must stay small enough that a future
backend can implement it without forcing premature abstraction across the UI.

## Context

Verification before this decision:
- SDL3 **3.4.20** is installed, with 86 headers and a CMake config package.
- `SDL_RenderGeometry(renderer, texture, vertices, num_vertices, indices, num_indices)`
  is present in the installed `SDL_render.h` and documented by the SDL wiki as
  available since SDL 3.2.0. It renders many textured triangles with per-vertex colour
  in one call, so a glyph atlas plus a quad batcher avoids the usual per-glyph draw
  call cost of 2D renderers.
- SDL's own GPU documentation says: "If you just need simple 2D graphics, the Render
  API is much easier to use but still hardware-accelerated. That said, even for 2D
  applications the performance benefits and expressiveness of the GPU API are
  significant." The GPU API requires backend-specific shader formats (SPIR-V, DXBC,
  DXIL, MSL), cross-compilation (e.g. SDL_shadercross), pass/state management, and
  correct resource cycling.
- Vulkan is present on this host, but availability does not by itself justify the
  complexity.
- The editor's visual output is fundamentally rectangles and textured quads.

## Alternatives considered

1. **Start with the SDL3 GPU API** — rejected: shader toolchains, resource cycling,
   and pass management add substantial code and maintenance cost before any evidence
   of need.
2. **Software rasterizer** — rejected: unnecessary; SDL's Render API is already
   hardware-accelerated where available.
3. **Third-party 2D/UI rendering library (e.g. NanoVG, Skia)** — rejected: introduces
   a large dependency and a second rendering model for no demonstrated benefit.
4. **Per-glyph `SDL_RenderTexture` calls with no batching** — rejected:
   `SDL_RenderGeometry` provides batching in the same API.

## Consequences

- The benchmark harness and frame trace counters are part of early work (the Phase 1
  benchmark skeleton) and must be kept runnable; a claimed performance result without
  a recorded benchmark configuration is not accepted.
- The draw-list interface is intentionally narrow: clear/rect/textured-quad batch/
  line/scissor/submit. UI code emits draw lists and never calls SDL rendering
  functions directly.
- No shader assets, GPU pipelines, or GPU timers are required in the initial build.
- If a GPU backend is eventually justified, it must implement the same draw-list
  interface; UI and text-layout code must not need changes.
- Failure to keep the benchmark repeatable would turn the 8 ms threshold into a
  meaningless number; this is a maintenance obligation.

## Conditions that justify revisiting

- The defined 200,000-glyph benchmark consistently exceeds ~8 ms/frame in release
  builds **and** profiling identifies the rendering backend (not layout, shaping, or
  allocation) as the limiting factor.
- A new target platform or requirement makes the chosen SDL Render backend unsuitable
  (for example, no accelerated backend available with acceptable latency).
- SDL3 deprecates or removes the Render API path used here.
- Any revisit must start with a profiling report and a superseding ADR; the threshold
  exists to prevent architecture changes based on intuition.
