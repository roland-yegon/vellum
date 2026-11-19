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

/* A bracket pair of single ASCII bytes. `open == close` models quotes, where
 * the same byte both opens and closes. */
typedef struct {
    char open;
    char close;
} VellumBracketPair;

#define VELLUM_BRACKET_MAX_PAIRS 8u

/* Built-in language-agnostic pairs: (), [], {}, "", ''. `*count` receives the
 * number of pairs and may be NULL; the returned array is static. */
const VellumBracketPair *vellum_bracket_defaults(size_t *count);

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

/* Configures the pairs used by `vellum_editor_type_char`. The array is copied
 * into fixed storage (at most VELLUM_BRACKET_MAX_PAIRS); NULL or zero restores
 * the defaults. Returns false for too many pairs or a NUL byte in a pair. When
 * a byte appears as the open of more than one pair, the first pair wins. */
bool vellum_editor_set_bracket_pairs(VellumEditor *editor, const VellumBracketPair *pairs,
                                     size_t count, VellumErrorInfo *error);
size_t vellum_editor_bracket_pair_count(const VellumEditor *editor);

/* Types a single ASCII byte with bracket completion. With a collapsed caret:
 * typing a closing byte directly before that same byte skips over it (so an
 * auto-inserted closer is not duplicated); typing an opening byte inserts the
 * pair as one undo entry and leaves the caret between the two bytes. With a
 * selection, typing an opening byte wraps it in the pair as one undo entry and
 * keeps the inner text selected. Any other byte is an ordinary insertion.
 * UTF-8 text should use `vellum_editor_insert_text` instead. */
bool vellum_editor_type_char(VellumEditor *editor, char ch, VellumErrorInfo *error);

/* Replaces the current selection, or inserts at the caret when it is collapsed. */
bool vellum_editor_insert_text(VellumEditor *editor, const char *text, size_t length,
                               VellumErrorInfo *error);

/* Moves the byte range [start, end) so it is inserted before the original
 * offset `target` (all three offsets are in the pre-move buffer). Dropping
 * inside the range is a no-op that leaves the buffer and selection unchanged;
 * so is an empty range. On a real move the bytes are deleted and reinserted
 * as one command-stack transaction (one undo entry), and the moved text is
 * reselected forward. Offsets must satisfy start <= end <= length and
 * target <= length. */
bool vellum_editor_move_range(VellumEditor *editor, size_t start, size_t end, size_t target,
                              VellumErrorInfo *error);

/* Drag-and-drop convenience: moves the current selection to `target`, keeping
 * the moved text selected. Requires a non-empty selection. */
bool vellum_editor_move_selection(VellumEditor *editor, size_t target, VellumErrorInfo *error);

/* Clipboard support stays SDL-free here: the platform layer owns the system
 * clipboard and passes its text to `paste`/`insert_text`.

 * Copies the selected bytes into a NUL-terminated allocation (caller frees with
 * free()). `*out_length` reports the byte count, which may include embedded
 * NULs; the returned C string terminates at the first NUL. Requires a non-empty
 * selection and leaves the buffer and selection unchanged. */
bool vellum_editor_copy(const VellumEditor *editor, char **out_text, size_t *out_length,
                        VellumErrorInfo *error);

/* Copies the selection out, then deletes it as one undo entry and collapses the
 * caret at the selection start. On failure `*out_text` is NULL. */
bool vellum_editor_cut(VellumEditor *editor, char **out_text, size_t *out_length,
                       VellumErrorInfo *error);

/* Text-level paste: replaces the selection or inserts at the caret. Equivalent
 * to `insert_text`; provided so call sites can name the operation explicitly. */
bool vellum_editor_paste(VellumEditor *editor, const char *text, size_t length,
                         VellumErrorInfo *error);

/* Deletes the selection. Otherwise backspace first removes an empty bracket
 * pair when the caret sits between the two bytes, then falls back to removing
 * the previous grapheme, or one indent unit while inside a line's leading
 * whitespace. Delete removes the next grapheme. Both are no-ops at the
 * corresponding buffer boundary. */
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
