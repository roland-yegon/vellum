/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#include "vellum/core/queue.h"

#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct VellumQueue {
    VellumMessage *slots;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
    SDL_Mutex *mutex;
    SDL_Condition *not_empty;
    SDL_Condition *not_full;
    bool shutdown;
};

VellumQueue *vellum_queue_create(size_t capacity)
{
    if (capacity == 0u || capacity > SIZE_MAX / sizeof(VellumMessage)) {
        return NULL;
    }

    VellumQueue *queue = malloc(sizeof(*queue));
    if (queue == NULL) {
        return NULL;
    }

    queue->slots = malloc(capacity * sizeof(VellumMessage));
    queue->mutex = SDL_CreateMutex();
    queue->not_empty = SDL_CreateCondition();
    queue->not_full = SDL_CreateCondition();
    if (queue->slots == NULL || queue->mutex == NULL || queue->not_empty == NULL ||
        queue->not_full == NULL) {
        if (queue->not_full != NULL) {
            SDL_DestroyCondition(queue->not_full);
        }
        if (queue->not_empty != NULL) {
            SDL_DestroyCondition(queue->not_empty);
        }
        if (queue->mutex != NULL) {
            SDL_DestroyMutex(queue->mutex);
        }
        free(queue->slots);
        free(queue);
        return NULL;
    }

    queue->capacity = capacity;
    queue->head = 0u;
    queue->tail = 0u;
    queue->count = 0u;
    queue->shutdown = false;
    memset(queue->slots, 0, capacity * sizeof(VellumMessage));
    return queue;
}

bool vellum_queue_try_push(VellumQueue *queue, const VellumMessage *message)
{
    if (queue == NULL || message == NULL) {
        return false;
    }

    SDL_LockMutex(queue->mutex);
    if (queue->shutdown || queue->count == queue->capacity) {
        SDL_UnlockMutex(queue->mutex);
        return false;
    }

    queue->slots[queue->tail] = *message;
    queue->tail = (queue->tail + 1u) % queue->capacity;
    queue->count++;
    SDL_SignalCondition(queue->not_empty);
    SDL_UnlockMutex(queue->mutex);
    return true;
}

bool vellum_queue_push(VellumQueue *queue, const VellumMessage *message)
{
    if (queue == NULL || message == NULL) {
        return false;
    }

    SDL_LockMutex(queue->mutex);
    while (queue->count == queue->capacity && !queue->shutdown) {
        SDL_WaitCondition(queue->not_full, queue->mutex);
    }
    if (queue->shutdown) {
        SDL_UnlockMutex(queue->mutex);
        return false;
    }

    queue->slots[queue->tail] = *message;
    queue->tail = (queue->tail + 1u) % queue->capacity;
    queue->count++;
    SDL_SignalCondition(queue->not_empty);
    SDL_UnlockMutex(queue->mutex);
    return true;
}

bool vellum_queue_try_pop(VellumQueue *queue, VellumMessage *message)
{
    if (queue == NULL || message == NULL) {
        return false;
    }

    SDL_LockMutex(queue->mutex);
    if (queue->count == 0u) {
        SDL_UnlockMutex(queue->mutex);
        return false;
    }

    *message = queue->slots[queue->head];
    queue->head = (queue->head + 1u) % queue->capacity;
    queue->count--;
    SDL_SignalCondition(queue->not_full);
    SDL_UnlockMutex(queue->mutex);
    return true;
}

bool vellum_queue_pop(VellumQueue *queue, VellumMessage *message)
{
    if (queue == NULL || message == NULL) {
        return false;
    }

    SDL_LockMutex(queue->mutex);
    while (queue->count == 0u && !queue->shutdown) {
        SDL_WaitCondition(queue->not_empty, queue->mutex);
    }
    if (queue->count == 0u) {
        SDL_UnlockMutex(queue->mutex);
        return false;
    }

    *message = queue->slots[queue->head];
    queue->head = (queue->head + 1u) % queue->capacity;
    queue->count--;
    SDL_SignalCondition(queue->not_full);
    SDL_UnlockMutex(queue->mutex);
    return true;
}

void vellum_queue_shutdown(VellumQueue *queue)
{
    if (queue == NULL) {
        return;
    }

    SDL_LockMutex(queue->mutex);
    queue->shutdown = true;
    SDL_BroadcastCondition(queue->not_empty);
    SDL_BroadcastCondition(queue->not_full);
    SDL_UnlockMutex(queue->mutex);
}

size_t vellum_queue_size(const VellumQueue *queue)
{
    if (queue == NULL) {
        return 0u;
    }
    SDL_LockMutex(queue->mutex);
    const size_t count = queue->count;
    SDL_UnlockMutex(queue->mutex);
    return count;
}

bool vellum_queue_is_shutdown(const VellumQueue *queue)
{
    if (queue == NULL) {
        return true;
    }
    SDL_LockMutex(queue->mutex);
    const bool shutdown = queue->shutdown;
    SDL_UnlockMutex(queue->mutex);
    return shutdown;
}

void vellum_queue_destroy(VellumQueue *queue)
{
    if (queue == NULL) {
        return;
    }

    vellum_queue_shutdown(queue);
    SDL_DestroyCondition(queue->not_full);
    SDL_DestroyCondition(queue->not_empty);
    SDL_DestroyMutex(queue->mutex);
    free(queue->slots);
    free(queue);
}
