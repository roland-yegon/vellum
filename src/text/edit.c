/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#include "vellum/text/edit.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define VELLUM_EDITOR_INDENT_CAPACITY 16u
#define VELLUM_EDITOR_DEFAULT_INDENT "    "

struct VellumEditor {
    VellumBuffer *buffer;
    VellumLineIndex *lines;
    VellumCommandStack *commands;
    VellumCursor *cursor;
    char indent[VELLUM_EDITOR_INDENT_CAPACITY];
    size_t indent_length;
};

static bool line_of(const VellumEditor *editor, size_t offset, size_t *line, VellumErrorInfo *error)
{
    if (!vellum_line_index_line_of(editor->lines, offset, line)) {
        vellum_error_set(error, VELLUM_ERR_INTERNAL, "offset %zu is outside the line index",
                         offset);
        return false;
    }
    return true;
}

static bool line_start_of(const VellumEditor *editor, size_t line, size_t *start,
                          VellumErrorInfo *error)
{
    *start = vellum_line_index_line_start(editor->lines, line);
    if (*start == SIZE_MAX) {
        vellum_error_set(error, VELLUM_ERR_INTERNAL, "missing start for line %zu", line);
        return false;
    }
    return true;
}

static bool content_end_of(const VellumEditor *editor, size_t line, size_t *end,
                           VellumErrorInfo *error)
{
    *end = vellum_line_index_content_end(editor->lines, editor->buffer, line);
    if (*end == SIZE_MAX) {
        vellum_error_set(error, VELLUM_ERR_INTERNAL, "missing content end for line %zu", line);
        return false;
    }
    return true;
}

static size_t leading_whitespace(const VellumEditor *editor, size_t start, size_t end)
{
    size_t offset = start;
    while (offset < end) {
        char byte = '\0';
        if (!vellum_buffer_read(editor->buffer, offset, &byte, 1u, NULL)) {
            break;
        }
        if (byte != ' ' && byte != '\t') {
            break;
        }
        ++offset;
    }
    return offset;
}

/* Deletes a range and leaves a collapsed caret at its start. The command layer
 * has already updated the line index by the time the cursor is notified. */
static bool erase(VellumEditor *editor, size_t start, size_t length, VellumErrorInfo *error)
{
    if (length == 0u) {
        return true;
    }
    return vellum_command_delete(editor->commands, start, length, error) &&
           vellum_cursor_notify_delete(editor->cursor, start, length, error) &&
           vellum_cursor_set(editor->cursor, start, error);
}

static bool insert_at(VellumEditor *editor, size_t offset, const char *text, size_t length,
                      VellumErrorInfo *error)
{
    if (length == 0u) {
        return true;
    }
    return vellum_command_insert(editor->commands, offset, text, length, error) &&
           vellum_cursor_notify_insert(editor->cursor, offset, length, error) &&
           vellum_cursor_set(editor->cursor, offset + length, error);
}

static bool replace_selection(VellumEditor *editor, const char *text, size_t length,
                              VellumErrorInfo *error)
{
    const size_t start = vellum_cursor_selection_start(editor->cursor);
    const size_t end = vellum_cursor_selection_end(editor->cursor);
    const size_t selected = end - start;
    if (selected == 0u) {
        return insert_at(editor, start, text, length, error);
    }

    const bool grouped = length > 0u;
    if (grouped && !vellum_command_begin(editor->commands, error)) {
        return false;
    }
    bool ok = vellum_command_delete(editor->commands, start, selected, error);
    if (ok && grouped) {
        ok = vellum_command_insert(editor->commands, start, text, length, error);
    }
    if (grouped) {
        if (ok) {
            ok = vellum_command_commit(editor->commands, error);
        } else {
            (void)vellum_command_abort(editor->commands, error);
        }
    }
    if (!ok || !vellum_cursor_notify_delete(editor->cursor, start, selected, error)) {
        return false;
    }
    if (grouped && !vellum_cursor_notify_insert(editor->cursor, start, length, error)) {
        return false;
    }
    return vellum_cursor_set(editor->cursor, start + length, error);
}

/* Reads the byte span of the grapheme next to the caret by temporarily making
 * a one-grapheme selection, then restores the caller's exact selection. */
static bool adjacent_grapheme(VellumCursor *cursor, bool backward, size_t *start, size_t *end,
                              VellumErrorInfo *error)
{
    const size_t saved_anchor = vellum_cursor_anchor(cursor);
    const size_t saved_head = vellum_cursor_position(cursor);
    if (!vellum_cursor_set(cursor, saved_head, error)) {
        return false;
    }
    const VellumCursorMotion motion = backward ? VELLUM_CURSOR_LEFT : VELLUM_CURSOR_RIGHT;
    if (!vellum_cursor_move(cursor, motion, true, error)) {
        (void)vellum_cursor_select(cursor, saved_anchor, saved_head, error);
        return false;
    }
    *start = vellum_cursor_selection_start(cursor);
    *end = vellum_cursor_selection_end(cursor);
    return vellum_cursor_select(cursor, saved_anchor, saved_head, error);
}

