/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */
#ifndef VELLUM_TEXT_CURSOR_SET_H
#define VELLUM_TEXT_CURSOR_SET_H

#include <stdbool.h>
#include <stddef.h>

#include "vellum/core/error.h"
#include "vellum/text/buffer.h"
#include "vellum/text/cursor.h"
#include "vellum/text/line_index.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A set of carets over a shared buffer and line index. The set always contains
 * at least one cursor, called the primary; single-caret editing is simply a set
 * of size one. Cursors are owned by the set and borrow the same buffer and line
 * index as `VellumCursor`, so those dependencies must outlive the set.
 *
 * Every cursor keeps the full `VellumCursor` semantics: grapheme-boundary
 * offsets, an anchor/head selection, and a desired column for vertical motion.
 * Movement and edit notifications are forwarded to every cursor in the set, so
 * existing caret-state logic is reused unchanged. Applying a single edit at
 * all carets is an edit-layer concern and is not done here.
 *
 * Pointers returned by `vellum_cursor_set_at` remain valid until that specific
 * cursor is removed or the set is destroyed; growing the set does not move the
 * `VellumCursor` objects themselves. */
typedef struct VellumCursorSet VellumCursorSet;

/* `buffer` and `lines` are borrowed and must already be current. The new set
 * has one collapsed primary cursor at byte offset 0. */
VellumCursorSet *vellum_cursor_set_create(VellumBuffer *buffer, VellumLineIndex *lines,
                                          VellumErrorInfo *error);
void vellum_cursor_set_destroy(VellumCursorSet *set);

/* Size and identity. The primary index is always in [0, count). */
size_t vellum_cursor_set_count(const VellumCursorSet *set);
size_t vellum_cursor_set_primary(const VellumCursorSet *set);
void vellum_cursor_set_set_primary(VellumCursorSet *set, size_t index);

/* Borrowed cursor access. Returns NULL for a NULL set or an out-of-range index. */
VellumCursor *vellum_cursor_set_at(VellumCursorSet *set, size_t index);
const VellumCursor *vellum_cursor_set_at_const(const VellumCursorSet *set, size_t index);
VellumCursor *vellum_cursor_set_primary_cursor(VellumCursorSet *set);

/* Adds a collapsed caret at a byte offset (snapped to a grapheme boundary), or
 * a caret with an explicit selection. If the new range overlaps any existing
 * cursor's selection, the call succeeds without adding a duplicate. */
bool vellum_cursor_set_add(VellumCursorSet *set, size_t offset, VellumErrorInfo *error);
bool vellum_cursor_set_add_selection(VellumCursorSet *set, size_t anchor, size_t head,
                                     VellumErrorInfo *error);

/* Removes the cursor at `index`, adjusting the primary index accordingly. The
 * last remaining cursor cannot be removed. `clear` keeps the primary cursor at
 * its current position, collapses it, and discards all other cursors. */
bool vellum_cursor_set_remove(VellumCursorSet *set, size_t index);
void vellum_cursor_set_clear(VellumCursorSet *set);

/* Convenience wrappers that act on the primary cursor only. */
bool vellum_cursor_set_set(VellumCursorSet *set, size_t offset, VellumErrorInfo *error);
bool vellum_cursor_set_select(VellumCursorSet *set, size_t anchor, size_t head,
                              VellumErrorInfo *error);

/* Applies an operation to every cursor. These stop at the first failure, so a
 * failing call may leave earlier cursors updated. */
bool vellum_cursor_set_move_all(VellumCursorSet *set, VellumCursorMotion motion, bool extend,
                                VellumErrorInfo *error);
void vellum_cursor_set_collapse_all(VellumCursorSet *set);
bool vellum_cursor_set_notify_insert(VellumCursorSet *set, size_t offset, size_t length,
                                     VellumErrorInfo *error);
bool vellum_cursor_set_notify_delete(VellumCursorSet *set, size_t offset, size_t length,
                                     VellumErrorInfo *error);

/* Checks the shared structures and every cursor. */
bool vellum_cursor_set_validate(VellumCursorSet *set);

#ifdef __cplusplus
}
#endif

#endif /* VELLUM_TEXT_CURSOR_SET_H */
