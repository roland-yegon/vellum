/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tests/unit/tests.h"
#include "vellum/core/error.h"
#include "vellum/text/buffer.h"
#include "vellum/text/command.h"
#include "vellum/text/cursor.h"
#include "vellum/text/edit.h"
#include "vellum/text/line_index.h"
#include "vellum/text/unicode.h"

static uint32_t next_random(uint32_t *state);

typedef struct {
    VellumBuffer *buffer;
    VellumLineIndex *lines;
    VellumCommandStack *commands;
    VellumCursor *cursor;
    VellumEditor *editor;
} Fixture;

static void fixture_destroy(Fixture *fixture)
{
    vellum_editor_destroy(fixture->editor);
    vellum_cursor_destroy(fixture->cursor);
    vellum_command_stack_destroy(fixture->commands);
    vellum_line_index_destroy(fixture->lines);
    vellum_buffer_destroy(fixture->buffer);
    memset(fixture, 0, sizeof(*fixture));
}

static bool fixture_init(Fixture *fixture, const char *text, size_t length)
{
    VellumErrorInfo error;
    memset(fixture, 0, sizeof(*fixture));
    fixture->buffer = vellum_buffer_create(text, length, &error);
    fixture->lines = vellum_line_index_create(&error);
    if (fixture->buffer == NULL || fixture->lines == NULL ||
        !vellum_line_index_rebuild(fixture->lines, fixture->buffer, &error)) {
        fixture_destroy(fixture);
        return false;
    }
    fixture->commands = vellum_command_stack_create(fixture->buffer, fixture->lines, 0u, &error);
    fixture->cursor = vellum_cursor_create(fixture->buffer, fixture->lines, &error);
    if (fixture->commands == NULL || fixture->cursor == NULL) {
        fixture_destroy(fixture);
        return false;
    }
    fixture->editor = vellum_editor_create(fixture->buffer, fixture->lines, fixture->commands,
                                           fixture->cursor, &error);
    if (fixture->editor == NULL) {
        fixture_destroy(fixture);
        return false;
    }
    return true;
}

static bool check_text(const Fixture *fixture, const char *expected, size_t length)
{
    VellumErrorInfo error;
    char *actual = vellum_buffer_snapshot(fixture->buffer, &error);
    if (actual == NULL) {
        return false;
    }
    const bool ok = strlen(actual) == length && memcmp(actual, expected, length) == 0;
    if (!ok) {
        fprintf(stderr, "edit text: expected '%s' (%zu), got '%s' (%zu)\n", expected, length,
                actual, strlen(actual));
    }
    free(actual);
    return ok;
}

static bool check_position(const Fixture *fixture, size_t head, const char *label)
{
    if (vellum_cursor_position(fixture->cursor) != head ||
        vellum_cursor_has_selection(fixture->cursor)) {
        fprintf(stderr, "edit %s: expected caret %zu collapsed, got %zu (selection %d)\n", label,
                head, vellum_cursor_position(fixture->cursor),
                vellum_cursor_has_selection(fixture->cursor) ? 1 : 0);
        return false;
    }
    return true;
}

static bool check_selection(const Fixture *fixture, size_t anchor, size_t head, const char *label)
{
    if (!vellum_cursor_has_selection(fixture->cursor) ||
        vellum_cursor_anchor(fixture->cursor) != anchor ||
        vellum_cursor_position(fixture->cursor) != head) {
        fprintf(stderr, "edit %s: expected selection [%zu,%zu), got [%zu,%zu) (has %d)\n", label,
                anchor, head, vellum_cursor_anchor(fixture->cursor),
                vellum_cursor_position(fixture->cursor),
                vellum_cursor_has_selection(fixture->cursor) ? 1 : 0);
        return false;
    }
    return true;
}

static bool insert_and_replace(void)
{
    Fixture fixture;
    if (!fixture_init(&fixture, "abc", 3u)) {
        return false;
    }
    VellumErrorInfo error;
    vellum_error_init(&error);
    bool ok = true;
#define EDIT_STEP(name, expr)                                                                      \
    do {                                                                                           \
        if (ok && !(expr)) {                                                                       \
            fprintf(stderr, "edit %s failed: %s\n", name, vellum_error_message(&error));           \
            ok = false;                                                                            \
        }                                                                                          \
    } while (0)

    EDIT_STEP("set one", vellum_cursor_set(fixture.cursor, 1u, &error));
    EDIT_STEP("insert", vellum_editor_insert_text(fixture.editor, "XY", 2u, &error));
    EDIT_STEP("inserted text", check_text(&fixture, "aXYbc", 5u));
    EDIT_STEP("inserted caret", check_position(&fixture, 3u, "inserted caret"));
    EDIT_STEP("undo", vellum_editor_undo(fixture.editor, &error));
    EDIT_STEP("undone text", check_text(&fixture, "abc", 3u));
    EDIT_STEP("redo", vellum_editor_redo(fixture.editor, &error));
    EDIT_STEP("redone text", check_text(&fixture, "aXYbc", 5u));

    EDIT_STEP("select XY", vellum_cursor_select(fixture.cursor, 1u, 3u, &error));
    EDIT_STEP("replace selection", vellum_editor_insert_text(fixture.editor, "Z", 1u, &error));
    EDIT_STEP("replaced text", check_text(&fixture, "aZbc", 4u));
    EDIT_STEP("replaced caret", check_position(&fixture, 2u, "replaced caret"));
    EDIT_STEP("undo replace", vellum_editor_undo(fixture.editor, &error));
    EDIT_STEP("undone replace", check_text(&fixture, "aXYbc", 5u));

    EDIT_STEP("select all", vellum_cursor_select(fixture.cursor, 0u, 5u, &error));
    EDIT_STEP("erase selection", vellum_editor_backspace(fixture.editor, &error));
    EDIT_STEP("erased text", check_text(&fixture, "", 0u));
    EDIT_STEP("undo erase", vellum_editor_undo(fixture.editor, &error));
    EDIT_STEP("restored text", check_text(&fixture, "aXYbc", 5u));

#undef EDIT_STEP
    fixture_destroy(&fixture);
    return ok;
}

