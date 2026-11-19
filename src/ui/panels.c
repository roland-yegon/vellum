/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */
#include "vellum/ui/panels.h"

#include <stdlib.h>
#include <string.h>

#include "vellum/graphics/text_layout.h"

typedef struct {
    VellumUiState *state;
    bool dragging;
    float grab_x;
    float grab_width;
} SplitterImpl;

typedef struct {
    VellumUiState *state;
} ActivityImpl;

typedef struct {
    int active_tab;
} TabStripImpl;

static VellumWidget *root_widget(VellumWidget *widget)
{
    while (vellum_widget_parent(widget) != NULL) {
        widget = vellum_widget_parent(widget);
    }
    return widget;
}

static bool is_focused(VellumWidget *widget)
{
    VellumWidget *root = widget;
    while (vellum_widget_parent(root) != NULL) {
        root = vellum_widget_parent(root);
    }
    return vellum_widget_focused(root) == widget;
}

static bool is_hovered(VellumWidget *widget)
{
    VellumWidget *root = widget;
    while (vellum_widget_parent(root) != NULL) {
        root = vellum_widget_parent(root);
    }
    return vellum_widget_hovered(root) == widget;
}

static float clamp_sidebar(float width)
{
    if (width < VELLUM_UI_SIDEBAR_MIN) {
        return VELLUM_UI_SIDEBAR_MIN;
    }
    if (width > VELLUM_UI_SIDEBAR_MAX) {
        return VELLUM_UI_SIDEBAR_MAX;
    }
    return width;
}

static VellumSize fill_measure(VellumWidget *widget, VellumConstraints constraints,
                               const VellumUiContext *ctx)
{
    (void)widget;
    (void)ctx;
    return vellum_constraints_apply(constraints,
                                    (VellumSize){constraints.max_width, constraints.max_height});
}

static void root_layout(VellumWidget *widget, VellumBounds bounds, const VellumUiContext *ctx)
{
    VellumWidget *main_row = vellum_widget_child(widget, 0u);
    VellumWidget *status_bar = vellum_widget_child(widget, 1u);
    float main_height = bounds.height;
    if (vellum_widget_visible(status_bar)) {
        const float height =
            bounds.height < VELLUM_UI_STATUS_HEIGHT ? bounds.height : VELLUM_UI_STATUS_HEIGHT;
        const VellumBounds status_bounds = {bounds.x, bounds.y + bounds.height - height,
                                            bounds.width, height};
        vellum_widget_layout(status_bar, status_bounds, ctx);
        main_height -= height;
    }
    if (main_row != NULL) {
        const VellumBounds main_bounds = {bounds.x, bounds.y, bounds.width, main_height};
        vellum_widget_layout(main_row, main_bounds, ctx);
    }
}

static void main_row_layout(VellumWidget *widget, VellumBounds bounds, const VellumUiContext *ctx)
{
    VellumWidget *activity = vellum_widget_child(widget, 0u);
    VellumWidget *sidebar = vellum_widget_child(widget, 1u);
    VellumWidget *splitter = vellum_widget_child(widget, 2u);
    VellumWidget *editor_column = vellum_widget_child(widget, 3u);
    float x = bounds.x;
    float remaining = bounds.width;

    const float activity_width =
        bounds.width < VELLUM_UI_ACTIVITY_WIDTH ? bounds.width : VELLUM_UI_ACTIVITY_WIDTH;
    if (activity != NULL) {
        const VellumBounds activity_bounds = {x, bounds.y, activity_width, bounds.height};
        vellum_widget_layout(activity, activity_bounds, ctx);
    }
    x += activity_width;
    remaining -= activity_width;

    if (vellum_widget_visible(sidebar) && sidebar != NULL) {
        const SplitterImpl *split = vellum_widget_impl(splitter);
        const bool has_splitter = vellum_widget_visible(splitter) && split != NULL;
        float width = split != NULL ? split->state->sidebar_width : VELLUM_UI_SIDEBAR_DEFAULT;
        if (width > remaining) {
            width = remaining;
        }
        if (has_splitter && width + VELLUM_UI_SPLITTER_WIDTH > remaining) {
            width =
                remaining > VELLUM_UI_SPLITTER_WIDTH ? remaining - VELLUM_UI_SPLITTER_WIDTH : 0.0f;
        }
        const VellumBounds sidebar_bounds = {x, bounds.y, width, bounds.height};
        vellum_widget_layout(sidebar, sidebar_bounds, ctx);
        x += width;
        remaining -= width;

        if (has_splitter) {
            const float splitter_width =
                remaining < VELLUM_UI_SPLITTER_WIDTH ? remaining : VELLUM_UI_SPLITTER_WIDTH;
            const VellumBounds splitter_bounds = {x, bounds.y, splitter_width, bounds.height};
            vellum_widget_layout(splitter, splitter_bounds, ctx);
            x += splitter_width;
            remaining -= splitter_width;
        }
    }
    if (editor_column != NULL) {
        const VellumBounds editor_bounds = {x, bounds.y, remaining, bounds.height};
        vellum_widget_layout(editor_column, editor_bounds, ctx);
    }
}

