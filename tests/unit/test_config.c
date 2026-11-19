/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "tests/unit/tests.h"
#include "vellum/config/config.h"
#include "vellum/core/error.h"

#ifndef VELLUM_CONFIG_DIR
#define VELLUM_CONFIG_DIR "."
#endif

#define TEST_INPUT "vellum_test_config_input.json"
#define TEST_OUTPUT "vellum_test_config_output.json"

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
    VellumSettings settings = vellum_settings_default();
    if (!write_file(TEST_INPUT, json)) {
        return false;
    }
    const bool failed = !vellum_settings_load(&settings, TEST_INPUT, &error);
    (void)remove(TEST_INPUT);
    if (!failed || error.code != VELLUM_ERR_PARSE) {
        return false;
    }
    return needle == NULL || strstr(vellum_error_message(&error), needle) != NULL;
}

bool test_config(void)
{
    VellumErrorInfo error;
    const VellumSettings defaults = vellum_settings_default();
    if (strcmp(defaults.theme, "dark") != 0 || defaults.font_size != 14u ||
        !defaults.show_sidebar || !defaults.show_status_bar) {
        return false;
    }

    char path[512];
    if (snprintf(path, sizeof(path), "%s/settings.json", VELLUM_CONFIG_DIR) < 0) {
        return false;
    }
    VellumSettings settings = vellum_settings_default();
    if (!vellum_settings_load(&settings, path, &error) || strcmp(settings.theme, "dark") != 0 ||
        settings.font_size != 14u || !settings.show_sidebar || !settings.show_status_bar) {
        return false;
    }

    if (!expect_parse_error("{\"theme\":}", "yyjson code") ||
        !expect_parse_error("{\"unknown\":true}", "unknown key 'unknown'") ||
        !expect_parse_error("{\"font_size\":12,\"font_size\":13}", "duplicate key 'font_size'") ||
        !expect_parse_error("{\"show_sidebar\":1}", "'show_sidebar' must be a boolean") ||
        !expect_parse_error("{\"font_size\":7}", "must be an integer in [8, 72]") ||
        !expect_parse_error("[]", "settings must be a JSON object") ||
        !expect_parse_error("{\"theme\":\"\"}", "non-empty name")) {
        return false;
    }

    settings = vellum_settings_default();
    strcpy(settings.theme, "light");
    settings.font_size = 20u;
    settings.show_sidebar = false;
    settings.show_status_bar = false;
    if (!vellum_settings_save(&settings, TEST_OUTPUT, &error)) {
        return false;
    }
    VellumSettings loaded = vellum_settings_default();
    const bool ok = vellum_settings_load(&loaded, TEST_OUTPUT, &error) &&
                    strcmp(loaded.theme, "light") == 0 && loaded.font_size == 20u &&
                    !loaded.show_sidebar && !loaded.show_status_bar;
    (void)remove(TEST_OUTPUT);
    return ok;
}
