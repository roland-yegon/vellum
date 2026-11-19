# Vellum — Development Roadmap

> **Status:** Approved baseline. Companion to `docs/ARCHITECTURE.md`. Written
> 2026-11-19 after inspecting the repository and verifying the installed toolchain;
> Phase 1 and Phase 2 are complete; Phase 3 is in progress.
>
> This is a plan of *testable increments*, not a wish list. A phase is complete only
> when its exit criteria pass. If a phase cannot meet its exit criteria, we stop and
> fix it rather than stacking features on an unstable base.

---

## 0. How to read this roadmap

- **Goal** — what the phase is for.
- **Scope** — concrete work items, in rough order.
- **Deliverables** — artifacts that exist when the phase is done.
- **Exit criteria** — observable, testable conditions. These are the gate.
- **Depends on / Risks** — sequencing constraints and known uncertainties.

Every phase includes: build passes with zero new warnings, unit tests pass under
ASan/UBSan in debug, documentation updated, and no regression in earlier phases.

## 1. Phase overview

| Phase | Milestone | Theme | Primary risk |
| --- | --- | --- | --- |
| 0 | M0 | Alignment and decisions | Wrong assumptions about environment (verified) |
| 1 | M1 | Foundation: build, window, events, shutdown | SDL3 init/logging mistakes |
| 2 | M2 | Native UI: layout, panels, theme, font pipeline | Renderer/shaping integration |
| 3 | M3 | Text engine: buffer, cursor, undo, scrolling | Buffer correctness at scale |
| 4 | M4 | Editor experience: tabs, syntax, find, folding | Incremental highlighting complexity |
| 5 | M5 | Project management and search | Async filesystem/search races |
| 6 | M6 | Developer intelligence: Tree-sitter, LSP | LSP variability across servers |
| 7 | M7 | Developer tools: terminal, Git, DAP | PTY/process lifecycle |
| 8 | M8 | Polish, performance, portability prep | Premature optimization |

Phases 1–3 are the critical path to a "real editor". Phases 4–8 add the product.

---

## Phase 0 — Alignment (current)

**Goal:** agree on architecture, dependency policy, and performance targets before
writing substantial code.

**Scope**
- Inspect repository and environment — **done**; findings in `docs/ARCHITECTURE.md` §3–§4.
- Verify SDL3 version and API surface — **done** against installed headers and official docs.
- Propose architecture, text-buffer choice, rendering choice, targets — **done** in `ARCHITECTURE.md`.
- Approve or amend these documents — **done** on 2026-11-19; approved decisions are
  recorded as formal ADRs in `docs/architecture/`.

**Deliverables:** `docs/ARCHITECTURE.md`, `docs/ROADMAP.md`, `docs/architecture/ADR-000` … `ADR-004`.

**Exit criteria:** met — architecture approved and ADRs written; open items are tracked in
`ARCHITECTURE.md` §11.

**Depends on:** nothing. **Risks:** none outstanding.

---

## Phase 1 — Foundation (M1)

**Goal:** a native SDL3 application that starts, renders a frame, handles input, and
shuts down cleanly, built with CMake/Ninja/Clang.

**Scope**
- Add `CMakeLists.txt` with presets (`debug`, `release`, `bench`). The `Makefile` is
  removed; CMake/Ninja is the only supported build path.
- Create the directory skeleton from `ARCHITECTURE.md` §6 with placeholder libraries.
- Migrate compiler from GCC/C23 to Clang/C17; fix warning flags
  (`-Wall -Wextra -Wpedantic -Wconversion -Wshadow`, warnings as errors in CI).
- Replace hard-coded `-lSDL3 -lSDL3_ttf` with proper dependency discovery
  (`find_package(SDL3 CONFIG REQUIRED)`; `sdl3-ttf` only if actually used).
- Implement `core/`: error type, logging (levels, subsystem tags, timestamps), arena
  allocator, dynamic array/string helpers, MPSC queue + thread helpers, trace counters.
- Implement `platform/`: window creation via SDL3, `SDL_Init`/`SDL_Quit`, event loop,
  keyboard/mouse translation, custom events via `SDL_RegisterEvents`/`SDL_PushEvent`,
  path helpers, graceful shutdown including `SIGINT`/`SIGTERM`.
- Implement the smallest `graphics/` draw-list API and an SDL Render backend: clear,
  rectangles, one batched `SDL_RenderGeometry` quad (the smoke test).
- Add `tests/unit` harness and a `tests/benchmarks` skeleton.
- Log SDL3 version and chosen renderer at startup.

**Deliverables:** buildable binary, unit-test target, benchmark target, updated README
build instructions.

**Exit criteria**
- `cmake --preset debug && cmake --build --preset debug` succeeds with zero warnings.
- Binary opens a window on this Wayland session; resizes; closes via button, `Esc`,
  and `SIGTERM` with no leak reports under ASan/UBSan and no "SDL quit" errors.
- `SDL_GetError()`-based error paths are exercised by a negative test (e.g. invalid
  window hint) and logged.
- A batched quad renders; frame timer and version info are logged.
- Unit tests for arena, dynamic array, and queue pass under ASan/UBSan.

