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

bool vellum_paths_preferences(char *buffer, size_t capacity, VellumErrorInfo *error);

#ifdef __cplusplus
}
#endif

#endif /* VELLUM_PLATFORM_PATHS_H */
