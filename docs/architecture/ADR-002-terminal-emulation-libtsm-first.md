# ADR-002: Terminal Emulation — libtsm First, Scoped In-House Fallback

- **Status:** Accepted. Adoption of libtsm is gated on the evaluation below.
- **Date:** 2026-11-19
- **Supersedes:** the open question "Terminal emulator" in `ARCHITECTURE.md` §11

## Decision

Evaluate **libtsm** as the preferred terminal-emulation state machine for Vellum's
integrated terminal. Pin a known release/commit before adoption. If it fails the
evaluation gate, implement a deliberately scoped in-house VT parser instead.

Regardless of which path is taken, keep these as **separate subsystems** with no
cross-dependencies beyond narrow interfaces:

1. terminal parsing / emulation state,
2. terminal screen model (cells, attributes, scrollback),
3. PTY management (process lifecycle, I/O, resize, signal handling),
4. terminal rendering (using the shared draw list and glyph atlas).

Do not claim complete xterm compatibility unless it has been demonstrated by tests.
Document the supported subset explicitly.

### Evaluation gate for libtsm

Adopt libtsm only if all of the following are confirmed at the pinned revision:

- **Maintenance status:** upstream is active and not archived. At investigation time,
  the canonical repository was `kmscon/libtsm` (created 2025-11-27, last push
  2026-11-10, not archived, latest tag `v4.8.0`). The older `Aetf/libtsm` README states
  that upstream development restarted at the kmscon repository. Re-check this at pin
  time and record the commit hash.
- **Target platforms:** builds and passes tests on Linux (primary) and does not
  introduce a hard dependency on GTK or another UI toolkit; macOS is desirable since
  the Aetf fork's 4.1.0 notes include a macOS build fix. Windows is out of scope for
  now but must not be made impossible by the choice.
- **Supported features:** verify by test that the pinned revision provides VT parsing,
  cursor movement, scrolling regions/margins, alternate screen, SGR colours (256 and
  24-bit), text attributes (bold/italic/underline/reverse), bracketed paste, mouse
  tracking modes, and selection APIs needed by our UI.
- **Unicode handling:** input is UTF-8; verify `wcwidth` behaviour, combining marks,
  wide glyphs, and invalid-sequence handling against a test corpus. Note that libtsm
  ships third-party `wcwidth` code with its own licence notice.
- **Rendering integration:** libtsm provides no rendering, fonts, or windowing. Verify
  that cell iteration (`tsm_screen_draw` with age information, or `tsm_screen_draw2`)
  lets us render only dirty cells through our own draw list and glyph atlas.
- **Memory and performance:** verify that scrollback is bounded via the provided API,
  that cell iteration cost is acceptable at our target sizes, and that no unbounded
  allocation occurs on hostile escape-sequence input.

### If the gate fails: in-house requirements

The in-house parser must be a state machine and must support, at minimum:

- VT/ANSI sequences required by common shells and CLI applications;
- cursor movement, cursor styles, and origin modes;
- basic SGR attributes and colours (16/256/24-bit);
- scrolling regions and alternate screen buffers;
- UTF-8 input and correct display widths;
- basic input modes, including bracketed paste and application cursor/keypad modes;
- terminal resizing with correct reflow behaviour;
- a configurable, bounded scrollback buffer.

Scope is expanded only when a concrete application demonstrates the need.

## Context

Evidence gathered before this decision (primary sources):

- libtsm describes itself as "a state machine for DEC VT100-VT520 compatible terminal
  emulators" that "does not provide any rendering nor window management" and has no
  runtime dependency beyond an ISO C library. It explicitly aims for xterm
  compatibility and has no GTK binding (unlike libvte), which fits Vellum's native C
  architecture.
- Public header inspection confirmed the split between `tsm_screen` (cell buffer) and
  `tsm_vte` (parser/input). Available APIs include `tsm_screen_new`, `tsm_screen_resize`,
  `tsm_screen_set_margins`, `tsm_screen_set_max_sb` / `tsm_screen_clear_sb` and
  scrollback navigation/getters, `TSM_SCREEN_ALTERNATE`, cell iteration with age,
  `tsm_vte_input`, `tsm_vte_handle_keyboard`, `tsm_vte_handle_mouse`, `tsm_vte_paste`,
  bracketed-paste and mouse-mode constants, OSC/mouse/bell callbacks, and 24-bit
  foreground/background colour in cell attributes.
- Release notes show active feature work: OSC 4/10/11 colour queries and word selection
  (4.3.0), shift-arrow keys and Meson build (4.2.0), configureable backspace, mouse
  support, scrollback line count/position getters, and macOS build fixes (4.1.0).
- Licence: libtsm is MIT, with third-party components (CCAN hash table, a `wcwidth`
  port, and UCS-4→UTF-8 code from terminology) that carry their own notices. Those
  third-party terms are independent of Vellum, which is GPL-2.0-only. If libtsm is
  vendored, all upstream notices must be preserved.
- Not installed on the development host. Adoption will require vendoring or an
  explicit build-time dependency; either way the revision must be pinned.

## Alternatives considered

1. **libvte** — feature-rich but tied to GTK; rejected as incompatible with a custom
   SDL3 UI and as an unnecessary large framework.
2. **In-house parser from the start** — maximum control and no external dependency,
   but a large, error-prone undertaking. Deferred to fallback status.
3. **A browser-based or shell-out terminal** — explicitly prohibited or functionally
   inadequate; rejected.
4. **A larger terminal library with rendering included** — rejected: Vellum already
   has a renderer, so such a library would create two rendering paths.

## Consequences

- Phase 7 begins with a time-boxed evaluation, not immediate adoption. The result and
  pinned revision are recorded in a short evaluation note before code is merged.
- If adopted, we own the integration: feeding PTY bytes into `tsm_vte_input`, routing
  `tsm_vte_write_cb` output back to the PTY, translating SDL key events to libtsm
  keysyms/modifiers, and drawing cells with our glyph atlas. Scrollback UI, selection
  UI, and terminal tabs remain Vellum code.
- Build integration must cope with libtsm's Meson-based upstream; vendoring the
  required sources into `third_party/` with pinned hashes is preferred for
  reproducibility. Licence notices must be included.
- If the fallback is used, the listed features become acceptance tests. No claim of
  full xterm compatibility is made without a documented conformance suite.

## Conditions that justify revisiting

- libtsm becomes unmaintained, archived, or incompatible with a target platform.
- Tests show missing sequences that break common tools (e.g. alternate-screen editors,
  `less`, `htop`, `vim`) and cannot be added locally at acceptable cost.
- Unicode/width handling proves incorrect for required scripts or emoji.
- Memory or CPU benchmarks exceed the targets under hostile input or large scrollback.
- A smaller, actively maintained C state machine with a cleaner licence becomes
  available and passes the same gate with less integration cost.
- Conversely: if the in-house parser reaches parity with less total code and no
  third-party notices, a superseding ADR may replace libtsm.
