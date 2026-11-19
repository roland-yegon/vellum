/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */
#ifndef VELLUM_TEXT_BUFFER_H
#define VELLUM_TEXT_BUFFER_H

#include <stdbool.h>
#include <stddef.h>

#include "vellum/core/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Byte-oriented piece table indexed by an AVL tree. Store 0 holds the original
 * text (copied today; read-only mmap when file loading lands), store 1 holds
 * append-only insertion data. A piece is (store, byte offset, byte length); each
 * tree node caches its subtree byte length, so offset lookup, insert, and delete
 * are O(log n). All offsets and lengths are bytes, never code points or graphemes. */
typedef struct VellumBuffer VellumBuffer;

/* `text` may be NULL only when `length` is 0. The original bytes are copied. */
VellumBuffer *vellum_buffer_create(const char *text, size_t length, VellumErrorInfo *error);
void vellum_buffer_destroy(VellumBuffer *buffer);

size_t vellum_buffer_length(const VellumBuffer *buffer);
size_t vellum_buffer_piece_count(const VellumBuffer *buffer);

/* Copies exactly `length` bytes starting at `offset` into `out`, which must be
 * large enough. `out` may be NULL only when `length` is 0. */
bool vellum_buffer_read(const VellumBuffer *buffer, size_t offset, char *out, size_t length,
                        VellumErrorInfo *error);

/* Inserts at a byte offset in [0, length]. `text` may be NULL only when `length`
 * is 0. Inserted bytes are appended to store 1 and never move once written. */
bool vellum_buffer_insert(VellumBuffer *buffer, size_t offset, const char *text, size_t length,
                          VellumErrorInfo *error);

/* Deletes the half-open byte range [offset, offset + length). */
bool vellum_buffer_delete(VellumBuffer *buffer, size_t offset, size_t length,
                          VellumErrorInfo *error);

/* Returns a malloc'd, NUL-terminated copy of the entire buffer; caller frees it.
 * Embedded NULs in the buffer are preserved before the terminator. */
char *vellum_buffer_snapshot(const VellumBuffer *buffer, VellumErrorInfo *error);

/* Diagnostic check of AVL balance, cached subtree lengths, and piece ranges.
 * Intended for tests and debugging; returns false for NULL or a broken tree. */
bool vellum_buffer_validate(const VellumBuffer *buffer);

#ifdef __cplusplus
}
#endif

#endif /* VELLUM_TEXT_BUFFER_H */
