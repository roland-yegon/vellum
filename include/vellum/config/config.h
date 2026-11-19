/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */
#ifndef VELLUM_CONFIG_CONFIG_H
#define VELLUM_CONFIG_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vellum/core/error.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VELLUM_SETTINGS_NAME_MAX 32u

typedef struct {
    char theme[VELLUM_SETTINGS_NAME_MAX];
    uint32_t font_size;
    bool show_sidebar;
    bool show_status_bar;
} VellumSettings;

/* Compiled-in fallback used when the shipped defaults or a user override cannot be
 * parsed. The shipped JSON is authoritative when it loads successfully. */
VellumSettings vellum_settings_default(void);

/* Reads a strict settings document. Missing keys keep the values already in
 * `settings`; unknown keys, duplicates, wrong types, and out-of-range values fail with
 * VELLUM_ERR_PARSE and a path-qualified message. */
bool vellum_settings_load(VellumSettings *settings, const char *path, VellumErrorInfo *error);

/* Writes settings as formatted JSON, replacing the file's existing content. */
bool vellum_settings_save(const VellumSettings *settings, const char *path, VellumErrorInfo *error);

#endif /* VELLUM_CONFIG_CONFIG_H */
