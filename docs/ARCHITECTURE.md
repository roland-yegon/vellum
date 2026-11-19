# Vellum — Architecture

> **Status:** Approved baseline with amendments recorded on 2026-11-19. Written after
> repository and environment inspection; architectural decisions are now approved.
>
> This document describes the *intended* architecture. Where the current repository
> differs, the difference is called out explicitly in §3 and §11.
>
> **Formal decisions are canonical in [`architecture/`](architecture/README.md).**
> Where this document summarizes a decision, the corresponding ADR takes precedence.

---

## 1. Purpose and scope

Vellum is a native code editor written primarily in C17, with SDL3 as the platform
foundation. This document defines the module boundaries, the key technical decisions,
and the rules that keep the codebase maintainable as features are added.

Non-goals for this document: a complete API reference, a feature checklist, or a
commit-by-commit schedule. Those belong in `ROADMAP.md` and later per-module docs.

## 2. Guiding principles

1. **Native first.** No embedded browser, no web UI stack, no scripting runtime as the
   application language. SDL3 owns the window, input, and event loop.
2. **The UI thread never blocks.** Anything that can take unpredictable time (file I/O,
   process spawn, LSP round-trips, syntax parsing of huge files, Git, search) runs off
   the main thread and delivers results through queues.
3. **Small, explicit interfaces.** Modules expose opaque structs and functions. No
   hidden global mutable state; dependencies are passed in at construction.
4. **Measure, then optimize.** Performance targets are stated in §10 and verified with
   benchmarks and profilers. We do not add complexity on speculation.
5. **Incremental delivery.** Each phase must leave the application buildable and
   testable. See `ROADMAP.md`.

## 3. Repository state at time of writing (findings)

This is a **new project**, not an existing implementation.

- `git log`: two commits (`Initial commit`, `Makefile: debug/release builds...`).
- Tracked files: `README.md`, `LICENSE`, `Makefile`, `.gitignore`, `.clang-format`,
  `src/main.c`.
- `src/main.c` is a 7-line `printf("Hello world")`. There is no SDL3 code yet.
- Uncommitted local changes exist and were **left untouched**:
  - `Makefile`: `LDLIBS` gained `-lSDL3 -lSDL3_ttf`.
  - `src/main.c`: formatting cleanup (`main(void)`, brace style).
- `build/debug/` contains generated artifacts only.
- Missing: `CMakeLists.txt`, `include/`, `docs/`, `tests/`, `assets/`, `config/`,
  all subsystem directories.
- `README.md` states the product vision but contains no design detail.

**Conclusion:** we are designing greenfield. The only work to preserve is the
Makefile's debug/release/sanitizer setup and the trivial `main.c`.

## 4. Verified toolchain and dependencies

Everything below was checked on this machine, not assumed.

| Component | Version / detail | Notes |
| --- | --- | --- |
| Clang | 23.1.5 | Primary compiler; ASan/UBSan verified working |
| GCC | 16.2.1 | Available; current Makefile uses this |
| CMake | 4.5.0 | Target build system |
| Ninja | 1.13.2 | Preferred generator |
| pkg-config | 3.0.11 | Module name is **`sdl3`** (lowercase), not `SDL3` |
| Git | 2.56.1 | Also used as an external tool by the Git module |
| clang-format | 23.1.5 | Matches `.clang-format` (LLVM, 4 spaces, 100 cols) |
| ripgrep (`rg`) | installed | Candidate for workspace search |
| SDL3 | 3.4.20 | Headers in `/usr/include/SDL3` (86 headers); CMake config `/usr/lib/cmake/SDL3/SDL3Config.cmake` |
| SDL3_ttf | 3.2.2 | pkg-config name `sdl3-ttf`; CMake config present |
| SDL3_image | 3.4.8 | Optional (icons/images) |
| SDL3_mixer | 3.2.4 | Not needed initially |
| FreeType | installed (`freetype2`) | Glyph rasterization |
| HarfBuzz | 15.1.0 | Text shaping |
| ICU | 78.3 (`icu-uc`, `icu-i18n`) | Unicode properties, grapheme clusters, bidi |
| Tree-sitter | **not installed** | Must be added later (vendored or packaged) |
| Vulkan loader | 1.4.363 present | SDL3 GPU "vulkan" backend is available on this host |
| Session | Wayland (`wayland-1`), `DISPLAY=:1` | Both X11 and Wayland paths available |
| Valgrind | not installed | Use ASan/UBSan instead |

