/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */
#ifndef VELLUM_CONFIG_FILE_WATCHER_H
#define VELLUM_CONFIG_FILE_WATCHER_H

#include <stdbool.h>
#include <stdint.h>

#include "vellum/core/error.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VELLUM_WATCH_MAX_PATHS 8u
#define VELLUM_WATCH_PATH_MAX 512u
#define VELLUM_WATCH_DEFAULT_POLL_MS 200u

typedef struct VellumFileWatcher VellumFileWatcher;

/* Creates a watcher backed by one fixed worker thread. When any watched file is
 * created, changed, or removed the worker posts an SDL application event with
 * `event_base` as its type and `message_code` as user.code. SDL events must already
 * be initialised. Pass 0 for `poll_ms` to use VELLUM_WATCH_DEFAULT_POLL_MS. */
VellumFileWatcher *vellum_file_watcher_create(uint32_t event_base, uint32_t message_code,
                                              uint32_t poll_ms, VellumErrorInfo *error);

/* Registers a file to watch. A file that does not exist yet is valid; its appearance
 * is detected. All state is captured at registration time. */
bool vellum_file_watcher_add(VellumFileWatcher *watcher, const char *path, VellumErrorInfo *error);

/* Signals the worker to stop, joins it, and releases all resources. Safe on NULL. */
void vellum_file_watcher_destroy(VellumFileWatcher *watcher);

#ifdef __cplusplus
}
#endif

#endif /* VELLUM_CONFIG_FILE_WATCHER_H */
