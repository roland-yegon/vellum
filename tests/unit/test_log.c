/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#include <stdbool.h>

#include "tests/unit/tests.h"
#include "vellum/core/log.h"

bool test_log(void)
{
    const VellumLogLevel original = vellum_log_get_level();

    vellum_log_set_level(VELLUM_LOG_WARN);
    if (vellum_log_get_level() != VELLUM_LOG_WARN) {
        return false;
    }
    if (!vellum_log_enabled(VELLUM_LOG_ERROR) || vellum_log_enabled(VELLUM_LOG_DEBUG)) {
        return false;
    }

    VELLUM_LOG_DEBUG("test", "suppressed %d", 1);
    VELLUM_LOG_INFO("test", "suppressed %d", 2);
    VELLUM_LOG_WARN("test", "warning path %d", 3);
    VELLUM_LOG_ERROR("test", "error path %d", 4);

    vellum_log_set_level(original);
    return vellum_log_get_level() == original;
}