**Depends on:** Phase 0. **Risks:** Wayland vs X11 renderer driver selection — handle by
letting SDL choose and logging the result, not hard-coding a driver.

### Phase 1 status — implemented and verified

- `cmake --preset debug|release|bench && cmake --build --preset <name>` succeeds with
  zero warnings; all presets enable warnings as errors. Debug adds ASan/UBSan.
- `ctest --preset debug|release|bench` passes: unit tests for error/log, arena, array,
  MPSC queue (including a three-producer stress test), trace, draw list, and SDL input
  translation, plus an integration test that drives `Esc`, close-request, and resize
  events through the real application loop using headless SDL drivers.
- Runtime smoke test on this Wayland session: window created, `opengl` renderer selected
  by SDL, one batched `SDL_RenderGeometry` call per frame, SDL version and frame work
  time logged, and `SIGTERM` handled with exit status 0 and no ASan/UBSan reports.
- The `SDL_GetError()` capture path is exercised by a synthetic-error negative test.
- Remaining gap: the ADR-004 **full 200,000-glyph rendering benchmark** (fixed font,
  theme, viewport, CPU/GPU/frame-latency breakdown) is not implemented. Only the
  draw-list construction skeleton exists, and it is explicitly labelled as such.
- Not automated here: physical window-manager close button and real mouse input,
  because no input-injection tool is installed. Their SDL event translations are
  covered by unit tests and the close-event integration case.

---

## Phase 2 — Native UI and text foundation (M2)

**Goal:** a real UI skeleton with panels and correctly rendered, shaped text.

**Scope**
- `ui/`: widget base (opaque struct, `layout`/`event`/`paint`), constraint layout,
  dirty subtree propagation, focus/tab order, hit-testing, mouse hover/press.
- Panels: activity bar, sidebar, editor area, bottom panel, status bar — each
  show/hide-able. Splitter widget with drag resizing.
- Theme system: semantic colour tokens, metrics, light + dark defaults, JSON loading.
- `text/`: UTF-8 validation, ICU grapheme boundaries, encoding detection metadata.
- `graphics/`: glyph atlas over FreeType; HarfBuzz shaping with run cache; line cache;
  draw-list quads for glyphs (verify single batched draw call per frame region).
- Config: settings schema, defaults in `config/`, user override path, reload on change.
- Icons: minimal monochrome icon set (vector or generated atlas) via SDL3_image if
  needed — only if a compelling use exists.

**Deliverables:** UI demo screen with a sidebar, tab strip, status bar, and a static
multilingual code sample rendered with shaping/ligatures.

**Exit criteria**
- Text renders correctly for ASCII, accented Latin, CJK, and an Arabic phrase (bidi
  check) in the demo; ligature sample (`!=`, `=>`) shapes as a single glyph where the
  font supports it.
- Resizing the window relayouts panels without clipping or flicker; DPI/scale changes
  re-rasterize the atlas.
- Theme switch from dark to light updates all panels via tokens, no per-widget colour code.
- Frame timer shows a stable frame cost with 1,000 visible glyphs; no per-frame heap
  churn measurable by the arena reset counter.
- Unit tests: grapheme boundary cases, shaping cache keys, config parse errors.

**Depends on:** M1. **Risks:** HarfBuzz/FreeType/ICU integration order — implement ICU
first (cheap), then FreeType atlas, then HarfBuzz, testing each in isolation.

### Phase 2 status — complete

Vellum's licence was switched from MIT to GPL-2.0-only before implementation began;
third-party components keep their own terms.

Implemented increments, each built and tested before proceeding:

- `vellum_text` provides ICU-backed strict UTF-8 validation with byte offset and
  diagnostic status, a `UBRK_CHARACTER` iterator over UTF-8 `UText`, and a first-strong
  base-direction helper for selecting a HarfBuzz run direction.
- Unit tests cover ASCII, combining Latin, CJK, Devanagari (documenting ICU's current
  default rules), an emoji ZWJ sequence, a regional-indicator flag, embedded NUL,
  malformed encodings, invalid arguments, and the ICU `int32` length guard.
- The FreeType glyph atlas is implemented in `vellum_graphics`: face loading, charmap
  lookup, an RGBA atlas page with coverage in alpha, a hash-based glyph cache,
  dirty tracking, and face metrics. Unit tests load all three bundled fonts and assert
  Latin, CJK, and Arabic glyph coverage.
- HarfBuzz run shaping is implemented with per-codepoint font fallback, `calt`/`liga`
  features, UTF-8 cluster byte offsets, a shaped-run cache verified byte-for-byte, and
  explicit synchronization of the shared FreeType face size. Probe output with the
  pinned fonts shows 1 glyph for `A`, the spacer+ligature pair for `!=` and `=>`, 2
  glyphs for `中文`, and 6 visual-order glyphs for `مرحبا`. JetBrains Mono emits a
  spacer glyph followed by the ligature rather than one collapsed glyph, and the unit
  test documents that observed behaviour.
- The draw list carries a `VellumTextureHandle` per textured quad; zero selects the
  built-in white texture. The renderer owns stable, never-reused handles for RGBA
  textures and groups geometry into one SDL geometry call per consecutive texture run.
  An integration test creates, updates, and destroys a texture, submits white and
  custom-textured quads, and verifies that unknown and destroyed handles are rejected.
