# Architecture Decision Records

This directory is the canonical record of architectural decisions for Vellum.
Decisions here are approved defaults. Treat them as binding until implementation
evidence demonstrates a significant problem, and then change them only through a
new record that explicitly supersedes the old one.

| ADR | Decision | Status |
| --- | --- | --- |
| [ADR-000](ADR-000-architecture-decision-records.md) | Use ADRs as the canonical decision record | Accepted |
| [ADR-001](ADR-001-json-parsing-yyjson.md) | yyjson as the default JSON parser | Accepted |
| [ADR-002](ADR-002-terminal-emulation-libtsm-first.md) | libtsm first for terminal emulation, scoped in-house fallback | Accepted (adoption gated on evaluation) |
| [ADR-003](ADR-003-tree-sitter-vendored-and-pinned.md) | Vendored Tree-sitter runtime and pinned grammars | Accepted |
| [ADR-004](ADR-004-rendering-sdl3-first-gpu-conditional.md) | SDL3 rendering first; GPU backend only when justified | Accepted |

Each record contains: Decision, Context, Alternatives considered, Consequences, and
Conditions that justify revisiting. High-level design and module boundaries remain in
[`../ARCHITECTURE.md`](../ARCHITECTURE.md); the phased plan is in
[`../ROADMAP.md`](../ROADMAP.md).
