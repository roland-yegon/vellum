/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#include "vellum/graphics/draw_list.h"

#include <stdint.h>
#include <stdlib.h>

#include "vellum/core/array.h"

/* Index 0 is the root scissor. Nested entries hold already-intersected rects,
 * so the renderer only needs the id recorded on each draw command. */
typedef struct {
    bool enabled;
    float x;
    float y;
    float width;
    float height;
} VellumClip;

struct VellumDrawList {
    VellumArray rects;
    VellumArray quads;
    VellumArray commands;
    VellumArray clips;
    VellumArray clip_stack;
    VellumColor clear_color;
    bool scissor_enabled;
    float scissor_x;
    float scissor_y;
    float scissor_width;
    float scissor_height;
};

static VellumClip root_clip(void)
{
    const VellumClip clip = {false, 0.0f, 0.0f, 0.0f, 0.0f};
    return clip;
}

static VellumClipId current_clip(const VellumDrawList *list)
{
    const size_t depth = vellum_array_size(&list->clip_stack);
    if (depth == 0u) {
        return VELLUM_CLIP_NONE;
    }
    const VellumClipId *ids = (const VellumClipId *)(const void *)list->clip_stack.data;
    return ids[depth - 1u];
}

static void set_root_clip(VellumDrawList *list)
{
    VellumClip *root = (VellumClip *)vellum_array_at(&list->clips, 0u);
    if (root != NULL) {
        *root = root_clip();
    }
}

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

    size_t command_capacity = rect_capacity;
    if (quad_capacity > SIZE_MAX - command_capacity) {
        command_capacity = 0u;
    } else {
        command_capacity += quad_capacity;
    }

    if (!vellum_array_init(&list->rects, sizeof(VellumRect), rect_capacity) ||
        !vellum_array_init(&list->quads, sizeof(VellumTexturedQuad), quad_capacity) ||
        !vellum_array_init(&list->commands, sizeof(VellumDrawCommand), command_capacity) ||
        !vellum_array_init(&list->clips, sizeof(VellumClip), 4u) ||
        !vellum_array_init(&list->clip_stack, sizeof(VellumClipId), 4u) ||
        vellum_array_push(&list->clips) == NULL) {
        vellum_array_deinit(&list->rects);
        vellum_array_deinit(&list->quads);
        vellum_array_deinit(&list->commands);
        vellum_array_deinit(&list->clips);
        vellum_array_deinit(&list->clip_stack);
        free(list);
        return NULL;
    }

    set_root_clip(list);
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
    vellum_array_deinit(&list->commands);
    vellum_array_deinit(&list->clips);
    vellum_array_deinit(&list->clip_stack);
    free(list);
}

void vellum_draw_list_clear(VellumDrawList *list)
{
    if (list == NULL) {
        return;
    }
    vellum_array_clear(&list->rects);
    vellum_array_clear(&list->quads);
    vellum_array_clear(&list->commands);
    vellum_array_clear(&list->clip_stack);
    set_root_clip(list);
    list->scissor_enabled = false;
}

bool vellum_draw_list_rect(VellumDrawList *list, float x, float y, float width, float height,
                           VellumColor color)
{
    if (list == NULL) {
        return false;
    }
    const size_t index = vellum_array_size(&list->rects);
    VellumDrawCommand *command = (VellumDrawCommand *)vellum_array_push(&list->commands);
    if (command == NULL) {
        return false;
    }
    VellumRect *rect = (VellumRect *)vellum_array_push(&list->rects);
    if (rect == NULL) {
        vellum_array_remove_swap(&list->commands, vellum_array_size(&list->commands) - 1u);
        return false;
    }
    rect->x = x;
    rect->y = y;
    rect->width = width;
    rect->height = height;
    rect->color = color;
    rect->clip = current_clip(list);
    command->kind = VELLUM_DRAW_RECT;
    command->index = index;
    return true;
}

