/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#include "vellum/core/arena.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct VellumArenaChunk {
    struct VellumArenaChunk *next;
    unsigned char *base;
    unsigned char *data;
    size_t capacity;
    size_t used;
    size_t data_alignment;
} VellumArenaChunk;

struct VellumArena {
    VellumArenaChunk *chunks;
    size_t chunk_size;
    size_t total_allocated;
    size_t total_used;
    size_t allocations;
};

static bool valid_alignment(size_t alignment)
{
    return alignment != 0u && (alignment & (alignment - 1u)) == 0u;
}

static bool align_up(size_t value, size_t alignment, size_t *result)
{
    if (!valid_alignment(alignment) || result == NULL) {
        return false;
    }
    const size_t mask = alignment - 1u;
    if (value > SIZE_MAX - mask) {
        return false;
    }
    *result = (value + mask) & ~mask;
    return true;
}

static VellumArenaChunk *arena_chunk_create(size_t capacity, size_t alignment)
{
    if (!valid_alignment(alignment)) {
        return NULL;
    }

    const size_t header = sizeof(VellumArenaChunk);
    const size_t slack = alignment - 1u;
    if (capacity > SIZE_MAX - header || header + capacity > SIZE_MAX - slack) {
        return NULL;
    }

    unsigned char *base = malloc(header + capacity + slack);
    if (base == NULL) {
        return NULL;
    }

    const uintptr_t raw = (uintptr_t)(base + header);
    if (raw > UINTPTR_MAX - (uintptr_t)slack) {
        free(base);
        return NULL;
    }
    const uintptr_t mask = (uintptr_t)slack;
    const uintptr_t aligned = (raw + mask) & ~mask;

    VellumArenaChunk *chunk = (VellumArenaChunk *)(void *)base;
    chunk->next = NULL;
    chunk->base = base;
    chunk->data = (unsigned char *)(void *)aligned;
    chunk->capacity = capacity;
    chunk->used = 0u;
    chunk->data_alignment = alignment;
    return chunk;
}

VellumArena *vellum_arena_create(size_t chunk_size)
{
    VellumArena *arena = malloc(sizeof(*arena));
    if (arena == NULL) {
        return NULL;
    }

    if (chunk_size == 0u) {
        chunk_size = VELLUM_ARENA_DEFAULT_CHUNK;
    }

    arena->chunks = arena_chunk_create(chunk_size, _Alignof(max_align_t));
    if (arena->chunks == NULL) {
        free(arena);
        return NULL;
    }

    arena->chunk_size = chunk_size;
    arena->total_allocated = chunk_size;
    arena->total_used = 0u;
    arena->allocations = 0u;
    return arena;
}

void *vellum_arena_alloc(VellumArena *arena, size_t size, size_t alignment)
{
    if (arena == NULL || size == 0u || !valid_alignment(alignment)) {
        return NULL;
    }

    VellumArenaChunk *chunk = arena->chunks;
    if (chunk != NULL && chunk->data_alignment >= alignment) {
        size_t offset = 0u;
        if (align_up(chunk->used, alignment, &offset) && offset <= chunk->capacity &&
            size <= chunk->capacity - offset) {
            chunk->used = offset + size;
            arena->total_used += size;
            arena->allocations++;
            return chunk->data + offset;
        }
    }

    if (size > SIZE_MAX - (alignment - 1u)) {
        return NULL;
    }
    const size_t needed = size + alignment - 1u;
    const size_t capacity = needed > arena->chunk_size ? needed : arena->chunk_size;

    chunk = arena_chunk_create(capacity, alignment);
    if (chunk == NULL) {
        return NULL;
    }
    chunk->next = arena->chunks;
    arena->chunks = chunk;
    arena->total_allocated += capacity;

    /* data is already aligned to at least `alignment`. */
    chunk->used = size;
    arena->total_used += size;
    arena->allocations++;
    return chunk->data;
}

void *vellum_arena_alloc_zeroed(VellumArena *arena, size_t size, size_t alignment)
{
    void *memory = vellum_arena_alloc(arena, size, alignment);
    if (memory != NULL) {
        memset(memory, 0, size);
    }
    return memory;
}

char *vellum_arena_strdup(VellumArena *arena, const char *text)
{
    if (text == NULL) {
        return NULL;
    }
    const size_t length = strlen(text);
    char *copy = vellum_arena_alloc(arena, length + 1u, _Alignof(char));
    if (copy != NULL) {
        memcpy(copy, text, length + 1u);
    }
    return copy;
}

size_t vellum_arena_used(const VellumArena *arena)
{
    return arena == NULL ? 0u : arena->total_used;
}

size_t vellum_arena_capacity(const VellumArena *arena)
{
    if (arena == NULL) {
        return 0u;
    }

    size_t total = 0u;
    for (const VellumArenaChunk *chunk = arena->chunks; chunk != NULL; chunk = chunk->next) {
        total += chunk->capacity;
    }
    return total;
}

size_t vellum_arena_allocation_count(const VellumArena *arena)
{
    return arena == NULL ? 0u : arena->allocations;
}

void vellum_arena_reset(VellumArena *arena)
{
    if (arena == NULL) {
        return;
    }
    for (VellumArenaChunk *chunk = arena->chunks; chunk != NULL; chunk = chunk->next) {
        chunk->used = 0u;
    }
    arena->total_used = 0u;
    arena->allocations = 0u;
}

void vellum_arena_destroy(VellumArena *arena)
{
    if (arena == NULL) {
        return;
    }

    VellumArenaChunk *chunk = arena->chunks;
    while (chunk != NULL) {
        VellumArenaChunk *next = chunk->next;
        free(chunk->base);
        chunk = next;
    }
    free(arena);
}