static void editor_column_layout(VellumWidget *widget, VellumBounds bounds,
                                 const VellumUiContext *ctx)
{
    VellumWidget *tab_strip = vellum_widget_child(widget, 0u);
    VellumWidget *editor = vellum_widget_child(widget, 1u);
    const float height =
        bounds.height < VELLUM_UI_TAB_STRIP_HEIGHT ? bounds.height : VELLUM_UI_TAB_STRIP_HEIGHT;
    if (tab_strip != NULL) {
        const VellumBounds tab_bounds = {bounds.x, bounds.y, bounds.width, height};
        vellum_widget_layout(tab_strip, tab_bounds, ctx);
    }
    if (editor != NULL) {
        const VellumBounds editor_bounds = {bounds.x, bounds.y + height, bounds.width,
                                            bounds.height - height};
        vellum_widget_layout(editor, editor_bounds, ctx);
    }
}

static bool paint_rect(const VellumUiContext *ctx, VellumBounds bounds, VellumThemeToken token,
                       VellumErrorInfo *error)
{
    if (ctx == NULL || ctx->draw_list == NULL || ctx->theme == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT,
                         "draw list and theme are required for painting");
        return false;
    }
    if (bounds.width <= 0.0f || bounds.height <= 0.0f) {
        return true;
    }
    return vellum_draw_list_rect(ctx->draw_list, bounds.x, bounds.y, bounds.width, bounds.height,
                                 vellum_theme_color(ctx->theme, token));
}

static bool paint_label(const VellumUiContext *ctx, const char *text, float x, float baseline_y,
                        VellumThemeToken token, VellumErrorInfo *error)
{
    if (ctx->shaper == NULL || ctx->fonts == NULL || ctx->atlas_texture == VELLUM_TEXTURE_NONE) {
        return true; /* Text resources are optional for pure layout tests. */
    }
    VellumTextLayoutParams params;
    memset(&params, 0, sizeof(params));
    params.texture = ctx->atlas_texture;
    params.color = vellum_theme_color(ctx->theme, token);
    params.x = x;
    params.baseline_y = baseline_y;
    params.direction = VELLUM_TEXT_LTR;
    return vellum_text_layout_line(ctx->shaper, ctx->fonts, ctx->draw_list, &params, text,
                                   strlen(text), ctx->font_size, NULL, error);
}

static bool paint_focus(VellumWidget *widget, const VellumUiContext *ctx, VellumErrorInfo *error)
{
    if (!is_focused(widget)) {
        return true;
    }
    const VellumBounds b = vellum_widget_bounds(widget);
    const VellumBounds edges[] = {{b.x, b.y, b.width, 1.0f},
                                  {b.x, b.y + b.height - 1.0f, b.width, 1.0f},
                                  {b.x, b.y, 1.0f, b.height},
                                  {b.x + b.width - 1.0f, b.y, 1.0f, b.height}};
    for (size_t i = 0u; i < sizeof(edges) / sizeof(edges[0]); ++i) {
        if (!paint_rect(ctx, edges[i], VELLUM_THEME_ACCENT, error)) {
            return false;
        }
    }
    return true;
}

static bool paint_activity(VellumWidget *widget, const VellumUiContext *ctx, VellumErrorInfo *error)
{
    const VellumBounds b = vellum_widget_bounds(widget);
    if (!paint_rect(ctx, b, VELLUM_THEME_SIDEBAR, error)) {
        return false;
    }
    for (int i = 0; i < 3; ++i) {
        const VellumBounds icon = {b.x + 10.0f, b.y + 12.0f + (float)i * 36.0f, 24.0f, 24.0f};
        const VellumThemeToken token = i == 0 ? VELLUM_THEME_ACCENT : VELLUM_THEME_TAB_BAR;
        if (!paint_rect(ctx, icon, token, error)) {
            return false;
        }
    }
    return paint_focus(widget, ctx, error);
}

