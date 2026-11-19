/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdint.h>

#include "tests/unit/tests.h"
#include "vellum/core/trace.h"

bool test_trace(void)
{
    VellumTrace trace;
    vellum_trace_init(&trace);
    if (vellum_trace_frame_count(&trace) != 0u || vellum_trace_average_ms(&trace) != 0.0) {
        return false;
    }

    for (uint32_t i = 0u; i < 4u; ++i) {
        vellum_trace_begin_frame(&trace);
        SDL_Delay(2u);
        vellum_trace_end_frame(&trace);
    }

    if (vellum_trace_frame_count(&trace) != 4u || vellum_trace_last_ms(&trace) <= 0.0 ||
        vellum_trace_average_ms(&trace) <= 0.0 || vellum_trace_max_ms(&trace) <= 0.0) {
        return false;
    }
    if (vellum_trace_last_work_ms(&trace) <= 0.0 || vellum_trace_average_work_ms(&trace) <= 0.0) {
        return false;
    }

    vellum_trace_reset(&trace);
    return vellum_trace_frame_count(&trace) == 0u && vellum_trace_max_ms(&trace) == 0.0;
}
