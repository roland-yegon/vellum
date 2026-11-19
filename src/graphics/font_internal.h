/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */
#ifndef VELLUM_GRAPHICS_FONT_INTERNAL_H
#define VELLUM_GRAPHICS_FONT_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "vellum/graphics/font.h"

/* Private bridge for the HarfBuzz shaper in the same library. */
FT_Face vellum_font_native_face(VellumFontSystem *system, uint32_t face_id);
size_t vellum_font_system_face_count(const VellumFontSystem *system);
bool vellum_font_native_set_pixel_size(FT_Face face, uint32_t pixel_size, VellumErrorInfo *error);

#endif /* VELLUM_GRAPHICS_FONT_INTERNAL_H */