static bool paint_sidebar(VellumWidget *widget, const VellumUiContext *ctx, VellumErrorInfo *error)
{
    const VellumBounds b = vellum_widget_bounds(widget);
    if (!paint_rect(ctx, b, VELLUM_THEME_SIDEBAR, error) ||
        !paint_rect(ctx, (VellumBounds){b.x, b.y, b.width, 28.0f}, VELLUM_THEME_TAB_BAR, error)) {
        return false;
    }
    for (int i = 0; i < 5; ++i) {
        const VellumBounds row = {b.x + 8.0f, b.y + 40.0f + (float)i * 26.0f,
                                  b.width > 16.0f ? b.width - 16.0f : 0.0f, 18.0f};
        const VellumThemeToken token =
            i == 0 || is_focused(widget) ? VELLUM_THEME_ACCENT : VELLUM_THEME_TAB_BAR;
        if (!paint_rect(ctx, row, token, error)) {
            return false;
        }
    }
    return paint_label(ctx, "EXPLORER", b.x + 10.0f, b.y + 19.0f, VELLUM_THEME_TEXT, error) &&
           paint_focus(widget, ctx, error);
}

static bool paint_splitter(VellumWidget *widget, const VellumUiContext *ctx, VellumErrorInfo *error)
{
    const VellumBounds b = vellum_widget_bounds(widget);
    if (!paint_rect(ctx, b, VELLUM_THEME_TAB_BAR, error)) {
        return false;
    }
    if (is_hovered(widget) || is_focused(widget)) {
        const VellumBounds line = {b.x + b.width * 0.5f - 1.0f, b.y, 2.0f, b.height};
        if (!paint_rect(ctx, line, VELLUM_THEME_ACCENT, error)) {
            return false;
        }
    }
    return true;
}

static bool paint_tab_strip(VellumWidget *widget, const VellumUiContext *ctx,
                            VellumErrorInfo *error)
{
    const VellumBounds b = vellum_widget_bounds(widget);
    if (!paint_rect(ctx, b, VELLUM_THEME_TAB_BAR, error)) {
        return false;
    }
    const TabStripImpl *tabs = vellum_widget_impl(widget);
    const int active = tabs == NULL ? 0 : tabs->active_tab;
    for (int i = 0; i < 2; ++i) {
        const VellumBounds tab = {b.x + 4.0f + (float)i * 128.0f, b.y + 2.0f, 124.0f,
                                  b.height > 4.0f ? b.height - 4.0f : 0.0f};
        if (!paint_rect(ctx, tab, i == active ? VELLUM_THEME_BACKGROUND : VELLUM_THEME_TAB_BAR,
                        error)) {
            return false;
        }
    }
    if (!paint_label(ctx, "demo.txt", b.x + 12.0f, b.y + 19.0f, VELLUM_THEME_TEXT, error) ||
        !paint_label(ctx, "README", b.x + 140.0f, b.y + 19.0f, VELLUM_THEME_TEXT, error)) {
        return false;
    }
    return paint_focus(widget, ctx, error);
}

static bool paint_editor(VellumWidget *widget, const VellumUiContext *ctx, VellumErrorInfo *error)
{
    const VellumBounds b = vellum_widget_bounds(widget);
    if (!paint_rect(ctx, b, VELLUM_THEME_BACKGROUND, error)) {
        return false;
    }
    if (is_focused(widget)) {
        const VellumBounds caret = {b.x + 12.0f, b.y + 12.0f, 2.0f,
                                    ctx->font_size > 0u ? (float)ctx->font_size : 14.0f};
        if (!paint_rect(ctx, caret, VELLUM_THEME_TEXT, error)) {
            return false;
        }
    }
    return paint_focus(widget, ctx, error);
}

static bool paint_status(VellumWidget *widget, const VellumUiContext *ctx, VellumErrorInfo *error)
{
    const VellumBounds b = vellum_widget_bounds(widget);
    return paint_rect(ctx, b, VELLUM_THEME_TAB_BAR, error) &&
           paint_label(ctx, "Ln 1, Col 1    UTF-8    Vellum", b.x + 10.0f, b.y + 17.0f,
                       VELLUM_THEME_TEXT, error);
}