- The app loads the three bundled faces, uploads the RGBA atlas into a renderer texture,
  rasterizes a small Latin/CJK/Arabic glyph set, and re-uploads only when the atlas is
  dirty. The demo scene now emits glyph quads from that texture.
- Pinned OFL-1.1 fonts and their notices are under `assets/fonts/`; the choice is
  recorded in [ADR-005](architecture/ADR-005-bundled-multilingual-fonts.md). The system
  text libraries and their licences are recorded in
  [ADR-006](architecture/ADR-006-icu-freetype-harfbuzz-text-pipeline.md).
- yyjson 0.13.0 is vendored under `third_party/yyjson/` with the release archive SHA-256
  and MIT notice recorded; the pin and parser choice are in
  [ADR-001](architecture/ADR-001-json-parsing-yyjson.md) and `ARCHITECTURE.md` §4. The
  vendored source is a separate CMake target so Vellum's `-Werror` flags do not apply.
- `vellum_config` wraps yyjson with typed settings (`theme`, `font_size`, panel visibility)
  and semantic light/dark theme tokens. It enforces a 1 MiB read bound, strict
  unknown/duplicate-key checks, and path-qualified parse errors; settings and both theme
  defaults ship in `config/`. Unit tests cover defaults, shipped defaults, save/load
  round-trip, malformed JSON, unknown and duplicate keys, wrong types, and out-of-range
  values. The app loads settings/themes with compiled-in fallbacks, drives every scene
  colour from theme tokens, and switches themes with `T` via the input event path.
- User overrides are composed under the SDL preferences directory and layered over the
  shipped defaults: settings overlay key-by-key, while a user theme fully replaces its
  shipped counterpart. A malformed override is reported and ignored, keeping the last
  valid values. `VellumFileWatcher` polls file content hashes on one fixed worker and
  posts an app event on change; the main thread reloads config, and subsequent layout
  uses the new font size. `T` persists the theme choice to the user settings file.
  Unit tests cover path composition and layering; an integration test verifies that a
  file rewrite produces an app event, and the app-events test covers the reload message.
- Debug, release, and bench presets build and pass `ctest` (unit, app events, renderer
  textures, file watcher); debug runs under ASan/UBSan, and `clang-format --dry-run
  --Werror` is clean.

- Single-line layout is implemented in `vellum_graphics`: `vellum_text_layout_line()`
  shapes a paragraph, resolves each glyph's face through the shaper's recorded
  `face_id`, and appends positioned atlas quads. HarfBuzz 15 reports visual-order RTL
  glyphs with positive advances, so RTL lines are placed from the right edge of the
  text box; ICU first-strong detection selects the direction automatically. Unit tests
  cover LTR order, RTL order and right alignment, whitespace, mixed-script face
  fallback, empty input, and invalid arguments.
- The app owns a `VellumShaper` and renders a static multilingual demo screen: title,
  subtitle, Latin (including ligature samples), CJK, Arabic, mixed Latin/CJK fallback,
  and a hint line, all coloured through theme tokens. Layout runs before the dirty-atlas
  upload so newly rasterized glyphs reach the GPU in the same frame. Debug, release,
  and bench builds plus CTest, `clang-format --dry-run --Werror`, and a real-window
  smoke test pass with the demo active.

- The retained widget tree (`src/ui/widget.c`) provides opaque widgets with measure,
  layout, paint, and event callbacks; constraint-based measurement; recursive
  layout/paint with dirty propagation; visibility; focus/tab order; hover; hit-testing;
  and pointer capture so a pressed widget receives motion until release. Unit tests
  cover all of these, including destroying a focused/hovered widget and paint failures.
- `src/ui/panels.c` builds the activity bar, sidebar, splitter, tab strip, editor area,
  and status bar, paints them from semantic theme tokens, and implements splitter
  drag/clamp/keyboard resize, activity-bar sidebar toggle, and tab switching. The app
  owns a `VellumUiState`, dispatches input into the root, mirrors settings visibility,
  relayouts on resize/config changes, and positions the multilingual demo text inside
  the editor panel. Unit tests cover exact geometry at 1000x700, visibility toggles,
  splitter clamping to `VELLUM_UI_SIDEBAR_MIN`/`MAX`, tab switching via paint colours,
  the activity toggle, and paint error handling.

- The bounded line cache (`src/graphics/line_cache.c`) stores fully laid-out
  single-line quads keyed by text bytes, pixel size, effective direction, box width,
  and face count. Repeated frames reuse geometry; the benchmark reaches 1,360 hits
  and 40 misses (the 40 distinct lines) with zero evictions.
- DPI/scale changes are handled end to end: SDL's
  `SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED` becomes `VELLUM_INPUT_SCALE_CHANGED`, the
  app re-queries `SDL_GetWindowDisplayScale()` (SDL sends no value), recomputes the
  effective pixel size, and re-rasterizes glyphs. A real-window smoke test showed
  scale 1.00 → 2.00, pixel size 14 → 28, and atlas upload #1 with 86 rasterizations
  before the first frame. `vellum_app_set_display_scale()` covers tests/settings, and
  the pixel size is recomputed on settings reload.