static bool grapheme_deletion(void)
{
    Fixture fixture;
    if (!fixture_init(&fixture,
                      "e\xCC\x81"
                      "x",
                      4u)) {
        return false;
    }
    VellumErrorInfo error;
    vellum_error_init(&error);
    bool ok = true;
#define EDIT_STEP(name, expr)                                                                      \
    do {                                                                                           \
        if (ok && !(expr)) {                                                                       \
            fprintf(stderr, "edit grapheme %s failed: %s\n", name, vellum_error_message(&error));  \
            ok = false;                                                                            \
        }                                                                                          \
    } while (0)

    EDIT_STEP("set end", vellum_cursor_set(fixture.cursor, 4u, &error));
    EDIT_STEP("backspace x", vellum_editor_backspace(fixture.editor, &error));
    EDIT_STEP("only e-acute", check_text(&fixture, "e\xCC\x81", 3u));
    EDIT_STEP("caret 3", check_position(&fixture, 3u, "caret 3"));
    EDIT_STEP("backspace cluster", vellum_editor_backspace(fixture.editor, &error));
    EDIT_STEP("empty", check_text(&fixture, "", 0u));
    EDIT_STEP("caret 0", check_position(&fixture, 0u, "caret 0"));
    EDIT_STEP("backspace at zero", vellum_editor_backspace(fixture.editor, &error));
    EDIT_STEP("still empty", check_text(&fixture, "", 0u));

    fixture_destroy(&fixture);
    static const char family[] = "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D"
                                 "\xF0\x9F\x91\xA7"
                                 "x";
    if (!fixture_init(&fixture, family, 19u)) {
        return false;
    }
    EDIT_STEP("set emoji end", vellum_cursor_set(fixture.cursor, 19u, &error));
    EDIT_STEP("backspace x", vellum_editor_backspace(fixture.editor, &error));
    EDIT_STEP("family only", check_text(&fixture, family, 18u));
    EDIT_STEP("at 18", check_position(&fixture, 18u, "at 18"));
    EDIT_STEP("backspace family", vellum_editor_backspace(fixture.editor, &error));
    EDIT_STEP("emoji empty", check_text(&fixture, "", 0u));

#undef EDIT_STEP
    fixture_destroy(&fixture);
    return ok;
}

static bool smart_backspace(void)
{
    Fixture fixture;
    if (!fixture_init(&fixture, "    abc", 7u)) {
        return false;
    }
    VellumErrorInfo error;
    vellum_error_init(&error);
    bool ok = true;
#define EDIT_STEP(name, expr)                                                                      \
    do {                                                                                           \
        if (ok && !(expr)) {                                                                       \
            fprintf(stderr, "edit smart %s failed: %s\n", name, vellum_error_message(&error));     \
            ok = false;                                                                            \
        }                                                                                          \
    } while (0)

    EDIT_STEP("set four", vellum_cursor_set(fixture.cursor, 4u, &error));
    EDIT_STEP("outdent backspace", vellum_editor_backspace(fixture.editor, &error));
    EDIT_STEP("indent removed", check_text(&fixture, "abc", 3u));
    EDIT_STEP("caret zero", check_position(&fixture, 0u, "caret zero"));

    fixture_destroy(&fixture);
    if (!fixture_init(&fixture, "    abc", 7u)) {
        return false;
    }
    EDIT_STEP("set two", vellum_cursor_set(fixture.cursor, 2u, &error));
    EDIT_STEP("partial outdent", vellum_editor_backspace(fixture.editor, &error));
    EDIT_STEP("two spaces left", check_text(&fixture, "  abc", 5u));
    EDIT_STEP("caret still zero", check_position(&fixture, 0u, "caret still zero"));

    fixture_destroy(&fixture);
    if (!fixture_init(&fixture, "\tabc", 4u)) {
        return false;
    }
    EDIT_STEP("tab indent", vellum_editor_set_indent_unit(fixture.editor, "\t", 1u, &error));
    EDIT_STEP("set one", vellum_cursor_set(fixture.cursor, 1u, &error));
    EDIT_STEP("tab backspace", vellum_editor_backspace(fixture.editor, &error));
    EDIT_STEP("tab removed", check_text(&fixture, "abc", 3u));

    /* At a line start there is no indentation to outdent, so backspace must
     * delete the preceding line break instead of being a no-op. */
    fixture_destroy(&fixture);
    if (!fixture_init(&fixture, "a\nb", 3u)) {
        return false;
    }
    EDIT_STEP("set line start", vellum_cursor_set(fixture.cursor, 2u, &error));
    EDIT_STEP("backspace newline", vellum_editor_backspace(fixture.editor, &error));
    EDIT_STEP("joined lines", check_text(&fixture, "ab", 2u));
    EDIT_STEP("joined caret", check_position(&fixture, 1u, "joined caret"));

    fixture_destroy(&fixture);
    if (!fixture_init(&fixture, "a\n\nb", 4u)) {
        return false;
    }
    EDIT_STEP("set empty line", vellum_cursor_set(fixture.cursor, 2u, &error));
    EDIT_STEP("backspace empty", vellum_editor_backspace(fixture.editor, &error));
    EDIT_STEP("one break left", check_text(&fixture, "a\nb", 3u));
    EDIT_STEP("empty-line caret", check_position(&fixture, 1u, "empty-line caret"));

#undef EDIT_STEP
    fixture_destroy(&fixture);
    return ok;
}

