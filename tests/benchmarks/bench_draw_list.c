/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "tests/benchmarks/benchmarks.h"
#include "vellum/graphics/draw_list.h"

#define BENCH_QUADS 200000u
#define BENCH_ITERATIONS 10u

static double elapsed_ms(uint64_t from, uint64_t to, uint64_t frequency)
{
    if (frequency == 0u || to < from) {
        return 0.0;
    }
    return (double)(to - from) * 1000.0 / (double)frequency;
}

bool bench_draw_list(void)
{
    VellumDrawList *list = vellum_draw_list_create(64u, BENCH_QUADS);
    if (list == NULL) {
        return false;
    }

    const uint64_t frequency = SDL_GetPerformanceFrequency();
    double times[BENCH_ITERATIONS] = {0.0};
    double total = 0.0;
    double minimum = 0.0;
    double maximum = 0.0;

    for (uint32_t iteration = 0u; iteration < BENCH_ITERATIONS; ++iteration) {
        const uint64_t start = SDL_GetPerformanceCounter();

        vellum_draw_list_clear(list);
        for (uint32_t i = 0u; i < BENCH_QUADS; ++i) {
            const float x = (float)(i % 240u) * 8.0f;
            const float y = (float)(i / 240u) * 16.0f;
            const VellumColor tint = vellum_color(0.85f, 0.87f, 0.92f, 1.0f);
            if (!vellum_draw_list_textured_quad(list, VELLUM_TEXTURE_NONE, x, y, 8.0f, 16.0f, 0.0f,
                                                0.0f, 1.0f, 1.0f, tint)) {
                vellum_draw_list_destroy(list);
                return false;
            }
        }

        const double ms = elapsed_ms(start, SDL_GetPerformanceCounter(), frequency);
        times[iteration] = ms;
        total += ms;
        if (iteration == 0u || ms < minimum) {
            minimum = ms;
        }
        if (ms > maximum) {
            maximum = ms;
        }
    }

    size_t count = 0u;
    const VellumTexturedQuad *quads = vellum_draw_list_quads(list, &count);
    const bool valid = quads != NULL && count == BENCH_QUADS;

    printf("draw-list construction (not the full ADR-004 rendering benchmark)\n");
    printf("  quads:            %u / frame\n", BENCH_QUADS);
    printf("  iterations:       %u\n", BENCH_ITERATIONS);
    printf("  synthetic scene:  1920x1080 viewport, 8x16 glyph cells\n");
    printf("  min / mean / max: %.3f / %.3f / %.3f ms\n", minimum, total / (double)BENCH_ITERATIONS,
           maximum);
    printf("  quads per second: %.2f M\n",
           (double)BENCH_QUADS * 1000.0 / (total / (double)BENCH_ITERATIONS) / 1000000.0);
    printf("  result:           %s\n", valid ? "ok" : "INVALID");

    vellum_draw_list_destroy(list);
    return valid;
}
