/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */
#include "vellum/config/theme.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include <yyjson.h>

#include "json_util.h"

#define KEY_NAME "name"
#define KEY_COLORS "colors"
#define KEY_METRICS "metrics"
#define KEY_SPACING "spacing"
#define KEY_RADIUS "radius"

static const char *const TOKEN_NAMES[VELLUM_THEME_TOKEN_COUNT] = {"background", "sidebar",
                                                                  "tab_bar", "text", "accent"};

#define SEEN_NAME 0x01u
#define SEEN_COLORS 0x02u
#define SEEN_METRICS 0x04u
#define SEEN_SPACING 0x08u
#define SEEN_RADIUS 0x10u

static VellumTheme make_theme(const char *name, const float (*colors)[4], float spacing,
                              float radius)
{
    VellumTheme theme;
    memset(&theme, 0, sizeof(theme));
    memcpy(theme.name, name, strlen(name));
    for (size_t i = 0u; i < VELLUM_THEME_TOKEN_COUNT; ++i) {
        theme.colors[i] = vellum_color(colors[i][0], colors[i][1], colors[i][2], colors[i][3]);
    }
    theme.spacing = spacing;
    theme.radius = radius;
    return theme;
}

VellumTheme vellum_theme_dark(void)
{
    static const float colors[VELLUM_THEME_TOKEN_COUNT][4] = {{0.04f, 0.05f, 0.07f, 1.0f},
                                                              {0.08f, 0.09f, 0.12f, 1.0f},
                                                              {0.11f, 0.12f, 0.16f, 1.0f},
                                                              {0.85f, 0.87f, 0.92f, 1.0f},
                                                              {0.35f, 0.70f, 0.95f, 1.0f}};
    return make_theme("dark", colors, 8.0f, 4.0f);
}