SDL3 GPU API entry points were verified against the installed `SDL_gpu.h` and the
official SDL wiki; the Render API's `SDL_RenderGeometry` was likewise verified
(present since SDL 3.2.0). No API is used in the design that was not checked.

## 5. System overview

```
 ┌───────────────────────────────────────────────────────────────────────┐
 │                          Application / main                          │
 │              lifecycle, event loop, dependency injection              │
 ├──────────────┬──────────────────────────────┬─────────────────────────┤
 │  UI layer    │        Editor layer           │      Services           │
 │  windows     │  buffers, views, cursors      │  LSP, Tree-sitter,      │
 │  panels      │  selections, commands         │  search, Git, DAP,      │
 │  widgets     │  undo, find/replace           │  terminal, filesystem   │
 │  layout      │                               │                         │
 ├──────────────┴──────────────┬────────────────┴─────────────────────────┤
 │  Text engine                │  Graphics / text pipeline               │
 │  piece table, line index,   │  draw list, glyph atlas, shaping,        │
 │  encoding, graphemes        │  SDL3 render backend                     │
 ├─────────────────────────────┴─────────────────────────────────────────┤
 │  Core / platform: SDL3 windowing & events, logging, errors,           │
 │  allocation, threading primitives, JSON, paths, config                │
 └───────────────────────────────────────────────────────────────────────┘
```

Data flows downward as calls and upward as events/results. A worker thread never
mutates UI state directly; it posts a result that the main thread applies.

## 6. Proposed repository layout

Refines the layout in the brief; each directory has one reason to exist.

```
vellum/
├── CMakeLists.txt              # top-level build, presets for debug/release/sanitize
├── cmake/                      # dependency discovery helpers
├── docs/                       # ARCHITECTURE, ROADMAP, DESIGN, ADRs
├── assets/{fonts,icons,themes}/
├── config/                     # default settings + keybindings shipped with app
├── include/vellum/             # public headers, grouped by subsystem
├── src/
│   ├── main.c                  # thin entry point -> app_run()
│   ├── core/                   # arena, log, error, event bus, thread/queue, string
│   ├── platform/               # SDL3 window/input, paths, process, signals
│   ├── graphics/               # draw list, renderer backends, glyph atlas
│   ├── text/                   # buffer, line index, encoding, graphemes, shaping
│   ├── editor/                 # document, view, cursor, selection, commands, undo
│   ├── ui/                     # widgets, layout, theme, hit-testing, focus
│   ├── syntax/                 # Tree-sitter integration, highlight spans
│   ├── language/               # LSP client, JSON-RPC, diagnostics, completion
│   ├── filesystem/             # watchers, async read, tree model
│   ├── project/                # workspace, explorer model, recent projects
│   ├── search/                 # in-file + workspace (rg driver), replace
│   ├── terminal/               # PTY, VT parser, terminal model
│   ├── git/                    # git CLI driver, status, diff, branch model
│   ├── debugger/               # DAP client, breakpoints, stack, watches
│   └── config/                 # settings, keybindings, themes (JSON)
├── tests/{unit,integration,benchmarks}/
└── third_party/                # vendored single-file libs / grammar sources only
```

Rule: a `.c` file belongs to exactly one subsystem and includes only its own private
headers plus public headers of dependencies. No `../../` reaches into a sibling's
internals.

## 7. Core subsystems

### 7.1 Application lifecycle and event loop

`main()` is deliberately tiny: parse args → `vellum_app_create()` → `vellum_app_run()`
→ `vellum_app_destroy()`. The app owns the SDL window, renderer, config, UI root, and
the worker dispatch queue.

Loop shape (single main thread):

