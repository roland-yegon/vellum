/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */
#include "vellum/ui/widget.h"

#include <stdlib.h>
#include <string.h>

#define VELLUM_WIDGET_INITIAL_CHILDREN 4u

struct VellumWidget {
    VellumWidgetKind kind;
    VellumWidgetClass class;
    void *impl;
    VellumWidget *parent;
    VellumWidget **children;
    size_t child_count;
    size_t child_capacity;
    VellumBounds bounds;
    bool visible;
    bool focusable;
    bool layout_dirty;
    /* Root-owned interaction state. */
    VellumWidget *focused;
    VellumWidget *hovered;
    VellumWidget *pressed;
};

static VellumWidget *root_of(VellumWidget *widget)
{
    while (widget->parent != NULL) {
        widget = widget->parent;
    }
    return widget;
}

static bool contains(const VellumWidget *widget, const VellumWidget *target)
{
    if (widget == target) {
        return true;
    }
    for (size_t i = 0u; i < widget->child_count; ++i) {
        if (contains(widget->children[i], target)) {
            return true;
        }
    }
    return false;
}

static void clear_interaction_refs(VellumWidget *root, const VellumWidget *target)
{
    if (root->focused == target) {
        root->focused = NULL;
    }
    if (root->hovered == target) {
        root->hovered = NULL;
    }
    if (root->pressed == target) {
        root->pressed = NULL;
    }
    if (contains(root, target)) {
        vellum_widget_mark_dirty(root);
    }
}

static void default_layout(VellumWidget *widget, VellumBounds bounds, const VellumUiContext *ctx)
{
    for (size_t i = 0u; i < widget->child_count; ++i) {
        if (widget->children[i]->visible) {
            vellum_widget_layout(widget->children[i], bounds, ctx);
        }
    }
}

VellumWidget *vellum_widget_create(VellumWidgetKind kind, const VellumWidgetClass *class,
                                   void *impl, VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (class == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "widget class is required");
        return NULL;
    }

    VellumWidget *widget = malloc(sizeof(*widget));
    if (widget == NULL) {
        vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not allocate widget");
        return NULL;
    }
    memset(widget, 0, sizeof(*widget));
    widget->kind = kind;
    widget->class = *class;
    widget->impl = impl;
    widget->visible = true;
    widget->layout_dirty = true;
    return widget;
}

void vellum_widget_destroy(VellumWidget *widget)
{
    if (widget == NULL) {
        return;
    }

    VellumWidget *root = root_of(widget);
    clear_interaction_refs(root, widget);
    if (widget->parent != NULL) {
        VellumWidget *parent = widget->parent;
        for (size_t i = 0u; i < parent->child_count; ++i) {
            if (parent->children[i] == widget) {
                memmove(&parent->children[i], &parent->children[i + 1u],
                        (parent->child_count - i - 1u) * sizeof(*parent->children));
                --parent->child_count;
                break;
            }
        }
        widget->parent = NULL;
    }
    while (widget->child_count > 0u) {
        VellumWidget *child = widget->children[--widget->child_count];
        child->parent = NULL;
        vellum_widget_destroy(child);
    }
    free(widget->children);
    if (widget->class.destroy != NULL) {
        widget->class.destroy(widget->impl);
    } else {
        free(widget->impl);
    }
    free(widget);
}

bool vellum_widget_add_child(VellumWidget *parent, VellumWidget *child)
{
    if (parent == NULL || child == NULL || child == parent || child->parent != NULL) {
        return false;
    }
    if (parent->child_count == parent->child_capacity) {
        const size_t capacity = parent->child_capacity == 0u ? VELLUM_WIDGET_INITIAL_CHILDREN
                                                             : parent->child_capacity * 2u;
        if (capacity < parent->child_capacity || capacity > SIZE_MAX / sizeof(*parent->children)) {
            return false;
        }
        VellumWidget **children = realloc(parent->children, capacity * sizeof(*children));
        if (children == NULL) {
            return false;
        }
        parent->children = children;
        parent->child_capacity = capacity;
    }
    parent->children[parent->child_count++] = child;
    child->parent = parent;
    vellum_widget_mark_dirty(parent);
    return true;
}

size_t vellum_widget_child_count(const VellumWidget *widget)
{
    return widget == NULL ? 0u : widget->child_count;
}

VellumWidget *vellum_widget_child(VellumWidget *widget, size_t index)
{
    if (widget == NULL || index >= widget->child_count) {
        return NULL;
    }
    return widget->children[index];
}

