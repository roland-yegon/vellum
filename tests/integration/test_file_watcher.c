/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "vellum/config/file_watcher.h"
#include "vellum/core/error.h"

#define WATCH_FILE "vellum_test_watch.txt"
#define WATCH_CODE 77u
#define POLL_MS 40u

static bool write_file(const char *text)
{
    FILE *file = fopen(WATCH_FILE, "wb");
    if (file == NULL) {
        return false;
    }
    const bool ok = fputs(text, file) >= 0;
    return fclose(file) == 0 && ok;
}

static bool wait_for_event(uint32_t base)
{
    for (int attempt = 0; attempt < 50; ++attempt) {
        SDL_Event event;
        if (SDL_WaitEventTimeout(&event, 100)) {
            if (event.type == base && (uint32_t)event.user.code == WATCH_CODE) {
                return true;
            }
        }
    }
    return false;
}

int main(void)
{
    bool ok = false;
    VellumFileWatcher *watcher = NULL;

    if (!SDL_Init(SDL_INIT_EVENTS)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        goto done;
    }

    VellumErrorInfo error;
    if (vellum_file_watcher_create(0u, WATCH_CODE, POLL_MS, &error) != NULL ||
        error.code != VELLUM_ERR_INVALID_ARGUMENT) {
        fprintf(stderr, "invalid event base accepted\n");
        goto done;
    }

    const uint32_t base = SDL_RegisterEvents(1u);
    if (base == UINT32_MAX) {
        fprintf(stderr, "SDL_RegisterEvents failed\n");
        goto done;
    }
    if (!write_file("first")) {
        fprintf(stderr, "could not create watch file\n");
        goto done;
    }

    watcher = vellum_file_watcher_create(base, WATCH_CODE, POLL_MS, &error);
    if (watcher == NULL) {
        fprintf(stderr, "watcher create failed: %s\n", vellum_error_message(&error));
        goto done;
    }
    if (!vellum_file_watcher_add(watcher, WATCH_FILE, &error) ||
        vellum_file_watcher_add(NULL, WATCH_FILE, &error) ||
        error.code != VELLUM_ERR_INVALID_ARGUMENT) {
        fprintf(stderr, "watcher add failed: %s\n", vellum_error_message(&error));
        goto done;
    }

    if (!write_file("second")) {
        fprintf(stderr, "could not rewrite watch file\n");
        goto done;
    }
    if (!wait_for_event(base)) {
        fprintf(stderr, "file change event not received\n");
        goto done;
    }
    ok = true;

done:
    vellum_file_watcher_destroy(watcher);
    (void)remove(WATCH_FILE);
    SDL_Quit();
    printf("%s\n", ok ? "file watcher integration: PASS" : "file watcher integration: FAIL");
    return ok ? 0 : 1;
}
