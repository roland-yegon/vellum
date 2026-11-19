/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "tests/unit/tests.h"
#include "vellum/config/theme.h"
#include "vellum/core/error.h"

#ifndef VELLUM_CONFIG_DIR
#define VELLUM_CONFIG_DIR "."
#endif

#define TEST_INPUT "vellum_test_theme_input.json"

static const char *VALID_THEME = "{\"name\":\"test\",\"colors\":{"
                                 "\"background\":[0,0,0,1],\"sidebar\":[0.1,0.2,0.3,1],"
                                 "\"tab_bar\":[0,0,0,1],\"text\":[1,1,1,1],\"accent\":[0,0.5,1,1]},"
                                 "\"metrics\":{\"spacing\":4,\"radius\":2}}";

static bool write_file(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return false;
    }
    const size_t length = strlen(text);
    const bool ok = fwrite(text, 1u, length, file) == length;
    return fclose(file) == 0 && ok;
}

static bool expect_parse_error(const char *json, const char *needle)
{
    VellumErrorInfo error;
    VellumTheme theme = vellum_theme_dark();
    if (!write_file(TEST_INPUT, json)) {
        return false;
    }
    const bool failed = !vellum_theme_load(&theme, TEST_INPUT, &error);
    (void)remove(TEST_INPUT);
    if (!failed || error.code != VELLUM_ERR_PARSE) {
        return false;
    }
    return needle == NULL || strstr(vellum_error_message(&error), needle) != NULL;
}

bool test_theme(void)
{
    VellumErrorInfo error;
    const VellumTheme dark = vellum_theme_dark();
    const VellumTheme light = vellum_theme_light();
    if (strcmp(vellum_theme_name(&dark), "dark") != 0 ||
        strcmp(vellum_theme_name(&light), "light") != 0 ||
        vellum_theme_color(&dark, VELLUM_THEME_BACKGROUND).r >=
            vellum_theme_color(&light, VELLUM_THEME_BACKGROUND).r ||
        vellum_theme_color(&dark, VELLUM_THEME_TEXT).r <=
            vellum_theme_color(&light, VELLUM_THEME_TEXT).r ||
        dark.spacing != 8.0f || dark.radius != 4.0f) {
        return false;
    }

    char path[512];
    VellumTheme loaded = vellum_theme_dark();
    if (snprintf(path, sizeof(path), "%s/themes/dark.json", VELLUM_CONFIG_DIR) < 0 ||
        !vellum_theme_load(&loaded, path, &error) || strcmp(loaded.name, "dark") != 0 ||
        vellum_theme_color(&loaded, VELLUM_THEME_ACCENT).b != 0.95f) {
        return false;
    }
    loaded = vellum_theme_light();
    if (snprintf(path, sizeof(path), "%s/themes/light.json", VELLUM_CONFIG_DIR) < 0 ||
        !vellum_theme_load(&loaded, path, &error) || strcmp(loaded.name, "light") != 0 ||
        vellum_theme_color(&loaded, VELLUM_THEME_TEXT).r != 0.12f) {
        return false;
    }

    if (!write_file(TEST_INPUT, VALID_THEME)) {
        return false;
    }
    loaded = vellum_theme_dark();
    const bool valid = vellum_theme_load(&loaded, TEST_INPUT, &error) &&
                       strcmp(loaded.name, "test") == 0 &&
                       vellum_theme_color(&loaded, VELLUM_THEME_SIDEBAR).g == 0.2f &&
                       loaded.spacing == 4.0f && loaded.radius == 2.0f;
    (void)remove(TEST_INPUT);
    if (!valid) {
        return false;
    }

    char extra[1024];
    const size_t length = strlen(VALID_THEME);
    memcpy(extra, VALID_THEME, length - 1u);
    memcpy(extra + length - 1u, ",\"extra\":1}", sizeof(",\"extra\":1}"));
    const char *missing_metrics =
        "{\"name\":\"x\",\"colors\":{\"background\":[0,0,0,1],\"sidebar\":[0,0,0,1],"
        "\"tab_bar\":[0,0,0,1],\"text\":[1,1,1,1],\"accent\":[0,0,0,1]}}";
    if (!expect_parse_error("{\"name\":}", "yyjson code") ||
        !expect_parse_error("{\"name\":\"x\",\"colors\":{},\"metrics\":{", "yyjson code") ||
        !expect_parse_error(extra, "unknown key 'extra'") ||
        !expect_parse_error("{\"name\":\"x\",\"name\":\"y\",\"colors\":{},\"metrics\":{}}",
                            "duplicate key 'name'") ||
        !expect_parse_error(missing_metrics, "theme needs name, colors, and metrics") ||
        !expect_parse_error("{\"name\":\"x\",\"colors\":{\"text\":[0,0,0]},\"metrics\":{}}",
                            "four channels") ||
        !expect_parse_error("{\"name\":\"x\",\"colors\":{\"text\":[0,0,0,2]},\"metrics\":{}}",
                            "outside [0, 1]")) {
        return false;
    }
    return true;
}
