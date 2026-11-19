# ADR-005: Bundle pinned multilingual fonts

- Status: Accepted
- Date: 2026-11-19
- Supersedes: none
- Related: [ADR-004](ADR-004-rendering-sdl3-first-gpu-conditional.md)

## Decision

Vellum will bundle three pinned, OFL-licensed variable font files under
`assets/fonts/`: JetBrains Mono for the editor UI/code sample, Noto Sans SC for CJK
fallback, and Noto Sans Arabic for Arabic fallback. Upstream files and checksums are
recorded in `assets/fonts/README.md`, and all upstream OFL notices are preserved.

Font selection is a fallback chain: primary face first, then script-specific faces
when the primary face lacks a code point. FreeType owns rasterization; HarfBuzz owns
shaping. Font files are data, not code dependencies, and are loaded at startup.

## Context

Phase 2 requires correctly rendered ASCII, accented Latin, CJK, and Arabic text, plus
a programming-ligature sample where the font supports it. The development host has
DejaVu, Liberation, Adwaita, and one emoji font, but `fc-list :lang=zh` and
`fc-list :lang=ar` return no usable fonts. Relying on host fonts would make the demo
and its tests non-reproducible and would fail the phase's exit criteria on this
machine.

The host has FreeType 26.7.20 and HarfBuzz 15.1.0 installed, so the rendering
libraries themselves are available. The missing piece is font coverage.

## Alternatives considered

1. **Use only host fonts.** Rejected: no CJK or Arabic coverage here; not reproducible.
2. **Use one universal font.** Rejected: no single reasonably sized font in the
   upstream set covers CJK, Arabic, and programming ligatures with the desired quality.
3. **Download fonts at build time.** Rejected: builds would depend on network access
   and upstream mutability.
4. **Bundle fonts without checksums or licence notices.** Rejected: violates the
   dependency and licensing rules.

## Consequences

- Positive: deterministic multilingual demo and tests; no host font dependency.
- Positive: OFL-1.1 permits bundling and modification with preserved notices.
- Negative: repository grows by roughly 18 MB, dominated by Noto Sans SC.
- Negative: fallback chain adds a small amount of face-management complexity.
- Negative: variable fonts must be instantiated through FreeType; the initial atlas
  will use the default instance (regular weight) until named instances are needed.

## Conditions for revisiting

- A smaller font with the same coverage becomes available and passes the same tests.
- The project's distribution size budget is revised with measured evidence.
- A font's licence changes in a way that no longer permits bundling.
