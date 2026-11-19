/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tests/benchmarks/benchmarks.h"
#include "vellum/core/error.h"
#include "vellum/text/buffer.h"
#include "vellum/text/line_index.h"

/* Phase 3 exit targets: 100 MB open and editing near the end of a 1M-line
 * file. Times are measured with SDL's monotonic performance counters; the
 * benchmark is built without sanitizers (the `bench` preset). */
#define OPEN_BYTES (100u * 1024u * 1024u)
#define OPEN_LINE_BYTES 100u
#define OPEN_LINES (OPEN_BYTES / OPEN_LINE_BYTES)

#define TYPING_LINES 1000000u
#define TYPING_LINE_BYTES 10u
#define TYPING_BYTES (TYPING_LINES * TYPING_LINE_BYTES)
#define TYPING_OPS 2000u
#define RANDOM_OPS 10000u

static char *make_document(size_t bytes, size_t line_bytes, char fill)
{
    char *text = malloc(bytes);
    if (text == NULL) {
        return NULL;
    }
    memset(text, (int)(unsigned char)fill, bytes);
    for (size_t i = line_bytes - 1u; i < bytes; i += line_bytes) {
        text[i] = '\n';
    }
    return text;
}

static double elapsed_ms(uint64_t start, uint64_t end, uint64_t frequency)
{
    return (double)(end - start) * 1000.0 / (double)frequency;
}

static double elapsed_us(uint64_t start, uint64_t end, uint64_t frequency)
{
    return (double)(end - start) * 1000000.0 / (double)frequency;
}

static int compare_double(const void *a, const void *b)
{
    const double left = *(const double *)a;
    const double right = *(const double *)b;
    return left < right ? -1 : (left > right ? 1 : 0);
}

static double percentile(const double *sorted, size_t count, double fraction)
{
    if (count == 0u) {
        return 0.0;
    }
    const size_t index = (size_t)((double)(count - 1u) * fraction);
    return sorted[index];
}

static void report_samples(double *samples, size_t count)
{
    qsort(samples, count, sizeof(samples[0]), compare_double);
    double total = 0.0;
    for (size_t i = 0u; i < count; ++i) {
        total += samples[i];
    }
    printf("  ops:                  %zu\n", count);
    printf("  mean / p50 (us):      %.3f / %.3f\n", total / (double)count,
           percentile(samples, count, 0.5));
    printf("  p95 / p99 (us):       %.3f / %.3f\n", percentile(samples, count, 0.95),
           percentile(samples, count, 0.99));
    printf("  max (us):             %.3f\n", percentile(samples, count, 1.0));
}

static bool bench_open(VellumErrorInfo *error)
{
    printf("== 100 MB file open ==\n");
    char *text = make_document(OPEN_BYTES, OPEN_LINE_BYTES, 'a');
    if (text == NULL) {
        fprintf(stderr, "could not allocate the 100 MB source text\n");
        return false;
    }

    const uint64_t frequency = SDL_GetPerformanceFrequency();
    const uint64_t t0 = SDL_GetPerformanceCounter();
    VellumBuffer *buffer = vellum_buffer_create(text, OPEN_BYTES, error);
    const uint64_t t1 = SDL_GetPerformanceCounter();
    VellumLineIndex *index = buffer == NULL ? NULL : vellum_line_index_create(error);
    const uint64_t t2 = SDL_GetPerformanceCounter();
    const bool indexed = index != NULL && vellum_line_index_rebuild(index, buffer, error);
    const uint64_t t3 = SDL_GetPerformanceCounter();
    if (buffer == NULL || !indexed) {
        fprintf(stderr, "open benchmark setup failed: %s\n", vellum_error_message(error));
        free(text);
        vellum_line_index_destroy(index);
        vellum_buffer_destroy(buffer);
        return false;
    }

    printf("  bytes / lines:        %zu / %zu\n", vellum_buffer_length(buffer),
           vellum_line_index_line_count(index));
    printf("  pieces:               %zu\n", vellum_buffer_piece_count(buffer));
    printf("  buffer_create (ms):   %.3f\n", elapsed_ms(t0, t1, frequency));
    printf("  index_create (ms):    %.3f\n", elapsed_ms(t1, t2, frequency));
    printf("  index_rebuild (ms):   %.3f\n", elapsed_ms(t2, t3, frequency));
    printf("  total open (ms):      %.3f\n", elapsed_ms(t0, t3, frequency));

    /* Structural checks run after the timed section. */
    const bool ok = vellum_buffer_validate(buffer) && vellum_line_index_validate(index, buffer) &&
                    vellum_line_index_line_count(index) == OPEN_LINES + 1u;
    if (!ok) {
        fprintf(stderr, "100 MB open benchmark produced an invalid buffer or index\n");
    }

    vellum_line_index_destroy(index);
    vellum_buffer_destroy(buffer);
    free(text);
    return ok;
}

