/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tests/unit/tests.h"
#include "vellum/text/buffer.h"

#define MODEL_CAPACITY 8192u
#define FUZZ_OPERATIONS 10000u

typedef struct {
    const char *text;
    size_t length;
} Chunk;

#define CHUNK(literal) {literal, sizeof(literal) - 1u}

static bool equals(const VellumBuffer *buffer, const char *expected, size_t length)
{
    char *snapshot = vellum_buffer_snapshot(buffer, NULL);
    if (snapshot == NULL || snapshot[length] != '\0' || memcmp(snapshot, expected, length) != 0) {
        free(snapshot);
        return false;
    }
    free(snapshot);
    return vellum_buffer_length(buffer) == length && vellum_buffer_validate(buffer);
}

static bool create_empty_and_invalid_arguments(void)
{
    VellumErrorInfo error;
    VellumBuffer *empty = vellum_buffer_create("", 0u, &error);
    if (empty == NULL || vellum_buffer_length(empty) != 0u ||
        vellum_buffer_piece_count(empty) != 0u || !vellum_buffer_validate(empty)) {
        vellum_buffer_destroy(empty);
        return false;
    }

    char byte = 'x';
    bool ok = vellum_buffer_read(empty, 0u, NULL, 0u, &error) && vellum_buffer_validate(empty) &&
              vellum_buffer_length(NULL) == 0u && vellum_buffer_piece_count(NULL) == 0u &&
              !vellum_buffer_validate(NULL);
    ok = ok && !vellum_buffer_read(empty, 1u, &byte, 1u, &error) &&
         error.code == VELLUM_ERR_INVALID_ARGUMENT;
    ok = ok && !vellum_buffer_read(empty, 0u, &byte, 1u, &error) &&
         !vellum_buffer_read(NULL, 0u, &byte, 1u, &error);
    ok = ok && !vellum_buffer_insert(empty, 1u, "a", 1u, &error);
    ok = ok && !vellum_buffer_insert(empty, 0u, NULL, 1u, &error);
    ok = ok && !vellum_buffer_insert(NULL, 0u, "a", 1u, &error);
    ok = ok && !vellum_buffer_delete(empty, 1u, 0u, &error);
    ok = ok && !vellum_buffer_delete(empty, 0u, 1u, &error);
    ok = ok && !vellum_buffer_delete(NULL, 0u, 0u, &error);
    ok = ok && vellum_buffer_insert(empty, 0u, NULL, 0u, &error) &&
         vellum_buffer_delete(empty, 0u, 0u, &error) && vellum_buffer_validate(empty);
    ok = ok && vellum_buffer_create(NULL, 1u, &error) == NULL &&
         error.code == VELLUM_ERR_INVALID_ARGUMENT;
    ok = ok && vellum_buffer_snapshot(NULL, &error) == NULL &&
         error.code == VELLUM_ERR_INVALID_ARGUMENT;

    char *snapshot = vellum_buffer_snapshot(empty, &error);
    ok = ok && snapshot != NULL && snapshot[0u] == '\0';
    free(snapshot);
    vellum_buffer_destroy(empty);
    vellum_buffer_destroy(NULL);
    return ok;
}

static bool insert_read_delete_basics(void)
{
    VellumErrorInfo error;
    VellumBuffer *buffer = vellum_buffer_create("hello world", 11u, &error);
    if (buffer == NULL) {
        return false;
    }

    bool ok = equals(buffer, "hello world", 11u);
    ok = ok && vellum_buffer_insert(buffer, 6u, "brave ", 6u, &error) &&
         equals(buffer, "hello brave world", 17u);

    char middle[5];
    ok = ok && vellum_buffer_read(buffer, 6u, middle, sizeof(middle), &error) &&
         memcmp(middle, "brave", sizeof(middle)) == 0;

    ok = ok && vellum_buffer_delete(buffer, 6u, 6u, &error) && equals(buffer, "hello world", 11u);
    ok = ok && vellum_buffer_insert(buffer, 11u, "!", 1u, &error) &&
         equals(buffer, "hello world!", 12u);
    ok = ok && vellum_buffer_insert(buffer, 0u, ">> ", 3u, &error) &&
         equals(buffer, ">> hello world!", 15u);
    ok = ok && vellum_buffer_delete(buffer, 0u, 15u, &error) && equals(buffer, "", 0u);
    ok = ok && vellum_buffer_piece_count(buffer) == 0u && vellum_buffer_validate(buffer);

    vellum_buffer_destroy(buffer);
    return ok;
}

static bool byte_offsets_and_binary_data(void)
{
    static const Chunk original = CHUNK("abcdefghij");
    static const Chunk inserted = CHUNK("XYZ");
    VellumErrorInfo error;
    VellumBuffer *buffer = vellum_buffer_create(original.text, original.length, &error);
    if (buffer == NULL) {
        return false;
    }

    bool ok = vellum_buffer_insert(buffer, 5u, inserted.text, inserted.length, &error) &&
              equals(buffer, "abcdeXYZfghij", 13u);
    ok = ok && vellum_buffer_delete(buffer, 3u, 8u, &error) && equals(buffer, "abcij", 5u);

    static const Chunk utf8 = CHUNK("世界\xD8\xB3\xD9\x84");
    static const Chunk binary = CHUNK("a\0b\xFF"
                                      "c");
    ok = ok && vellum_buffer_insert(buffer, 2u, utf8.text, utf8.length, &error);
    ok = ok && vellum_buffer_insert(buffer, 0u, binary.text, binary.length, &error);

    const size_t expected_length = 5u + utf8.length + binary.length;
    char expected[64];
    memcpy(expected, binary.text, binary.length);
    memcpy(expected + binary.length, "ab", 2u);
    memcpy(expected + binary.length + 2u, utf8.text, utf8.length);
    memcpy(expected + binary.length + 2u + utf8.length, "cij", 3u);

    char *snapshot = vellum_buffer_snapshot(buffer, &error);
    ok = ok && snapshot != NULL && snapshot[expected_length] == '\0' &&
         memcmp(snapshot, expected, expected_length) == 0;
    free(snapshot);
    ok = ok && vellum_buffer_validate(buffer) && vellum_buffer_length(buffer) == expected_length;

    ok = ok && vellum_buffer_delete(buffer, 0u, expected_length, &error) && equals(buffer, "", 0u);
    vellum_buffer_destroy(buffer);
    return ok;
}