static bool newline_auto_indent(void)
{
    Fixture fixture;
    if (!fixture_init(&fixture, "  abc", 5u)) {
        return false;
    }
    VellumErrorInfo error;
    vellum_error_init(&error);
    bool ok = true;
#define EDIT_STEP(name, expr)                                                                      \
    do {                                                                                           \
        if (ok && !(expr)) {                                                                       \
            fprintf(stderr, "edit newline %s failed: %s\n", name, vellum_error_message(&error));   \
            ok = false;                                                                            \
        }                                                                                          \
    } while (0)

    EDIT_STEP("set end", vellum_cursor_set(fixture.cursor, 5u, &error));
    EDIT_STEP("newline", vellum_editor_newline(fixture.editor, &error));
    EDIT_STEP("auto indented", check_text(&fixture, "  abc\n  ", 8u));
    EDIT_STEP("caret after indent", check_position(&fixture, 8u, "caret after indent"));
    EDIT_STEP("two lines", vellum_line_index_line_count(fixture.lines) == 2u);

    fixture_destroy(&fixture);
    if (!fixture_init(&fixture, "  abc", 5u)) {
        return false;
    }
    EDIT_STEP("set three", vellum_cursor_set(fixture.cursor, 3u, &error));
    EDIT_STEP("newline middle", vellum_editor_newline(fixture.editor, &error));
    EDIT_STEP("split with indent", check_text(&fixture, "  a\n  bc", 8u));
    EDIT_STEP("caret six", check_position(&fixture, 6u, "caret six"));

#undef EDIT_STEP
    fixture_destroy(&fixture);
    return ok;
}

static bool indent_outdent(void)
{
    Fixture fixture;
    if (!fixture_init(&fixture, "abc\ndef", 7u)) {
        return false;
    }
    VellumErrorInfo error;
    vellum_error_init(&error);
    bool ok = true;
#define EDIT_STEP(name, expr)                                                                      \
    do {                                                                                           \
        if (ok && !(expr)) {                                                                       \
            fprintf(stderr, "edit indent %s failed: %s\n", name, vellum_error_message(&error));    \
            ok = false;                                                                            \
        }                                                                                          \
    } while (0)

    /* A collapsed caret indents only its own line. */
    EDIT_STEP("indent line zero", vellum_editor_indent(fixture.editor, &error));
    EDIT_STEP("line zero indented", check_text(&fixture, "    abc\ndef", 11u));
    EDIT_STEP("caret shifted", check_position(&fixture, 4u, "caret shifted"));
    EDIT_STEP("undo indent", vellum_editor_undo(fixture.editor, &error));
    EDIT_STEP("unindented", check_text(&fixture, "abc\ndef", 7u));

    EDIT_STEP("set line one", vellum_cursor_set(fixture.cursor, 4u, &error));
    EDIT_STEP("indent line one", vellum_editor_indent(fixture.editor, &error));
    EDIT_STEP("line one indented", check_text(&fixture, "abc\n    def", 11u));
    EDIT_STEP("caret eight", check_position(&fixture, 8u, "caret eight"));
    EDIT_STEP("undo line one", vellum_editor_undo(fixture.editor, &error));
    EDIT_STEP("redo line one", vellum_editor_redo(fixture.editor, &error));
    EDIT_STEP("redone line one", check_text(&fixture, "abc\n    def", 11u));
    EDIT_STEP("undo again", vellum_editor_undo(fixture.editor, &error));

    /* A selection spanning both lines indents and outdents both as one entry. */
    EDIT_STEP("select both", vellum_cursor_select(fixture.cursor, 0u, 7u, &error));
    EDIT_STEP("indent both", vellum_editor_indent(fixture.editor, &error));
    EDIT_STEP("both indented", check_text(&fixture, "    abc\n    def", 15u));
    EDIT_STEP("outdent both", vellum_editor_outdent(fixture.editor, &error));
    EDIT_STEP("both outdented", check_text(&fixture, "abc\ndef", 7u));

    /* A selection ending exactly at a line start excludes that line. */
    fixture_destroy(&fixture);
    if (!fixture_init(&fixture, "abc\ndef", 7u)) {
        return false;
    }
    EDIT_STEP("select first line", vellum_cursor_select(fixture.cursor, 1u, 4u, &error));
    EDIT_STEP("indent selected", vellum_editor_indent(fixture.editor, &error));
    EDIT_STEP("only first indented", check_text(&fixture, "    abc\ndef", 11u));

    /* Mixed whitespace outdents by at most one unit. */
    fixture_destroy(&fixture);
    if (!fixture_init(&fixture, "  abc", 5u)) {
        return false;
    }
    EDIT_STEP("outdent short indent", vellum_editor_outdent(fixture.editor, &error));
    EDIT_STEP("short outdent", check_text(&fixture, "abc", 3u));

    fixture_destroy(&fixture);
    if (!fixture_init(&fixture, "abc", 3u)) {
        return false;
    }
    EDIT_STEP("outdent no indent", vellum_editor_outdent(fixture.editor, &error));
    EDIT_STEP("unchanged", check_text(&fixture, "abc", 3u));

#undef EDIT_STEP
    fixture_destroy(&fixture);
    return ok;
}

static bool check_copied(const char *text, size_t length, const char *expected,
                         size_t expected_length)
{
    if (length != expected_length || strlen(text) != expected_length ||
        memcmp(text, expected, expected_length) != 0) {
        fprintf(stderr, "edit copy: expected %zu bytes '%s', got %zu bytes '%s'\n", expected_length,
                expected, length, text);
        return false;
    }
    return true;
}