1. Drain SDL events (`SDL_PollEvent`) and application events (registered via
   `SDL_RegisterEvents`, posted with `SDL_PushEvent`).
2. Process accumulated input into editor commands; apply commands to documents.
3. Mark dirty regions from document/theme/layout changes.
4. If dirty: rebuild layout for dirty subtrees, emit a draw list, render, present.
5. If clean: block on `SDL_WaitEventTimeout` with a bounded timeout. **No idle spin,
   no per-frame allocations.**

`SDL_Event` is translated once into a Vellum input event (key, text, pointer, wheel,
drop) so UI code has no SDL dependencies. SDL-specific code stays in `platform/`.

### 7.2 Threading model

A fixed, small set of threads; no thread-per-file or per-keystroke churn.

| Thread | Responsibility |
| --- | --- |
| Main | SDL events, input, commands, layout, render. Never waits on I/O or IPC. |
| I/O worker | File reads, `mmap`, line-index scans, directory enumeration, watches. |
| Language worker(s) | LSP JSON-RPC loops, Tree-sitter incremental parses, diagnostics. |
| Job worker | Workspace search (`rg` process), Git CLI processes, build tasks. |
| PTY reader (per terminal) | Blocking reads from the PTY into a ring buffer; main thread parses VT. |

Communication: lock-protected MPSC queues; results become application events consumed
on the main thread. Cancellation tokens are passed to workers so closing a tab or
workspace aborts stale work. All SDL calls that are not documented thread-safe stay on
the main thread (notably rendering and `SDL_RenderGeometry`).

### 7.3 Rendering architecture — decision

**Chosen: SDL3 Render API with batched geometry via `SDL_RenderGeometry`, driven by an
internal draw list. SDL3 GPU API is the escape hatch, not the starting point.**

Evidence and rationale:

- `SDL_RenderGeometry(renderer, texture, vertices, n, indices, n)` is present in the
  installed `SDL_render.h` and documented by the SDL wiki as available since SDL 3.2.0.
  It renders many textured triangles in one call with per-vertex colour. A glyph atlas
  plus a quad batcher therefore needs **one draw call for thousands of glyphs**. The
  usual reason to avoid 2D renderers (one draw call per glyph) does not apply.
- SDL3's own GPU documentation states: *"If you just need simple 2D graphics, the
  Render API is much easier to use but still hardware-accelerated"*, while noting the
  GPU API's benefits. For a UI that is rectangles + textured quads, the simpler API
  buys correctness and speed of development with no measurable loss at our targets.
- The GPU API requires shaders in a backend-specific format (SPIR-V/DXBC/DXIL/MSL),
  `SDL_shadercross` or AOT compilation, resource cycling, and careful pass/state
  management. That is real complexity and a real maintenance cost. We adopt it only
  if a benchmark proves the Render backend is the bottleneck.
- The internal `vellum_draw_list` interface is intentionally tiny: `begin`, `rect`,
  `textured_quad` (batched), `line`, `scissor`, `end/submit`. A future GPU backend
  implements the same interface with one instanced draw; no UI code changes.

This satisfies "do not introduce an unnecessarily complicated rendering architecture"
while preserving a credible high-performance path.

### 7.4 Text and font pipeline

Owned by `text/` + `graphics/`, independent of editor logic:

1. **ICU** validates UTF-8, provides grapheme-cluster boundaries (so cursor movement
   and deletion are user-perceived-character correct), case folding, and bidi where
   needed. Internally all text is UTF-8 bytes.
2. **HarfBuzz** shapes a run of UTF-8 text into glyph IDs and positions. Shaping is done
   per *run* (not per glyph) so ligatures work; results are cached by a key of
   (text hash, font/style ID, available width, DPI).
3. **FreeType** rasterizes glyphs into a glyph atlas texture (R8 for monochrome AA;
   RGBA added later for colour emoji / LCD subpixel). Cache key: (face, pixel size,
   glyph index, flags). Atlas grows in pages; eviction only when full.
