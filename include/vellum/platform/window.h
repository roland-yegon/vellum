/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#ifndef VELLUM_PLATFORM_WINDOW_H
#define VELLUM_PLATFORM_WINDOW_H

#include <stdbool.h>

#include "vellum/core/error.h"

struct SDL_Window;

typedef struct VellumWindow VellumWindow;

VellumWindow *vellum_window_create(int width, int height, const char *title,
                                   VellumErrorInfo *error);
struct SDL_Window *vellum_window_handle(const VellumWindow *window);
void vellum_window_get_size(const VellumWindow *window, int *width, int *height);
bool vellum_window_is_minimized(const VellumWindow *window);
void vellum_window_destroy(VellumWindow *window);

#endif /* VELLUM_PLATFORM_WINDOW_H */
