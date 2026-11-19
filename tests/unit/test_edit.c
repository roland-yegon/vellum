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
