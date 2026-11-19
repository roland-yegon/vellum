# yyjson (vendored)

Vellum vendors yyjson 0.13.0 as its JSON parser and serializer, per
[ADR-001](../../docs/architecture/ADR-001-json-parsing-yyjson.md).

- Upstream: <https://github.com/ibireme/yyjson>
- Pinned release tag: `0.13.0`
- Release archive SHA-256:
  `34e0f62a2bc11ab20d601e8ca1cc2b2079503aa45119a19133d89d19b94a0fae`
- Files copied verbatim from the release archive: `src/yyjson.h`, `src/yyjson.c`, `LICENSE`.
- Licence: MIT (see `LICENSE`). Vellum itself remains GPL-2.0-only; this notice is
  preserved unchanged.

The vendored source is a separate CMake target and is not built with Vellum's strict
warning flags. To update yyjson, create a superseding ADR, replace these files from a
new pinned archive, and update the checksum above.
