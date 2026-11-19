/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */
#include "vellum/config/config.h"

#include <string.h>

#include <yyjson.h>

#include "json_util.h"

#define KEY_THEME "theme"
#define KEY_FONT_SIZE "font_size"
#define KEY_SHOW_SIDEBAR "show_sidebar"
#define KEY_SHOW_STATUS_BAR "show_status_bar"

#define SEEN_THEME 0x01u
#define SEEN_FONT_SIZE 0x02u
#define SEEN_SIDEBAR 0x04u
#define SEEN_STATUS_BAR 0x08u

#define MIN_FONT_SIZE 8u
#define MAX_FONT_SIZE 72u

VellumSettings vellum_settings_default(void)
{
    VellumSettings settings;
    memset(&settings, 0, sizeof(settings));
    memcpy(settings.theme, "dark", sizeof("dark") - 1u);
    settings.font_size = 14u;
    settings.show_sidebar = true;
    settings.show_status_bar = true;
    return settings;
}

static bool duplicate_key(VellumErrorInfo *error, const char *path, const char *key)
{
    vellum_error_set(error, VELLUM_ERR_PARSE, "%s: duplicate key '%s'", path, key);
    return false;
}

static bool unknown_key(VellumErrorInfo *error, const char *path, const yyjson_val *key)
{
    vellum_error_set(error, VELLUM_ERR_PARSE, "%s: unknown key '%.*s'", path,
                     (int)yyjson_get_len(key),
                     yyjson_get_str(key) != NULL ? yyjson_get_str(key) : "");
    return false;
}

static bool parse_settings(VellumSettings *settings, const yyjson_val *root, const char *path,
                           VellumErrorInfo *error)
{
    if (!yyjson_is_obj(root)) {
        vellum_json_schema_error(error, path, "settings must be a JSON object");
        return false;
    }

    unsigned seen = 0u;
    yyjson_obj_iter iterator;
    yyjson_obj_iter_init(root, &iterator);
    yyjson_val *key;
    while ((key = yyjson_obj_iter_next(&iterator)) != NULL) {
        yyjson_val *value = yyjson_obj_iter_get_val(key);
        if (vellum_json_key_equals(key, KEY_THEME)) {
            if ((seen & SEEN_THEME) != 0u) {
                return duplicate_key(error, path, KEY_THEME);
            }
            seen |= SEEN_THEME;
            const char *theme = yyjson_get_str(value);
            const size_t length = yyjson_get_len(value);
            if (theme == NULL || length == 0u || length >= VELLUM_SETTINGS_NAME_MAX) {
                vellum_error_set(error, VELLUM_ERR_PARSE,
                                 "%s: '%s' must be a non-empty name under %u bytes", path,
                                 KEY_THEME, (unsigned)VELLUM_SETTINGS_NAME_MAX - 1u);
                return false;
            }
            memcpy(settings->theme, theme, length);
            settings->theme[length] = '\0';
        } else if (vellum_json_key_equals(key, KEY_FONT_SIZE)) {
            if ((seen & SEEN_FONT_SIZE) != 0u) {
                return duplicate_key(error, path, KEY_FONT_SIZE);
            }
            seen |= SEEN_FONT_SIZE;
            const uint64_t size = yyjson_get_uint(value);
            if (!yyjson_is_uint(value) || size < MIN_FONT_SIZE || size > MAX_FONT_SIZE) {
                vellum_error_set(error, VELLUM_ERR_PARSE, "%s: '%s' must be an integer in [%u, %u]",
                                 path, KEY_FONT_SIZE, (unsigned)MIN_FONT_SIZE,
                                 (unsigned)MAX_FONT_SIZE);
                return false;
            }
            settings->font_size = (uint32_t)size;
        } else if (vellum_json_key_equals(key, KEY_SHOW_SIDEBAR)) {
            if ((seen & SEEN_SIDEBAR) != 0u) {
                return duplicate_key(error, path, KEY_SHOW_SIDEBAR);
            }
            seen |= SEEN_SIDEBAR;
            if (!yyjson_is_bool(value)) {
                vellum_json_schema_error(error, path, "'show_sidebar' must be a boolean");
                return false;
            }
            settings->show_sidebar = yyjson_get_bool(value);
        } else if (vellum_json_key_equals(key, KEY_SHOW_STATUS_BAR)) {
            if ((seen & SEEN_STATUS_BAR) != 0u) {
                return duplicate_key(error, path, KEY_SHOW_STATUS_BAR);
            }
            seen |= SEEN_STATUS_BAR;
            if (!yyjson_is_bool(value)) {
                vellum_json_schema_error(error, path, "'show_status_bar' must be a boolean");
                return false;
            }
            settings->show_status_bar = yyjson_get_bool(value);
        } else {
            return unknown_key(error, path, key);
        }
    }
    return true;
}

bool vellum_settings_load(VellumSettings *settings, const char *path, VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (settings == NULL || path == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT,
                         "settings load needs a target and path");
        return false;
    }
    yyjson_doc *document = vellum_json_load_bounded(path, error);
    if (document == NULL) {
        return false;
    }
    const bool ok = parse_settings(settings, yyjson_doc_get_root(document), path, error);
    yyjson_doc_free(document);
    return ok;
}

bool vellum_settings_save(const VellumSettings *settings, const char *path, VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (settings == NULL || path == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT,
                         "settings save needs a source and path");
        return false;
    }

    yyjson_mut_doc *document = yyjson_mut_doc_new(NULL);
    if (document == NULL) {
        vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not allocate settings JSON");
        return false;
    }
    yyjson_mut_val *root = yyjson_mut_obj(document);
    yyjson_mut_doc_set_root(document, root);
    if (!yyjson_mut_obj_add_str(document, root, KEY_THEME, settings->theme) ||
        !yyjson_mut_obj_add_uint(document, root, KEY_FONT_SIZE, settings->font_size) ||
        !yyjson_mut_obj_add_bool(document, root, KEY_SHOW_SIDEBAR, settings->show_sidebar) ||
        !yyjson_mut_obj_add_bool(document, root, KEY_SHOW_STATUS_BAR, settings->show_status_bar)) {
        vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not build settings JSON");
        yyjson_mut_doc_free(document);
        return false;
    }

    yyjson_write_err write_error;
    const bool ok = yyjson_mut_write_file(path, document, YYJSON_WRITE_PRETTY, NULL, &write_error);
    if (!ok) {
        vellum_error_set(error, VELLUM_ERR_IO, "%s: %s (yyjson code %d)", path,
                         write_error.msg != NULL ? write_error.msg : "write failed",
                         (int)write_error.code);
    }
    yyjson_mut_doc_free(document);
    return ok;
}
