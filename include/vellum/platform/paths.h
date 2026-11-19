/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#ifndef VELLUM_PLATFORM_PATHS_H
#define VELLUM_PLATFORM_PATHS_H

#include <stdbool.h>
#include <stddef.h>

#include "vellum/core/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Platform preferences directory (SDL_GetPrefPath), including the trailing separator. */
bool vellum_paths_preferences(char *buffer, size_t capacity, VellumErrorInfo *error);

/* Composes a user-writable config path by appending `relative` to the preferences
 * directory. `relative` must be a relative path without `..` components. */
bool vellum_paths_user_config(char *buffer, size_t capacity, const char *relative,
                              VellumErrorInfo *error);

/* True when `path` names an existing file. Absence is not an error. */
bool vellum_path_exists(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* VELLUM_PLATFORM_PATHS_H */