static bool repeated_edits_stay_balanced(void)
{
    VellumErrorInfo error;
    VellumBuffer *buffer = vellum_buffer_create("", 0u, &error);
    if (buffer == NULL) {
        return false;
    }

    bool ok = true;
    for (uint32_t i = 0u; i < 2000u; ++i) {
        ok = ok && vellum_buffer_insert(buffer, 0u, "x", 1u, &error);
    }
    ok = ok && vellum_buffer_length(buffer) == 2000u && vellum_buffer_validate(buffer);
    char *snapshot = vellum_buffer_snapshot(buffer, &error);
    ok = ok && snapshot != NULL && strlen(snapshot) == 2000u;
    for (size_t i = 0u; snapshot != NULL && i < 2000u; ++i) {
        ok = ok && snapshot[i] == 'x';
    }
    free(snapshot);

    for (uint32_t i = 0u; i < 1000u; ++i) {
        ok = ok && vellum_buffer_insert(buffer, vellum_buffer_length(buffer) / 2u, "y", 1u, &error);
    }
    ok = ok && vellum_buffer_length(buffer) == 3000u && vellum_buffer_validate(buffer);
    for (uint32_t i = 0u; i < 3000u; ++i) {
        ok = ok && vellum_buffer_delete(buffer, 0u, 1u, &error);
    }
    ok = ok && equals(buffer, "", 0u) && vellum_buffer_piece_count(buffer) == 0u;

    vellum_buffer_destroy(buffer);
    return ok;
}

static uint32_t next_random(uint32_t *state)
{
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static bool differential_fuzz(void)
{
    static const Chunk snippets[] = {
        CHUNK(""),          CHUNK("a"),         CHUNK("hello"), CHUNK("\n\t"),
        CHUNK("const int"), CHUNK("\xC3\xA9"),  CHUNK("世界"),  CHUNK("\xD8\xB3\xD9\x84"),
        CHUNK("x\0y"),      CHUNK("0123456789")};
    const size_t snippet_count = sizeof(snippets) / sizeof(snippets[0u]);

    char model[MODEL_CAPACITY];
    static const char seed[] = "hello 世界 مرحبا\n";
    size_t model_length = sizeof(seed) - 1u;
    memcpy(model, seed, model_length);

    VellumErrorInfo error;
    VellumBuffer *buffer = vellum_buffer_create(model, model_length, &error);
    if (buffer == NULL) {
        return false;
    }

    uint32_t state = 0x5EED1234u;
    bool ok = true;
    for (uint32_t operation = 0u; operation < FUZZ_OPERATIONS && ok; ++operation) {
        const uint32_t choice = next_random(&state) % 100u;
        if (choice < 45u) {
            const Chunk *snippet = &snippets[next_random(&state) % snippet_count];
            if (snippet->length > MODEL_CAPACITY - model_length) {
                continue;
            }
            const size_t position = next_random(&state) % (model_length + 1u);
            ok = vellum_buffer_insert(buffer, position, snippet->text, snippet->length, &error);
            memmove(model + position + snippet->length, model + position, model_length - position);
            memcpy(model + position, snippet->text, snippet->length);
            model_length += snippet->length;
        } else if (choice < 85u && model_length > 0u) {
            const size_t position = next_random(&state) % model_length;
            size_t length = 1u + (next_random(&state) % 16u);
            if (length > model_length - position) {
                length = model_length - position;
            }
            ok = vellum_buffer_delete(buffer, position, length, &error);
            memmove(model + position, model + position + length, model_length - position - length);
            model_length -= length;
        } else if (model_length > 0u) {
            const size_t position = next_random(&state) % model_length;
            const size_t length = 1u + (next_random(&state) % 32u);
            const size_t available = model_length - position;
            const size_t count = length < available ? length : available;
            char actual[32];
            ok = vellum_buffer_read(buffer, position, actual, count, &error) &&
                 memcmp(actual, model + position, count) == 0;
        }

        ok = ok && vellum_buffer_length(buffer) == model_length && vellum_buffer_validate(buffer);
        if (ok && (operation % 128u == 0u || operation + 1u == FUZZ_OPERATIONS)) {
            char *snapshot = vellum_buffer_snapshot(buffer, &error);
            if (snapshot == NULL || snapshot[model_length] != '\0' ||
                memcmp(snapshot, model, model_length) != 0) {
                ok = false;
            }
            free(snapshot);
        }
        if (!ok) {
            fprintf(stderr, "buffer fuzz failed at operation %u (%s)\n", operation,
                    vellum_error_message(&error));
        }
    }

    vellum_buffer_destroy(buffer);
    return ok;
}

bool test_buffer(void)
{
    return create_empty_and_invalid_arguments() && insert_read_delete_basics() &&
           byte_offsets_and_binary_data() && repeated_edits_stay_balanced() && differential_fuzz();
}