VellumEditor *vellum_editor_create(VellumBuffer *buffer, VellumLineIndex *lines,
                                   VellumCommandStack *commands, VellumCursor *cursor,
                                   VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (buffer == NULL || lines == NULL || commands == NULL || cursor == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT,
                         "an editor needs a buffer, line index, command stack, and cursor");
        return NULL;
    }
    VellumEditor *editor = calloc(1u, sizeof(*editor));
    if (editor == NULL) {
        vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not allocate editor");
        return NULL;
    }
    editor->buffer = buffer;
    editor->lines = lines;
    editor->commands = commands;
    editor->cursor = cursor;
    memcpy(editor->indent, VELLUM_EDITOR_DEFAULT_INDENT, sizeof(VELLUM_EDITOR_DEFAULT_INDENT) - 1u);
    editor->indent_length = sizeof(VELLUM_EDITOR_DEFAULT_INDENT) - 1u;
    return editor;
}

void vellum_editor_destroy(VellumEditor *editor)
{
    free(editor);
}

bool vellum_editor_set_indent_unit(VellumEditor *editor, const char *unit, size_t length,
                                   VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (editor == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "set indent needs an editor");
        return false;
    }
    if (unit == NULL || length == 0u) {
        memcpy(editor->indent, VELLUM_EDITOR_DEFAULT_INDENT,
               sizeof(VELLUM_EDITOR_DEFAULT_INDENT) - 1u);
        editor->indent_length = sizeof(VELLUM_EDITOR_DEFAULT_INDENT) - 1u;
        return true;
    }
    if (length > VELLUM_EDITOR_INDENT_CAPACITY) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "indent unit exceeds %u bytes",
                         VELLUM_EDITOR_INDENT_CAPACITY);
        return false;
    }
    for (size_t i = 0u; i < length; ++i) {
        if (unit[i] == '\n' || unit[i] == '\r') {
            vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT,
                             "indent unit cannot contain a line break");
            return false;
        }
    }
    memcpy(editor->indent, unit, length);
    editor->indent_length = length;
    return true;
}

size_t vellum_editor_indent_length(const VellumEditor *editor)
{
    return editor == NULL ? 0u : editor->indent_length;
}

bool vellum_editor_insert_text(VellumEditor *editor, const char *text, size_t length,
                               VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (editor == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "insert needs an editor");
        return false;
    }
    return replace_selection(editor, text, length, error);
}

bool vellum_editor_backspace(VellumEditor *editor, VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (editor == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "backspace needs an editor");
        return false;
    }
    if (vellum_cursor_has_selection(editor->cursor)) {
        return replace_selection(editor, "", 0u, error);
    }

    const size_t position = vellum_cursor_position(editor->cursor);
    if (position == 0u) {
        return true;
    }
    size_t line = 0u;
    size_t line_start = 0u;
    size_t content_end = 0u;
    if (!line_of(editor, position, &line, error) ||
        !line_start_of(editor, line, &line_start, error) ||
        !content_end_of(editor, line, &content_end, error)) {
        return false;
    }
    const size_t indent_end = leading_whitespace(editor, line_start, content_end);
    /* Only treat this as an outdent when the caret actually sits inside an
     * existing run of leading whitespace. At a line start (including an empty
     * line) there is nothing to outdent, so fall through to grapheme deletion
     * and remove the preceding newline/terminator instead. */
    if (position > line_start && position <= indent_end) {
        size_t remove = editor->indent_length;
        if (remove > position - line_start) {
            remove = position - line_start;
        }
        return erase(editor, position - remove, remove, error);
    }

    size_t start = 0u;
    size_t end = 0u;
    if (!adjacent_grapheme(editor->cursor, true, &start, &end, error)) {
        return false;
    }
    return erase(editor, start, end - start, error);
}

bool vellum_editor_delete(VellumEditor *editor, VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (editor == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "delete needs an editor");
        return false;
    }
    if (vellum_cursor_has_selection(editor->cursor)) {
        return replace_selection(editor, "", 0u, error);
    }
    if (vellum_cursor_position(editor->cursor) >= vellum_buffer_length(editor->buffer)) {
        return true;
    }

    size_t start = 0u;
    size_t end = 0u;
    if (!adjacent_grapheme(editor->cursor, false, &start, &end, error)) {
        return false;
    }
    return erase(editor, start, end - start, error);
}

