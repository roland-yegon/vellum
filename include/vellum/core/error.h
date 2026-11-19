/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#ifndef VELLUM_CORE_ERROR_H
#define VELLUM_CORE_ERROR_H

#include <stdbool.h>
#include <stddef.h>

#include "vellum/core/attributes.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VELLUM_OK = 0,
    VELLUM_ERR_INVALID_ARGUMENT,
    VELLUM_ERR_ALLOC_FAILED,
    VELLUM_ERR_CAPACITY,
    VELLUM_ERR_PLATFORM,
    VELLUM_ERR_NOT_FOUND,
    VELLUM_ERR_IO,
    VELLUM_ERR_PARSE,
    VELLUM_ERR_UNSUPPORTED,
    VELLUM_ERR_INTERNAL
} VellumError;

#define VELLUM_ERROR_MESSAGE_SIZE 512

typedef struct {
    VellumError code;
    char message[VELLUM_ERROR_MESSAGE_SIZE];
} VellumErrorInfo;

void vellum_error_init(VellumErrorInfo *info);
bool vellum_error_is_ok(const VellumErrorInfo *info);
void vellum_error_set(VellumErrorInfo *info, VellumError code, const char *fmt, ...)
    VELLUM_PRINTF_ATTR(3, 4);
void vellum_error_set_sdl(VellumErrorInfo *info, VellumError code, const char *context);
const char *vellum_error_name(VellumError code);
const char *vellum_error_message(const VellumErrorInfo *info);

#ifdef __cplusplus
}
#endif

#endif /* VELLUM_CORE_ERROR_H */