static bool clipboard_text_operations(void)
{
    Fixture fixture;
    if (!fixture_init(&fixture, "abcdef", 6u)) {
        return false;
    }
    VellumErrorInfo error;
    vellum_error_init(&error);
    bool ok = true;
    char *copied = NULL;
    size_t copied_length = 0u;

#define EDIT_STEP(name, expr)                                                                      \
    do {                                                                                           \
        if (ok && !(expr)) {                                                                       \
            fprintf(stderr, "edit clipboard %s failed: %s\n", name, vellum_error_message(&error)); \
            ok = false;                                                                            \
        }                                                                                          \
    } while (0)

    EDIT_STEP("select bcd", vellum_cursor_select(fixture.cursor, 1u, 4u, &error));
    EDIT_STEP("copy", vellum_editor_copy(fixture.editor, &copied, &copied_length, &error));
    EDIT_STEP("copied bytes", check_copied(copied, copied_length, "bcd", 3u));
    EDIT_STEP("copy unchanged", check_text(&fixture, "abcdef", 6u));
    EDIT_STEP("copy keeps selection", vellum_cursor_anchor(fixture.cursor) == 1u &&
                                          vellum_cursor_position(fixture.cursor) == 4u);
    free(copied);
    copied = NULL;

    /* Copy with a collapsed caret is rejected and clears the outputs. */
    vellum_cursor_collapse(fixture.cursor);
    copied = (char *)0x1;
    if (vellum_editor_copy(fixture.editor, &copied, &copied_length, &error) ||
        error.code != VELLUM_ERR_INVALID_ARGUMENT || copied != NULL || copied_length != 0u) {
        fprintf(stderr, "edit copy should reject a collapsed selection\n");
        ok = false;
    }

    /* Paste at the caret, then paste over a selection. */
    EDIT_STEP("set three", vellum_cursor_set(fixture.cursor, 3u, &error));
    EDIT_STEP("paste", vellum_editor_paste(fixture.editor, "XY", 2u, &error));
    EDIT_STEP("pasted text", check_text(&fixture, "abcXYdef", 8u));
    EDIT_STEP("pasted caret", check_position(&fixture, 5u, "pasted caret"));
    EDIT_STEP("select XY", vellum_cursor_select(fixture.cursor, 3u, 5u, &error));
    EDIT_STEP("paste over", vellum_editor_paste(fixture.editor, "Z", 1u, &error));
    EDIT_STEP("replaced text", check_text(&fixture, "abcZdef", 7u));
    EDIT_STEP("undo paste", vellum_editor_undo(fixture.editor, &error));
    EDIT_STEP("undo pasted", check_text(&fixture, "abcXYdef", 8u));

    /* Cut copies out and deletes in one undo entry. */
    EDIT_STEP("select XY again", vellum_cursor_select(fixture.cursor, 3u, 5u, &error));
    EDIT_STEP("cut", vellum_editor_cut(fixture.editor, &copied, &copied_length, &error));
    EDIT_STEP("cut bytes", check_copied(copied, copied_length, "XY", 2u));
    EDIT_STEP("cut text", check_text(&fixture, "abcdef", 6u));
    EDIT_STEP("cut caret", check_position(&fixture, 3u, "cut caret"));
    free(copied);
    copied = NULL;
    EDIT_STEP("undo cut", vellum_editor_undo(fixture.editor, &error));
    EDIT_STEP("undo cut text", check_text(&fixture, "abcXYdef", 8u));

    /* Embedded NULs are reported by out_length even though the C string stops. */
    fixture_destroy(&fixture);
    static const char with_nul[] = {'a', '\0', 'b', 'c'};
    if (!fixture_init(&fixture, with_nul, sizeof(with_nul))) {
        return false;
    }
    EDIT_STEP("select with NUL", vellum_cursor_select(fixture.cursor, 0u, 4u, &error));
    EDIT_STEP("copy NUL", vellum_editor_copy(fixture.editor, &copied, &copied_length, &error));
    if (ok && (copied_length != 4u || strlen(copied) != 1u || copied[1u] != '\0' ||
               memcmp(copied, with_nul, sizeof(with_nul)) != 0)) {
        fprintf(stderr, "edit copy lost embedded NUL bytes\n");
        ok = false;
    }
    free(copied);
    copied = NULL;

    /* NULL and collapsed-cut handling. */
    if (vellum_editor_copy(NULL, &copied, &copied_length, &error) ||
        vellum_editor_copy(fixture.editor, NULL, &copied_length, &error) ||
        vellum_editor_cut(NULL, &copied, &copied_length, &error) ||
        vellum_editor_paste(NULL, "x", 1u, &error) ||
        vellum_editor_paste(fixture.editor, NULL, 1u, &error) ||
        vellum_editor_insert_text(fixture.editor, NULL, 1u, &error)) {
        fprintf(stderr, "edit clipboard NULL handling failed\n");
        ok = false;
    }
    vellum_cursor_collapse(fixture.cursor);
    if (vellum_editor_cut(fixture.editor, &copied, &copied_length, &error) ||
        error.code != VELLUM_ERR_INVALID_ARGUMENT || copied != NULL) {
        fprintf(stderr, "edit cut should reject a collapsed selection\n");
        ok = false;
    }

#undef EDIT_STEP
    fixture_destroy(&fixture);
    return ok;
}

