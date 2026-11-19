/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#include <stdbool.h>
#include <stddef.h>

#include "tests/unit/tests.h"
#include "vellum/graphics/draw_list.h"

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
        !vellum_draw_list_textured_quad(list, 0.0f, 0.0f, 8.0f, 8.0f, 0.0f, 0.0f, 1.0f, 1.0f,
                                        blue)) {
        vellum_draw_list_destroy(list);
        return false;
    }
    if (vellum_draw_list_command_count(list) != 3u) {
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
    if (quads == NULL || count != 1u || quads[0].u1 != 1.0f || quads[0].width != 8.0f) {
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
    return true;
}