VellumWidget *vellum_widget_parent(VellumWidget *widget)
{
    return widget == NULL ? NULL : widget->parent;
}

VellumWidgetKind vellum_widget_kind(const VellumWidget *widget)
{
    return widget == NULL ? VELLUM_WIDGET_CONTAINER : widget->kind;
}

void *vellum_widget_impl(VellumWidget *widget)
{
    return widget == NULL ? NULL : widget->impl;
}

VellumBounds vellum_widget_bounds(const VellumWidget *widget)
{
    VellumBounds bounds = {0.0f, 0.0f, 0.0f, 0.0f};
    return widget == NULL ? bounds : widget->bounds;
}

void vellum_widget_set_visible(VellumWidget *widget, bool visible)
{
    if (widget == NULL || widget->visible == visible) {
        return;
    }
    widget->visible = visible;
    vellum_widget_mark_dirty(widget);
}

bool vellum_widget_visible(const VellumWidget *widget)
{
    return widget != NULL && widget->visible;
}

void vellum_widget_set_focusable(VellumWidget *widget, bool focusable)
{
    if (widget != NULL) {
        widget->focusable = focusable;
    }
}

bool vellum_widget_focusable(const VellumWidget *widget)
{
    return widget != NULL && widget->focusable;
}

void vellum_widget_mark_dirty(VellumWidget *widget)
{
    for (; widget != NULL; widget = widget->parent) {
        widget->layout_dirty = true;
    }
}

bool vellum_widget_layout_dirty(const VellumWidget *widget)
{
    return widget != NULL && widget->layout_dirty;
}

VellumSize vellum_constraints_apply(VellumConstraints constraints, VellumSize size)
{
    if (size.width < constraints.min_width) {
        size.width = constraints.min_width;
    }
    if (size.height < constraints.min_height) {
        size.height = constraints.min_height;
    }
    if (constraints.max_width >= constraints.min_width && size.width > constraints.max_width) {
        size.width = constraints.max_width;
    }
    if (constraints.max_height >= constraints.min_height && size.height > constraints.max_height) {
        size.height = constraints.max_height;
    }
    return size;
}

VellumSize vellum_widget_measure(VellumWidget *widget, VellumConstraints constraints,
                                 const VellumUiContext *ctx)
{
    if (widget != NULL && widget->class.measure != NULL) {
        return widget->class.measure(widget, constraints, ctx);
    }
    return vellum_constraints_apply(constraints,
                                    (VellumSize){constraints.min_width, constraints.min_height});
}

void vellum_widget_layout(VellumWidget *widget, VellumBounds bounds, const VellumUiContext *ctx)
{
    if (widget == NULL) {
        return;
    }
    widget->bounds = bounds;
    if (widget->class.layout != NULL) {
        widget->class.layout(widget, bounds, ctx);
    } else {
        default_layout(widget, bounds, ctx);
    }
    widget->layout_dirty = false;
}

bool vellum_widget_paint(VellumWidget *widget, const VellumUiContext *ctx, VellumErrorInfo *error)
{
    if (widget == NULL || !widget->visible) {
        return true;
    }
    if (widget->class.paint != NULL && !widget->class.paint(widget, ctx, error)) {
        return false;
    }
    for (size_t i = 0u; i < widget->child_count; ++i) {
        if (!vellum_widget_paint(widget->children[i], ctx, error)) {
            return false;
        }
    }
    return true;
}

VellumWidget *vellum_widget_hit_test(VellumWidget *root, float x, float y)
{
    if (root == NULL || !root->visible) {
        return NULL;
    }
    const VellumBounds bounds = root->bounds;
    if (x < bounds.x || y < bounds.y || x >= bounds.x + bounds.width ||
        y >= bounds.y + bounds.height) {
        return NULL;
    }
    for (size_t i = root->child_count; i > 0u; --i) {
        VellumWidget *hit = vellum_widget_hit_test(root->children[i - 1u], x, y);
        if (hit != NULL) {
            return hit;
        }
    }
    return root;
}

VellumWidget *vellum_widget_focused(const VellumWidget *root)
{
    if (root == NULL || root->focused == NULL || !root->focused->visible) {
        return NULL;
    }
    return root->focused;
}

VellumWidget *vellum_widget_hovered(const VellumWidget *root)
{
    if (root == NULL || root->hovered == NULL || !root->hovered->visible) {
        return NULL;
    }
    return root->hovered;
}

bool vellum_widget_set_focus(VellumWidget *widget)
{
    if (widget == NULL || !widget->focusable || !widget->visible) {
        return false;
    }
    VellumWidget *root = root_of(widget);
    if (root->focused == widget) {
        return true;
    }
    root->focused = widget;
    vellum_widget_mark_dirty(widget);
    return true;
}

