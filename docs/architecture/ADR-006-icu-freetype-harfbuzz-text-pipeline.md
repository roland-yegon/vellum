# ADR-006: Use ICU, FreeType, and HarfBuzz for the text pipeline

- Status: Accepted
- Date: 2026-11-19
- Supersedes: none
- Related: [ADR-004](ADR-004-rendering-sdl3-first-gpu-conditional.md),
  [ADR-005](ADR-005-bundled-multilingual-fonts.md)

## Decision

Vellum will use three system libraries, discovered through `pkg-config`, for the text
foundation:

- **ICU** (`icu-uc`) for strict UTF-8 validation, grapheme-cluster boundaries, and
  character direction properties.
- **FreeType** (`freetype2`) for loading font faces and rasterizing glyph masks into an
  atlas.
- **HarfBuzz** (`harfbuzz`) for shaping runs of UTF-8 text into glyph IDs and
  positions, including ligatures and Arabic joining.

The main thread owns these objects and their caches. No embedded browser or
SDL3_ttf-based text path is part of the core pipeline.

## Context

Phase 2 requires correct Unicode handling, user-perceived-character cursor movement,
multilingual text, and code-font ligatures. The installed toolchain was inspected
before implementation:

| Library | Installed version | Header/module checked | Licence |
| --- | --- | --- | --- |
| ICU | 78.3 | `icu-uc`, `<unicode/utf8.h>`, `<unicode/ubrk.h>`, `<unicode/uchar.h>` | Unicode licence; BSD-style components |
| FreeType | 26.7.20 (pkg-config), Arch package 2.15.0 | `freetype2`, `<ft2build.h>`, `FT_FREETYPE_H` | FTL OR GPL-2.0-or-later; used under FTL |
| HarfBuzz | 15.1.0 | `harfbuzz`, `<hb.h>`, `<hb-ft.h>` | MIT |

The APIs actually used were checked in the installed headers: ICU `U8_NEXT`,
`ubrk_open`, `ubrk_setUText`, `ubrk_first`, `ubrk_next`, `u_charDirection`, and
`u_errorName`; FreeType `FT_Init_FreeType`, `FT_New_Face`, `FT_Get_Char_Index`,
`FT_Set_Pixel_Sizes`, `FT_Load_Glyph`, `FT_Render_Glyph`, `FT_Done_Face`, and
`FT_Done_FreeType`; HarfBuzz `hb_ft_font_create`, `hb_ft_font_changed`,
`hb_font_set_scale`, `hb_buffer_create`, `hb_buffer_add_utf8`,
`hb_buffer_guess_segment_properties`, `hb_feature_from_string`, `hb_shape`,
`hb_buffer_get_glyph_infos`, and `hb_buffer_get_glyph_positions`.

These libraries are dynamically linked system dependencies. Their licences are
independent of Vellum's GPL-2.0-only code and no upstream licence is replaced.

## Alternatives considered

1. **SDL3_ttf.** Rejected as the core path: it wraps FreeType and HarfBuzz but hides
   the atlas, cache, and shaping policy that Vellum needs. It remains linked nowhere.
2. **Platform text APIs.** Rejected: would fragment Unicode behaviour and add native
   UI stacks contrary to the SDL3-only platform layer.
3. **Hand-written UTF-8 and grapheme rules.** Rejected: high correctness cost for
   combining marks, ZWJ sequences, and Indic scripts; ICU is already installed.
4. **Per-glyph shaping with FreeType advances.** Rejected: ligatures and Arabic
   joining require run-level shaping.

## Consequences

- Positive: reproducible Unicode semantics, correct run shaping, and explicit control
  of caching and atlas layout.
- Positive: system packages avoid vendoring three large C/C++ trees.
- Negative: builds require the three development packages; CMake fails early with a
  `pkg-config` error if any is missing.
- Negative: ICU's `UText`/break-iterator bridge uses `int32_t` byte offsets; inputs
  larger than `INT32_MAX` bytes are rejected by this phase's bridge.
- Negative: full mixed-direction bidi reordering is not yet implemented. The current
  `vellum_text_base_direction()` implements first-strong direction for selecting a
  HarfBuzz run direction.
- Negative: the atlas and shaper share `FT_Face` objects, so face size changes must be
  synchronized with cached HarfBuzz fonts before shaping.

## Conditions for revisiting

- A measured correctness or performance failure in one library that a scoped in-house
  implementation can fix without increasing Unicode risk.
- A platform target where the library cannot be supplied reproducibly.
- A licence change that makes dynamic linking incompatible with distribution plans.