#### Phase 2 exit evidence

All Phase 2 exit criteria are met:

- **Text and bidi:** the demo renders Latin (including `!=`/`=>` ligature samples),
  CJK, an Arabic phrase laid out right-to-left from the box's right edge, and
  mixed-script fallback. Font tests assert glyph coverage for accented Latin, CJK,
  and Arabic with all three pinned fonts; layout tests cover LTR/RTL order and
  right alignment, whitespace, fallback, empty input, and invalid arguments.
- **Resize and scale:** resizing relayouts the retained widget tree (unit tests check
  exact geometry at 1000x700 and splitter clamping); the scale evidence above shows
  atlas re-rasterization rather than stale glyph geometry.
- **Theme:** dark/light switching uses semantic tokens only, persists to user
  settings, and reloads live from the watched override files.
- **Frame cost and churn:** the release text-frame benchmark runs the real pipeline
  over 1,000 glyphs (800 textured quads, 4 rects, 2 SDL geometry calls). Across three
  runs, p50 is 0.697/0.744/1.079 ms, p95 is 3.468/9.623/5.048 ms, and the mean is
  1.119/2.216/1.416 ms — within the ≤ 4 ms UI + text budget. Arena usage is stable:
  35 resets for 35 rendered frames (one per frame), 1,048,576 B capacity, 1,040 B
  peak used, 40 arena allocations/frame, so no per-frame heap churn is measurable.
  The 200,000-glyph stress scene (200,000 glyphs, 172,560 quads, 2 geometry calls)
  averages 22.9–24.5 ms; per ADR-004 the profile-first rule was applied: ~27% arena
  + layout, ~73% render submit, no cache evictions and one atlas upload, i.e. CPU
  geometry/overdraw rather than batching or churn. **No GPU backend change is
  proposed.** Full configuration, phase breakdown, and distributions are recorded in
  [`docs/benchmarks/2026-11-19-text-frame.md`](benchmarks/2026-11-19-text-frame.md).
- **Unit tests:** ICU grapheme boundary cases, shaping cache keys, config parse
  errors, line-cache keys/eviction, scale math and event translation, plus the
  renderer-texture and file-watcher integration tests. `ctest` passes 4/4 in debug,
  release, and bench builds; debug is clean under ASan/UBSan and
  `clang-format --dry-run --Werror` is clean.

---

## Phase 3 — Text engine (M3)

**Goal:** a correct, fast, standalone editing buffer with no UI dependency.

**Scope**
- `text/buffer.c`: piece table + balanced tree (AVL or weight-balanced), split/insert/
  delete, cached subtree lengths, position↔piece conversion, iterators.
- Append-only storage chunks; read-only `mmap` of the original file; large-file path.
- Line index: two-level newline-offset index, lazy background build, incremental
  update near edits, CRLF/LF metadata.
- Command system: insert/delete/replace commands with inverse records; coalescing;
  undo/redo stack with bounded memory; transaction grouping.
- Cursor/selection primitives: grapheme-aware movement, word/lines, selection,
  multiple cursors as a set of positions with edit remapping.
- Editing operations: tab/indent/outdent, smart backspace, bracket completion,
  auto-indent (language-agnostic defaults in this phase).
- Clipboard via SDL clipboard; drag-and-drop text within the editor.
- Scrolling model integrated with the line index and view rect.
- Benchmarks: 100 MB file open; 1M-line typing; random-edit fuzz against a naive
  reference model.

**Deliverables:** buffer library + test suite + benchmark report committed under
`docs/benchmarks/` (numbers, not claims).

**Exit criteria**
- Randomized differential fuzz (10k+ operations) matches a simple string model byte-for-byte.
- Undo/redo restores exact state, including after coalesced typing and multi-edit groups.
- 100 MB file opens under the §10 target; editing near the end of a 1M-line file stays
  under the latency target in the benchmark.
- Grapheme tests: cursor never lands inside a combining sequence or emoji ZWJ sequence.
- No memory growth across 10k edit/undo cycles (ASan/UBSan + RSS check).

**Depends on:** M1 (core), M2 (grapheme/layout integration for the view). **Risks:** tree
balancing bugs — prefer a well-known AVL implementation with heavy unit tests over an
exotic structure.

### Phase 3 status — in progress

The standalone buffer core, line index, command/undo stack, and cursor/selection
layer are implemented and verified as the first Phase 3 increments:

- `vellum_text` now exposes a byte-oriented piece-table API in
  `include/vellum/text/buffer.h`: create/destroy, length and piece count, `read`,
  `insert`, `delete`, `snapshot`, and a diagnostic `validate`.
- `src/text/buffer.c` implements an AVL-indexed piece tree with cached subtree byte
  lengths, append-only insertion storage, OOM-safe node reservation before mutation,
  split/join primitives that restore the AVL invariant even for large subtree-height
  differences, and recursive validation of balance and piece ranges.
