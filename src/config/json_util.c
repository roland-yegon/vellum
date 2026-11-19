/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */
#include "json_util.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VELLUM_JSON_MAX_BYTES (1u * 1024u * 1024u)

yyjson_doc *vellum_json_load_bounded(const char *path, VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (path == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "JSON path is NULL");
        return NULL;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        vellum_error_set(error, VELLUM_ERR_IO, "%s: could not open file", path);
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        vellum_error_set(error, VELLUM_ERR_IO, "%s: could not seek file", path);
        fclose(file);
        return NULL;
    }
    const long length = ftell(file);
    if (length < 0 || (uint64_t)length > (uint64_t)VELLUM_JSON_MAX_BYTES) {
        vellum_error_set(error, VELLUM_ERR_CAPACITY, "%s: JSON file exceeds %u bytes", path,
                         VELLUM_JSON_MAX_BYTES);
        fclose(file);
        return NULL;
    }
    rewind(file);

    const size_t size = (size_t)length;
    char *buffer = malloc(size + 1u);
    if (buffer == NULL) {
        vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "%s: could not read JSON file", path);
        fclose(file);
        return NULL;
    }
    const size_t read = fread(buffer, 1u, size, file);
    fclose(file);
    if (read != size) {
        free(buffer);
        vellum_error_set(error, VELLUM_ERR_IO, "%s: short read while loading JSON", path);
        return NULL;
    }
    buffer[read] = '\0';

    yyjson_read_err read_error;
    yyjson_doc *document = yyjson_read_opts(buffer, read, YYJSON_READ_NOFLAG, NULL, &read_error);
    free(buffer);
    if (document == NULL) {
        vellum_error_set(error, VELLUM_ERR_PARSE, "%s:%zu: %s (yyjson code %d)", path,
                         read_error.pos, read_error.msg != NULL ? read_error.msg : "invalid JSON",
                         (int)read_error.code);
    }
    return document;
}

void vellum_json_schema_error(VellumErrorInfo *error, const char *path, const char *message)
{
    vellum_error_set(error, VELLUM_ERR_PARSE, "%s: %s", path, message);
}

bool vellum_json_key_equals(const yyjson_val *key, const char *literal)
{
    const char *text = yyjson_get_str(key);
    const size_t length = yyjson_get_len(key);
    return text != NULL && strlen(literal) == length && memcmp(text, literal, length) == 0;
}
