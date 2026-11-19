/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "tests/unit/tests.h"
#include "vellum/core/error.h"
#include "vellum/platform/paths.h"

#define TEST_FILE "vellum_test_paths.txt"

bool test_paths(void)
{
    VellumErrorInfo error;
    char preferences[512];
    if (!vellum_paths_preferences(preferences, sizeof(preferences), &error) ||
        preferences[0] == '\0') {
        return false;
    }

    char user[512];
    if (!vellum_paths_user_config(user, sizeof(user), "settings.json", &error) ||
        strstr(user, "settings.json") == NULL ||
        strncmp(user, preferences, strlen(preferences)) != 0) {
        return false;
    }

    char theme[512];
    if (!vellum_paths_user_config(theme, sizeof(theme), "themes/dark.json", &error) ||
        strstr(theme, "themes/dark.json") == NULL) {
        return false;
    }

    if (vellum_paths_user_config(user, sizeof(user), "../evil.json", &error) ||
        error.code != VELLUM_ERR_INVALID_ARGUMENT ||
        vellum_paths_user_config(user, sizeof(user), "/etc/passwd", &error) ||
        error.code != VELLUM_ERR_INVALID_ARGUMENT ||
        vellum_paths_user_config(user, 4u, "settings.json", &error) ||
        error.code != VELLUM_ERR_CAPACITY) {
        return false;
    }

    FILE *file = fopen(TEST_FILE, "wb");
    if (file == NULL) {
        return false;
    }
    (void)fputs("x", file);
    (void)fclose(file);
    const bool exists = vellum_path_exists(TEST_FILE);
    (void)remove(TEST_FILE);
    if (!exists || vellum_path_exists(TEST_FILE) || vellum_path_exists(NULL)) {
        return false;
    }
    return true;
}