- Unit tests in `tests/unit/test_buffer.c` cover empty/invalid arguments, boundary
  reads, binary and UTF-8 byte offsets, 3,000 concentrated edits while checking AVL
  balance after every operation, and a deterministic 10,000-operation differential
  fuzz against a naive `memmove` model. Every operation checks length and tree
  validity; snapshots are compared byte-for-byte. The same suite was additionally
  run at 1,000,000 operations under ASan/UBSan as a stress check.
- `vellum_text` also exposes a line index in `include/vellum/text/line_index.h`:
  rebuild, incremental insert/delete notification, line-start lookup, offset→line
  lookup, per-line terminator and content-end queries, dominant ending, and a
  diagnostic validator.
- `src/text/line_index.c` uses a flat sorted array of line-start byte offsets with
  binary search. The two-level array from the design remains a large-file
  optimisation, and the lazy I/O-worker build remains deferred until file loading
  exists. LF, CRLF, and standalone CR are recognised; terminator style is derived
  on demand from the buffer, so edits cannot leave stale ending metadata. Incremental
  updates rescan only the affected span and shift the untouched tail.
- Unit tests in `tests/unit/test_line_index.c` cover empty and unterminated buffers,
  LF/CRLF/CR and mixed endings, offset→line boundaries, terminator and content-end
  queries, dominant ending, inserts and deletes at the start/middle/end, CRLF boundary
  transitions, and a deterministic 10,000-operation differential fuzz against a fresh
  full rebuild after every edit. The same suite was additionally run at 1,000,000
  operations under ASan/UBSan as a stress check.
- `vellum_text` also exposes `include/vellum/text/command.h`, an inverse-operation
  command stack over the buffer and optional line index. Inserts, deletes, and
  replacements record both forward and inverse payloads before mutating the buffer;
  undo/redo replay them in the correct order. Non-nested transactions group several
  edits into one undo entry, and a byte budget discards the oldest undo entries while
  keeping stack order intact. Adjacent standalone inserts coalesce into one undo entry,
  so a run of typing is undone at once; deletes, replacements, undo/redo, and
  transactions break the run.
- `src/text/command.c` applies each edit as delete-then-insert, notifies the line
  index after each step, and attempts to restore deleted bytes if a later insert
  allocation fails. Undo/redo and transaction abort apply inverse operations in
  reverse order. A diagnostic validator checks the buffer, line index, edit payloads,
  and memory accounting.
- Unit tests in `tests/unit/test_command.c` cover insert/delete/replace undo and redo,
  transaction commit/abort, bounded-memory eviction, adjacent-insert coalescing (with
  and without newlines), and a 10,000-operation differential fuzz against a naive
  string model with its own undo/redo history. The same suite was additionally run at
  200,000 operations under ASan/UBSan as a stress check.
- `vellum_text` also exposes `include/vellum/text/cursor.h`, a single caret and
  selection over the buffer and line index. All offsets are UTF-8 bytes on extended
  grapheme-cluster boundaries. Movement covers grapheme-aware left/right, logical-line
  up/down (with a preserved desired column), line/buffer start and end, and selection
  extension. Insert/delete notifications remap the anchor and head around edits. A
  per-line ICU grapheme scan falls back to byte boundaries on invalid UTF-8. Visual
  (wrapped) line movement is deferred to the layout-aware view.
- `src/text/cursor.c` reuses scratch storage for one line's bytes and boundaries,
  snapping set/select offsets and post-edit positions to grapheme boundaries. Vertical
  movement stops before line terminators. A diagnostic validator checks both offsets
  are on real grapheme boundaries.
- Unit tests in `tests/unit/test_cursor.c` cover empty buffers, clamping, combining
  marks, an emoji ZWJ sequence, LF/CRLF/CR line crossing, sticky-column vertical
  movement, selection extension, edit remapping, invalid UTF-8 fallback, and a
  10,000-operation differential fuzz against a contiguous-text model with a naive line
  scanner. The same suite was additionally run at 200,000 operations under ASan/UBSan
  as a stress check.
- `vellum_text` also exposes `include/vellum/text/edit.h`, a thin coordination layer
  over the buffer, line index, command stack, and cursor. It adds insertion/replacement
  of the selection, grapheme-aware backspace/delete (including smart outdent of one
  indent unit inside leading whitespace), newline with auto-indent copied from the
  current line, and indent/outdent of every logical line touched by the selection.
  Every operation goes through the command stack and notifies the cursor, so each call
  is one undo entry; undo/redo wrappers clamp and re-snap the caret. The indent unit is
  configurable (default four spaces). Drag-and-drop, applying a single edit at all carets,
  and wrapped/visual-line movement remain deferred.
- Bracket completion is built into `vellum_text` with the `VellumBracketPair` type and
  `vellum_bracket_defaults` in `include/vellum/text/edit.h`. `vellum_editor_set_bracket_pairs`
  configures up to `VELLUM_BRACKET_MAX_PAIRS` single-byte pairs (default `()`, `[]`, `{}`,
  `""`, `''`); `vellum_editor_type_char` auto-closes an opening byte as one undo entry,
  steps over an auto-inserted closer on type-over, and wraps a selection in one transaction
  while reselecting the inner text. Backspace first removes an empty adjacent pair before
  its normal grapheme/outdent logic. Configuration is language-agnostic; language-specific
  rules remain deferred.