static bool bracket_configuration(void)
{
    Fixture fixture;
    if (!fixture_init(&fixture, "", 0u)) {
        return false;
    }
    VellumErrorInfo error;
    vellum_error_init(&error);
    bool ok = true;

    size_t count = 0u;
    const VellumBracketPair *defaults = vellum_bracket_defaults(&count);
    if (defaults == NULL || count != 5u || defaults[0u].open != '(' || defaults[0u].close != ')' ||
        defaults[4u].open != '\'' || defaults[4u].close != '\'' ||
        vellum_bracket_defaults(NULL) == NULL) {
        fprintf(stderr, "edit bracket defaults are wrong\n");
        ok = false;
    }
    if (vellum_editor_bracket_pair_count(fixture.editor) != 5u) {
        fprintf(stderr, "edit bracket default count is wrong\n");
        ok = false;
    }

    const VellumBracketPair custom[] = {{'<', '>'}, {'|', '|'}};
    if (!vellum_editor_set_bracket_pairs(fixture.editor, custom, 2u, &error) ||
        vellum_editor_bracket_pair_count(fixture.editor) != 2u) {
        fprintf(stderr, "edit bracket configure failed: %s\n", vellum_error_message(&error));
        ok = false;
    }
    if (ok && !vellum_editor_type_char(fixture.editor, '<', &error)) {
        fprintf(stderr, "edit bracket custom type failed: %s\n", vellum_error_message(&error));
        ok = false;
    }
    if (ok && !check_text(&fixture, "<>", 2u)) {
        ok = false;
    }

    if (!vellum_editor_set_bracket_pairs(fixture.editor, NULL, 0u, &error) ||
        vellum_editor_bracket_pair_count(fixture.editor) != 5u) {
        fprintf(stderr, "edit bracket restore defaults failed\n");
        ok = false;
    }

    VellumBracketPair too_many[VELLUM_BRACKET_MAX_PAIRS + 1u];
    for (size_t i = 0u; i <= VELLUM_BRACKET_MAX_PAIRS; ++i) {
        too_many[i] = (VellumBracketPair){'(', ')'};
    }
    const VellumBracketPair with_nul[] = {{'(', '\0'}};
    if (vellum_editor_set_bracket_pairs(fixture.editor, too_many, VELLUM_BRACKET_MAX_PAIRS + 1u,
                                        &error) ||
        error.code != VELLUM_ERR_INVALID_ARGUMENT ||
        vellum_editor_set_bracket_pairs(fixture.editor, with_nul, 1u, &error) ||
        error.code != VELLUM_ERR_INVALID_ARGUMENT) {
        fprintf(stderr, "edit bracket bad configuration was accepted\n");
        ok = false;
    }
    if (vellum_editor_set_bracket_pairs(NULL, custom, 2u, &error) ||
        vellum_editor_bracket_pair_count(NULL) != 0u ||
        vellum_editor_type_char(NULL, 'a', &error) ||
        vellum_editor_type_char(fixture.editor, '\0', &error)) {
        fprintf(stderr, "edit bracket NULL handling failed\n");
        ok = false;
    }

    fixture_destroy(&fixture);
    return ok;
}

static bool bracket_typing(void)
{
    Fixture fixture;
    if (!fixture_init(&fixture, "", 0u)) {
        return false;
    }
    VellumErrorInfo error;
    vellum_error_init(&error);
    bool ok = true;
#define EDIT_STEP(name, expr)                                                                      \
    do {                                                                                           \
        if (ok && !(expr)) {                                                                       \
            fprintf(stderr, "edit bracket %s failed: %s\n", name, vellum_error_message(&error));   \
            ok = false;                                                                            \
        }                                                                                          \
    } while (0)

    /* A collapsed caret auto-closes a pair as one undo entry. */
    EDIT_STEP("type open", vellum_editor_type_char(fixture.editor, '(', &error));
    EDIT_STEP("auto closed", check_text(&fixture, "()", 2u));
    EDIT_STEP("caret inside", check_position(&fixture, 1u, "caret inside"));
    EDIT_STEP("undo pair", vellum_editor_undo(fixture.editor, &error));
    EDIT_STEP("undone pair", check_text(&fixture, "", 0u));
    EDIT_STEP("redo pair", vellum_editor_redo(fixture.editor, &error));
    EDIT_STEP("redone pair", check_text(&fixture, "()", 2u));

    /* Typing a closer before an existing closer steps past it. */
    fixture_destroy(&fixture);
    if (!fixture_init(&fixture, "()", 2u)) {
        return false;
    }
    EDIT_STEP("caret between", vellum_cursor_set(fixture.cursor, 1u, &error));
    EDIT_STEP("type over", vellum_editor_type_char(fixture.editor, ')', &error));
    EDIT_STEP("unchanged", check_text(&fixture, "()", 2u));
    EDIT_STEP("stepped past", check_position(&fixture, 2u, "stepped past"));

    /* A closer with no matching byte after it inserts normally. */
    fixture_destroy(&fixture);
    if (!fixture_init(&fixture, "abc", 3u)) {
        return false;
    }
    EDIT_STEP("caret zero", vellum_cursor_set(fixture.cursor, 0u, &error));
    EDIT_STEP("stray close", vellum_editor_type_char(fixture.editor, ')', &error));
    EDIT_STEP("stray inserted", check_text(&fixture, ")abc", 4u));
    EDIT_STEP("stray caret", check_position(&fixture, 1u, "stray caret"));

    /* Nesting a pair inside a pair inserts at the caret and keeps both. */
    fixture_destroy(&fixture);
    if (!fixture_init(&fixture, "()", 2u)) {
        return false;
    }
    EDIT_STEP("inside outer", vellum_cursor_set(fixture.cursor, 1u, &error));
    EDIT_STEP("type inner", vellum_editor_type_char(fixture.editor, '[', &error));
    EDIT_STEP("nested", check_text(&fixture, "([])", 4u));
    EDIT_STEP("nested caret", check_position(&fixture, 2u, "nested caret"));

    /* Symmetric quotes: the first types a pair, the second steps over it. */
    fixture_destroy(&fixture);
    if (!fixture_init(&fixture, "", 0u)) {
        return false;
    }
    EDIT_STEP("type quote", vellum_editor_type_char(fixture.editor, '"', &error));
    EDIT_STEP("quote pair", check_text(&fixture, "\"\"", 2u));
    EDIT_STEP("quote caret", check_position(&fixture, 1u, "quote caret"));
    EDIT_STEP("type quote again", vellum_editor_type_char(fixture.editor, '"', &error));
    EDIT_STEP("still quote pair", check_text(&fixture, "\"\"", 2u));
    EDIT_STEP("past quotes", check_position(&fixture, 2u, "past quotes"));

    /* A non-bracket byte is an ordinary insertion. */
    EDIT_STEP("type normal", vellum_editor_type_char(fixture.editor, 'x', &error));
    EDIT_STEP("normal inserted", check_text(&fixture, "\"\"x", 3u));

#undef EDIT_STEP
    fixture_destroy(&fixture);
    return ok;
}