VellumTheme vellum_theme_light(void)
{
    static const float colors[VELLUM_THEME_TOKEN_COUNT][4] = {{0.96f, 0.97f, 0.99f, 1.0f},
                                                              {0.90f, 0.92f, 0.95f, 1.0f},
                                                              {0.84f, 0.87f, 0.92f, 1.0f},
                                                              {0.12f, 0.15f, 0.20f, 1.0f},
                                                              {0.05f, 0.40f, 0.75f, 1.0f}};
    return make_theme("light", colors, 8.0f, 4.0f);
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

static bool json_number(const yyjson_val *value, double *out)
{
    if (yyjson_is_real(value)) {
        *out = yyjson_get_real(value);
        return true;
    }
    if (yyjson_is_int(value)) {
        *out = (double)yyjson_get_sint(value);
        return true;
    }
    return false;
}

static bool parse_color(VellumErrorInfo *error, const char *path, const char *token,
                        const yyjson_val *value, VellumColor *color)
{
    if (!yyjson_is_arr(value) || yyjson_arr_size(value) != 4u) {
        vellum_error_set(error, VELLUM_ERR_PARSE, "%s: color '%s' must have four channels", path,
                         token);
        return false;
    }
    float channels[4];
    for (size_t i = 0u; i < 4u; ++i) {
        double number = 0.0;
        if (!json_number(yyjson_arr_get(value, i), &number)) {
            vellum_error_set(error, VELLUM_ERR_PARSE, "%s: color '%s' channel %zu is not a number",
                             path, token, i);
            return false;
        }
        if (number < 0.0 || number > 1.0 || !isfinite(number)) {
            vellum_error_set(error, VELLUM_ERR_PARSE,
                             "%s: color '%s' channel %zu is outside [0, 1]", path, token, i);
            return false;
        }
        channels[i] = (float)number;
    }
    *color = vellum_color(channels[0], channels[1], channels[2], channels[3]);
    return true;
}

static bool parse_colors(VellumErrorInfo *error, const char *path, const yyjson_val *object,
                         VellumTheme *theme)
{
    if (!yyjson_is_obj(object)) {
        vellum_json_schema_error(error, path, "'colors' must be an object");
        return false;
    }
    uint32_t seen = 0u;
    yyjson_obj_iter iterator;
    yyjson_obj_iter_init(object, &iterator);
    yyjson_val *key;
    while ((key = yyjson_obj_iter_next(&iterator)) != NULL) {
        size_t token = VELLUM_THEME_TOKEN_COUNT;
        for (size_t i = 0u; i < VELLUM_THEME_TOKEN_COUNT; ++i) {
            if (vellum_json_key_equals(key, TOKEN_NAMES[i])) {
                token = i;
                break;
            }
        }
        if (token == VELLUM_THEME_TOKEN_COUNT) {
            return unknown_key(error, path, key);
        }
        const uint32_t bit = 1u << (uint32_t)token;
        if ((seen & bit) != 0u) {
            return duplicate_key(error, path, TOKEN_NAMES[token]);
        }
        seen |= bit;
        if (!parse_color(error, path, TOKEN_NAMES[token], yyjson_obj_iter_get_val(key),
                         &theme->colors[token])) {
            return false;
        }
    }
    if (seen != (1u << (uint32_t)VELLUM_THEME_TOKEN_COUNT) - 1u) {
        vellum_json_schema_error(error, path, "'colors' must define every semantic token");
        return false;
    }
    return true;
}

static bool parse_metric(VellumErrorInfo *error, const char *path, const char *key,
                         const yyjson_val *value, float *out)
{
    double number = 0.0;
    if (!json_number(value, &number) || number < 0.0 || number > 128.0 || !isfinite(number)) {
        vellum_error_set(error, VELLUM_ERR_PARSE, "%s: '%s' must be a number in [0, 128]", path,
                         key);
        return false;
    }
    *out = (float)number;
    return true;
}

static bool parse_metrics(VellumErrorInfo *error, const char *path, const yyjson_val *object,
                          VellumTheme *theme)
{
    if (!yyjson_is_obj(object)) {
        vellum_json_schema_error(error, path, "'metrics' must be an object");
        return false;
    }
    unsigned seen = 0u;
    yyjson_obj_iter iterator;
    yyjson_obj_iter_init(object, &iterator);
    yyjson_val *key;
    while ((key = yyjson_obj_iter_next(&iterator)) != NULL) {
        yyjson_val *value = yyjson_obj_iter_get_val(key);
        if (vellum_json_key_equals(key, KEY_SPACING)) {
            if ((seen & SEEN_SPACING) != 0u) {
                return duplicate_key(error, path, KEY_SPACING);
            }
            seen |= SEEN_SPACING;
            if (!parse_metric(error, path, KEY_SPACING, value, &theme->spacing)) {
                return false;
            }
        } else if (vellum_json_key_equals(key, KEY_RADIUS)) {
            if ((seen & SEEN_RADIUS) != 0u) {
                return duplicate_key(error, path, KEY_RADIUS);
            }
            seen |= SEEN_RADIUS;
            if (!parse_metric(error, path, KEY_RADIUS, value, &theme->radius)) {
                return false;
            }
        } else {
            return unknown_key(error, path, key);
        }
    }
    if (seen != (SEEN_SPACING | SEEN_RADIUS)) {
        vellum_json_schema_error(error, path, "'metrics' must define spacing and radius");
        return false;
    }
    return true;
}

static bool parse_theme(VellumTheme *theme, const yyjson_val *root, const char *path,
                        VellumErrorInfo *error)
{
    if (!yyjson_is_obj(root)) {
        vellum_json_schema_error(error, path, "theme must be a JSON object");
        return false;
    }
    unsigned seen = 0u;
    yyjson_obj_iter iterator;
    yyjson_obj_iter_init(root, &iterator);
    yyjson_val *key;
    while ((key = yyjson_obj_iter_next(&iterator)) != NULL) {
        yyjson_val *value = yyjson_obj_iter_get_val(key);
        if (vellum_json_key_equals(key, KEY_NAME)) {
            if ((seen & SEEN_NAME) != 0u) {
                return duplicate_key(error, path, KEY_NAME);
            }
            seen |= SEEN_NAME;
            const char *name = yyjson_get_str(value);
            const size_t length = yyjson_get_len(value);
            if (name == NULL || length == 0u || length >= VELLUM_THEME_NAME_MAX) {
                vellum_error_set(error, VELLUM_ERR_PARSE,
                                 "%s: 'name' must be a non-empty string under %u bytes", path,
                                 (unsigned)VELLUM_THEME_NAME_MAX - 1u);
                return false;
            }
            memcpy(theme->name, name, length);
            theme->name[length] = '\0';
        } else if (vellum_json_key_equals(key, KEY_COLORS)) {
            if ((seen & SEEN_COLORS) != 0u) {
                return duplicate_key(error, path, KEY_COLORS);
            }
            seen |= SEEN_COLORS;
            if (!parse_colors(error, path, value, theme)) {
                return false;
            }
        } else if (vellum_json_key_equals(key, KEY_METRICS)) {
            if ((seen & SEEN_METRICS) != 0u) {
                return duplicate_key(error, path, KEY_METRICS);
            }
            seen |= SEEN_METRICS;
            if (!parse_metrics(error, path, value, theme)) {
                return false;
            }
        } else {
            return unknown_key(error, path, key);
        }
    }
    if (seen != (SEEN_NAME | SEEN_COLORS | SEEN_METRICS)) {
        vellum_json_schema_error(error, path, "theme needs name, colors, and metrics");
        return false;
    }
    return true;
}

bool vellum_theme_load(VellumTheme *theme, const char *path, VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (theme == NULL || path == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "theme load needs a target and path");
        return false;
    }
    *theme = vellum_theme_dark();
    yyjson_doc *document = vellum_json_load_bounded(path, error);
    if (document == NULL) {
        return false;
    }
    const bool ok = parse_theme(theme, yyjson_doc_get_root(document), path, error);
    yyjson_doc_free(document);
    return ok;
}

VellumColor vellum_theme_color(const VellumTheme *theme, VellumThemeToken token)
{
    if (theme == NULL || (int)token < 0 || token >= VELLUM_THEME_TOKEN_COUNT) {
        return vellum_color(0.0f, 0.0f, 0.0f, 1.0f);
    }
    return theme->colors[token];
}

const char *vellum_theme_name(const VellumTheme *theme)
{
    return theme == NULL ? "none" : theme->name;
}
