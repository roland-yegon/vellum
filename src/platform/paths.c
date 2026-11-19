/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#include "vellum/platform/paths.h"

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdio.h>
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

static bool relative_is_safe(const char *relative)
{
    if (relative == NULL || relative[0] == '\0' || relative[0] == '/' || relative[0] == '\\') {
        return false;
    }
    for (const char *part = relative;;) {
        const char *separator = strpbrk(part, "/\\");
        const size_t length = separator == NULL ? strlen(part) : (size_t)(separator - part);
        if (length == 2u && part[0] == '.' && part[1] == '.') {
            return false;
        }
        if (separator == NULL) {
            break;
        }
        part = separator + 1;
    }
    return true;
}

bool vellum_paths_user_config(char *buffer, size_t capacity, const char *relative,
                              VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (buffer == NULL || capacity == 0u || !relative_is_safe(relative)) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "invalid user config path");
        return false;
    }

    char preferences[512];
    if (!vellum_paths_preferences(preferences, sizeof(preferences), error)) {
        return false;
    }

    const size_t length = strlen(preferences);
    const char *separator =
        length > 0u && (preferences[length - 1u] == '/' || preferences[length - 1u] == '\\') ? ""
                                                                                             : "/";
    const int written = snprintf(buffer, capacity, "%s%s%s", preferences, separator, relative);
    if (written < 0 || (size_t)written >= capacity) {
        vellum_error_set(error, VELLUM_ERR_CAPACITY, "user config path exceeds buffer size");
        return false;
    }
    return true;
}

bool vellum_path_exists(const char *path)
{
    if (path == NULL) {
        return false;
    }
    SDL_PathInfo info;
    return SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_FILE;
}