static bool bracket_wrap_and_backspace(void)
{
    Fixture fixture;
    if (!fixture_init(&fixture, "abcde", 5u)) {
        return false;
    }
    VellumErrorInfo error;
    vellum_error_init(&error);
    bool ok = true;
#define EDIT_STEP(name, expr)                                                                      \
    do {                                                                                           \
        if (ok && !(expr)) {                                                                       \
            fprintf(stderr, "edit bracket wrap %s failed: %s\n", name,                             \
                    vellum_error_message(&error));                                                 \
            ok = false;                                                                            \
        }                                                                                          \
    } while (0)

    /* Wrapping a selection keeps the inner text selected as one undo entry. */
    EDIT_STEP("select bcd", vellum_cursor_select(fixture.cursor, 1u, 4u, &error));
    EDIT_STEP("wrap", vellum_editor_type_char(fixture.editor, '(', &error));
    EDIT_STEP("wrapped text", check_text(&fixture, "a(bcd)e", 7u));
    EDIT_STEP("inner selected", check_selection(&fixture, 2u, 5u, "inner selected"));
    EDIT_STEP("wrap undo", vellum_editor_undo(fixture.editor, &error));
    EDIT_STEP("unwrapped", check_text(&fixture, "abcde", 5u));

    /* Backspace on an empty pair removes both bytes as one entry. */
    fixture_destroy(&fixture);
    if (!fixture_init(&fixture, "()", 2u)) {
        return false;
    }
    EDIT_STEP("inside empty", vellum_cursor_set(fixture.cursor, 1u, &error));
    EDIT_STEP("backspace pair", vellum_editor_backspace(fixture.editor, &error));
    EDIT_STEP("pair gone", check_text(&fixture, "", 0u));
    EDIT_STEP("caret zero", check_position(&fixture, 0u, "caret zero"));
    EDIT_STEP("undo backspace", vellum_editor_undo(fixture.editor, &error));
    EDIT_STEP("pair restored", check_text(&fixture, "()", 2u));

    /* Backspace on a non-empty pair deletes the preceding byte first. */
    fixture_destroy(&fixture);
    if (!fixture_init(&fixture, "(x)", 3u)) {
        return false;
    }
    EDIT_STEP("before close", vellum_cursor_set(fixture.cursor, 2u, &error));
    EDIT_STEP("backspace content", vellum_editor_backspace(fixture.editor, &error));
    EDIT_STEP("content gone", check_text(&fixture, "()", 2u));
    EDIT_STEP("backspace empty", vellum_editor_backspace(fixture.editor, &error));
    EDIT_STEP("all gone", check_text(&fixture, "", 0u));

    /* An empty pair inside leading whitespace removes only the pair. */
    fixture_destroy(&fixture);
    if (!fixture_init(&fixture, "  ()", 4u)) {
        return false;
    }
    EDIT_STEP("in indented pair", vellum_cursor_set(fixture.cursor, 3u, &error));
    EDIT_STEP("backspace indented pair", vellum_editor_backspace(fixture.editor, &error));
    EDIT_STEP("indent kept", check_text(&fixture, "  ", 2u));

#undef EDIT_STEP
    fixture_destroy(&fixture);
    return ok;
}

static bool ascii_is_open(char ch)
{
    return ch == '(' || ch == '[' || ch == '{' || ch == '"' || ch == '\'';
}

static bool ascii_is_close(char ch)
{
    return ch == ')' || ch == ']' || ch == '}' || ch == '"' || ch == '\'';
}

static char ascii_close_of(char ch)
{
    switch (ch) {
    case '(':
        return ')';
    case '[':
        return ']';
    case '{':
        return '}';
    case '"':
        return '"';
    case '\'':
        return '\'';
    default:
        return '\0';
    }
}

static void model_delete(char *text, size_t *length, size_t start, size_t count)
{
    memmove(text + start, text + start + count, *length - start - count);
    *length -= count;
    text[*length] = '\0';
}

static void model_insert(char *text, size_t *length, size_t at, const char *bytes, size_t count)
{
    memmove(text + at + count, text + at, *length - at);
    memcpy(text + at, bytes, count);
    *length += count;
    text[*length] = '\0';
}