static bool splitter_event(VellumWidget *widget, const VellumInput *input,
                           const VellumUiContext *ctx)
{
    (void)ctx;
    SplitterImpl *split = vellum_widget_impl(widget);
    if (split == NULL) {
        return false;
    }
    if (input->type == VELLUM_INPUT_POINTER_BUTTON) {
        if (input->data.pointer.button != VELLUM_POINTER_BUTTON_LEFT) {
            return false;
        }
        if (input->data.pointer.pressed) {
            split->dragging = true;
            split->grab_x = input->data.pointer.x;
            split->grab_width = split->state->sidebar_width;
        } else {
            split->dragging = false;
        }
        return true;
    }
    if (input->type == VELLUM_INPUT_POINTER_MOTION && split->dragging) {
        const float width =
            clamp_sidebar(split->grab_width + input->data.pointer.x - split->grab_x);
        if (width != split->state->sidebar_width) {
            split->state->sidebar_width = width;
            vellum_widget_mark_dirty(vellum_widget_parent(widget));
        }
        return true;
    }
    if (input->type == VELLUM_INPUT_KEY &&
        (input->data.key.key == VELLUM_KEY_LEFT || input->data.key.key == VELLUM_KEY_RIGHT)) {
        const float delta = input->data.key.key == VELLUM_KEY_RIGHT ? 8.0f : -8.0f;
        const float width = clamp_sidebar(split->state->sidebar_width + delta);
        if (width != split->state->sidebar_width) {
            split->state->sidebar_width = width;
            vellum_widget_mark_dirty(vellum_widget_parent(widget));
        }
        return true;
    }
    return false;
}

static bool activity_event(VellumWidget *widget, const VellumInput *input,
                           const VellumUiContext *ctx)
{
    (void)ctx;
    ActivityImpl *activity = vellum_widget_impl(widget);
    if (activity == NULL) {
        return false;
    }
    const bool activate = input->type == VELLUM_INPUT_POINTER_BUTTON &&
                          input->data.pointer.pressed &&
                          input->data.pointer.button == VELLUM_POINTER_BUTTON_LEFT;
    const bool key_activate =
        input->type == VELLUM_INPUT_KEY &&
        (input->data.key.key == VELLUM_KEY_ENTER || input->data.key.key == VELLUM_KEY_SPACE);
    if (!activate && !key_activate) {
        return false;
    }
    vellum_ui_apply_settings(root_widget(widget), activity->state, !activity->state->show_sidebar,
                             activity->state->show_status_bar);
    return true;
}

static bool tab_strip_event(VellumWidget *widget, const VellumInput *input,
                            const VellumUiContext *ctx)
{
    (void)ctx;
    TabStripImpl *tabs = vellum_widget_impl(widget);
    if (tabs == NULL) {
        return false;
    }
    if (input->type == VELLUM_INPUT_POINTER_BUTTON && input->data.pointer.pressed &&
        input->data.pointer.button == VELLUM_POINTER_BUTTON_LEFT) {
        const VellumBounds b = vellum_widget_bounds(widget);
        const int active = input->data.pointer.x - b.x < 128.0f ? 0 : 1;
        if (active != tabs->active_tab) {
            tabs->active_tab = active;
            vellum_widget_mark_dirty(widget);
        }
        return true;
    }
    if (input->type == VELLUM_INPUT_KEY &&
        (input->data.key.key == VELLUM_KEY_LEFT || input->data.key.key == VELLUM_KEY_RIGHT)) {
        const int active = input->data.key.key == VELLUM_KEY_LEFT ? 0 : 1;
        if (active != tabs->active_tab) {
            tabs->active_tab = active;
            vellum_widget_mark_dirty(widget);
        }
        return true;
    }
    return false;
}

static const VellumWidgetClass root_class = {fill_measure, root_layout, NULL, NULL, NULL};
static const VellumWidgetClass row_class = {fill_measure, main_row_layout, NULL, NULL, NULL};
static const VellumWidgetClass column_class = {fill_measure, editor_column_layout, NULL, NULL,
                                               NULL};
static const VellumWidgetClass activity_class = {fill_measure, NULL, paint_activity, activity_event,
                                                 NULL};
static const VellumWidgetClass sidebar_class = {fill_measure, NULL, paint_sidebar, NULL, NULL};
static const VellumWidgetClass splitter_class = {fill_measure, NULL, paint_splitter, splitter_event,
                                                 NULL};
static const VellumWidgetClass tab_class = {fill_measure, NULL, paint_tab_strip, tab_strip_event,
                                            NULL};
static const VellumWidgetClass editor_class = {fill_measure, NULL, paint_editor, NULL, NULL};
static const VellumWidgetClass status_class = {fill_measure, NULL, paint_status, NULL, NULL};