bool vellum_draw_list_textured_quad(VellumDrawList *list, VellumTextureHandle texture, float x,
                                    float y, float width, float height, float u0, float v0,
                                    float u1, float v1, VellumColor tint)
{
    if (list == NULL) {
        return false;
    }
    const size_t index = vellum_array_size(&list->quads);
    VellumDrawCommand *command = (VellumDrawCommand *)vellum_array_push(&list->commands);
    if (command == NULL) {
        return false;
    }
    VellumTexturedQuad *quad = (VellumTexturedQuad *)vellum_array_push(&list->quads);
    if (quad == NULL) {
        vellum_array_remove_swap(&list->commands, vellum_array_size(&list->commands) - 1u);
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
    quad->clip = current_clip(list);
    command->kind = VELLUM_DRAW_QUAD;
    command->index = index;
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
    VellumClip *root = (VellumClip *)vellum_array_at(&list->clips, 0u);
    if (root != NULL) {
        root->enabled = enabled;
        root->x = x;
        root->y = y;
        root->width = width;
        root->height = height;
    }
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

bool vellum_draw_list_push_clip(VellumDrawList *list, float x, float y, float width, float height)
{
    if (list == NULL || vellum_array_size(&list->clips) > UINT32_MAX) {
        return false;
    }

    VellumClip parent = root_clip();
    const VellumClipId parent_id = current_clip(list);
    const VellumClip *stored =
        (const VellumClip *)(const void *)vellum_array_at_const(&list->clips, (size_t)parent_id);
    if (stored != NULL) {
        parent = *stored;
    }

    float ix = x;
    float iy = y;
    float iw = width > 0.0f ? width : 0.0f;
    float ih = height > 0.0f ? height : 0.0f;
    if (parent.enabled) {
        const float left = ix > parent.x ? ix : parent.x;
        const float top = iy > parent.y ? iy : parent.y;
        const float right = ix + iw < parent.x + parent.width ? ix + iw : parent.x + parent.width;
        const float bottom =
            iy + ih < parent.y + parent.height ? iy + ih : parent.y + parent.height;
        ix = left;
        iy = top;
        iw = right > left ? right - left : 0.0f;
        ih = bottom > top ? bottom - top : 0.0f;
    }

    const VellumClipId id = (VellumClipId)vellum_array_size(&list->clips);
    VellumClip *clip = (VellumClip *)vellum_array_push(&list->clips);
    if (clip == NULL) {
        return false;
    }
    clip->enabled = true;
    clip->x = ix;
    clip->y = iy;
    clip->width = iw;
    clip->height = ih;

    VellumClipId *slot = (VellumClipId *)vellum_array_push(&list->clip_stack);
    if (slot == NULL) {
        vellum_array_remove_swap(&list->clips, (size_t)id);
        return false;
    }
    *slot = id;
    return true;
}

bool vellum_draw_list_pop_clip(VellumDrawList *list)
{
    if (list == NULL) {
        return false;
    }
    const size_t depth = vellum_array_size(&list->clip_stack);
    if (depth == 0u) {
        return false;
    }
    vellum_array_remove_swap(&list->clip_stack, depth - 1u);
    return true;
}

VellumClipId vellum_draw_list_clip_id(const VellumDrawList *list)
{
    return list == NULL ? VELLUM_CLIP_NONE : current_clip(list);
}

size_t vellum_draw_list_clip_depth(const VellumDrawList *list)
{
    return list == NULL ? 0u : vellum_array_size(&list->clip_stack);
}

bool vellum_draw_list_clip_rect(const VellumDrawList *list, VellumClipId id, bool *enabled,
                                float *x, float *y, float *width, float *height)
{
    if (list == NULL || (size_t)id >= vellum_array_size(&list->clips)) {
        return false;
    }
    const VellumClip *clip =
        (const VellumClip *)(const void *)vellum_array_at_const(&list->clips, (size_t)id);
    if (clip == NULL) {
        return false;
    }
    if (enabled != NULL) {
        *enabled = clip->enabled;
    }
    if (x != NULL) {
        *x = clip->x;
    }
    if (y != NULL) {
        *y = clip->y;
    }
    if (width != NULL) {
        *width = clip->width;
    }
    if (height != NULL) {
        *height = clip->height;
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

const VellumDrawCommand *vellum_draw_list_commands(const VellumDrawList *list, size_t *count)
{
    if (count != NULL) {
        *count = list == NULL ? 0u : vellum_array_size(&list->commands);
    }
    return list == NULL ? NULL : (const VellumDrawCommand *)(const void *)list->commands.data;
}

size_t vellum_draw_list_command_count(const VellumDrawList *list)
{
    return list == NULL ? 0u : vellum_array_size(&list->commands);
}
