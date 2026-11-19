# ADR-001: JSON Parsing with yyjson

- **Status:** Accepted
- **Date:** 2026-11-19
- **Supersedes:** the open question "JSON parser choice" in `ARCHITECTURE.md` §11

## Decision

Use **yyjson** as Vellum's default JSON parser and serializer for settings,
keybindings, themes, LSP messages, DAP messages, and other machine-readable JSON.

- Vendor or pin a known release. The verified reference is **yyjson 0.13.0**
  (locally installed via `pkg-config yyjson`; header reports
  `YYJSON_VERSION_STRING "0.13.0"`). Prefer the single-header/single-source
  distribution (`yyjson.h` + `yyjson.c`) under `third_party/` with a pinned commit or
  release archive and recorded checksum.
- Use `yyjson_read` / `yyjson_read_file` with a `yyjson_read_err` out-parameter for all
  reads; use `yyjson_mut_*` APIs plus `yyjson_mut_write*` for writes.
- Malformed configuration must never crash or silently corrupt state. On parse error,
  report file path, `err.code`, `err.msg`, and `err.pos`, then fall back to defaults
  (or to the last valid in-memory settings) and continue startup.
- Parsing that can be expensive (large configuration, LSP/DAP message streams,
  workspace-level JSON) runs on a worker thread, never on the UI thread. Results are
  marshalled to the main thread through the normal application event queue. Small
  startup config parsing is permitted on the main thread only if measured to be
  negligible; the trace counter must confirm this.
- No second JSON library is introduced for configuration or protocol work without a
  superseding ADR.

## Context

Vellum needs JSON in two distinct roles: low-volume human-authored files
(settings, keybindings, themes) and potentially high-volume machine-authored protocol
traffic (LSP and DAP JSON-RPC). The parser must be fast, portable C, and must give
precise errors for hand-edited files. It must also support writing settings back out.

Verification performed before this decision:
- yyjson is installed locally at version 0.13.0 (`-lyyjson`); the installed header
  provides `yyjson_read_err` (code, msg, pos), immutable read APIs, and mutable
  write APIs (`yyjson_mut_doc_new`, `yyjson_mut_write`, `yyjson_mut_write_file`).
- The upstream project describes itself as "a high performance JSON library written
  in ANSI C", MIT licensed, RFC 8259 compliant with UTF-8 validation, distributed as
  one `.h` and one `.c` file. Its published benchmarks place it well ahead of cJSON
  and jansson for DOM parsing and serialization.
- yyjson's documented limitations: an array/object is a linked-list-like structure,
  so keyed/indexed lookup is slower than iterating; duplicate keys are allowed and
  order is preserved; immutable documents require a mutable copy for modification.
  A streaming API is listed as not yet implemented.

## Alternatives considered

1. **jsmn** — extremely small and allocation-free, but it is a tokenizer, not a DOM:
   it would require substantial glue for typed access, error reporting, and writing.
   Rejected due to integration cost.
2. **cJSON** — ubiquitous and easy, but materially slower and its error reporting is
   weaker. Rejected given LSP message volume.
3. **Hand-written JSON parser** — rejected: unnecessary, error-prone, and would
   duplicate a well-tested MIT-licensed component.
4. **Use SDL's or another dependency's incidental JSON helpers** — rejected: no
   suitable public API, and coupling config to rendering would be wrong layering.

## Consequences

- One small vendored C library is added under `third_party/`, with its MIT notice
  preserved and its pinned version recorded in `ARCHITECTURE.md` §4/§9.
- A thin Vellum config layer must wrap yyjson so the rest of the application sees
  typed settings, not `yyjson_val *`. This layer owns defaults, validation of unknown
  keys, duplicate-key policy, and user-facing error formatting.
- Because parsed documents are immutable, editing settings requires
  `yyjson_doc_mut_copy` or constructing a mutable document; the config layer hides
  this.
- The lack of a streaming API means we must bound input size for untrusted protocol
  messages and parse them on workers, rather than assuming constant memory.
- LSP/DAP parsing speed improves, but correctness now depends on our version-gating
  and cancellation logic, not on the parser.

## Conditions that justify revisiting

- yyjson ceases to be maintained, or a licence/compliance review rejects its terms.
- A measured need for streaming JSON (e.g. multi-gigabyte protocol payloads) that
  yyjson cannot satisfy within memory bounds.
- A benchmark shows parser cost is negligible and a much smaller dependency would
  materially reduce binary size or build time — and the replacement provides equally
  precise error reporting and serialization.
- Any proposal to add a second JSON library must be justified here first.
