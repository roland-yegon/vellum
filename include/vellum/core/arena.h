/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#ifndef VELLUM_CORE_ARENA_H
#define VELLUM_CORE_ARENA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VELLUM_ARENA_DEFAULT_CHUNK (64u * 1024u)

typedef struct VellumArena VellumArena;

VellumArena *vellum_arena_create(size_t chunk_size);
void *vellum_arena_alloc(VellumArena *arena, size_t size, size_t alignment);
void *vellum_arena_alloc_zeroed(VellumArena *arena, size_t size, size_t alignment);
char *vellum_arena_strdup(VellumArena *arena, const char *text);
size_t vellum_arena_used(const VellumArena *arena);
size_t vellum_arena_capacity(const VellumArena *arena);
size_t vellum_arena_allocation_count(const VellumArena *arena);
/* Lifetime number of resets; paired with the count above this shows per-frame churn. */
uint64_t vellum_arena_reset_count(const VellumArena *arena);
void vellum_arena_reset(VellumArena *arena);
void vellum_arena_destroy(VellumArena *arena);

#ifdef __cplusplus
}
#endif

#endif /* VELLUM_CORE_ARENA_H */
