/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */
#include "vellum/config/file_watcher.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WATCH_READ_MAX (1u * 1024u * 1024u)
#define FNV_OFFSET_BASIS 1469598103934665603ULL
#define FNV_PRIME 1099511628211ULL

typedef struct {
    char path[VELLUM_WATCH_PATH_MAX];
    bool present;
    uint64_t hash;
} WatchedFile;

struct VellumFileWatcher {
    SDL_Thread *thread;
    SDL_Mutex *mutex;
    SDL_AtomicInt stop;
    uint32_t event_base;
    uint32_t message_code;
    uint32_t poll_ms;
    size_t count;
    WatchedFile files[VELLUM_WATCH_MAX_PATHS];
};

/* Content hashing avoids filesystem timestamp granularity issues: a rewrite within
 * the same second is still detected. The size is folded in so truncation is seen. */
static uint64_t hash_file(const char *path, bool *present)
{
    *present = false;
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return 0u;
    }

    uint64_t hash = FNV_OFFSET_BASIS;
    unsigned char buffer[4096];
    size_t total = 0u;
    size_t read;
    while (total < WATCH_READ_MAX && (read = fread(buffer, 1u, sizeof(buffer), file)) > 0u) {
        const size_t take = total + read <= WATCH_READ_MAX ? read : WATCH_READ_MAX - total;
        for (size_t i = 0u; i < take; ++i) {
            hash ^= buffer[i];
            hash *= FNV_PRIME;
        }
        total += take;
        if (take < read) {
            break;
        }
    }
    fclose(file);

    hash ^= (uint64_t)total;
    hash *= FNV_PRIME;
    *present = true;
    return hash;
}

static bool file_changed(const WatchedFile *file, uint64_t hash, bool present)
{
    return present != file->present || (present && hash != file->hash);
}

static int SDLCALL watch_thread(void *data)
{
    VellumFileWatcher *watcher = data;
    while (SDL_GetAtomicInt(&watcher->stop) == 0) {
        SDL_Delay(watcher->poll_ms);
        if (SDL_GetAtomicInt(&watcher->stop) != 0) {
            break;
        }

        WatchedFile snapshot[VELLUM_WATCH_MAX_PATHS];
        size_t count = 0u;
        SDL_LockMutex(watcher->mutex);
        count = watcher->count;
        memcpy(snapshot, watcher->files, count * sizeof(snapshot[0]));
        SDL_UnlockMutex(watcher->mutex);

        bool changed = false;
        for (size_t i = 0u; i < count; ++i) {
            bool present = false;
            const uint64_t hash = hash_file(snapshot[i].path, &present);
            if (!file_changed(&snapshot[i], hash, present)) {
                continue;
            }
            changed = true;
            SDL_LockMutex(watcher->mutex);
            if (i < watcher->count && strcmp(watcher->files[i].path, snapshot[i].path) == 0) {
                watcher->files[i].present = present;
                watcher->files[i].hash = hash;
            }
            SDL_UnlockMutex(watcher->mutex);
        }

        if (changed) {
            SDL_Event event;
            SDL_zero(event);
            event.type = watcher->event_base;
            event.user.code = (int)watcher->message_code;
            (void)SDL_PushEvent(&event);
        }
    }
    return 0;
}

VellumFileWatcher *vellum_file_watcher_create(uint32_t event_base, uint32_t message_code,
                                              uint32_t poll_ms, VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (event_base == 0u) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT,
                         "watcher needs a registered event base");
        return NULL;
    }

    VellumFileWatcher *watcher = calloc(1u, sizeof(*watcher));
    if (watcher == NULL) {
        vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not allocate file watcher");
        return NULL;
    }
    watcher->mutex = SDL_CreateMutex();
    if (watcher->mutex == NULL) {
        vellum_error_set_sdl(error, VELLUM_ERR_PLATFORM, "SDL_CreateMutex");
        free(watcher);
        return NULL;
    }

    watcher->event_base = event_base;
    watcher->message_code = message_code;
    watcher->poll_ms = poll_ms == 0u ? VELLUM_WATCH_DEFAULT_POLL_MS : poll_ms;
    SDL_SetAtomicInt(&watcher->stop, 0);

    watcher->thread = SDL_CreateThread(watch_thread, "vellum-config-watch", watcher);
    if (watcher->thread == NULL) {
        vellum_error_set_sdl(error, VELLUM_ERR_PLATFORM, "SDL_CreateThread");
        SDL_DestroyMutex(watcher->mutex);
        free(watcher);
        return NULL;
    }
    return watcher;
}

bool vellum_file_watcher_add(VellumFileWatcher *watcher, const char *path, VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (watcher == NULL || path == NULL || path[0] == '\0') {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "invalid watch path");
        return false;
    }
    if (strlen(path) >= VELLUM_WATCH_PATH_MAX) {
        vellum_error_set(error, VELLUM_ERR_CAPACITY, "watch path exceeds buffer size");
        return false;
    }

    bool present = false;
    const uint64_t hash = hash_file(path, &present);

    SDL_LockMutex(watcher->mutex);
    size_t index = watcher->count;
    bool ok = true;
    if (index >= VELLUM_WATCH_MAX_PATHS) {
        vellum_error_set(error, VELLUM_ERR_CAPACITY, "too many watched files");
        ok = false;
    } else {
        WatchedFile *entry = &watcher->files[index];
        memcpy(entry->path, path, strlen(path) + 1u);
        entry->present = present;
        entry->hash = hash;
        watcher->count++;
    }
    SDL_UnlockMutex(watcher->mutex);
    return ok;
}

void vellum_file_watcher_destroy(VellumFileWatcher *watcher)
{
    if (watcher == NULL) {
        return;
    }
    SDL_SetAtomicInt(&watcher->stop, 1);
    if (watcher->thread != NULL) {
        SDL_WaitThread(watcher->thread, NULL);
    }
    if (watcher->mutex != NULL) {
        SDL_DestroyMutex(watcher->mutex);
    }
    free(watcher);
}
