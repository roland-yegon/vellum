# Vellum — Development Roadmap

> **Status:** Approved baseline. Companion to `docs/ARCHITECTURE.md`. Written
> 2026-11-19 after inspecting the repository and verifying the installed toolchain;
> Phase 1 is complete and Phase 2 is in progress.
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

### Phase 2 status — in progress

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
- Debug, release, and bench presets build and pass `ctest` (unit, app events, renderer
  textures); debug runs under ASan/UBSan, and `clang-format --dry-run --Werror` is clean.

Remaining Phase 2 work: user override files and reload-on-change, widgets/panels, and
the multilingual demo screen.

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

Phase 1 is complete and verified. In Phase 2, ICU validation/graphemes, the FreeType
atlas, HarfBuzz shaping, renderer texture support, atlas upload, and typed config/themes
are complete and tested. Next add user override files and reload-on-change, widgets/panels,
and the multilingual demo screen.