static bool bracket_differential_fuzz(void)
{
    static const char alphabet[] = {'(', ')', '[', ']', '{', '}', '"', '\'', 'x'};
    const size_t alphabet_size = sizeof(alphabet);
    Fixture fixture;
    if (!fixture_init(&fixture, "", 0u)) {
        return false;
    }
    char model[256];
    model[0] = '\0';
    size_t length = 0u;
    size_t anchor = 0u;
    size_t head = 0u;
    uint32_t state = 0xB0A6B17Bu;
    VellumErrorInfo error;
    vellum_error_init(&error);
    bool ok = true;

    for (uint32_t op = 0u; op < 5000u && ok; ++op) {
        vellum_error_init(&error);
        const bool type = (next_random(&state) & 1u) != 0u || anchor != head || length == 0u;
        if (type && length < 100u) {
            const char ch = alphabet[next_random(&state) % alphabet_size];
            if (anchor == head) {
                if (ascii_is_close(ch) && head < length && model[head] == ch) {
                    ++head;
                    anchor = head;
                } else if (ascii_is_open(ch)) {
                    const char pair[2u] = {ch, ascii_close_of(ch)};
                    model_insert(model, &length, head, pair, 2u);
                    ++head;
                    anchor = head;
                } else {
                    model_insert(model, &length, head, &ch, 1u);
                    ++head;
                    anchor = head;
                }
                if (!vellum_editor_type_char(fixture.editor, ch, &error)) {
                    fprintf(stderr, "edit bracket fuzz type failed at op %u: %s\n", op,
                            vellum_error_message(&error));
                    ok = false;
                    break;
                }
            } else {
                const size_t start = anchor < head ? anchor : head;
                const size_t end = anchor < head ? head : anchor;
                const size_t selected = end - start;
                if (ascii_is_open(ch)) {
                    char wrapped[258u];
                    wrapped[0u] = ch;
                    memcpy(wrapped + 1u, model + start, selected);
                    wrapped[selected + 1u] = ascii_close_of(ch);
                    model_delete(model, &length, start, selected);
                    model_insert(model, &length, start, wrapped, selected + 2u);
                    anchor = start + 1u;
                    head = anchor + selected;
                } else {
                    model_delete(model, &length, start, selected);
                    model_insert(model, &length, start, &ch, 1u);
                    anchor = head = start + 1u;
                }
                if (!vellum_editor_type_char(fixture.editor, ch, &error)) {
                    fprintf(stderr, "edit bracket fuzz wrap failed at op %u: %s\n", op,
                            vellum_error_message(&error));
                    ok = false;
                    break;
                }
            }
        } else if (head > 0u) {
            if (anchor != head) {
                const size_t start = anchor < head ? anchor : head;
                const size_t end = anchor < head ? head : anchor;
                model_delete(model, &length, start, end - start);
                anchor = head = start;
            } else if (head < length && ascii_is_open(model[head - 1u]) &&
                       ascii_close_of(model[head - 1u]) == model[head]) {
                model_delete(model, &length, head - 1u, 2u);
                --head;
                anchor = head;
            } else {
                model_delete(model, &length, head - 1u, 1u);
                --head;
                anchor = head;
            }
            if (!vellum_editor_backspace(fixture.editor, &error)) {
                fprintf(stderr, "edit bracket fuzz backspace failed at op %u: %s\n", op,
                        vellum_error_message(&error));
                ok = false;
                break;
            }
        } else {
            continue;
        }

        if (!check_text(&fixture, model, length) ||
            (anchor == head ? !check_position(&fixture, head, "bracket fuzz caret")
                            : !check_selection(&fixture, anchor, head, "bracket fuzz selection"))) {
            fprintf(stderr, "edit bracket fuzz state failed at op %u\n", op);
            ok = false;
            break;
        }
    }

    if (ok && !vellum_editor_validate(fixture.editor, &error)) {
        fprintf(stderr, "edit bracket fuzz final validate failed: %s\n",
                vellum_error_message(&error));
        ok = false;
    }
    fixture_destroy(&fixture);
    return ok;
}

static bool configuration_and_validation(void)
{
    Fixture fixture;
    if (!fixture_init(&fixture, "abc", 3u)) {
        return false;
    }
    VellumErrorInfo error;
    vellum_error_init(&error);
    bool ok = true;

    if (vellum_editor_indent_length(fixture.editor) != 4u ||
        !vellum_editor_set_indent_unit(fixture.editor, "\t\t", 2u, &error) ||
        vellum_editor_indent_length(fixture.editor) != 2u ||
        !vellum_editor_set_indent_unit(fixture.editor, NULL, 0u, &error) ||
        vellum_editor_indent_length(fixture.editor) != 4u) {
        fprintf(stderr, "edit indent unit configuration failed\n");
        ok = false;
    }
    if (vellum_editor_set_indent_unit(fixture.editor, "12345678901234567", 17u, &error) ||
        error.code != VELLUM_ERR_INVALID_ARGUMENT ||
        vellum_editor_set_indent_unit(fixture.editor, "a\nb", 3u, &error) ||
        error.code != VELLUM_ERR_INVALID_ARGUMENT) {
        fprintf(stderr, "edit indent unit should reject bad input\n");
        ok = false;
    }
    if (!vellum_editor_validate(fixture.editor, &error)) {
        fprintf(stderr, "edit validate failed: %s\n", vellum_error_message(&error));
        ok = false;
    }
    if (vellum_editor_validate(NULL, &error) || vellum_editor_indent_length(NULL) != 0u ||
        vellum_editor_insert_text(NULL, "x", 1u, &error) || vellum_editor_backspace(NULL, &error) ||
        vellum_editor_delete(NULL, &error) || vellum_editor_newline(NULL, &error) ||
        vellum_editor_indent(NULL, &error) || vellum_editor_outdent(NULL, &error) ||
        vellum_editor_undo(NULL, &error) || vellum_editor_redo(NULL, &error)) {
        fprintf(stderr, "edit NULL handling failed\n");
        ok = false;
    }
    vellum_editor_destroy(NULL);

    fixture_destroy(&fixture);
    return ok;
}

#define FUZZ_OPERATIONS 10000u
#define MAX_TEXT 2048u

