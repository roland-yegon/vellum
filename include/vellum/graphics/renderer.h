/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#ifndef VELLUM_GRAPHICS_RENDERER_H
#define VELLUM_GRAPHICS_RENDERER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vellum/core/error.h"
#include "vellum/graphics/draw_list.h"

/* The SDL renderer backend is an implementation detail: callers see only this
 * interface and an opaque SDL_Window pointer. */
struct SDL_Window;

typedef struct VellumRenderer VellumRenderer;

VellumRenderer *vellum_renderer_create(struct SDL_Window *window, VellumErrorInfo *error);
bool vellum_renderer_submit(VellumRenderer *renderer, const VellumDrawList *list,
                            VellumErrorInfo *error);
const char *vellum_renderer_name(const VellumRenderer *renderer);
uint32_t vellum_renderer_draw_calls(const VellumRenderer *renderer);
size_t vellum_renderer_vertex_capacity(const VellumRenderer *renderer);
void vellum_renderer_destroy(VellumRenderer *renderer);

#endif /* VELLUM_GRAPHICS_RENDERER_H */
