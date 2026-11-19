/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdint.h>

#include "tests/unit/tests.h"
#include "vellum/core/queue.h"

#define PRODUCER_COUNT 3u
#define MESSAGES_PER_PRODUCER 100u

typedef struct {
    VellumQueue *queue;
    uint32_t producer_id;
} ProducerContext;

static int SDLCALL producer(void *data)
{
    const ProducerContext *context = data;
    for (uint32_t i = 0u; i < MESSAGES_PER_PRODUCER; ++i) {
        VellumMessage message = {0};
        message.type = context->producer_id;
        message.a = i;
        if (!vellum_queue_push(context->queue, &message)) {
            return 1;
        }
    }
    return 0;
}

bool test_queue(void)
{
    VellumQueue *queue = vellum_queue_create(0u);
    if (queue != NULL) {
        return false;
    }
    queue = vellum_queue_create(8u);
    if (queue == NULL) {
        return false;
    }

    VellumMessage message = {0};
    message.type = 1u;
    message.a = 42u;
    message.ptr = queue;
    if (!vellum_queue_try_push(queue, &message) || vellum_queue_size(queue) != 1u) {
        vellum_queue_destroy(queue);
        return false;
    }

    VellumMessage received = {0};
    if (!vellum_queue_try_pop(queue, &received) || received.type != 1u || received.a != 42u ||
        received.ptr != queue || vellum_queue_try_pop(queue, &received)) {
        vellum_queue_destroy(queue);
        return false;
    }

    for (uint32_t i = 0u; i < 8u; ++i) {
        message.a = i;
        if (!vellum_queue_try_push(queue, &message)) {
            vellum_queue_destroy(queue);
            return false;
        }
    }
    if (vellum_queue_try_push(queue, &message)) {
        vellum_queue_destroy(queue);
        return false;
    }

    vellum_queue_shutdown(queue);
    if (!vellum_queue_is_shutdown(queue) || vellum_queue_push(queue, &message)) {
        vellum_queue_destroy(queue);
        return false;
    }
    for (uint32_t i = 0u; i < 8u; ++i) {
        if (!vellum_queue_pop(queue, &received) || received.a != i) {
            vellum_queue_destroy(queue);
            return false;
        }
    }
    if (vellum_queue_pop(queue, &received)) {
        vellum_queue_destroy(queue);
        return false;
    }
    vellum_queue_destroy(queue);

    /* Real MPSC stress: multiple producers, small capacity, blocking push/pop. */
    queue = vellum_queue_create(16u);
    if (queue == NULL) {
        return false;
    }
    SDL_Thread *threads[PRODUCER_COUNT] = {0};
    ProducerContext contexts[PRODUCER_COUNT] = {0};
    for (uint32_t i = 0u; i < PRODUCER_COUNT; ++i) {
        contexts[i].queue = queue;
        contexts[i].producer_id = i + 1u;
        threads[i] = SDL_CreateThread(producer, "queue-producer", &contexts[i]);
        if (threads[i] == NULL) {
            vellum_queue_shutdown(queue);
            for (uint32_t j = 0u; j < i; ++j) {
                SDL_WaitThread(threads[j], NULL);
            }
            vellum_queue_destroy(queue);
            return false;
        }
    }

    uint32_t next[PRODUCER_COUNT] = {0u};
    const uint32_t total = PRODUCER_COUNT * MESSAGES_PER_PRODUCER;
    for (uint32_t i = 0u; i < total; ++i) {
        if (!vellum_queue_pop(queue, &received) || received.type == 0u ||
            received.type > PRODUCER_COUNT || received.a != next[received.type - 1u]) {
            vellum_queue_shutdown(queue);
            for (uint32_t j = 0u; j < PRODUCER_COUNT; ++j) {
                SDL_WaitThread(threads[j], NULL);
            }
            vellum_queue_destroy(queue);
            return false;
        }
        next[received.type - 1u]++;
    }

    for (uint32_t i = 0u; i < PRODUCER_COUNT; ++i) {
        int status = 0;
        SDL_WaitThread(threads[i], &status);
        if (status != 0 || next[i] != MESSAGES_PER_PRODUCER) {
            vellum_queue_destroy(queue);
            return false;
        }
    }

    vellum_queue_destroy(queue);
    return true;
}
