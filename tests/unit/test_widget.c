/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "tests/unit/tests.h"
#include "vellum/ui/widget.h"

typedef struct {
    VellumSize measured;
    VellumBounds assigned;
    int measures;
    int layouts;
    int paints;
    int events;
    bool paint_fail;
    bool event_handled;
} TestImpl;

static VellumSize test_measure(VellumWidget *widget, VellumConstraints constraints,
                               const VellumUiContext *ctx)
{
    (void)ctx;
    TestImpl *impl = vellum_widget_impl(widget);
    ++impl->measures;
    return vellum_constraints_apply(constraints, impl->measured);
}

static void widget_test_layout(VellumWidget *widget, VellumBounds bounds,
                               const VellumUiContext *ctx)
{
    TestImpl *impl = vellum_widget_impl(widget);
    ++impl->layouts;
    impl->assigned = bounds;
    float y = bounds.y;
    for (size_t i = 0u; i < vellum_widget_child_count(widget); ++i) {
        VellumWidget *child = vellum_widget_child(widget, i);
        if (vellum_widget_visible(child)) {
            const VellumBounds child_bounds = {bounds.x, y, bounds.width, 10.0f};
            vellum_widget_layout(child, child_bounds, ctx);
            y += 10.0f;
        }
    }
}

static bool test_paint(VellumWidget *widget, const VellumUiContext *ctx, VellumErrorInfo *error)
{
    (void)ctx;
    (void)error;
    TestImpl *impl = vellum_widget_impl(widget);
    ++impl->paints;
    return !impl->paint_fail;
}

static bool test_event(VellumWidget *widget, const VellumInput *input, const VellumUiContext *ctx)
{
    (void)input;
    (void)ctx;
    TestImpl *impl = vellum_widget_impl(widget);
    ++impl->events;
    return impl->event_handled;
}

static const VellumWidgetClass test_class = {test_measure, widget_test_layout, test_paint,
                                             test_event, free};

static VellumWidget *make_widget(VellumWidgetKind kind, VellumSize measured, VellumErrorInfo *error)
{
    TestImpl *impl = malloc(sizeof(*impl));
    if (impl == NULL) {
        return NULL;
    }
    memset(impl, 0, sizeof(*impl));
    impl->measured = measured;
    impl->event_handled = true;
    return vellum_widget_create(kind, &test_class, impl, error);
}

static TestImpl *impl_of(VellumWidget *widget)
{
    return (TestImpl *)vellum_widget_impl(widget);
}

