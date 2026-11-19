/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#ifndef VELLUM_PLATFORM_CLIPBOARD_H
#define VELLUM_PLATFORM_CLIPBOARD_H

#include <stdbool.h>

#include "vellum/core/error.h"

/* Thin wrapper over the SDL3 system text clipboard. Calls must happen on the
 * main thread and require the SDL video subsystem to be initialized (the
 * headless "dummy" driver is enough). Text is UTF-8 and NUL-terminated, so
 * embedded NULs cannot be represented on the system clipboard. */

/* True when the clipboard currently holds non-empty text. */
bool vellum_clipboard_has_text(void);

/* Replaces the clipboard contents with `text`. NULL or an empty string clears
 * it. Returns false on an SDL error. */
bool vellum_clipboard_set_text(const char *text, VellumErrorInfo *error);

/* Returns a malloc'd NUL-terminated UTF-8 copy of the clipboard text; caller
 * frees with free(). An empty clipboard yields an empty string; NULL indicates
 * failure (SDL error or out of memory). */
char *vellum_clipboard_get_text(VellumErrorInfo *error);

#endif /* VELLUM_PLATFORM_CLIPBOARD_H */
