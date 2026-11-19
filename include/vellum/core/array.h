/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#ifndef VELLUM_CORE_ARRAY_H
#define VELLUM_CORE_ARRAY_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned char *data;
    size_t length;
    size_t capacity;
    size_t element_size;
} VellumArray;

bool vellum_array_init(VellumArray *array, size_t element_size, size_t capacity);
void vellum_array_deinit(VellumArray *array);
bool vellum_array_reserve(VellumArray *array, size_t capacity);
void *vellum_array_push(VellumArray *array);
bool vellum_array_append(VellumArray *array, const void *element);
void *vellum_array_at(VellumArray *array, size_t index);
const void *vellum_array_at_const(const VellumArray *array, size_t index);
void vellum_array_remove_swap(VellumArray *array, size_t index);
void vellum_array_clear(VellumArray *array);
size_t vellum_array_size(const VellumArray *array);
size_t vellum_array_byte_size(const VellumArray *array);

#ifdef __cplusplus
}
#endif

#endif /* VELLUM_CORE_ARRAY_H */
