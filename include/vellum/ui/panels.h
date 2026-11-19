/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */
#ifndef VELLUM_UI_PANELS_H
#define VELLUM_UI_PANELS_H

#include <stdbool.h>
#include <stddef.h>

#include "vellum/core/error.h"
#include "vellum/ui/widget.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VELLUM_UI_ACTIVITY_WIDTH 44.0f
#define VELLUM_UI_TAB_STRIP_HEIGHT 28.0f
#define VELLUM_UI_STATUS_HEIGHT 24.0f
#define VELLUM_UI_SPLITTER_WIDTH 4.0f
#define VELLUM_UI_SIDEBAR_MIN 120.0f
#define VELLUM_UI_SIDEBAR_MAX 400.0f
#define VELLUM_UI_SIDEBAR_DEFAULT 220.0f

/* Shared, app-owned panel state. The splitter and activity bar mutate it; the app
 * mirrors panel visibility from settings and keeps the state alive for the tree's
 * lifetime. */
typedef struct {
    float sidebar_width;
    bool show_sidebar;
    bool show_status_bar;
} VellumUiState;

static inline VellumUiState vellum_ui_state_default(void)
{
    VellumUiState state = {VELLUM_UI_SIDEBAR_DEFAULT, true, true};
    return state;
}

/* Builds the demo panel tree. Returns its root; the caller owns it and `state`. */
VellumWidget *vellum_ui_create(VellumUiState *state, VellumErrorInfo *error);

/* The editor content widget, inside which the app paints document text. */
VellumWidget *vellum_ui_editor(VellumWidget *root);

/* Updates visibility from settings and marks the tree dirty when anything changes. */
void vellum_ui_apply_settings(VellumWidget *root, VellumUiState *state, bool show_sidebar,
                              bool show_status_bar);

#ifdef __cplusplus
}
#endif

#endif /* VELLUM_UI_PANELS_H */
