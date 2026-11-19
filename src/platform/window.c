/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#include "vellum/platform/window.h"

#include <SDL3/SDL.h>
#include <stdlib.h>

struct VellumWindow {
    SDL_Window *window;
};

VellumWindow *vellum_window_create(int width, int height, const char *title, VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (width <= 0 || height <= 0 || title == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "invalid window parameters");
        return NULL;
    }

    VellumWindow *wrapper = malloc(sizeof(*wrapper));
    if (wrapper == NULL) {
        vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not allocate window state");
        return NULL;
    }

    wrapper->window = SDL_CreateWindow(title, width, height, SDL_WINDOW_RESIZABLE);
    if (wrapper->window == NULL) {
        vellum_error_set_sdl(error, VELLUM_ERR_PLATFORM, "SDL_CreateWindow");
        free(wrapper);
        return NULL;
    }
    return wrapper;
}

struct SDL_Window *vellum_window_handle(const VellumWindow *window)
{
    return window == NULL ? NULL : window->window;
}

void vellum_window_get_size(const VellumWindow *window, int *width, int *height)
{
    if (window == NULL || window->window == NULL) {
        if (width != NULL) {
            *width = 0;
        }
        if (height != NULL) {
            *height = 0;
        }
        return;
    }
    int current_width = 0;
    int current_height = 0;
    (void)SDL_GetWindowSize(window->window, &current_width, &current_height);
    if (width != NULL) {
        *width = current_width;
    }
    if (height != NULL) {
        *height = current_height;
    }
}

bool vellum_window_is_minimized(const VellumWindow *window)
{
    if (window == NULL || window->window == NULL) {
        return false;
    }
    return (SDL_GetWindowFlags(window->window) & SDL_WINDOW_MINIMIZED) != 0u;
}

void vellum_window_destroy(VellumWindow *window)
{
    if (window == NULL) {
        return;
    }
    if (window->window != NULL) {
        SDL_DestroyWindow(window->window);
    }
    free(window);
}
