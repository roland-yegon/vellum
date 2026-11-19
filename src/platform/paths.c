/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#include "vellum/platform/paths.h"

#include <SDL3/SDL.h>
#include <string.h>

bool vellum_paths_preferences(char *buffer, size_t capacity, VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (buffer == NULL || capacity == 0u) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "invalid path buffer");
        return false;
    }

    char *path = SDL_GetPrefPath("vellum", "vellum");
    if (path == NULL) {
        vellum_error_set_sdl(error, VELLUM_ERR_PLATFORM, "SDL_GetPrefPath");
        return false;
    }

    const size_t length = strlen(path);
    if (length >= capacity) {
        vellum_error_set(error, VELLUM_ERR_CAPACITY, "preference path exceeds buffer size");
        SDL_free(path);
        return false;
    }
    memcpy(buffer, path, length + 1u);
    SDL_free(path);
    return true;
}
