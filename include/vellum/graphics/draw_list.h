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

/* Identifies a clip rectangle in a draw list. Zero is the root (the
 * draw-list-wide scissor); positive ids are returned indirectly by
 * vellum_draw_list_clip_id after vellum_draw_list_push_clip. */
typedef uint32_t VellumClipId;
#define VELLUM_CLIP_NONE 0u

typedef struct {
    float x;
    float y;
    float width;
    float height;
    VellumColor color;
    VellumClipId clip;
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
    VellumClipId clip;
} VellumTexturedQuad;

/* Draw commands are recorded in insertion order. The renderer batches
 * contiguous commands sharing a kind, texture, and clip id. */
typedef enum { VELLUM_DRAW_RECT = 0, VELLUM_DRAW_QUAD } VellumDrawKind;

typedef struct {
    VellumDrawKind kind;
    size_t index; /* index into the rect or quad array */
} VellumDrawCommand;

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
/* Pushes a clip rect intersected with the currently active clip. Positive clip
 * ids are stable for the lifetime of the list; the id can be retrieved with
 * vellum_draw_list_clip_id. Popping restores the parent clip. Clips only affect
 * commands added while they are active; set the root scissor before pushing. */
bool vellum_draw_list_push_clip(VellumDrawList *list, float x, float y, float width, float height);
bool vellum_draw_list_pop_clip(VellumDrawList *list);
VellumClipId vellum_draw_list_clip_id(const VellumDrawList *list);
size_t vellum_draw_list_clip_depth(const VellumDrawList *list);

/* Queries the effective rect for a clip id. `enabled` is false only for the
 * root clip when no scissor is set; nested clips are always enabled (their
 * width/height may be zero after intersection, which clips everything). */
bool vellum_draw_list_clip_rect(const VellumDrawList *list, VellumClipId id, bool *enabled,
                                float *x, float *y, float *width, float *height);

const VellumRect *vellum_draw_list_rects(const VellumDrawList *list, size_t *count);
const VellumTexturedQuad *vellum_draw_list_quads(const VellumDrawList *list, size_t *count);
const VellumDrawCommand *vellum_draw_list_commands(const VellumDrawList *list, size_t *count);
size_t vellum_draw_list_command_count(const VellumDrawList *list);

#ifdef __cplusplus
}
#endif

#endif /* VELLUM_GRAPHICS_DRAW_LIST_H */