4. The layout pass turns shaped runs into positioned quads appended to the draw list.
5. **Line caching:** shaped/laid-out lines are cached; only dirty lines are re-shaped.
   Scrolling reuses cached geometry and only builds newly visible lines.

`SDL3_ttf` is **not** part of the core pipeline: we use FreeType/HarfBuzz directly to
control shaping and caching. It remains linked temporarily because the current
Makefile references it; it should be removed from `LDLIBS` once the custom pipeline is
live, unless a small debug-label path finds a use for it.

### 7.5 Text buffer — decision

**Chosen: a piece table indexed by a balanced tree, with append-only storage; the
original file is memory-mapped read-only.**

A piece is `(buffer_id, byte_offset, length)`. The document is an ordered sequence of
pieces; each tree node caches the total byte length of its subtree, so offset ↔ piece
lookup and split/insert/delete are `O(log n)`.

Evaluation:

| Structure | Local edit | Remote edit | Large files | Multi-cursor | Undo | Complexity | Verdict |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Gap buffer | O(1) at gap, O(n) to move | O(n) | poor (copy/move) | poor (gap moves) | simple | low | Rejected as primary buffer |
| Rope | O(log n) | O(log n) | good | good | elegant with persistent nodes | high | Deferred; revisit for collaborative editing |
| **Piece table + tree** | **O(log n)** | **O(log n)** | **excellent (mmap original)** | **good** | **command/inverse log** | **medium** | **Chosen** |
| Naive array of lines | O(n) | O(n) | poor | poor | simple | low | Rejected |

Why this fits Vellum:

- Opening a large file does not copy its contents: `mmap` the file as buffer 0 and
  build the piece tree over it. All edits append to buffer 1 (or a chain of chunks),
  so no data is ever shifted.
- Edits are local tree operations; multiple cursors are just a sequence of edits with
  position remapping — there is no gap to relocate.
- Undo is an inverse-command stack (split/insert/delete records), with coalescing for
  runs of typing. Snapshots are periodic, not per keystroke. A persistent (immutable)
  tree can be introduced later for cheap snapshots without changing callers.
- Memory overhead: one tree node per piece plus cached lengths; bounded and predictable.

Supporting structures:

- **Line index:** two-level array of line-start byte offsets, built lazily in the I/O
  worker by scanning newlines, with incremental updates near edits. CRLF and LF are
  preserved as bytes; each line records its terminator style.
- **Encoding:** UTF-8 throughout; ICU provides grapheme boundaries so cursor/selection
  operations are correct for Unicode. Encoding detection (UTF-8/BOM/legacy) happens at
  load and is recorded as document metadata.
- **Ownership:** the buffer owns its mmap and appended chunks; views hold offsets, not
  pointers into buffer memory.

### 7.6 Editor model

- `Document`: buffer + path + encoding + line-ending metadata + undo stack + dirty flag.
- `View`: a rectangular region showing part of a document; owns scroll, wrap mode,
  cursor(s), selection(s), and local options (whitespace, minimap). Multiple views may
  share one document; this is how split views work.
- `EditorCommand`: the only way to mutate a document. Commands are undoable and are
  the seam where macros, tests, and future multi-cursor logic plug in.
- Tab management is a UI concern over a list of open documents/views; closing a tab
  asks services to cancel in-flight work for that document.

### 7.7 UI framework

A **retained widget tree with immediate-mode draw-list emission**:

- Widgets are opaque structs with `layout`, `event`, `paint` callbacks. Parent→child
  ownership; destruction is recursive.
- Layout is constraint-based (min/content size → assigned rect). Dirty flags propagate
  upward so unchanged subtrees are skipped.
- Focus and keyboard navigation are explicit (tab order, escape/hotkey handling). No
  implicit global focus stack.
- Theme is a set of semantic colour tokens and metrics (spacing, radii, font roles),
  not raw widget colours. Light/dark themes are data, not code paths.
- Activity bar, sidebar, editor area, bottom panel, minimap, and status bar are panels
  whose visibility and size are configurable.

### 7.8 Services

