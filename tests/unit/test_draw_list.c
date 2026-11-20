/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "tests/unit/tests.h"
#include "vellum/graphics/draw_list.h"

static bool clip_is(const VellumDrawList *list, VellumClipId id, bool enabled, float x, float y,
                    float width, float height, const char *label)
{
    bool actual_enabled = !enabled;
    float ax = 0.0f;
    float ay = 0.0f;
    float aw = 0.0f;
    float ah = 0.0f;
    if (!vellum_draw_list_clip_rect(list, id, &actual_enabled, &ax, &ay, &aw, &ah) ||
        actual_enabled != enabled || ax != x || ay != y || aw != width || ah != height) {
        fprintf(
            stderr,
            "draw list clip %s: expected (%d,%.1f,%.1f,%.1f,%.1f), got (%d,%.1f,%.1f,%.1f,%.1f)\n",
            label, enabled ? 1 : 0, x, y, width, height, actual_enabled ? 1 : 0, ax, ay, aw, ah);
        return false;
    }
    return true;
}

static bool clip_stack_basics(void)
{
    VellumDrawList *list = vellum_draw_list_create(4u, 4u);
    if (list == NULL) {
        return false;
    }
    bool ok = vellum_draw_list_clip_id(list) == VELLUM_CLIP_NONE &&
              vellum_draw_list_clip_depth(list) == 0u && !vellum_draw_list_pop_clip(list);
    ok = clip_is(list, VELLUM_CLIP_NONE, false, 0.0f, 0.0f, 0.0f, 0.0f, "root disabled") && ok;
    ok = !vellum_draw_list_clip_rect(list, 1u, NULL, NULL, NULL, NULL, NULL) && ok;

    ok = vellum_draw_list_push_clip(list, 5.0f, 6.0f, 7.0f, 8.0f) && ok;
    const VellumClipId first = vellum_draw_list_clip_id(list);
    ok = first == 1u && vellum_draw_list_clip_depth(list) == 1u && ok;
    ok = clip_is(list, first, true, 5.0f, 6.0f, 7.0f, 8.0f, "first") && ok;

    ok = vellum_draw_list_push_clip(list, 100.0f, 100.0f, -2.0f, -3.0f) && ok;
    const VellumClipId empty = vellum_draw_list_clip_id(list);
    ok = empty == 2u && vellum_draw_list_clip_depth(list) == 2u && ok;
    ok = clip_is(list, empty, true, 100.0f, 100.0f, 0.0f, 0.0f, "negative size") && ok;

    ok = vellum_draw_list_pop_clip(list) && ok;
    ok = vellum_draw_list_clip_id(list) == first && vellum_draw_list_clip_depth(list) == 1u && ok;
    ok = vellum_draw_list_pop_clip(list) && ok;
    ok = vellum_draw_list_clip_id(list) == VELLUM_CLIP_NONE &&
         vellum_draw_list_clip_depth(list) == 0u && ok;
    ok = !vellum_draw_list_pop_clip(list) && ok;

    ok = !vellum_draw_list_push_clip(NULL, 0.0f, 0.0f, 1.0f, 1.0f) &&
         !vellum_draw_list_pop_clip(NULL) && vellum_draw_list_clip_id(NULL) == VELLUM_CLIP_NONE &&
         vellum_draw_list_clip_depth(NULL) == 0u &&
         !vellum_draw_list_clip_rect(NULL, 0u, NULL, NULL, NULL, NULL, NULL) && ok;
    size_t count = 99u;
    const VellumDrawCommand *commands = vellum_draw_list_commands(NULL, &count);
    ok = commands == NULL && count == 0u && ok;

    vellum_draw_list_destroy(list);
    return ok;
}

