/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#include "vellum/platform/clipboard.h"

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

bool vellum_clipboard_has_text(void)
{
    return SDL_HasClipboardText();
}

bool vellum_clipboard_set_text(const char *text, VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (!SDL_SetClipboardText(text)) {
        vellum_error_set_sdl(error, VELLUM_ERR_PLATFORM, "SDL_SetClipboardText");
        return false;
    }
    return true;
}

char *vellum_clipboard_get_text(VellumErrorInfo *error)
{
    vellum_error_init(error);
    char *native = SDL_GetClipboardText();
    if (native == NULL) {
        vellum_error_set_sdl(error, VELLUM_ERR_PLATFORM, "SDL_GetClipboardText");
        return NULL;
    }

    const size_t length = strlen(native);
    char *copy = malloc(length + 1u);
    if (copy == NULL) {
        vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not copy clipboard text");
        SDL_free(native);
        return NULL;
    }
    memcpy(copy, native, length + 1u);
    SDL_free(native);
    return copy;
}
