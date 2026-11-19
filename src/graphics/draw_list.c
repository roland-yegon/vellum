/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#include "vellum/graphics/draw_list.h"

#include <stdlib.h>

#include "vellum/core/array.h"

struct VellumDrawList {
    VellumArray rects;
    VellumArray quads;
    VellumColor clear_color;
    bool scissor_enabled;
    float scissor_x;
    float scissor_y;
    float scissor_width;
    float scissor_height;
};

VellumColor vellum_color(float r, float g, float b, float a)
{
    const VellumColor color = {r, g, b, a};
    return color;
}

VellumDrawList *vellum_draw_list_create(size_t rect_capacity, size_t quad_capacity)
{
    VellumDrawList *list = malloc(sizeof(*list));
    if (list == NULL) {
        return NULL;
    }

    if (!vellum_array_init(&list->rects, sizeof(VellumRect), rect_capacity) ||
        !vellum_array_init(&list->quads, sizeof(VellumTexturedQuad), quad_capacity)) {
        vellum_array_deinit(&list->rects);
        vellum_array_deinit(&list->quads);
        free(list);
        return NULL;
    }

    list->clear_color = vellum_color(0.04f, 0.05f, 0.07f, 1.0f);
    list->scissor_enabled = false;
    list->scissor_x = 0.0f;
    list->scissor_y = 0.0f;
    list->scissor_width = 0.0f;
    list->scissor_height = 0.0f;
    return list;
}

void vellum_draw_list_destroy(VellumDrawList *list)
{
    if (list == NULL) {
        return;
    }
    vellum_array_deinit(&list->rects);
    vellum_array_deinit(&list->quads);
    free(list);
}

void vellum_draw_list_clear(VellumDrawList *list)
{
    if (list == NULL) {
        return;
    }
    vellum_array_clear(&list->rects);
    vellum_array_clear(&list->quads);
    list->scissor_enabled = false;
}

bool vellum_draw_list_rect(VellumDrawList *list, float x, float y, float width, float height,
                           VellumColor color)
{
    if (list == NULL) {
        return false;
    }
    VellumRect *rect = vellum_array_push(&list->rects);
    if (rect == NULL) {
        return false;
    }
    rect->x = x;
    rect->y = y;
    rect->width = width;
    rect->height = height;
    rect->color = color;
    return true;
}

bool vellum_draw_list_textured_quad(VellumDrawList *list, VellumTextureHandle texture, float x,
                                    float y, float width, float height, float u0, float v0,
                                    float u1, float v1, VellumColor tint)
{
    if (list == NULL) {
        return false;
    }
    VellumTexturedQuad *quad = vellum_array_push(&list->quads);
    if (quad == NULL) {
        return false;
    }
    quad->texture = texture;
    quad->x = x;
    quad->y = y;
    quad->width = width;
    quad->height = height;
    quad->u0 = u0;
    quad->v0 = v0;
    quad->u1 = u1;
    quad->v1 = v1;
    quad->tint = tint;
    return true;
}

void vellum_draw_list_set_clear_color(VellumDrawList *list, VellumColor color)
{
    if (list != NULL) {
        list->clear_color = color;
    }
}

VellumColor vellum_draw_list_clear_color(const VellumDrawList *list)
{
    return list == NULL ? vellum_color(0.0f, 0.0f, 0.0f, 1.0f) : list->clear_color;
}

void vellum_draw_list_set_scissor(VellumDrawList *list, bool enabled, float x, float y, float width,
                                  float height)
{
    if (list == NULL) {
        return;
    }
    list->scissor_enabled = enabled;
    list->scissor_x = x;
    list->scissor_y = y;
    list->scissor_width = width;
    list->scissor_height = height;
}

bool vellum_draw_list_scissor(const VellumDrawList *list, bool *enabled, float *x, float *y,
                              float *width, float *height)
{
    if (list == NULL) {
        return false;
    }
    if (enabled != NULL) {
        *enabled = list->scissor_enabled;
    }
    if (x != NULL) {
        *x = list->scissor_x;
    }
    if (y != NULL) {
        *y = list->scissor_y;
    }
    if (width != NULL) {
        *width = list->scissor_width;
    }
    if (height != NULL) {
        *height = list->scissor_height;
    }
    return true;
}

const VellumRect *vellum_draw_list_rects(const VellumDrawList *list, size_t *count)
{
    if (count != NULL) {
        *count = list == NULL ? 0u : vellum_array_size(&list->rects);
    }
    return list == NULL ? NULL : (const VellumRect *)(const void *)list->rects.data;
}

const VellumTexturedQuad *vellum_draw_list_quads(const VellumDrawList *list, size_t *count)
{
    if (count != NULL) {
        *count = list == NULL ? 0u : vellum_array_size(&list->quads);
    }
    return list == NULL ? NULL : (const VellumTexturedQuad *)(const void *)list->quads.data;
}

size_t vellum_draw_list_command_count(const VellumDrawList *list)
{
    if (list == NULL) {
        return 0u;
    }
    return vellum_array_size(&list->rects) + vellum_array_size(&list->quads);
}