static bool nested_clips(void)
{
    VellumDrawList *list = vellum_draw_list_create(4u, 4u);
    if (list == NULL) {
        return false;
    }
    bool ok = true;
    vellum_draw_list_set_scissor(list, true, 10.0f, 20.0f, 100.0f, 100.0f);
    ok = clip_is(list, VELLUM_CLIP_NONE, true, 10.0f, 20.0f, 100.0f, 100.0f, "root scissor") && ok;

    ok = vellum_draw_list_push_clip(list, 0.0f, 0.0f, 50.0f, 50.0f) && ok;
    const VellumClipId outer = vellum_draw_list_clip_id(list);
    ok = clip_is(list, outer, true, 10.0f, 20.0f, 40.0f, 30.0f, "outer intersect") && ok;

    ok = vellum_draw_list_push_clip(list, 30.0f, 30.0f, 40.0f, 40.0f) && ok;
    const VellumClipId inner = vellum_draw_list_clip_id(list);
    ok = inner == 2u && vellum_draw_list_clip_depth(list) == 2u && ok;
    ok = clip_is(list, inner, true, 30.0f, 30.0f, 20.0f, 20.0f, "inner intersect") && ok;

    ok = vellum_draw_list_pop_clip(list) && ok;
    ok = vellum_draw_list_clip_id(list) == outer &&
         clip_is(list, outer, true, 10.0f, 20.0f, 40.0f, 30.0f, "outer restored") && ok;
    ok = vellum_draw_list_pop_clip(list) && ok;
    ok = vellum_draw_list_clip_id(list) == VELLUM_CLIP_NONE &&
         vellum_draw_list_clip_depth(list) == 0u && ok;

    /* A clip entirely outside its parent becomes empty, but stays enabled. */
    ok = vellum_draw_list_push_clip(list, 500.0f, 500.0f, 10.0f, 10.0f) && ok;
    ok = clip_is(list, vellum_draw_list_clip_id(list), true, 500.0f, 500.0f, 0.0f, 0.0f,
                 "disjoint") &&
         ok;
    ok = vellum_draw_list_pop_clip(list) && ok;

    vellum_draw_list_clear(list);
    ok =
        vellum_draw_list_clip_depth(list) == 0u && vellum_draw_list_command_count(list) == 0u && ok;
    ok = clip_is(list, VELLUM_CLIP_NONE, false, 0.0f, 0.0f, 0.0f, 0.0f, "cleared root") && ok;

    vellum_draw_list_destroy(list);
    return ok;
}

static bool clip_command_stream(void)
{
    VellumDrawList *list = vellum_draw_list_create(8u, 8u);
    if (list == NULL) {
        return false;
    }
    const VellumColor white = vellum_color(1.0f, 1.0f, 1.0f, 1.0f);
    bool ok = true;

    vellum_draw_list_set_scissor(list, true, 0.0f, 0.0f, 100.0f, 100.0f);
    ok = vellum_draw_list_push_clip(list, 10.0f, 10.0f, 50.0f, 50.0f) && ok;
    const VellumClipId outer = vellum_draw_list_clip_id(list);
    ok = vellum_draw_list_rect(list, 0.0f, 0.0f, 1.0f, 1.0f, white) &&
         vellum_draw_list_textured_quad(list, VELLUM_TEXTURE_NONE, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f,
                                        0.0f, 1.0f, 1.0f, white) &&
         ok;
    ok = vellum_draw_list_pop_clip(list) && ok;
    ok = vellum_draw_list_rect(list, 2.0f, 2.0f, 1.0f, 1.0f, white) && ok;
    ok = vellum_draw_list_push_clip(list, 20.0f, 20.0f, 30.0f, 30.0f) && ok;
    const VellumClipId inner = vellum_draw_list_clip_id(list);
    ok = vellum_draw_list_textured_quad(list, 42u, 3.0f, 3.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
                                        white) &&
         ok;

    size_t count = 0u;
    const VellumRect *rects = vellum_draw_list_rects(list, &count);
    ok = count == 2u && rects != NULL && rects[0].clip == outer &&
         rects[1].clip == VELLUM_CLIP_NONE && ok;
    const VellumTexturedQuad *quads = vellum_draw_list_quads(list, &count);
    ok = count == 2u && quads != NULL && quads[0].clip == outer && quads[1].clip == inner && ok;

    const VellumDrawCommand *commands = vellum_draw_list_commands(list, &count);
    if (commands == NULL || count != 4u || vellum_draw_list_command_count(list) != 4u) {
        fprintf(stderr, "draw list command stream count is wrong\n");
        ok = false;
    } else {
        ok = commands[0].kind == VELLUM_DRAW_RECT && commands[0].index == 0u &&
             commands[1].kind == VELLUM_DRAW_QUAD && commands[1].index == 0u &&
             commands[2].kind == VELLUM_DRAW_RECT && commands[2].index == 1u &&
             commands[3].kind == VELLUM_DRAW_QUAD && commands[3].index == 1u && ok;
        if (!ok) {
            fprintf(stderr, "draw list command stream order is wrong\n");
        }
    }

    vellum_draw_list_destroy(list);
    return ok;
}

