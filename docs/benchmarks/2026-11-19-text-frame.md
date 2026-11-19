# Text-frame benchmark — 2026-11-19

Raw output of `./build/release/vellum_bench_text_frame`, three consecutive release
runs on the development host. This is the recorded evidence for the ADR-004
investigation threshold and the Phase 2 exit criteria.

## How to reproduce

```sh
cmake --preset release
cmake --build --preset release
ctest --preset release --output-on-failure
./build/release/vellum_bench_text_frame
```

`VELLUM_BENCH_HEADLESS=1` forces SDL's `dummy` video and `software` render drivers
for CI smoke runs; those numbers are **not** the performance evidence below,
which uses the default accelerated driver SDL selects.

## Fixed configuration (every run)

| Setting | Value |
| --- | --- |
| Build | release (`-O3`, no sanitizers), warnings as errors |
| SDL / renderer / OS | SDL 3.4.20 / `opengl` (SDL-selected) / Linux |
| VSync | off (`SDL_SetRenderVSync(renderer, 0)`) |
| Viewport / editor | 1280x720 / 1184x636 |
| Font / pixel size / scale | JetBrains Mono / 14 px / 1.00 |
| Theme | dark (semantic tokens) |
| Scene | 40 rows, sidebar, status bar, current-line highlight, selection, 4 syntax colours |
| Workloads | 25 columns = 1,000 glyphs; 5,000 columns = 200,000 glyphs |
| Timing | 5 warmup frames, 30 measured frames, `SDL_GetPerformanceCounter()` |
| Frame phases | `arena + scene layout`, `atlas upload`, `render submit` |

## Distributions (CPU ms per measured frame)

### 1,000 glyphs

| Run | min | p50 | p95 | max | mean | stddev |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | 0.440 | 0.744 | 3.468 | 5.325 | 1.119 | 1.069 |
| 2 | 0.716 | 1.079 | 9.623 | 9.954 | 2.216 | 2.405 |
| 3 | 0.556 | 0.697 | 5.048 | 10.738 | 1.416 | 2.044 |

### 200,000 glyphs (ADR-004 stress workload)

| Run | min | p50 | p95 | max | mean | stddev |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | 22.405 | 23.732 | 26.746 | 26.973 | 24.031 | 1.224 |
| 2 | 21.839 | 23.797 | 33.143 | 40.259 | 24.501 | 3.631 |
| 3 | 21.355 | 22.534 | 25.109 | 26.243 | 22.903 | 1.190 |

## Phase breakdown (CPU ms, mean / max, share of mean frame)

### 1,000 glyphs

| Run | arena + scene layout | atlas upload | render submit |
| --- | --- | --- | --- |
| 1 | 0.119 / 0.183 (10.6%) | 0.000 / 0.001 | 0.999 / 5.231 (89.3%) |
| 2 | 0.181 / 0.332 (8.2%) | 0.000 / 0.000 | 2.033 / 9.795 (91.8%) |
| 3 | 0.122 / 0.218 (8.6%) | 0.000 / 0.001 | 1.293 / 10.630 (91.3%) |

### 200,000 glyphs

| Run | arena + scene layout | atlas upload | render submit |
| --- | --- | --- | --- |
| 1 | 6.540 / 8.205 (27.2%) | 0.000 / 0.0 | 17.490 / 20.205 (72.8%) |
| 2 | 6.722 / 10.121 (27.4%) | 0.000 / 0.001 | 17.779 / 30.138 (72.6%) |
| 3 | 6.294 / 7.683 (27.5%) | 0.000 / 0.001 | 16.608 / 18.812 (72.5%) |

## Counters (identical in all three runs)

| Counter | 1,000 glyphs | 200,000 glyphs |
| --- | --- | --- |
| Glyphs / textured quads | 1,000 / 800 | 200,000 / 172,560 |
| Rects / SDL geometry calls | 4 / 2 | 4 / 2 |
| Renderer vertex capacity | 4,096 | 1,048,576 |
| Line-cache hits / misses / evictions | 1,360 / 40 / 0 | 1,360 / 40 / 0 |
| Glyph rasterizations / atlas uploads | 25 / 1 | 33 / 1 |
| Arena resets (5 warmup + 30 measured) | 35 | 35 |
| Arena capacity / peak used | 1,048,576 / 1,040 B | 1,048,576 / 200,040 B |
| Arena allocations per frame | 40 | 40 |

Space glyphs advance the pen but emit no quad, so the quad count is lower than
the glyph count. The line cache reaches steady state after the first frame of
each workload (40 misses = 40 distinct lines); the 5 warmup frames keep the
measured frames free of rasterization and atlas-upload cost.

## Interpretation

- The 1,000-glyph editor workload is well under the ADR-004 ~8 ms investigation
  threshold: p50 is 0.70–1.08 ms and the mean is 1.12–2.22 ms across the three
  runs. The single p95 outlier (9.623 ms, run 2) coincides with a render-submit
  maximum of 9.795 ms, not with layout (0.332 ms maximum), so it is an
  intermittent presentation/driver spike rather than a text-pipeline regression.
- The 200,000-glyph stress workload exceeds the threshold (mean 22.9–24.5 ms),
  which triggers ADR-004's **profile first** clause. The phase breakdown shows
  ~27% arena/scene layout and ~73% render submit, with only **two** SDL geometry
  calls per frame. This is CPU-side geometry construction plus quad overdraw in a
  1280x720 viewport, not a draw-call or batching failure and not shaping/atlas
  churn (zero cache evictions, one atlas upload). A custom GPU backend would not
  remove either measured cost, so no architecture change is proposed. Follow-up
  work should profile dirty-region rendering and overdraw before revisiting the
  backend; ADR-004's "conditions that justify revisiting" still require a
  profiling report and superseding ADR.
- Arena usage is stable across frames (35 resets for 35 rendered frames, one
  reset per frame, no growth in capacity), so there is no measurable per-frame
  heap churn in the text/layout path.
- A later verification run after the full debug/release/bench CTest matrix
  reproduced the same counters and shape: 1,000-glyph p50 0.695 ms, mean 1.065 ms;
  200,000-glyph mean 25.093 ms with 28.2% layout / 71.8% render submit.