static bool bench_typing(VellumErrorInfo *error)
{
    printf("== 1M-line end typing ==\n");
    char *text = make_document(TYPING_BYTES, TYPING_LINE_BYTES, 'b');
    if (text == NULL) {
        fprintf(stderr, "could not allocate the 1M-line source text\n");
        return false;
    }
    VellumBuffer *buffer = vellum_buffer_create(text, TYPING_BYTES, error);
    VellumLineIndex *index = buffer == NULL ? NULL : vellum_line_index_create(error);
    double *samples = malloc(TYPING_OPS * sizeof(*samples));
    if (buffer == NULL || index == NULL || samples == NULL ||
        !vellum_line_index_rebuild(index, buffer, error)) {
        fprintf(stderr, "typing benchmark setup failed: %s\n", vellum_error_message(error));
        free(samples);
        vellum_line_index_destroy(index);
        vellum_buffer_destroy(buffer);
        free(text);
        return false;
    }

    const uint64_t frequency = SDL_GetPerformanceFrequency();
    size_t offset = vellum_buffer_length(buffer);
    bool ok = true;
    for (uint32_t i = 0u; i < TYPING_OPS && ok; ++i) {
        const uint64_t start = SDL_GetPerformanceCounter();
        ok = vellum_buffer_insert(buffer, offset, "x", 1u, error) &&
             vellum_line_index_notify_insert(index, buffer, offset, 1u, error);
        const uint64_t end = SDL_GetPerformanceCounter();
        if (!ok) {
            fprintf(stderr, "typing insert at op %u failed: %s\n", i, vellum_error_message(error));
            break;
        }
        samples[i] = elapsed_us(start, end, frequency);
        ++offset;
    }
    if (ok) {
        report_samples(samples, TYPING_OPS);
        ok = vellum_buffer_validate(buffer) && vellum_line_index_validate(index, buffer) &&
             vellum_buffer_length(buffer) == TYPING_BYTES + TYPING_OPS;
    }

    free(samples);
    vellum_line_index_destroy(index);
    vellum_buffer_destroy(buffer);
    free(text);
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

static bool bench_random_edits(VellumErrorInfo *error)
{
    printf("== 1M-line random edits ==\n");
    char *text = make_document(TYPING_BYTES, TYPING_LINE_BYTES, 'c');
    if (text == NULL) {
        fprintf(stderr, "could not allocate the 1M-line source text\n");
        return false;
    }
    VellumBuffer *buffer = vellum_buffer_create(text, TYPING_BYTES, error);
    VellumLineIndex *index = buffer == NULL ? NULL : vellum_line_index_create(error);
    double *samples = malloc(RANDOM_OPS * sizeof(*samples));
    if (buffer == NULL || index == NULL || samples == NULL ||
        !vellum_line_index_rebuild(index, buffer, error)) {
        fprintf(stderr, "random-edit benchmark setup failed: %s\n", vellum_error_message(error));
        free(samples);
        vellum_line_index_destroy(index);
        vellum_buffer_destroy(buffer);
        free(text);
        return false;
    }

    const uint64_t frequency = SDL_GetPerformanceFrequency();
    uint32_t state = 0x5EED1234u;
    bool ok = true;
    for (uint32_t i = 0u; i < RANDOM_OPS && ok; ++i) {
        const size_t length = vellum_buffer_length(buffer);
        const size_t offset = (size_t)(next_random(&state) % (uint32_t)length);
        const bool insert = (next_random(&state) & 1u) != 0u;
        const uint64_t start = SDL_GetPerformanceCounter();
        if (insert) {
            ok = vellum_buffer_insert(buffer, offset, "Z", 1u, error) &&
                 vellum_line_index_notify_insert(index, buffer, offset, 1u, error);
        } else {
            ok = vellum_buffer_delete(buffer, offset, 1u, error) &&
                 vellum_line_index_notify_delete(index, buffer, offset, 1u, error);
        }
        const uint64_t end = SDL_GetPerformanceCounter();
        if (!ok) {
            fprintf(stderr, "random edit at op %u failed: %s\n", i, vellum_error_message(error));
            break;
        }
        samples[i] = elapsed_us(start, end, frequency);
    }
    if (ok) {
        report_samples(samples, RANDOM_OPS);
        ok = vellum_buffer_validate(buffer) && vellum_line_index_validate(index, buffer);
    }

    free(samples);
    vellum_line_index_destroy(index);
    vellum_buffer_destroy(buffer);
    free(text);
    return ok;
}

bool bench_text_buffer(void)
{
    VellumErrorInfo error;
    vellum_error_init(&error);
    bool ok = bench_open(&error);
    ok = bench_typing(&error) && ok;
    ok = bench_random_edits(&error) && ok;
    return ok;
}
