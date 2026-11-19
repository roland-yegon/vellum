/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#ifndef VELLUM_APP_H
#define VELLUM_APP_H

#include <stdint.h>

#include "vellum/core/error.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VellumApp VellumApp;

VellumApp *vellum_app_create(VellumErrorInfo *error);
int vellum_app_run(VellumApp *app);
void vellum_app_destroy(VellumApp *app);

/* Registered SDL event type for app messages, for external producers and tests. */
uint32_t vellum_app_event_base(const VellumApp *app);

#ifdef __cplusplus
}
#endif

#endif /* VELLUM_APP_H */
