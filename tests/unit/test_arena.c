/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "tests/unit/tests.h"
#include "vellum/core/arena.h"

bool test_arena(void)
{
    VellumArena *arena = vellum_arena_create(128u);
    if (arena == NULL || vellum_arena_used(arena) != 0u || vellum_arena_reset_count(arena) != 0u ||
        vellum_arena_reset_count(NULL) != 0u) {
        return false;
    }

    uint32_t *values = vellum_arena_alloc(arena, 4u * sizeof(uint32_t), 16u);
    if (values == NULL || ((uintptr_t)values % 16u) != 0u) {
        vellum_arena_destroy(arena);
        return false;
    }
    values[0] = 11u;
    values[3] = 44u;
    if (vellum_arena_used(arena) != 4u * sizeof(uint32_t) ||
        vellum_arena_allocation_count(arena) != 1u) {
        vellum_arena_destroy(arena);
        return false;
    }

    void *zeroed = vellum_arena_alloc_zeroed(arena, 64u, 8u);
    if (zeroed == NULL) {
        vellum_arena_destroy(arena);
        return false;
    }
    const unsigned char *bytes = zeroed;
    for (size_t i = 0u; i < 64u; ++i) {
        if (bytes[i] != 0u) {
            vellum_arena_destroy(arena);
            return false;
        }
    }

    char *copy = vellum_arena_strdup(arena, "vellum");
    if (copy == NULL || strcmp(copy, "vellum") != 0) {
        vellum_arena_destroy(arena);
        return false;
    }

    if (vellum_arena_alloc(arena, 16u, 3u) != NULL || vellum_arena_alloc(arena, 0u, 8u) != NULL ||
        vellum_arena_alloc(NULL, 8u, 8u) != NULL) {
        vellum_arena_destroy(arena);
        return false;
    }

    const size_t capacity_before = vellum_arena_capacity(arena);
    void *large = vellum_arena_alloc(arena, 4096u, 64u);
    if (large == NULL || ((uintptr_t)large % 64u) != 0u ||
        vellum_arena_capacity(arena) <= capacity_before) {
        vellum_arena_destroy(arena);
        return false;
    }

    vellum_arena_reset(arena);
    if (vellum_arena_used(arena) != 0u || vellum_arena_allocation_count(arena) != 0u ||
        vellum_arena_reset_count(arena) != 1u) {
        vellum_arena_destroy(arena);
        return false;
    }
    vellum_arena_reset(arena);
    if (vellum_arena_reset_count(arena) != 2u) {
        vellum_arena_destroy(arena);
        return false;
    }
    vellum_arena_reset(NULL);

    vellum_arena_destroy(arena);
    return true;
}
