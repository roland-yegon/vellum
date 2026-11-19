/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */
#ifndef VELLUM_CONFIG_THEME_H
#define VELLUM_CONFIG_THEME_H

#include <stdbool.h>
#include <stddef.h>

#include "vellum/core/error.h"
#include "vellum/graphics/draw_list.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VELLUM_THEME_NAME_MAX 32u

typedef enum {
    VELLUM_THEME_BACKGROUND = 0,
    VELLUM_THEME_SIDEBAR,
    VELLUM_THEME_TAB_BAR,
    VELLUM_THEME_TEXT,
    VELLUM_THEME_ACCENT,
    VELLUM_THEME_TOKEN_COUNT
} VellumThemeToken;

typedef struct {
    char name[VELLUM_THEME_NAME_MAX];
    VellumColor colors[VELLUM_THEME_TOKEN_COUNT];
    float spacing;
    float radius;
} VellumTheme;

VellumTheme vellum_theme_dark(void);
VellumTheme vellum_theme_light(void);

/* Loads a complete theme document. Missing keys, unknown keys, duplicates, wrong
 * types, and out-of-range values fail with VELLUM_ERR_PARSE. */
bool vellum_theme_load(VellumTheme *theme, const char *path, VellumErrorInfo *error);

VellumColor vellum_theme_color(const VellumTheme *theme, VellumThemeToken token);
const char *vellum_theme_name(const VellumTheme *theme);

#endif /* VELLUM_CONFIG_THEME_H */