- **Config:** JSON files (defaults in `config/`, user overrides in the platform config
  dir). Settings, keybindings, and themes are separate documents. **yyjson** is the
  approved parser, pinned/vendored; malformed files are reported with path, code,
  message, and byte position and then fall back to defaults. Expensive parsing runs on
  workers, never on the UI thread. See [ADR-001](architecture/ADR-001-json-parsing-yyjson.md).
- **Filesystem/project:** workspace root, async directory enumeration, file watches,
  safe create/rename/delete with confirmation and error reporting.
- **Search:** in-file search uses the buffer directly (regex via a small C regex engine
  or ICU regex); workspace search drives `rg` as an external process and streams
  results. Git-aware filtering delegates ignore rules to `rg`/Git rather than
  reimplementing them.
- **Language:** Tree-sitter runs independently of LSP so highlighting works with no
  server installed. The runtime is vendored and grammars are pinned to explicit
  revisions; parsing runs on a worker and consumes incremental buffer edits. See
  [ADR-003](architecture/ADR-003-tree-sitter-vendored-and-pinned.md). LSP is JSON-RPC
  over stdio with a per-server process; it supplies completion, diagnostics,
  navigation, rename, code actions, signature help, symbols, and semantic tokens.
  Results are merged into a per-document annotation layer.
- **Terminal:** POSIX `openpty`/`forkpty` on Linux; terminal emulation is **libtsm
  first**, gated on the evaluation in
  [ADR-002](architecture/ADR-002-terminal-emulation-libtsm-first.md), with a scoped
  in-house state machine as fallback. Parsing, screen state, PTY management, and
  rendering remain separate. No browser-based terminal.
- **Git:** external `git` CLI with `--porcelain`-style machine-readable output. No Git
  internals in-process initially.
- **Debugger:** DAP client over JSON-RPC; breakpoints, stack, variables, watches, and
  debug console are UI views over DAP state.

## 8. Ownership and error conventions

- Every heap object has one owner. Constructors are `*_create(...)`, destructors
  `*_destroy(...)`; destructors accept `NULL` and are idempotent where practical.
- Short-lived, high-churn allocations (draw lists, shaped runs per frame) use an arena
  reset per frame/operation. Long-lived data uses explicit malloc/free.
- Functions that can fail return `bool` or a `VellumResult`/error out-parameter. Never
  silently ignore an allocation, `mmap`, `fopen`, or `SDL_*` failure. Errors are logged
  with subsystem + operation + `SDL_GetError()`/errno context where relevant.
- Public headers use opaque structs. Public headers must not include SDL headers.
- External input (files, config, LSP messages, PTY bytes, `git` output) is validated
  before use; sizes are bounded; no unchecked arithmetic on offsets.
- Debug builds: ASan + UBSan + `-Wall -Wextra -Wpedantic -Wconversion -Wshadow`.
  Warnings are errors in CI; no warning suppressions without a documented reason.

## 9. Build system

Migrate from the current hand-written Makefile to **CMake + Ninja + Clang**, keeping a
thin `make` wrapper for developer ergonomics if desired.

- Top-level `CMakeLists.txt` with `add_subdirectory` per subsystem (static libraries)
  and a small `src/main.c` executable linking them.
- Presets: `debug` (ASan/UBSan, `-O0 -g`), `release` (`-O2`/`-O3`, `NDEBUG`),
  `bench` (release + tracing counters).
- Dependencies via `find_package(SDL3 CONFIG REQUIRED)` and `pkg_check_modules` for
  `freetype2`, `harfbuzz`, `icu-uc`, `icu-i18n`. Note that CMake's package is `SDL3`
  while pkg-config's module is `sdl3` — both were verified present.
- Sanitizers on Linux by default in debug; LTO considered for release after
  measurement.
- The existing Makefile's debug/release/sanitizer behaviour and dep tracking are
  preserved in spirit; its `CFLAGS` should change from C23 to `-std=c17`, `CC` from
  `gcc` to `clang`, and `LDLIBS` should use `pkg-config --libs sdl3` rather than a
  hard-coded `-lSDL3` — **proposed changes, not yet applied.**

## 10. Initial performance targets

