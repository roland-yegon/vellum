/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tests/unit/tests.h"
#include "vellum/core/error.h"
#include "vellum/platform/clipboard.h"

static bool check_get(const char *expected, VellumErrorInfo *error)
{
    char *text = vellum_clipboard_get_text(error);
    if (text == NULL) {
        fprintf(stderr, "clipboard get failed: %s\n", vellum_error_message(error));
        return false;
    }
    const bool ok = strcmp(text, expected) == 0;
    if (!ok) {
        fprintf(stderr, "clipboard get: expected '%s', got '%s'\n", expected, text);
    }
    free(text);
    return ok;
}

static bool roundtrip(void)
{
    VellumErrorInfo error;
    vellum_error_init(&error);

    if (!vellum_clipboard_set_text("hello", &error)) {
        fprintf(stderr, "clipboard set failed: %s\n", vellum_error_message(&error));
        return false;
    }
    if (!vellum_clipboard_has_text()) {
        fprintf(stderr, "clipboard has_text is false after a non-empty set\n");
        return false;
    }
    if (!check_get("hello", &error)) {
        return false;
    }

    /* UTF-8 text survives the platform roundtrip. */
    static const char utf8[] = "e\xCC\x81 \xE4\xB8\xAD\xE6\x96\x87";
    if (!vellum_clipboard_set_text(utf8, &error) || !check_get(utf8, &error)) {
        fprintf(stderr, "clipboard UTF-8 roundtrip failed: %s\n", vellum_error_message(&error));
        return false;
    }

    /* An empty string reads back as an empty C string but is not "has text". */
    if (!vellum_clipboard_set_text("", &error) || vellum_clipboard_has_text() ||
        !check_get("", &error)) {
        fprintf(stderr, "clipboard empty-string handling failed\n");
        return false;
    }

    /* NULL clears the clipboard. */
    if (!vellum_clipboard_set_text(NULL, &error) || vellum_clipboard_has_text() ||
        !check_get("", &error)) {
        fprintf(stderr, "clipboard NULL handling failed\n");
        return false;
    }
    return true;
}

bool test_clipboard(void)
{
    (void)SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "clipboard test could not initialize dummy video: %s\n", SDL_GetError());
        return false;
    }
    const bool ok = roundtrip();
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return ok;
}