- `src/text/edit.c` groups multi-edit calls in transactions so a failed operation
  aborts cleanly, and deletes the preceding line break when backspacing at a line start
  rather than treating it as a zero-width outdent.
- Unit tests in `tests/unit/test_edit.c` cover insertion/replacement with undo/redo,
  deleting combining marks and emoji ZWJ clusters, smart backspace (including at line
  starts and on empty lines), newline auto-indent, single- and multi-line indent/outdent,
  indent-unit configuration, NULL handling, and a 10,000-operation differential fuzz
  against a contiguous-string model. The same fuzz was additionally run at 200,000
  operations under ASan/UBSan as a stress check. Bracket tests cover default and custom
  pair configuration, auto-close/type-over with undo/redo, symmetric quotes, nesting,
  selection wrapping with inner reselection, empty-pair backspace (including inside
  leading whitespace), and a 5,000-operation differential fuzz against a naive ASCII model.
- Clipboard support is split across two layers. `vellum_text` adds SDL-free
  `vellum_editor_copy`, `vellum_editor_cut`, and `vellum_editor_paste`: copy reads the
  selected byte range into a caller-owned NUL-terminated allocation (reporting the exact
  byte length, including embedded NULs), cut copies and deletes the selection as one
  undo entry, and paste replaces the selection or inserts at the caret. `vellum_platform`
  adds `include/vellum/platform/clipboard.h`, a thin main-thread wrapper over
  `SDL_SetClipboardText`/`SDL_GetClipboardText`/`SDL_HasClipboardText` that returns a
  malloc'd UTF-8 copy. SDL3 3.4.20 already provides these APIs, so no new dependency is
  introduced. App-level keybindings and editor-state ownership are deferred to the
  editor-experience phase.
- Unit tests in `tests/unit/test_edit.c` cover copy/cut/paste, collapsed-selection
  rejection, embedded NULs, and undo of cuts. `tests/unit/test_clipboard.c` exercises the
  SDL wrapper headlessly with the dummy video driver (ASCII and UTF-8 roundtrips, empty
  and cleared clipboard).
- A layout-independent scrolling model lives in `include/vellum/text/scroll.h` and
  `src/text/scroll.c` inside the SDL-free `vellum_text` library. It borrows the buffer
  and line index, derives vertical content extent from `line_count * line_height`, and
  takes the horizontal content width from the caller (typically layout metrics). It
  clamps scroll offsets to the view, maps scroll position to visible logical lines and
  byte ranges, and supports minimal scroll-to-line/offset. No SDL/FreeType dependency is
  added; the view rect's origin and draw-list clipping stay in the UI layer. Wrapped
  (visual) line movement and scrollbar widgets remain deferred.
- Unit tests in `tests/unit/test_scroll.c` cover extents and clamping in both axes,
  visible-line and byte-range mapping (including empty documents and unaligned views),
  scroll-to-line/offset, horizontal ensure-visible, re-clamping after an edit shrinks the
  line count, and an invariant sweep across view heights and scroll offsets.
- Multiple cursors are provided by `include/vellum/text/cursor_set.h` and
  `src/text/cursor_set.c` in the SDL-free `vellum_text` library. `VellumCursorSet`
  owns one or more `VellumCursor` instances over a shared buffer and line index,
  always keeps a primary caret, and reuses the single-cursor logic unchanged. It
  supports adding collapsed or selected carets (ignoring overlapping duplicates),
  removal and primary-index adjustment, clearing back to one caret, moving or
  collapsing every caret, and remapping every caret on insert/delete. Applying a
  single edit simultaneously at all carets stays in the edit/command-transaction
  layer (see Phase 4); this increment supplies the set primitive and its remapping.
- Unit tests in `tests/unit/test_cursor_set.c` cover creation, add/dedupe, grapheme
  snapping, primary selection and removal, clear, all-caret movement and extension,
  insert/delete remapping, NULL handling, and a structural-plus-edit stress sweep.
- Scale benchmarks in `tests/benchmarks/bench_text_buffer.c` cover the Phase 3 exit
  targets: a 100 MB file open (165–168 ms, well under the 1 s §10 target) and 1M-line
  end typing (24–27 µs p95, under the 16 ms §10 target). A 10,000-operation random-edit
  stress workload is also measured (~1.5 ms median, ~3.3 ms p99); it exceeds the generic
  < 1 ms p99 edit target because the flat line-index array shifts up to 8 MB on a middle
  line insert/delete. The two-level index remains deferred. Full numbers and verdicts
  are in [`benchmarks/2026-11-19-text-buffer-scale.md`](benchmarks/2026-11-19-text-buffer-scale.md).

Remaining Phase 3 increments (drag-and-drop and scrollbar rendering) are not yet
implemented; the phase exit criteria are therefore not yet met.

---

## Phase 4 — Editor experience (M4)

**Goal:** turn the engine into a usable editor.

**Scope**
- Documents, views, split views, tabs (open/close/dirty indicator/reopen closed tab).
- Line numbers, current-line highlight, whitespace visualization, word wrap,
  configurable tab size, bracket matching/pair highlighting, code folding.