bool test_draw_list(void)
{
    VellumDrawList *list = vellum_draw_list_create(4u, 4u);
    if (list == NULL) {
        return false;
    }

    size_t count = 99u;
    (void)vellum_draw_list_rects(list, &count);
    const bool no_rects = count == 0u;
    (void)vellum_draw_list_quads(list, &count);
    if (!no_rects || count != 0u || vellum_draw_list_command_count(list) != 0u) {
        vellum_draw_list_destroy(list);
        return false;
    }

    const VellumColor red = vellum_color(1.0f, 0.0f, 0.0f, 1.0f);
    const VellumColor blue = vellum_color(0.0f, 0.0f, 1.0f, 0.5f);
    if (!vellum_draw_list_rect(list, 1.0f, 2.0f, 3.0f, 4.0f, red) ||
        !vellum_draw_list_rect(list, 5.0f, 6.0f, 7.0f, 8.0f, blue) ||
        !vellum_draw_list_textured_quad(list, VELLUM_TEXTURE_NONE, 0.0f, 0.0f, 8.0f, 8.0f, 0.0f,
                                        0.0f, 1.0f, 1.0f, blue) ||
        !vellum_draw_list_textured_quad(list, 42u, 10.0f, 10.0f, 4.0f, 4.0f, 0.0f, 0.0f, 1.0f, 1.0f,
                                        red)) {
        vellum_draw_list_destroy(list);
        return false;
    }
    if (vellum_draw_list_command_count(list) != 4u) {
        vellum_draw_list_destroy(list);
        return false;
    }

    const VellumRect *rects = vellum_draw_list_rects(list, &count);
    if (rects == NULL || count != 2u || rects[0].x != 1.0f || rects[0].height != 4.0f ||
        rects[1].color.a != 0.5f) {
        vellum_draw_list_destroy(list);
        return false;
    }
    const VellumTexturedQuad *quads = vellum_draw_list_quads(list, &count);
    if (quads == NULL || count != 2u || quads[0].u1 != 1.0f || quads[0].width != 8.0f ||
        quads[0].texture != VELLUM_TEXTURE_NONE || quads[1].texture != 42u) {
        vellum_draw_list_destroy(list);
        return false;
    }

    vellum_draw_list_set_clear_color(list, blue);
    if (vellum_draw_list_clear_color(list).a != 0.5f) {
        vellum_draw_list_destroy(list);
        return false;
    }

    bool enabled = false;
    float x = 0.0f;
    vellum_draw_list_set_scissor(list, true, 10.0f, 20.0f, 30.0f, 40.0f);
    if (!vellum_draw_list_scissor(list, &enabled, &x, NULL, NULL, NULL) || !enabled || x != 10.0f) {
        vellum_draw_list_destroy(list);
        return false;
    }

    vellum_draw_list_clear(list);
    if (vellum_draw_list_command_count(list) != 0u ||
        !vellum_draw_list_scissor(list, &enabled, NULL, NULL, NULL, NULL) || enabled ||
        vellum_draw_list_clear_color(list).a != 0.5f) {
        vellum_draw_list_destroy(list);
        return false;
    }

    vellum_draw_list_destroy(list);

    if (!clip_stack_basics()) {
        fprintf(stderr, "draw list clip_stack_basics failed\n");
        return false;
    }
    if (!nested_clips()) {
        fprintf(stderr, "draw list nested_clips failed\n");
        return false;
    }
    if (!clip_command_stream()) {
        fprintf(stderr, "draw list clip_command_stream failed\n");
        return false;
    }
    return true;
}
