# ADR-003: Vendored Tree-sitter Runtime and Pinned Grammars

- **Status:** Accepted
- **Date:** 2026-11-19
- **Supersedes:** the open question "Tree-sitter acquisition" in `ARCHITECTURE.md` §11

## Decision

Use **Tree-sitter** for incremental syntax parsing, folding, and syntax highlighting,
independent of any language server.

- **Vendor the runtime** into `third_party/` rather than depending on an arbitrary
  system-installed version. Pin a known release. Reference version at decision time:
  **v0.27.2**, published 2026-11-07, commit
  `ef2365d0f9f1a370d721a82470d69c3646fe97f0`. Record the exact commit and checksum in
  the vendoring manifest.
- Maintain an **explicit list of supported grammars**, each pinned to a specific commit
  or tag, versioned together with the query files used for highlighting/folding. The
  initial set is C, C++, JSON, Markdown, Python, Rust, Go, and shell.
- **No system-installed Tree-sitter runtime or grammar** is consumed implicitly.
- Parsing happens **off the rendering path**, on a language worker thread, using
  incremental parses driven by edits from the piece-table buffer. Results are
  published to the UI as an immutable annotation layer.
- Integration with the text buffer uses byte offsets and a description of the edited
  ranges; it must not require the parser to hold pointers into buffer memory.
- **Malformed or incomplete source must not crash parsing.** Parse errors are data,
  not failures; the highlighter must degrade to a plain-text fallback for affected
  ranges.
- Grammar updates are **independently manageable**: one grammar per change, with its
  pinned revision, query changes, and tests in a single reviewable unit. Runtime
  upgrades are separate changes.

## Context

Vellum must remain useful when no language server is installed, so syntax information
cannot depend solely on LSP semantic tokens. Tree-sitter provides incremental parsing
with error recovery, which matches the editor's incremental architecture and the
principal "editing must stay responsive" requirement.

Verification before this decision:
- Tree-sitter is **not installed** on this host (no pkg-config entry, no headers), so
  relying on a system package would add an undeclared setup step and unpinned versions.
- The upstream project is active: release v0.27.2 was published on 2026-11-07 and
  includes fixes to the C library (for example ERROR-node handling and reverse cursor
  traversal), confirming ongoing maintenance.
- The runtime is C and is designed to be compiled into applications; grammars generate
  C sources (`parser.c`, plus `scanner.c` where needed). This fits a C17/CMake build.

## Alternatives considered

1. **System-provided Tree-sitter package** — rejected: versions vary by distribution,
   builds would not be reproducible, and it is not installed here.
2. **Regex-based highlighting** — rejected: no structural understanding, poor
   correctness on nested constructs, and no folding support.
3. **LSP-only semantic highlighting** — rejected: violates the requirement that the
   editor work with no language server installed, and semantic tokens are
   server-dependent.
4. **Hand-written lexers per language** — rejected: high maintenance cost and no
   incremental/error-tolerant parsing.

## Consequences

- Vendored runtime and generated grammar sources increase repository size and require
  licence notices (Tree-sitter is MIT licensed; individual grammars carry their own
  licences, to be recorded per grammar).
- Grammar ABI compatibility with the runtime is a real constraint: runtime and
  grammars must be pinned and upgraded deliberately, with a build-time check where
  practical.
- A synchronization layer is needed between the piece table and Tree-sitter's input
  interface, including incremental edit ranges and byte-offset mapping to UTF-8.
  This layer must be unit-tested against random edits.
- Syntax and folding queries are versioned assets; a grammar update may require query
  updates and highlight regression tests.
- Parsing on a worker thread means highlight results are asynchronous; the UI must
  handle stale results and version gating, and must render without them initially.

## Conditions that justify revisiting

- Tree-sitter ceases to be maintained or its licence changes incompatibly.
- The cost of pinning/upgrading many grammars outweighs the benefit for the supported
  language set.
- A native C alternative with comparable incremental and error-recovery behaviour
  produces a measurably smaller build and simpler integration.
- The editor's buffer model changes fundamentally (for example to a CRDT for
  collaboration) and a different parser integration becomes substantially cheaper.
- A grammar proves unusable (incorrect, unmaintained, or incompatible licence); that
  grammar alone may be removed without revisiting this ADR.
