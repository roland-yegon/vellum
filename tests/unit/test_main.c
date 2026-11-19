/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#include <stdbool.h>
#include <stdio.h>

#include "tests/unit/tests.h"
#include "vellum/core/log.h"

typedef bool (*TestFunction)(void);

int main(void)
{
    vellum_log_init();
    vellum_log_set_level(VELLUM_LOG_ERROR);

    const TestFunction tests[] = {test_error,   test_log,   test_arena,  test_array,
                                  test_queue,   test_trace, test_events, test_draw_list,
                                  test_unicode, test_font,  test_shape,  test_layout,
                                  test_config,  test_theme, test_paths};
    const char *names[] = {"error", "log",    "arena",     "array",   "queue",
                           "trace", "events", "draw_list", "unicode", "font",
                           "shape", "layout", "config",    "theme",   "paths"};
    const size_t count = sizeof(tests) / sizeof(tests[0]);

    size_t passed = 0u;
    for (size_t i = 0u; i < count; ++i) {
        if (tests[i]()) {
            printf("PASS  %s\n", names[i]);
            passed++;
        } else {
            printf("FAIL  %s\n", names[i]);
        }
    }

    vellum_log_shutdown();
    printf("%zu/%zu tests passed\n", passed, count);
    return passed == count ? 0 : 1;
}