static uint32_t next_random(uint32_t *state)
{
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static size_t model_boundaries(const char *text, size_t length, size_t *boundaries)
{
    VellumErrorInfo error;
    VellumGraphemeIterator *iterator = vellum_grapheme_iterator_create(text, length, &error);
    if (iterator == NULL) {
        return 0u;
    }
    size_t count = 0u;
    size_t boundary = 0u;
    while (vellum_grapheme_iterator_next(iterator, &boundary)) {
        boundaries[count++] = boundary;
    }
    vellum_grapheme_iterator_destroy(iterator);
    return count;
}

static bool model_erase(char *model, size_t *length, size_t *head, size_t start, size_t count)
{
    memmove(model + start, model + start + count, *length - start - count);
    *length -= count;
    model[*length] = '\0';
    *head = start;
    return true;
}

static bool differential_fuzz(void)
{
    static const char *snippets[] = {"a",
                                     "b",
                                     "\n",
                                     "\r\n",
                                     "xy",
                                     "\xE4\xB8\xAD\xE6\x96\x87",
                                     "e\xCC\x81",
                                     ("\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D"
                                      "\xF0\x9F\x91\xA7")};
    const size_t snippet_count = sizeof(snippets) / sizeof(snippets[0]);
    Fixture fixture;
    if (!fixture_init(&fixture, "", 0u)) {
        return false;
    }
    char model[MAX_TEXT + 2u];
    size_t length = 0u;
    size_t head = 0u;
    uint32_t state = 0x5EEDFACEu;
    VellumErrorInfo error;
    vellum_error_init(&error);
    bool ok = true;

    for (uint32_t op = 0u; op < FUZZ_OPERATIONS && ok; ++op) {
        const uint32_t choice = next_random(&state) % 100u;
        vellum_error_init(&error);

        if (choice < 50u) {
            const char *snippet = snippets[next_random(&state) % snippet_count];
            const size_t insert_length = strlen(snippet);
            if (length + insert_length > MAX_TEXT / 2u) {
                continue;
            }
            if (!vellum_editor_insert_text(fixture.editor, snippet, insert_length, &error)) {
                fprintf(stderr, "edit fuzz insert failed at op %u: %s\n", op,
                        vellum_error_message(&error));
                ok = false;
                break;
            }
            memmove(model + head + insert_length, model + head, length - head);
            memcpy(model + head, snippet, insert_length);
            length += insert_length;
            model[length] = '\0';
            head += insert_length;
        } else {
            size_t boundaries[MAX_TEXT + 2u];
            const size_t count = model_boundaries(model, length, boundaries);
            const bool backward = next_random(&state) % 2u != 0u;
            size_t start = head;
            size_t end = head;
            if (backward) {
                for (size_t i = 0u; i < count && boundaries[i] < head; ++i) {
                    start = boundaries[i];
                }
            } else {
                for (size_t i = 0u; i < count; ++i) {
                    if (boundaries[i] > head) {
                        end = boundaries[i];
                        break;
                    }
                }
            }
            const bool change = end > start;
            if (backward ? !vellum_editor_backspace(fixture.editor, &error)
                         : !vellum_editor_delete(fixture.editor, &error)) {
                fprintf(stderr, "edit fuzz delete failed at op %u: %s\n", op,
                        vellum_error_message(&error));
                ok = false;
                break;
            }
            if (change) {
                model_erase(model, &length, &head, start, end - start);
            }
            if (head > length) {
                head = length;
            }
        }

        /* Byte-for-byte and caret checks run every operation; the expensive
         * full structural validation runs once after the whole run. */
        if (!check_text(&fixture, model, length) || !check_position(&fixture, head, "fuzz caret")) {
            fprintf(stderr, "edit fuzz state failed at op %u (head=%zu len=%zu choice=%u)\n", op,
                    head, length, choice);
            ok = false;
            break;
        }
    }

    if (ok && !vellum_editor_validate(fixture.editor, &error)) {
        fprintf(stderr, "edit fuzz final validate failed: %s\n", vellum_error_message(&error));
        ok = false;
    }

    fixture_destroy(&fixture);
    return ok;
}

bool test_edit(void)
{
    if (!insert_and_replace()) {
        fprintf(stderr, "edit insert_and_replace failed\n");
        return false;
    }
    if (!grapheme_deletion()) {
        fprintf(stderr, "edit grapheme_deletion failed\n");
        return false;
    }
    if (!smart_backspace()) {
        fprintf(stderr, "edit smart_backspace failed\n");
        return false;
    }
    if (!newline_auto_indent()) {
        fprintf(stderr, "edit newline_auto_indent failed\n");
        return false;
    }
    if (!indent_outdent()) {
        fprintf(stderr, "edit indent_outdent failed\n");
        return false;
    }
    if (!clipboard_text_operations()) {
        fprintf(stderr, "edit clipboard_text_operations failed\n");
        return false;
    }
    if (!bracket_configuration()) {
        fprintf(stderr, "edit bracket_configuration failed\n");
        return false;
    }
    if (!bracket_typing()) {
        fprintf(stderr, "edit bracket_typing failed\n");
        return false;
    }
    if (!bracket_wrap_and_backspace()) {
        fprintf(stderr, "edit bracket_wrap_and_backspace failed\n");
        return false;
    }
    if (!bracket_differential_fuzz()) {
        fprintf(stderr, "edit bracket_differential_fuzz failed\n");
        return false;
    }
    if (!configuration_and_validation()) {
        fprintf(stderr, "edit configuration_and_validation failed\n");
        return false;
    }
    if (!differential_fuzz()) {
        fprintf(stderr, "edit differential_fuzz failed\n");
        return false;
    }
    return true;
}