- Find/replace in file: plain, regex (ICU regex or small C engine), case options,
  match navigation, selection-scoped search.
- Minimap (optional/configurable) and scrollbar/overview.
- Multiple cursors / multi-selection editing wired to command transactions.
- File encoding detection and CRLF/LF round-trip preservation end-to-end.
- Basic language-agnostic indentation and bracket rules.

**Deliverables:** a daily-usable text editor for code and plain text.

**Exit criteria**
- All editor features in §A of the brief that are listed above are demonstrable in
  integration tests or scripted smoke tests (synthetic input events).
- Split views edit the same document consistently; both cursors and dirty state sync.
- Regex replace with capture groups works; invalid regex reports a clear error.
- A file with mixed CRLF/LF and a BOM round-trips without changing untouched bytes.
- Editor latency targets from §10 met in the benchmark scenario.

**Depends on:** M2, M3. **Risks:** regex engine choice — evaluate ICU regex vs a small
C library; do not pull in a large dependency for this alone.

---

## Phase 5 — Project management and search (M5)

**Goal:** work on projects, not just files.

**Scope**
- Workspace model: open folder, recent projects, multi-root later if needed.
- File explorer: tree model, expand/collapse, hidden-file toggle, filters, context
  menus, create/rename/delete/move with confirmation and error handling.
- Async directory enumeration + file watches; results marshalled to the main thread.
- Workspace search: `rg` driver, streaming results, regex/exclusion patterns,
  Git-aware ignore handling, result navigation; replace across workspace with preview.
- Drag-and-drop of files into the editor; open-from-command-line and from file manager.

**Deliverables:** folder-based editing workflow with project search.

**Exit criteria**
- Opening a folder with 50,000 files keeps the UI responsive (no frame > 16.6 ms in
  trace log) and populates the tree incrementally.
- File operations succeed on happy path and report errors on permission/name conflicts
  without corrupting the tree model.
- Workspace search returns results in under 1 s for a 100 MB source tree and cancellation
  stops the `rg` process promptly.
- Replacing across workspace requires an explicit confirmation and is undoable per file.

**Depends on:** M4. **Risks:** watcher API differences — abstract behind `filesystem/`
  and allow polling fallback.

---

## Phase 6 — Developer intelligence (M6)

**Goal:** syntax and language intelligence, with graceful degradation.

**Scope**
- Tree-sitter: vendor the runtime at the pinned revision and add pinned grammars
  (C, C++, JSON, Markdown, Python, Rust, Go, shell to start) per
  [ADR-003](architecture/ADR-003-tree-sitter-vendored-and-pinned.md); incremental parse
  tied to buffer edits on a worker; highlight query engine producing style spans;
  indentation/folding queries where available; fuzz malformed/incomplete input.
- LSP client: JSON-RPC over stdio, transport, initialize/shutdown handshake, capability
  negotiation, document sync (incremental where supported), per-server process lifecycle.
- Features: completion, diagnostics, go-to-definition/declaration, find references,
  hover, rename, code actions, signature help, document/workspace symbols, semantic
  tokens merged with Tree-sitter styles.
- UI: completion popup, diagnostic list/squiggles, hover card, signature help, symbol
  palette, code-action lightbulb.
- Server registry/config (per-language command and settings).
- Ensure the editor remains fully functional with no servers installed.

**Deliverables:** working highlighting with zero LSP; optional LSP for configured languages.

**Exit criteria**
- Disabling all language servers still yields correct Tree-sitter highlighting and folding.
- With a server installed (e.g. `clangd`), completion, diagnostics, go-to-definition,
  rename, and hover all work in an integration test.
- A stale LSP response after fast typing is discarded (version gating); no crashes on
  malformed JSON or server exit.
- Incremental re-highlight after a one-line edit stays under the §10 target.

**Depends on:** M4, M5, ADR-001 (yyjson) and ADR-003 (vendored Tree-sitter). **Risks:** LSP
server quirks — build a conformance test harness against one well-behaved server first
and treat other servers as compatibility work, not design drivers.

---

## Phase 7 — Developer tools (M7)

**Goal:** terminal, Git, and debugging in one native window.

