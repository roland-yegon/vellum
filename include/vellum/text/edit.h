/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */
#ifndef VELLUM_TEXT_EDIT_H
#define VELLUM_TEXT_EDIT_H

#include <stdbool.h>
#include <stddef.h>

#include "vellum/core/error.h"
#include "vellum/text/buffer.h"
#include "vellum/text/command.h"
#include "vellum/text/cursor.h"
#include "vellum/text/line_index.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Coordinates editing operations over a buffer, its line index, an undoable
 * command stack, and a cursor. All four dependencies are borrowed and must
 * outlive the editor. Every operation goes through the command stack, so a
 * single call is undoable and callers may group several calls in a transaction.
 *
 * The line index must already be current, and after each edit the cursor is
 * notified so its anchor and head keep their positions relative to the text. */
typedef struct VellumEditor VellumEditor;

VellumEditor *vellum_editor_create(VellumBuffer *buffer, VellumLineIndex *lines,
                                   VellumCommandStack *commands, VellumCursor *cursor,
                                   VellumErrorInfo *error);
void vellum_editor_destroy(VellumEditor *editor);

/* Sets the string inserted by indent and removed by outdent, and copied after
 * newline. The string is copied into fixed storage (at most 16 bytes); NULL or
 * zero length restores the default of four spaces. */
bool vellum_editor_set_indent_unit(VellumEditor *editor, const char *unit, size_t length,
                                   VellumErrorInfo *error);
size_t vellum_editor_indent_length(const VellumEditor *editor);

/* Replaces the current selection, or inserts at the caret when it is collapsed. */
bool vellum_editor_insert_text(VellumEditor *editor, const char *text, size_t length,
                               VellumErrorInfo *error);

/* Deletes the selection. Otherwise backspace removes the previous grapheme,
 * or one indent unit while inside a line's leading whitespace. Delete removes
 * the next grapheme. Both are no-ops at the corresponding buffer boundary. */
bool vellum_editor_backspace(VellumEditor *editor, VellumErrorInfo *error);
bool vellum_editor_delete(VellumEditor *editor, VellumErrorInfo *error);

/* Inserts a newline followed by the current line's leading whitespace. */
bool vellum_editor_newline(VellumEditor *editor, VellumErrorInfo *error);

/* Indents or outdents every logical line touched by the selection (or the
 * caret's line). Outdent removes at most one indent unit of leading whitespace
 * per line. Each call is a single undo entry. */
bool vellum_editor_indent(VellumEditor *editor, VellumErrorInfo *error);
bool vellum_editor_outdent(VellumEditor *editor, VellumErrorInfo *error);

/* Undo/redo wrappers. The command stack restores the buffer and line index;
 * these collapse the selection and snap the caret to a grapheme boundary at its
 * previous byte offset, clamped to the new buffer length. Precise per-edit caret
 * restoration needs change records from the command stack and is deferred. */
bool vellum_editor_undo(VellumEditor *editor, VellumErrorInfo *error);
bool vellum_editor_redo(VellumEditor *editor, VellumErrorInfo *error);

bool vellum_editor_validate(VellumEditor *editor, VellumErrorInfo *error);

#ifdef __cplusplus
}
#endif

#endif /* VELLUM_TEXT_EDIT_H */
