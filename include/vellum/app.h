/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#ifndef VELLUM_APP_H
#define VELLUM_APP_H

#include <stdint.h>

#include "vellum/core/error.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VellumApp VellumApp;

VellumApp *vellum_app_create(VellumErrorInfo *error);
int vellum_app_run(VellumApp *app);
void vellum_app_destroy(VellumApp *app);

/* Registered SDL event type for app messages, for external producers and tests. */
uint32_t vellum_app_event_base(const VellumApp *app);

/* Effective text pixel size for a font size at a UI/display scale, rounded to
 * the nearest integer and clamped to [1, 512]. Pure helper, safe to call anywhere. */
uint32_t vellum_app_pixel_size_for_scale(uint32_t font_size, float scale);

/* Current display scale and the resulting text pixel size. */
float vellum_app_display_scale(const VellumApp *app);
uint32_t vellum_app_text_pixel_size(const VellumApp *app);

/* Overrides the display scale (also used by tests and by settings in future).
 * A nonpositive scale re-queries SDL for the window's display scale. Changing it
 * changes the pixel-size cache key, so glyphs are re-rasterized at the new size. */
void vellum_app_set_display_scale(VellumApp *app, float scale);

#ifdef __cplusplus
}
#endif

#endif /* VELLUM_APP_H */