bool vellum_editor_newline(VellumEditor *editor, VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (editor == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "newline needs an editor");
        return false;
    }
    size_t line = 0u;
    size_t line_start = 0u;
    size_t content_end = 0u;
    if (!line_of(editor, vellum_cursor_selection_start(editor->cursor), &line, error) ||
        !line_start_of(editor, line, &line_start, error) ||
        !content_end_of(editor, line, &content_end, error)) {
        return false;
    }
    const size_t indent_end = leading_whitespace(editor, line_start, content_end);
    const size_t indent_length = indent_end - line_start;
    const size_t length = 1u + indent_length;
    char *text = malloc(length);
    if (text == NULL) {
        vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not allocate newline text");
        return false;
    }
    text[0u] = '\n';
    if (indent_length > 0u &&
        !vellum_buffer_read(editor->buffer, line_start, text + 1u, indent_length, error)) {
        free(text);
        return false;
    }
    const bool ok = replace_selection(editor, text, length, error);
    free(text);
    return ok;
}

static bool selected_lines(const VellumEditor *editor, size_t *first, size_t *last,
                           VellumErrorInfo *error)
{
    const size_t start = vellum_cursor_selection_start(editor->cursor);
    const size_t end = vellum_cursor_selection_end(editor->cursor);
    if (!line_of(editor, start, first, error) || !line_of(editor, end, last, error)) {
        return false;
    }
    if (end > start) {
        size_t last_start = 0u;
        if (!line_start_of(editor, *last, &last_start, error)) {
            return false;
        }
        if (end == last_start && *last > *first) {
            --*last;
        }
    }
    return true;
}

bool vellum_editor_indent(VellumEditor *editor, VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (editor == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "indent needs an editor");
        return false;
    }
    size_t first = 0u;
    size_t last = 0u;
    if (!selected_lines(editor, &first, &last, error) ||
        !vellum_command_begin(editor->commands, error)) {
        return false;
    }
    bool ok = true;
    for (size_t line = last + 1u; line > first && ok;) {
        --line;
        size_t start = 0u;
        if (!line_start_of(editor, line, &start, error)) {
            ok = false;
            break;
        }
        ok = vellum_command_insert(editor->commands, start, editor->indent, editor->indent_length,
                                   error) &&
             vellum_cursor_notify_insert(editor->cursor, start, editor->indent_length, error);
    }
    if (ok) {
        ok = vellum_command_commit(editor->commands, error);
    } else {
        (void)vellum_command_abort(editor->commands, error);
    }
    return ok;
}

bool vellum_editor_outdent(VellumEditor *editor, VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (editor == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "outdent needs an editor");
        return false;
    }
    size_t first = 0u;
    size_t last = 0u;
    if (!selected_lines(editor, &first, &last, error) ||
        !vellum_command_begin(editor->commands, error)) {
        return false;
    }
    bool ok = true;
    for (size_t line = last + 1u; line > first && ok;) {
        --line;
        size_t start = 0u;
        size_t content_end = 0u;
        if (!line_start_of(editor, line, &start, error) ||
            !content_end_of(editor, line, &content_end, error)) {
            ok = false;
            break;
        }
        const size_t whitespace = leading_whitespace(editor, start, content_end) - start;
        size_t remove = editor->indent_length;
        if (remove > whitespace) {
            remove = whitespace;
        }
        if (remove > 0u) {
            ok = vellum_command_delete(editor->commands, start, remove, error) &&
                 vellum_cursor_notify_delete(editor->cursor, start, remove, error);
        }
    }
    if (ok) {
        ok = vellum_command_commit(editor->commands, error);
    } else {
        (void)vellum_command_abort(editor->commands, error);
    }
    return ok;
}

static bool travel_history(VellumEditor *editor, bool undo, VellumErrorInfo *error)
{
    size_t head = vellum_cursor_position(editor->cursor);
    const bool ok = undo ? vellum_command_undo(editor->commands, error)
                         : vellum_command_redo(editor->commands, error);
    if (!ok) {
        return false;
    }
    const size_t total = vellum_buffer_length(editor->buffer);
    if (head > total) {
        head = total;
    }
    return vellum_cursor_set(editor->cursor, head, error);
}

bool vellum_editor_undo(VellumEditor *editor, VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (editor == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "undo needs an editor");
        return false;
    }
    return travel_history(editor, true, error);
}

bool vellum_editor_redo(VellumEditor *editor, VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (editor == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "redo needs an editor");
        return false;
    }
    return travel_history(editor, false, error);
}

bool vellum_editor_validate(VellumEditor *editor, VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (editor == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "validate needs an editor");
        return false;
    }
    return vellum_buffer_validate(editor->buffer) &&
           vellum_line_index_validate(editor->lines, editor->buffer) &&
           vellum_command_validate(editor->commands, error) &&
           vellum_cursor_validate(editor->cursor);
}
