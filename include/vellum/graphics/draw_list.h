/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#ifndef VELLUM_GRAPHICS_DRAW_LIST_H
#define VELLUM_GRAPHICS_DRAW_LIST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float r;
    float g;
    float b;
    float a;
} VellumColor;

/* Renderer-owned texture handle. Zero selects the built-in white texture, which is
 * useful for solid tinted geometry; nonzero handles come from the renderer. */
typedef uint32_t VellumTextureHandle;
#define VELLUM_TEXTURE_NONE 0u

typedef struct {
    float x;
    float y;
    float width;
    float height;
    VellumColor color;
} VellumRect;

typedef struct {
    VellumTextureHandle texture;
    float x;
    float y;
    float width;
    float height;
    float u0;
    float v0;
    float u1;
    float v1;
    VellumColor tint;
} VellumTexturedQuad;

typedef struct VellumDrawList VellumDrawList;

VellumColor vellum_color(float r, float g, float b, float a);
VellumDrawList *vellum_draw_list_create(size_t rect_capacity, size_t quad_capacity);
void vellum_draw_list_destroy(VellumDrawList *list);
void vellum_draw_list_clear(VellumDrawList *list);
bool vellum_draw_list_rect(VellumDrawList *list, float x, float y, float width, float height,
                           VellumColor color);
bool vellum_draw_list_textured_quad(VellumDrawList *list, VellumTextureHandle texture, float x,
                                    float y, float width, float height, float u0, float v0,
                                    float u1, float v1, VellumColor tint);
void vellum_draw_list_set_clear_color(VellumDrawList *list, VellumColor color);
VellumColor vellum_draw_list_clear_color(const VellumDrawList *list);
void vellum_draw_list_set_scissor(VellumDrawList *list, bool enabled, float x, float y, float width,
                                  float height);
bool vellum_draw_list_scissor(const VellumDrawList *list, bool *enabled, float *x, float *y,
                              float *width, float *height);
const VellumRect *vellum_draw_list_rects(const VellumDrawList *list, size_t *count);
const VellumTexturedQuad *vellum_draw_list_quads(const VellumDrawList *list, size_t *count);
size_t vellum_draw_list_command_count(const VellumDrawList *list);

#ifdef __cplusplus
}
#endif

#endif /* VELLUM_GRAPHICS_DRAW_LIST_H */