**Scope**
- Terminal: POSIX PTY (`forkpty`), shell selection, multiple sessions/tabs, emulation
  per [ADR-002](architecture/ADR-002-terminal-emulation-libtsm-first.md) — run the
  libtsm evaluation gate first, pin the revision on adoption, otherwise use the scoped
  in-house state machine — scrollback, text selection, clipboard, working-directory
  awareness (follow active file's project/root), resize propagation. Keep parsing,
  screen state, PTY management, and rendering separate.
- Git: repository detection, status indicators in explorer and tabs, stage/unstage,
  commit, branch switching, diff view, change navigation, history log — all via `git` CLI.
- Build/tasks: run configured commands, capture output into the bottom panel, map
  compiler errors to diagnostics where formats are known.
- DAP: client transport, breakpoint management, launch/attach configs, continue/step,
  stack, variables, watches, debug console; one adapter to start (e.g. `lldb-dap` or
  `debugpy` depending on availability).
- Bottom panel tabbed between Terminal / Diagnostics / Output / Debug Console.

**Deliverables:** end-to-end edit → build → debug loop.

**Exit criteria**
- Interactive shell runs in a PTY: `vim` and `htop` render correctly; resize works;
  copy/paste works; closing a terminal tab kills its child process.
- Git status updates after file changes; stage/commit/branch operations work in a
  scratch repository test; errors from `git` are surfaced, not swallowed.
- A breakpoint is hit; step over/into/out and variable inspection work with the chosen
  adapter.
- All child processes are reaped on shutdown; no zombies under ASan/UBSan test run.

**Depends on:** M5, M6. **Risks:** terminal emulation is a large subtree — constrain
scope to a documented xterm subset and add capabilities only when a real program needs
them.

---

## Phase 8 — Polish, performance, portability prep (M8)

**Goal:** make Vellum fast, accessible, configurable, and ready for other platforms.

**Scope**
- Profiling with `perf` and trace counters; address real bottlenecks only.
- Run the repeatable rendering benchmark defined in
  [ADR-004](architecture/ADR-004-rendering-sdl3-first-gpu-conditional.md): 200,000
  visible glyphs, fixed viewport/font/theme, release build, recorded hardware and
  driver, CPU/GPU/frame-latency measurements. At ~8 ms/frame, profile first; add a GPU
  backend behind the draw-list interface **only if justified** by that evidence.
- Memory optimization: arenas, atlas eviction, line-index compaction, lazy structure
  loading.
- Accessibility: keyboard traversal of all panels, focus indicators, configurable
  contrast, screen-reader-friendly labels where feasible.
- Customization: keybinding editor, theme editor, layout persistence, UI scale.
- Packaging: Linux AppImage/Flatpak or tarball with documented dependencies; CI with
  sanitizers, tests, and format checks.
- Cross-platform review: isolate remaining POSIX assumptions in `platform/`; write a
  Windows/macOS porting checklist. Actual ports are **not** in this phase.

**Deliverables:** release build, packaging artifacts, benchmark report, porting checklist.

**Exit criteria**
- All §10 targets measured and either met or explicitly revised with evidence.
- Cold start, 100 MB file open, and idle CPU benchmarks recorded in `docs/benchmarks/`.
- Full keyboard-only navigation of core workflows verified.
- CI green on clang-format, warnings, tests, ASan/UBSan.

**Depends on:** M1–M7. **Risks:** scope creep — performance work must be driven by
measured data, not intuition.

---

## 2. Cross-cutting tracks (run continuously)

- **Testing:** unit tests for every pure algorithm; integration tests driving synthetic
  SDL events; fuzz tests for buffer, JSON, LSP messages, and VT parser. Sanitizers in
  debug CI.
- **Documentation:** keep `ARCHITECTURE.md`, `ROADMAP.md`, and per-module README files in
  sync with code. Formal ADRs live in `docs/architecture/`; a changed decision requires a
  new superseding ADR, never a silent edit.
- **Performance:** trace counters and benchmark scenarios land in Phase 1, before there
  is anything worth optimizing. Every performance claim must link to a benchmark file.
- **Dependencies:** every new dependency requires an ADR with justification, alternatives,
  consequences, and a licence check. Prefer vendoring small C libraries with pinned
  commits over unpinned system packages.

## 3. Deliberate deferrals

To keep the project honest, these are explicitly **not** early work:

- CRDT / collaborative editing (would change the buffer to a persistent rope/CRDT).
- GPU rasterization of text (until the Render backend is proven insufficient).
- A plugin runtime or embedded scripting language (architecture must allow it; do not
  build it prematurely).
- Windows/macOS ports, mobile, web targets.
- Git implemented in-process, custom regex/search engine replacing `rg`, or a
  browser-based terminal.

## 4. Immediate next action

Phase 1 and Phase 2 are complete and verified. Phase 2 delivered ICU
validation/graphemes, the FreeType atlas, HarfBuzz shaping, renderer textures and
atlas upload, single-line text layout, the line cache, DPI/scale re-rasterization,
the multilingual demo screen, the retained widget tree and panels, typed
config/themes, user overrides with live reload, and the text-frame benchmark with
recorded exit evidence (see above). Phase 3 has advanced with the standalone buffer
core, line index, and command stack: a byte-oriented piece table indexed by an AVL
tree, OOM-safe split/insert/delete, a flat-array line index with incremental updates,
inverse-operation undo/redo with transactions, bounded memory, adjacent-insert typing
coalescing, grapheme-aware cursor/selection with edit remapping, editing operations
(insert/replace, grapheme-aware delete, smart backspace, newline auto-indent, and
indent/outdent), SDL-backed clipboard support via a text-level copy/cut/paste API and a
platform clipboard wrapper, a layout-independent scrolling model over the line index,
multiple cursors as a remapped caret set, configurable bracket completion (auto-close,
type-over, selection wrapping, empty-pair backspace), and scale benchmarks with recorded
exit evidence (100 MB open and 1M-line end typing both pass), plus differential fuzz tests.
The next actions are drag-and-drop text movement and scrollbar rendering, keeping all
earlier builds, tests, and benchmarks green.