VellumWidget *vellum_ui_create(VellumUiState *state, VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (state == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "UI state is required");
        return NULL;
    }

    ActivityImpl *activity_impl = malloc(sizeof(*activity_impl));
    SplitterImpl *split_impl = malloc(sizeof(*split_impl));
    TabStripImpl *tab_impl = malloc(sizeof(*tab_impl));
    if (activity_impl == NULL || split_impl == NULL || tab_impl == NULL) {
        vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not allocate panel state");
        free(activity_impl);
        free(split_impl);
        free(tab_impl);
        return NULL;
    }
    activity_impl->state = state;
    split_impl->state = state;
    split_impl->dragging = false;
    split_impl->grab_x = 0.0f;
    split_impl->grab_width = state->sidebar_width;
    tab_impl->active_tab = 0;

    VellumWidget *root = vellum_widget_create(VELLUM_WIDGET_CONTAINER, &root_class, NULL, error);
    VellumWidget *row = vellum_widget_create(VELLUM_WIDGET_CONTAINER, &row_class, NULL, error);
    VellumWidget *column =
        vellum_widget_create(VELLUM_WIDGET_CONTAINER, &column_class, NULL, error);
    VellumWidget *activity =
        vellum_widget_create(VELLUM_WIDGET_ACTIVITY_BAR, &activity_class, activity_impl, error);
    VellumWidget *sidebar =
        vellum_widget_create(VELLUM_WIDGET_SIDEBAR, &sidebar_class, NULL, error);
    VellumWidget *splitter =
        vellum_widget_create(VELLUM_WIDGET_SPLITTER, &splitter_class, split_impl, error);
    VellumWidget *tabs = vellum_widget_create(VELLUM_WIDGET_TAB_STRIP, &tab_class, tab_impl, error);
    VellumWidget *editor = vellum_widget_create(VELLUM_WIDGET_EDITOR, &editor_class, NULL, error);
    VellumWidget *status =
        vellum_widget_create(VELLUM_WIDGET_STATUS_BAR, &status_class, NULL, error);
    if (root == NULL || row == NULL || column == NULL || activity == NULL || sidebar == NULL ||
        splitter == NULL || tabs == NULL || editor == NULL || status == NULL) {
        goto fail;
    }

    if (!vellum_widget_add_child(root, row) || !vellum_widget_add_child(root, status) ||
        !vellum_widget_add_child(row, activity) || !vellum_widget_add_child(row, sidebar) ||
        !vellum_widget_add_child(row, splitter) || !vellum_widget_add_child(row, column) ||
        !vellum_widget_add_child(column, tabs) || !vellum_widget_add_child(column, editor)) {
        goto fail;
    }
    vellum_widget_set_focusable(activity, true);
    vellum_widget_set_focusable(splitter, true);
    vellum_widget_set_focusable(tabs, true);
    vellum_widget_set_focusable(editor, true);
    vellum_ui_apply_settings(root, state, state->show_sidebar, state->show_status_bar);
    return root;

fail:
    vellum_widget_destroy(root); /* Destroys every widget already attached to the tree. */
#define DESTROY_IF_DETACHED(widget)                                                                \
    do {                                                                                           \
        if ((widget) != NULL && vellum_widget_parent(widget) == NULL) {                            \
            vellum_widget_destroy(widget);                                                         \
        }                                                                                          \
    } while (0)
    DESTROY_IF_DETACHED(row);
    DESTROY_IF_DETACHED(column);
    DESTROY_IF_DETACHED(activity);
    DESTROY_IF_DETACHED(sidebar);
    DESTROY_IF_DETACHED(splitter);
    DESTROY_IF_DETACHED(tabs);
    DESTROY_IF_DETACHED(editor);
    DESTROY_IF_DETACHED(status);
#undef DESTROY_IF_DETACHED
    return NULL;
}

VellumWidget *vellum_ui_editor(VellumWidget *root)
{
    VellumWidget *row = vellum_widget_child(root, 0u);
    VellumWidget *column = vellum_widget_child(row, 3u);
    return vellum_widget_child(column, 1u);
}

void vellum_ui_apply_settings(VellumWidget *root, VellumUiState *state, bool show_sidebar,
                              bool show_status_bar)
{
    if (root == NULL || state == NULL) {
        return;
    }
    VellumWidget *row = vellum_widget_child(root, 0u);
    VellumWidget *sidebar = vellum_widget_child(row, 1u);
    VellumWidget *splitter = vellum_widget_child(row, 2u);
    VellumWidget *status_bar = vellum_widget_child(root, 1u);
    state->show_sidebar = show_sidebar;
    state->show_status_bar = show_status_bar;
    vellum_widget_set_visible(sidebar, show_sidebar);
    vellum_widget_set_visible(splitter, show_sidebar);
    vellum_widget_set_visible(status_bar, show_status_bar);
}
