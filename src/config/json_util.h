/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */
#ifndef VELLUM_CONFIG_JSON_UTIL_H
#define VELLUM_CONFIG_JSON_UTIL_H

#include <stdbool.h>

#include "vellum/core/error.h"
#include <yyjson.h>

/* Reads a JSON document from `path` with a 1 MiB bound. On failure the error message
 * includes the path and yyjson's code, message, and byte position. */
yyjson_doc *vellum_json_load_bounded(const char *path, VellumErrorInfo *error);

/* Adds a schema error (unknown key, duplicate key, wrong type, out of range) with the
 * file path so hand-edited config failures are actionable. */
void vellum_json_schema_error(VellumErrorInfo *error, const char *path, const char *message);

/* True when a yyjson string key equals a NUL-terminated literal, including keys that
 * themselves contain embedded NUL bytes. */
bool vellum_json_key_equals(const yyjson_val *key, const char *literal);

#endif /* VELLUM_CONFIG_JSON_UTIL_H */