Targets are for release builds on a mid-range x86-64 Linux machine (the current host
is the reference). They are hypotheses to validate with `tests/benchmarks`, not claims
of current performance.

| Metric | Target | How measured |
| --- | --- | --- |
| Cold start → first frame | < 200 ms | `clock_gettime` instrumentation at milestones |
| Warm start → first frame | < 60 ms | same, with OS cache warm |
| Keystroke → presented frame (p95), ≤10k-line file | < 16 ms | instrumented input-to-present timestamp |
| Edit operation, typical line | < 1 ms (p99) | microbenchmarks |
| Open 100 MB file | < 1 s | mmap + lazy line index; benchmark |
| Typing latency in 1M-line file | < 16 ms p95 | benchmark with generated file |
| Re-highlight after small edit | < 2 ms | Tree-sitter incremental parse benchmark |
| Idle CPU | < 0.5% of one core | event-driven loop; `perf`/`pidstat` |
| RSS, 20 typical tabs | < 150 MB | `/proc/self/status` in scenario test |
| Memory for 100 MB file | < 2× file size | RSS after open, before edits |
| Frame budget | ≤ 4 ms UI + text, ≤ 16.6 ms total | frame timer counters |

Instrumentation is built in from Phase 1 (a lightweight `vellum_trace` with counters
and scoped timers, compiled out in release unless `VELLUM_TRACE` is set). Profiling
tools: `perf`, Clang sanitizers, RenderDoc only if a GPU backend is added.

## 11. Remaining open questions and risks

Resolved decisions are recorded in [`architecture/`](architecture/README.md) and are
no longer open: JSON parsing (ADR-001), terminal emulation (ADR-002), Tree-sitter
acquisition (ADR-003), and the rendering backend / GPU threshold (ADR-004). Remaining
items:

1. **LSP process model** — one process per language server (standard) vs a multiplexer.
   Start with one-per-server; revisit only with evidence of resource problems.
2. **macOS/Windows portability** — architecture keeps platform code in `platform/`, but
   no cross-platform work is scheduled until Linux is solid.
3. **Exact regex engine for in-file search** — ICU regex vs a small C engine; decide
   during Phase 4 with a benchmark and a licence check.
4. **libtsm evaluation outcome** — ADR-002 defines the gate; the result and pinned
   revision must be recorded before terminal implementation begins.

## 12. Decision summary

Formal, numbered ADRs live in [`docs/architecture/`](architecture/README.md). The table
below is a non-normative summary; the ADR is authoritative wherever both cover a topic.

| Decision | Rationale | Record |
| --- | --- | --- |
| C17 + Clang + CMake/Ninja | Brief's stack; verified installed | this document |
| SDL3 Render API + batched geometry first; GPU only if proven necessary | `SDL_RenderGeometry` batches glyph quads; GPU API adds shader/cycling complexity | [ADR-004](architecture/ADR-004-rendering-sdl3-first-gpu-conditional.md) |
| FreeType + HarfBuzz + ICU directly | Control over caching, shaping, ligatures, Unicode correctness | this document |
| Piece table + balanced tree + mmap original | Best balance of edit speed, memory, large-file behaviour, multi-cursor | this document |
| Command/inverse-log undo, not full snapshots | Bounded memory; snapshots later via persistent tree if needed | this document |
| Tree-sitter vendored and pinned, independent of LSP | Reproducible builds; editor useful with no server installed | [ADR-003](architecture/ADR-003-tree-sitter-vendored-and-pinned.md) |
| yyjson for JSON, pinned and vendored | Fast, MIT, precise errors, single `.h`/`.c` | [ADR-001](architecture/ADR-001-json-parsing-yyjson.md) |
| libtsm first for terminal emulation, in-house fallback | State-machine-only C library, no UI coupling; gated evaluation | [ADR-002](architecture/ADR-002-terminal-emulation-libtsm-first.md) |
| External tools for Git/search/build | Avoid reimplementing mature, optimized tools | this document |

---

**Next step:** begin Phase 1 (Foundation) on instruction. No implementation changes
have been made; only documentation has been added or updated.
