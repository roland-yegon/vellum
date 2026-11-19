/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#include "vellum/core/array.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

bool vellum_array_init(VellumArray *array, size_t element_size, size_t capacity)
{
    if (array == NULL || element_size == 0u) {
        return false;
    }

    array->data = NULL;
    array->length = 0u;
    array->capacity = 0u;
    array->element_size = element_size;
    return capacity == 0u || vellum_array_reserve(array, capacity);
}

void vellum_array_deinit(VellumArray *array)
{
    if (array == NULL) {
        return;
    }
    free(array->data);
    array->data = NULL;
    array->length = 0u;
    array->capacity = 0u;
    array->element_size = 0u;
}

bool vellum_array_reserve(VellumArray *array, size_t capacity)
{
    if (array == NULL || array->element_size == 0u || capacity <= array->capacity) {
        return false;
    }
    if (capacity > SIZE_MAX / array->element_size) {
        return false;
    }

    const size_t bytes = capacity * array->element_size;
    unsigned char *data = realloc(array->data, bytes);
    if (data == NULL) {
        return false;
    }

    array->data = data;
    array->capacity = capacity;
    return true;
}

void *vellum_array_push(VellumArray *array)
{
    if (array == NULL || array->element_size == 0u || array->length == SIZE_MAX) {
        return NULL;
    }
    if (array->length == array->capacity) {
        size_t capacity = array->capacity == 0u ? 8u : array->capacity;
        if (capacity > SIZE_MAX / 2u) {
            return NULL;
        }
        capacity *= 2u;
        if (!vellum_array_reserve(array, capacity)) {
            return NULL;
        }
    }

    void *element = array->data + array->length * array->element_size;
    memset(element, 0, array->element_size);
    array->length++;
    return element;
}

bool vellum_array_append(VellumArray *array, const void *element)
{
    if (element == NULL) {
        return false;
    }
    void *destination = vellum_array_push(array);
    if (destination == NULL) {
        return false;
    }
    memcpy(destination, element, array->element_size);
    return true;
}

void *vellum_array_at(VellumArray *array, size_t index)
{
    if (array == NULL || index >= array->length) {
        return NULL;
    }
    return array->data + index * array->element_size;
}

const void *vellum_array_at_const(const VellumArray *array, size_t index)
{
    if (array == NULL || index >= array->length) {
        return NULL;
    }
    return array->data + index * array->element_size;
}

void vellum_array_remove_swap(VellumArray *array, size_t index)
{
    if (array == NULL || index >= array->length) {
        return;
    }

    const size_t last = array->length - 1u;
    if (index != last) {
        memcpy(array->data + index * array->element_size, array->data + last * array->element_size,
               array->element_size);
    }
    array->length = last;
}

void vellum_array_clear(VellumArray *array)
{
    if (array != NULL) {
        array->length = 0u;
    }
}

size_t vellum_array_size(const VellumArray *array)
{
    return array == NULL ? 0u : array->length;
}

size_t vellum_array_byte_size(const VellumArray *array)
{
    return array == NULL ? 0u : array->length * array->element_size;
}
