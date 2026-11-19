/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "tests/unit/tests.h"
#include "vellum/config/theme.h"
#include "vellum/graphics/draw_list.h"
#include "vellum/ui/panels.h"

static bool bounds_eq(VellumBounds b, float x, float y, float width, float height)
{
    return b.x == x && b.y == y && b.width == width && b.height == height;
}

static bool near(float a, float b)
{
    const float delta = a - b;
    return delta >= -0.001f && delta <= 0.001f;
}

static VellumInput key_input(VellumKey key)
{
    VellumInput input;
    memset(&input, 0, sizeof(input));
    input.type = VELLUM_INPUT_KEY;
    input.data.key.key = key;
    return input;
}

static VellumInput pointer_input(float x, float y, uint8_t button, bool pressed, bool motion)
{
    VellumInput input;
    memset(&input, 0, sizeof(input));
    input.type = motion ? VELLUM_INPUT_POINTER_MOTION : VELLUM_INPUT_POINTER_BUTTON;
    input.data.pointer.x = x;
    input.data.pointer.y = y;
    input.data.pointer.button = button;
    input.data.pointer.pressed = pressed;
    return input;
}

static bool color_eq(VellumColor a, VellumColor b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static size_t find_rect(const VellumDrawList *list, VellumBounds bounds)
{
    size_t count = 0u;
    const VellumRect *rects = vellum_draw_list_rects(list, &count);
    for (size_t i = 0u; i < count; ++i) {
        if (bounds_eq((VellumBounds){rects[i].x, rects[i].y, rects[i].width, rects[i].height},
                      bounds.x, bounds.y, bounds.width, bounds.height)) {
            return i;
        }
    }
    return SIZE_MAX;
}

static bool layout_geometry(void)
{
    VellumErrorInfo error;
    VellumUiState state = vellum_ui_state_default();
    VellumWidget *root = vellum_ui_create(&state, &error);
    if (root == NULL) {
        return false;
    }

    VellumWidget *row = vellum_widget_child(root, 0u);
    VellumWidget *status = vellum_widget_child(root, 1u);
    VellumWidget *activity = vellum_widget_child(row, 0u);
    VellumWidget *sidebar = vellum_widget_child(row, 1u);
    VellumWidget *splitter = vellum_widget_child(row, 2u);
    VellumWidget *column = vellum_widget_child(row, 3u);
    VellumWidget *tabs = vellum_widget_child(column, 0u);
    VellumWidget *editor = vellum_widget_child(column, 1u);
    bool ok = row != NULL && status != NULL && activity != NULL && sidebar != NULL &&
              splitter != NULL && column != NULL && tabs != NULL && editor != NULL &&
              vellum_ui_editor(root) == editor &&
              vellum_widget_kind(activity) == VELLUM_WIDGET_ACTIVITY_BAR &&
              vellum_widget_kind(sidebar) == VELLUM_WIDGET_SIDEBAR &&
              vellum_widget_kind(splitter) == VELLUM_WIDGET_SPLITTER &&
              vellum_widget_kind(tabs) == VELLUM_WIDGET_TAB_STRIP &&
              vellum_widget_kind(editor) == VELLUM_WIDGET_EDITOR &&
              vellum_widget_kind(status) == VELLUM_WIDGET_STATUS_BAR;

    VellumUiContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    const VellumBounds window = {0.0f, 0.0f, 1000.0f, 700.0f};
    vellum_widget_layout(root, window, &ctx);

    ok = ok && bounds_eq(vellum_widget_bounds(status), 0.0f, 676.0f, 1000.0f, 24.0f) &&
         bounds_eq(vellum_widget_bounds(row), 0.0f, 0.0f, 1000.0f, 676.0f) &&
         bounds_eq(vellum_widget_bounds(activity), 0.0f, 0.0f, 44.0f, 676.0f) &&
         bounds_eq(vellum_widget_bounds(sidebar), 44.0f, 0.0f, 220.0f, 676.0f) &&
         bounds_eq(vellum_widget_bounds(splitter), 264.0f, 0.0f, 4.0f, 676.0f) &&
         bounds_eq(vellum_widget_bounds(column), 268.0f, 0.0f, 732.0f, 676.0f) &&
         bounds_eq(vellum_widget_bounds(tabs), 268.0f, 0.0f, 732.0f, 28.0f) &&
         bounds_eq(vellum_widget_bounds(editor), 268.0f, 28.0f, 732.0f, 648.0f);

    vellum_widget_destroy(root);
    return ok;
}

static bool layout_visibility(void)
{
    VellumErrorInfo error;
    VellumUiState state = vellum_ui_state_default();
    VellumWidget *root = vellum_ui_create(&state, &error);
    if (root == NULL) {
        return false;
    }
    VellumWidget *row = vellum_widget_child(root, 0u);
    VellumWidget *status = vellum_widget_child(root, 1u);
    VellumWidget *activity = vellum_widget_child(row, 0u);
    VellumWidget *sidebar = vellum_widget_child(row, 1u);
    VellumWidget *splitter = vellum_widget_child(row, 2u);
    VellumWidget *column = vellum_widget_child(row, 3u);
    VellumWidget *editor = vellum_widget_child(column, 1u);

    vellum_ui_apply_settings(root, &state, false, false);
    bool ok = !vellum_widget_visible(sidebar) && !vellum_widget_visible(splitter) &&
              !vellum_widget_visible(status) && !state.show_sidebar && !state.show_status_bar;

    VellumUiContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    const VellumBounds window = {0.0f, 0.0f, 1000.0f, 700.0f};
    vellum_widget_layout(root, window, &ctx);
    ok = ok && bounds_eq(vellum_widget_bounds(row), 0.0f, 0.0f, 1000.0f, 700.0f) &&
         bounds_eq(vellum_widget_bounds(activity), 0.0f, 0.0f, 44.0f, 700.0f) &&
         bounds_eq(vellum_widget_bounds(column), 44.0f, 0.0f, 956.0f, 700.0f) &&
         bounds_eq(vellum_widget_bounds(editor), 44.0f, 28.0f, 956.0f, 672.0f);

    vellum_ui_apply_settings(root, &state, true, true);
    ok = ok && vellum_widget_visible(sidebar) && vellum_widget_visible(splitter) &&
         vellum_widget_visible(status) && state.show_sidebar && state.show_status_bar;
    vellum_widget_layout(root, window, &ctx);
    ok = ok && bounds_eq(vellum_widget_bounds(row), 0.0f, 0.0f, 1000.0f, 676.0f) &&
         bounds_eq(vellum_widget_bounds(column), 268.0f, 0.0f, 732.0f, 676.0f);

    vellum_widget_destroy(root);
    return ok;
}

static bool splitter_resize(void)
{
    VellumErrorInfo error;
    VellumUiState state = vellum_ui_state_default();
    VellumWidget *root = vellum_ui_create(&state, &error);
    if (root == NULL) {
        return false;
    }
    VellumWidget *row = vellum_widget_child(root, 0u);
    VellumWidget *sidebar = vellum_widget_child(row, 1u);
    VellumWidget *splitter = vellum_widget_child(row, 2u);

    VellumUiContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    const VellumBounds window = {0.0f, 0.0f, 1000.0f, 700.0f};
    vellum_widget_layout(root, window, &ctx);
    bool ok = near(state.sidebar_width, VELLUM_UI_SIDEBAR_DEFAULT);

    VellumInput input = pointer_input(266.0f, 100.0f, VELLUM_POINTER_BUTTON_LEFT, true, false);
    ok = ok && vellum_widget_dispatch_event(root, &input, &ctx);
    input = pointer_input(366.0f, 100.0f, 0u, false, true);
    ok = ok && vellum_widget_dispatch_event(root, &input, &ctx);
    ok = ok && near(state.sidebar_width, 320.0f);
    vellum_widget_layout(root, window, &ctx);
    ok = ok && bounds_eq(vellum_widget_bounds(sidebar), 44.0f, 0.0f, 320.0f, 676.0f) &&
         bounds_eq(vellum_widget_bounds(splitter), 364.0f, 0.0f, 4.0f, 676.0f);
    input = pointer_input(366.0f, 100.0f, VELLUM_POINTER_BUTTON_LEFT, false, false);
    ok = ok && vellum_widget_dispatch_event(root, &input, &ctx);

    /* Dragging far right clamps to the maximum even after the pointer leaves the
     * splitter, because the pressed widget captures pointer motion. */
    input = pointer_input(366.0f, 100.0f, VELLUM_POINTER_BUTTON_LEFT, true, false);
    ok = ok && vellum_widget_dispatch_event(root, &input, &ctx);
    input = pointer_input(1000.0f, 100.0f, 0u, false, true);
    ok = ok && vellum_widget_dispatch_event(root, &input, &ctx);
    ok = ok && near(state.sidebar_width, VELLUM_UI_SIDEBAR_MAX);
    input = pointer_input(1000.0f, 100.0f, VELLUM_POINTER_BUTTON_LEFT, false, false);
    ok = ok && vellum_widget_dispatch_event(root, &input, &ctx);
    vellum_widget_layout(root, window, &ctx);
    ok = ok && bounds_eq(vellum_widget_bounds(splitter), 444.0f, 0.0f, 4.0f, 676.0f);

    input = pointer_input(446.0f, 100.0f, VELLUM_POINTER_BUTTON_LEFT, true, false);
    ok = ok && vellum_widget_dispatch_event(root, &input, &ctx);
    input = pointer_input(0.0f, 100.0f, 0u, false, true);
    ok = ok && vellum_widget_dispatch_event(root, &input, &ctx);
    ok = ok && near(state.sidebar_width, VELLUM_UI_SIDEBAR_MIN);
    input = pointer_input(0.0f, 100.0f, VELLUM_POINTER_BUTTON_LEFT, false, false);
    ok = ok && vellum_widget_dispatch_event(root, &input, &ctx);

    ok = ok && vellum_widget_set_focus(splitter);
    input = key_input(VELLUM_KEY_RIGHT);
    ok = ok && vellum_widget_dispatch_event(root, &input, &ctx);
    ok = ok && near(state.sidebar_width, VELLUM_UI_SIDEBAR_MIN + 8.0f);
    input = key_input(VELLUM_KEY_LEFT);
    ok = ok && vellum_widget_dispatch_event(root, &input, &ctx);
    ok = ok && near(state.sidebar_width, VELLUM_UI_SIDEBAR_MIN);

    vellum_widget_destroy(root);
    return ok;
}

static bool tab_switching(void)
{
    VellumErrorInfo error;
    VellumUiState state = vellum_ui_state_default();
    VellumWidget *root = vellum_ui_create(&state, &error);
    if (root == NULL) {
        return false;
    }
    VellumWidget *row = vellum_widget_child(root, 0u);
    VellumWidget *column = vellum_widget_child(row, 3u);
    VellumWidget *tabs = vellum_widget_child(column, 0u);

    VellumUiContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    const VellumBounds window = {0.0f, 0.0f, 1000.0f, 700.0f};
    vellum_widget_layout(root, window, &ctx);
    const VellumBounds strip = vellum_widget_bounds(tabs);
    const VellumBounds tab0 = {strip.x + 4.0f, strip.y + 2.0f, 124.0f, strip.height - 4.0f};
    const VellumBounds tab1 = {strip.x + 132.0f, strip.y + 2.0f, 124.0f, strip.height - 4.0f};

    VellumDrawList *list = vellum_draw_list_create(256u, 256u);
    VellumTheme theme = vellum_theme_dark();
    if (list == NULL) {
        vellum_widget_destroy(root);
        return false;
    }
    ctx.draw_list = list;
    ctx.theme = &theme;
    const VellumColor background = vellum_theme_color(&theme, VELLUM_THEME_BACKGROUND);
    const VellumColor tab_bar = vellum_theme_color(&theme, VELLUM_THEME_TAB_BAR);
    bool ok = !color_eq(background, tab_bar);

    VellumErrorInfo paint_error;
    vellum_draw_list_clear(list);
    ok = ok && vellum_widget_paint(tabs, &ctx, &paint_error);
    size_t count = 0u;
    const VellumRect *rects = vellum_draw_list_rects(list, &count);
    const size_t first = find_rect(list, tab0);
    const size_t second = find_rect(list, tab1);
    ok = ok && first < count && second < count && color_eq(rects[first].color, background) &&
         color_eq(rects[second].color, tab_bar);

    ok = ok && vellum_widget_set_focus(tabs);
    VellumInput input = key_input(VELLUM_KEY_RIGHT);
    ok = ok && vellum_widget_dispatch_event(root, &input, &ctx);
    vellum_draw_list_clear(list);
    ok = ok && vellum_widget_paint(tabs, &ctx, &paint_error);
    rects = vellum_draw_list_rects(list, &count);
    ok = ok && find_rect(list, tab0) < count && find_rect(list, tab1) < count &&
         color_eq(rects[find_rect(list, tab0)].color, tab_bar) &&
         color_eq(rects[find_rect(list, tab1)].color, background);

    input =
        pointer_input(strip.x + 20.0f, strip.y + 10.0f, VELLUM_POINTER_BUTTON_LEFT, true, false);
    ok = ok && vellum_widget_dispatch_event(root, &input, &ctx);
    input =
        pointer_input(strip.x + 20.0f, strip.y + 10.0f, VELLUM_POINTER_BUTTON_LEFT, false, false);
    (void)vellum_widget_dispatch_event(root, &input, &ctx);
    vellum_draw_list_clear(list);
    ok = ok && vellum_widget_paint(tabs, &ctx, &paint_error);
    rects = vellum_draw_list_rects(list, &count);
    ok = ok && find_rect(list, tab0) < count &&
         color_eq(rects[find_rect(list, tab0)].color, background);

    vellum_draw_list_destroy(list);
    vellum_widget_destroy(root);
    return ok;
}

static bool activity_toggle(void)
{
    VellumErrorInfo error;
    VellumUiState state = vellum_ui_state_default();
    VellumWidget *root = vellum_ui_create(&state, &error);
    if (root == NULL) {
        return false;
    }
    VellumWidget *row = vellum_widget_child(root, 0u);
    VellumWidget *activity = vellum_widget_child(row, 0u);
    VellumWidget *sidebar = vellum_widget_child(row, 1u);
    VellumWidget *splitter = vellum_widget_child(row, 2u);
    VellumWidget *column = vellum_widget_child(row, 3u);

    VellumUiContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    const VellumBounds window = {0.0f, 0.0f, 1000.0f, 700.0f};
    vellum_widget_layout(root, window, &ctx);
    bool ok =
        state.show_sidebar && vellum_widget_visible(sidebar) && vellum_widget_visible(splitter);

    ok = ok && vellum_widget_set_focus(activity);
    VellumInput input = key_input(VELLUM_KEY_ENTER);
    ok = ok && vellum_widget_dispatch_event(root, &input, &ctx);
    ok = ok && !state.show_sidebar && !vellum_widget_visible(sidebar) &&
         !vellum_widget_visible(splitter);
    vellum_widget_layout(root, window, &ctx);
    ok = ok && bounds_eq(vellum_widget_bounds(column), 44.0f, 0.0f, 956.0f, 676.0f);

    input = key_input(VELLUM_KEY_SPACE);
    ok = ok && vellum_widget_dispatch_event(root, &input, &ctx);
    ok = ok && state.show_sidebar && vellum_widget_visible(sidebar) &&
         vellum_widget_visible(splitter);
    vellum_widget_layout(root, window, &ctx);
    ok = ok && bounds_eq(vellum_widget_bounds(column), 268.0f, 0.0f, 732.0f, 676.0f);

    vellum_widget_destroy(root);
    return ok;
}

static bool paint_behavior(void)
{
    VellumErrorInfo error;
    if (vellum_ui_create(NULL, &error) != NULL || error.code != VELLUM_ERR_INVALID_ARGUMENT) {
        return false;
    }

    VellumUiState state = vellum_ui_state_default();
    VellumWidget *root = vellum_ui_create(&state, &error);
    if (root == NULL) {
        return false;
    }
    VellumUiContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    VellumErrorInfo paint_error;
    bool ok = !vellum_widget_paint(root, &ctx, &paint_error) &&
              paint_error.code == VELLUM_ERR_INVALID_ARGUMENT;

    VellumDrawList *list = vellum_draw_list_create(256u, 256u);
    VellumTheme theme = vellum_theme_dark();
    if (list == NULL) {
        vellum_widget_destroy(root);
        return false;
    }
    ctx.draw_list = list;
    ctx.theme = &theme;
    const VellumBounds window = {0.0f, 0.0f, 1000.0f, 700.0f};
    vellum_widget_layout(root, window, &ctx);
    ok = ok && vellum_widget_paint(root, &ctx, &paint_error);
    size_t count = 0u;
    vellum_draw_list_rects(list, &count);
    ok = ok && count > 0u;

    vellum_draw_list_destroy(list);
    vellum_widget_destroy(root);
    return ok;
}

bool test_panels(void)
{
    return layout_geometry() && layout_visibility() && splitter_resize() && tab_switching() &&
           activity_toggle() && paint_behavior();
}