bool test_widget(void)
{
    VellumErrorInfo error;
    VellumUiContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    VellumInput input;
    memset(&input, 0, sizeof(input));
    const VellumSize small = {30.0f, 8.0f};
    const VellumSize large = {300.0f, 80.0f};

    if (vellum_widget_create(VELLUM_WIDGET_CONTAINER, NULL, NULL, &error) != NULL ||
        error.code != VELLUM_ERR_INVALID_ARGUMENT) {
        return false;
    }

    VellumWidget *root = make_widget(VELLUM_WIDGET_CONTAINER, large, &error);
    VellumWidget *first = make_widget(VELLUM_WIDGET_SIDEBAR, small, &error);
    VellumWidget *second = make_widget(VELLUM_WIDGET_EDITOR, small, &error);
    if (root == NULL || first == NULL || second == NULL) {
        goto fail;
    }
    if (!vellum_widget_add_child(root, first) || !vellum_widget_add_child(root, second)) {
        goto fail;
    }
    if (vellum_widget_child_count(root) != 2u || vellum_widget_child(root, 0u) != first ||
        vellum_widget_child(root, 1u) != second || vellum_widget_child(root, 2u) != NULL ||
        vellum_widget_parent(first) != root || vellum_widget_kind(second) != VELLUM_WIDGET_EDITOR ||
        vellum_widget_add_child(root, root)) {
        goto fail;
    }

    const VellumConstraints constraints = {20.0f, 10.0f, 200.0f, 100.0f};
    const VellumSize measured = vellum_widget_measure(first, constraints, &ctx);
    if (measured.width != 30.0f || measured.height != 10.0f || impl_of(first)->measures != 1) {
        goto fail;
    }

    vellum_widget_mark_dirty(first);
    if (!vellum_widget_layout_dirty(root)) {
        goto fail;
    }
    const VellumBounds root_bounds = {0.0f, 0.0f, 100.0f, 100.0f};
    vellum_widget_layout(root, root_bounds, &ctx);
    if (vellum_widget_layout_dirty(root) || vellum_widget_layout_dirty(first) ||
        impl_of(first)->assigned.y != 0.0f || impl_of(second)->assigned.y != 10.0f) {
        goto fail;
    }

    if (vellum_widget_hit_test(root, 5.0f, 5.0f) != first ||
        vellum_widget_hit_test(root, 5.0f, 15.0f) != second ||
        vellum_widget_hit_test(root, 5.0f, 120.0f) != NULL) {
        goto fail;
    }

    /* An invisible subtree is neither painted nor hit-tested. */
    vellum_widget_set_visible(second, false);
    if (vellum_widget_visible(second) || !vellum_widget_layout_dirty(root) ||
        vellum_widget_hit_test(root, 5.0f, 15.0f) != root) {
        goto fail;
    }
    vellum_widget_layout(root, root_bounds, &ctx);
    if (!vellum_widget_paint(root, &ctx, &error)) {
        goto fail;
    }
    const int second_paints = impl_of(second)->paints;
    if (second_paints != 0) {
        goto fail;
    }
    impl_of(second)->paint_fail = true;
    vellum_widget_set_visible(second, true);
    if (vellum_widget_paint(root, &ctx, &error)) {
        goto fail;
    }
    impl_of(second)->paint_fail = false;
    if (!vellum_widget_paint(root, &ctx, &error)) {
        goto fail;
    }

    /* Tab order follows tree order and wraps in both directions. */
    vellum_widget_set_focusable(first, true);
    vellum_widget_set_focusable(second, true);
    if (!vellum_widget_set_focus(first) || vellum_widget_focused(root) != first ||
        vellum_widget_set_focus(root)) {
        goto fail;
    }
    if (!vellum_widget_focus_next(root, false) || vellum_widget_focused(root) != second ||
        !vellum_widget_focus_next(root, false) || vellum_widget_focused(root) != first ||
        !vellum_widget_focus_next(root, true) || vellum_widget_focused(root) != second) {
        goto fail;
    }

    /* Key and text events go to the focused widget; Tab navigates instead. */
    if (!vellum_widget_set_focus(first)) {
        goto fail;
    }
    const int events_before = impl_of(first)->events;
    input.type = VELLUM_INPUT_KEY;
    input.data.key.key = VELLUM_KEY_OTHER;
    if (!vellum_widget_dispatch_event(root, &input, &ctx) ||
        impl_of(first)->events != events_before + 1) {
        goto fail;
    }
    input.data.key.key = VELLUM_KEY_TAB;
    if (!vellum_widget_dispatch_event(root, &input, &ctx) ||
        vellum_widget_focused(root) != second) {
        goto fail;
    }

    /* Hover follows the hit widget; pointer presses focus and dispatch there. */
    input.type = VELLUM_INPUT_POINTER_MOTION;
    input.data.pointer.x = 5.0f;
    input.data.pointer.y = 5.0f;
    if (!vellum_widget_dispatch_event(root, &input, &ctx) || vellum_widget_hovered(root) != first) {
        goto fail;
    }
    input.data.pointer.y = 120.0f;
    if (!vellum_widget_dispatch_event(root, &input, &ctx) || vellum_widget_hovered(root) != NULL) {
        goto fail;
    }
    input.type = VELLUM_INPUT_POINTER_BUTTON;
    input.data.pointer.x = 5.0f;
    input.data.pointer.y = 15.0f;
    input.data.pointer.button = VELLUM_POINTER_BUTTON_LEFT;
    input.data.pointer.pressed = true;
    if (!vellum_widget_dispatch_event(root, &input, &ctx) ||
        vellum_widget_focused(root) != second) {
        goto fail;
    }
    input.data.pointer.pressed = false;
    if (!vellum_widget_dispatch_event(root, &input, &ctx)) {
        goto fail;
    }

    /* Destroying a hovered/focused widget clears the root's interaction state. */
    input.type = VELLUM_INPUT_POINTER_MOTION;
    input.data.pointer.x = 5.0f;
    input.data.pointer.y = 5.0f;
    if (!vellum_widget_dispatch_event(root, &input, &ctx) || vellum_widget_hovered(root) != first ||
        !vellum_widget_set_focus(first)) {
        goto fail;
    }
    vellum_widget_destroy(first);
    if (vellum_widget_child_count(root) != 1u || vellum_widget_focused(root) == first ||
        vellum_widget_hovered(root) == first) {
        goto fail;
    }
    vellum_widget_destroy(root);
    vellum_widget_destroy(NULL);
    return true;

fail:
    vellum_widget_destroy(root);
    return false;
}
