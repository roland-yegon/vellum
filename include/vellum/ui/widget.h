/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */
#ifndef VELLUM_UI_WIDGET_H
#define VELLUM_UI_WIDGET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vellum/config/theme.h"
#include "vellum/core/error.h"
#include "vellum/graphics/draw_list.h"
#include "vellum/graphics/font.h"
#include "vellum/graphics/shape.h"
#include "vellum/platform/events.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float x;
    float y;
    float width;
    float height;
} VellumBounds;

typedef struct {
    float width;
    float height;
} VellumSize;

typedef struct {
    float min_width;
    float min_height;
    float max_width;
    float max_height;
} VellumConstraints;

/* Services available to widget callbacks. Text resources are optional: panels skip
 * their labels when `shaper`, `fonts`, or `atlas_texture` is unset, which keeps pure
 * layout tests independent of the font pipeline. */
typedef struct {
    VellumDrawList *draw_list;
    const VellumTheme *theme;
    VellumShaper *shaper;
    VellumFontSystem *fonts;
    VellumTextureHandle atlas_texture;
    uint32_t font_size;
} VellumUiContext;

typedef enum {
    VELLUM_WIDGET_CONTAINER = 0,
    VELLUM_WIDGET_ACTIVITY_BAR,
    VELLUM_WIDGET_SIDEBAR,
    VELLUM_WIDGET_SPLITTER,
    VELLUM_WIDGET_TAB_STRIP,
    VELLUM_WIDGET_EDITOR,
    VELLUM_WIDGET_STATUS_BAR
} VellumWidgetKind;

typedef struct VellumWidget VellumWidget;

/* Callbacks are supplied by each widget module. `destroy` owns `impl`; when it is
 * NULL the widget simply frees the impl pointer. `layout` is responsible for laying
 * out visible children; `paint` is followed by an automatic child traversal. */
typedef struct {
    VellumSize (*measure)(VellumWidget *widget, VellumConstraints constraints,
                          const VellumUiContext *ctx);
    void (*layout)(VellumWidget *widget, VellumBounds bounds, const VellumUiContext *ctx);
    bool (*paint)(VellumWidget *widget, const VellumUiContext *ctx, VellumErrorInfo *error);
    bool (*event)(VellumWidget *widget, const VellumInput *input, const VellumUiContext *ctx);
    void (*destroy)(void *impl);
} VellumWidgetClass;

VellumWidget *vellum_widget_create(VellumWidgetKind kind, const VellumWidgetClass *class,
                                   void *impl, VellumErrorInfo *error);
void vellum_widget_destroy(VellumWidget *widget);

/* Adds `child` to `parent`'s tail and transfers ownership on success only. */
bool vellum_widget_add_child(VellumWidget *parent, VellumWidget *child);
size_t vellum_widget_child_count(const VellumWidget *widget);
VellumWidget *vellum_widget_child(VellumWidget *widget, size_t index);
VellumWidget *vellum_widget_parent(VellumWidget *widget);
VellumWidgetKind vellum_widget_kind(const VellumWidget *widget);
void *vellum_widget_impl(VellumWidget *widget);
VellumBounds vellum_widget_bounds(const VellumWidget *widget);

void vellum_widget_set_visible(VellumWidget *widget, bool visible);
bool vellum_widget_visible(const VellumWidget *widget);
void vellum_widget_set_focusable(VellumWidget *widget, bool focusable);
bool vellum_widget_focusable(const VellumWidget *widget);

/* Marks the widget and all ancestors as needing layout. */
void vellum_widget_mark_dirty(VellumWidget *widget);
bool vellum_widget_layout_dirty(const VellumWidget *widget);
VellumSize vellum_constraints_apply(VellumConstraints constraints, VellumSize size);
VellumSize vellum_widget_measure(VellumWidget *widget, VellumConstraints constraints,
                                 const VellumUiContext *ctx);
void vellum_widget_layout(VellumWidget *widget, VellumBounds bounds, const VellumUiContext *ctx);
bool vellum_widget_paint(VellumWidget *widget, const VellumUiContext *ctx, VellumErrorInfo *error);

/* Hit testing returns the deepest visible widget under the point. */
VellumWidget *vellum_widget_hit_test(VellumWidget *root, float x, float y);
VellumWidget *vellum_widget_focused(const VellumWidget *root);
VellumWidget *vellum_widget_hovered(const VellumWidget *root);
bool vellum_widget_set_focus(VellumWidget *widget);
bool vellum_widget_focus_next(VellumWidget *root, bool reverse);
bool vellum_widget_dispatch_event(VellumWidget *root, const VellumInput *input,
                                  const VellumUiContext *ctx);

#ifdef __cplusplus
}
#endif

#endif /* VELLUM_UI_WIDGET_H */