static size_t count_focusable(const VellumWidget *widget)
{
    size_t count = widget->focusable && widget->visible ? 1u : 0u;
    for (size_t i = 0u; i < widget->child_count; ++i) {
        count += count_focusable(widget->children[i]);
    }
    return count;
}

static void find_focus_index(const VellumWidget *widget, const VellumWidget *target, size_t *index,
                             bool *found)
{
    if (!widget->visible || *found) {
        return;
    }
    if (widget == target) {
        *found = true;
        return;
    }
    if (widget->focusable) {
        ++*index;
    }
    for (size_t i = 0u; i < widget->child_count; ++i) {
        find_focus_index(widget->children[i], target, index, found);
    }
}

/* Indexes always follow tree order; `reverse` only changes which index is chosen. */
static void find_focus_at(VellumWidget *widget, size_t target, size_t *index, VellumWidget **result)
{
    if (!widget->visible || *result != NULL) {
        return;
    }
    if (widget->focusable) {
        if (*index == target) {
            *result = widget;
            return;
        }
        ++*index;
    }
    for (size_t i = 0u; i < widget->child_count; ++i) {
        find_focus_at(widget->children[i], target, index, result);
    }
}

bool vellum_widget_focus_next(VellumWidget *root, bool reverse)
{
    if (root == NULL) {
        return false;
    }
    const size_t count = count_focusable(root);
    if (count == 0u) {
        return false;
    }

    size_t current = 0u;
    bool found = false;
    if (root->focused != NULL) {
        find_focus_index(root, root->focused, &current, &found);
    }
    const size_t step = reverse ? count - 1u : 1u;
    const size_t target = found ? (current + step) % count : (reverse ? count - 1u : 0u);

    size_t index = 0u;
    VellumWidget *next = NULL;
    find_focus_at(root, target, &index, &next);
    return next != NULL && vellum_widget_set_focus(next);
}

static bool call_event(VellumWidget *widget, const VellumInput *input, const VellumUiContext *ctx)
{
    return widget != NULL && widget->class.event != NULL && widget->class.event(widget, input, ctx);
}

bool vellum_widget_dispatch_event(VellumWidget *root, const VellumInput *input,
                                  const VellumUiContext *ctx)
{
    if (root == NULL || input == NULL || ctx == NULL) {
        return false;
    }

    if (input->type == VELLUM_INPUT_KEY && input->data.key.key == VELLUM_KEY_TAB) {
        (void)vellum_widget_focus_next(root, (input->data.key.modifiers & VELLUM_MOD_SHIFT) != 0u);
        return true; /* Tab is a navigation key; never forward it as text. */
    }
    if (input->type == VELLUM_INPUT_KEY || input->type == VELLUM_INPUT_TEXT ||
        input->type == VELLUM_INPUT_WHEEL) {
        return call_event(vellum_widget_focused(root), input, ctx);
    }
    if (input->type == VELLUM_INPUT_POINTER_MOTION) {
        VellumWidget *hit =
            vellum_widget_hit_test(root, input->data.pointer.x, input->data.pointer.y);
        /* A pressed widget captures the pointer until release, so drags keep working
         * even when the cursor leaves the widget's small hit area. */
        VellumWidget *target = root->pressed != NULL ? root->pressed : hit;
        VellumWidget *previous = root->hovered;
        const bool changed = previous != target;
        if (changed) {
            (void)call_event(previous, input, ctx);
            root->hovered = target;
        }
        const bool handled = call_event(target, input, ctx);
        if (changed) {
            if (previous != NULL) {
                vellum_widget_mark_dirty(previous);
            }
            if (target != NULL) {
                vellum_widget_mark_dirty(target);
            }
        }
        return handled || changed;
    }
    if (input->type == VELLUM_INPUT_POINTER_BUTTON) {
        VellumWidget *hit =
            vellum_widget_hit_test(root, input->data.pointer.x, input->data.pointer.y);
        VellumWidget *target = hit;
        if (input->data.pointer.pressed) {
            if (input->data.pointer.button == VELLUM_POINTER_BUTTON_LEFT) {
                root->pressed = hit;
                if (hit != NULL) {
                    (void)vellum_widget_set_focus(hit);
                }
            }
        } else {
            target = root->pressed != NULL ? root->pressed : hit;
            root->pressed = NULL;
        }
        return call_event(target, input, ctx);
    }
    return false;
}
