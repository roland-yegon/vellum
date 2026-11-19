/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tests/unit/tests.h"
#include "vellum/text/buffer.h"
#include "vellum/text/command.h"
#include "vellum/text/line_index.h"

#define FUZZ_OPERATIONS 10000u
#define MAX_TEXT 4096u
#define MAX_HISTORY 512u

typedef struct {
    VellumBuffer *buffer;
    VellumLineIndex *lines;
    VellumCommandStack *stack;
} Fixture;

static char undo_states[MAX_HISTORY][MAX_TEXT];
static size_t undo_lengths[MAX_HISTORY];
static char redo_states[MAX_HISTORY][MAX_TEXT];
static size_t redo_lengths[MAX_HISTORY];

static bool fixture_init(Fixture *fixture, const char *text, size_t length, size_t memory_limit)
{
    VellumErrorInfo error;
    fixture->buffer = vellum_buffer_create(text, length, &error);
    fixture->lines = vellum_line_index_create(&error);
    if (fixture->buffer == NULL || fixture->lines == NULL ||
        !vellum_line_index_rebuild(fixture->lines, fixture->buffer, &error)) {
        return false;
    }
    fixture->stack =
        vellum_command_stack_create(fixture->buffer, fixture->lines, memory_limit, &error);
    return fixture->stack != NULL;
}

static void fixture_destroy(Fixture *fixture)
{
    vellum_command_stack_destroy(fixture->stack);
    vellum_line_index_destroy(fixture->lines);
    vellum_buffer_destroy(fixture->buffer);
    memset(fixture, 0, sizeof(*fixture));
}

static bool check_text(const Fixture *fixture, const char *expected, size_t length)
{
    VellumErrorInfo error;
    char *actual = vellum_buffer_snapshot(fixture->buffer, &error);
    if (actual == NULL) {
        return false;
    }
    const bool text_ok = strlen(actual) == length && memcmp(actual, expected, length) == 0;
    if (!text_ok) {
        fprintf(stderr, "expected '%s' (%zu), got '%s' (%zu)\n", expected, length, actual,
                strlen(actual));
    }
    free(actual);
    if (!vellum_command_validate(fixture->stack, &error)) {
        fprintf(stderr, "command validation failed: %s\n", vellum_error_message(&error));
        return false;
    }
    return text_ok;
}

static bool basic_edits(void)
{
    Fixture fixture;
    if (!fixture_init(&fixture, "hello\nworld", 11u, 0u)) {
        return false;
    }
    VellumErrorInfo error;
    vellum_error_init(&error);
    bool ok = true;
#define STEP(name, expr)                                                                           \
    do {                                                                                           \
        if (ok && !(expr)) {                                                                       \
            fprintf(stderr, "basic %s failed: %s\n", name, vellum_error_message(&error));          \
            ok = false;                                                                            \
        }                                                                                          \
    } while (0)

    STEP("initial", check_text(&fixture, "hello\nworld", 11u));
    STEP("insert", vellum_command_insert(fixture.stack, 5u, " brave", 6u, &error));
    STEP("insert text", check_text(&fixture, "hello brave\nworld", 17u));
    STEP("undo flags",
         vellum_command_can_undo(fixture.stack) && !vellum_command_can_redo(fixture.stack));
    STEP("undo", vellum_command_undo(fixture.stack, &error));
    STEP("undo text", check_text(&fixture, "hello\nworld", 11u));
    STEP("redo", vellum_command_redo(fixture.stack, &error));
    STEP("redo text", check_text(&fixture, "hello brave\nworld", 17u));

    STEP("delete", vellum_command_delete(fixture.stack, 6u, 5u, &error));
    STEP("delete text", check_text(&fixture, "hello \nworld", 12u));
    STEP("undo delete", vellum_command_undo(fixture.stack, &error));
    STEP("undo delete text", check_text(&fixture, "hello brave\nworld", 17u));

    STEP("replace", vellum_command_replace(fixture.stack, 0u, 5u, "goodbye", 7u, &error));
    STEP("replace text", check_text(&fixture, "goodbye brave\nworld", 19u));
    STEP("line count", vellum_line_index_line_count(fixture.lines) == 2u);
    STEP("line ending",
         vellum_line_index_ending(fixture.lines, fixture.buffer, 0u) == VELLUM_LINE_ENDING_LF);
    STEP("undo replace", vellum_command_undo(fixture.stack, &error));
    STEP("undo replace text", check_text(&fixture, "hello brave\nworld", 17u));

    STEP("undo insert", vellum_command_undo(fixture.stack, &error));
    STEP("undo insert text", check_text(&fixture, "hello\nworld", 11u));
    STEP("empty undo", !vellum_command_undo(fixture.stack, &error));
    STEP("undo flags after", !vellum_command_can_undo(fixture.stack));
    STEP("redo insert", vellum_command_redo(fixture.stack, &error));
    STEP("redo insert text", check_text(&fixture, "hello brave\nworld", 17u));
    STEP("redo replace", vellum_command_redo(fixture.stack, &error));
    STEP("redo replace text", check_text(&fixture, "goodbye brave\nworld", 19u));

#undef STEP
    fixture_destroy(&fixture);
    return ok;
}

static bool transactions(void)
{
    Fixture fixture;
    if (!fixture_init(&fixture, "abc", 3u, 0u)) {
        return false;
    }
    VellumErrorInfo error;
    bool ok = true;
    const size_t initial_depth = vellum_command_undo_depth(fixture.stack);

    ok = ok && vellum_command_begin(fixture.stack, &error) &&
         vellum_command_in_transaction(fixture.stack) &&
         !vellum_command_begin(fixture.stack, &error) &&
         !vellum_command_undo(fixture.stack, &error);
    ok = ok && vellum_command_insert(fixture.stack, 0u, "X", 1u, &error) &&
         check_text(&fixture, "Xabc", 4u);
    ok = ok && vellum_command_insert(fixture.stack, 4u, "Y", 1u, &error) &&
         check_text(&fixture, "XabcY", 5u);
    ok = ok && vellum_command_commit(fixture.stack, &error) &&
         !vellum_command_in_transaction(fixture.stack) &&
         vellum_command_undo_depth(fixture.stack) == initial_depth + 1u;
    ok = ok && vellum_command_undo(fixture.stack, &error) && check_text(&fixture, "abc", 3u);
    ok = ok && vellum_command_redo(fixture.stack, &error) && check_text(&fixture, "XabcY", 5u);

    ok = ok && vellum_command_begin(fixture.stack, &error) &&
         vellum_command_delete(fixture.stack, 0u, 1u, &error) && check_text(&fixture, "abcY", 4u) &&
         vellum_command_insert(fixture.stack, 0u, "Z", 1u, &error) &&
         check_text(&fixture, "ZabcY", 5u);
    ok = ok && vellum_command_abort(fixture.stack, &error) &&
         !vellum_command_in_transaction(fixture.stack) && check_text(&fixture, "XabcY", 5u) &&
         vellum_command_undo_depth(fixture.stack) == initial_depth + 1u;

    ok = ok && vellum_command_begin(fixture.stack, &error) &&
         vellum_command_commit(fixture.stack, &error) &&
         vellum_command_undo_depth(fixture.stack) == initial_depth + 1u;

    fixture_destroy(&fixture);
    return ok;
}

static bool bounded_memory(void)
{
    Fixture fixture;
    if (!fixture_init(&fixture, "", 0u, 256u)) {
        return false;
    }
    VellumErrorInfo error;
    bool ok = true;

    for (size_t i = 0u; i < 50u; ++i) {
        ok = ok &&
             vellum_command_insert(fixture.stack, vellum_buffer_length(fixture.buffer), "x", 1u,
                                   &error) &&
             vellum_command_memory_usage(fixture.stack) <= 256u;
        if (!ok) {
            break;
        }
    }
    ok = ok && vellum_command_undo_depth(fixture.stack) > 0u &&
         vellum_command_undo_depth(fixture.stack) < 50u &&
         vellum_command_validate(fixture.stack, &error);

    while (vellum_command_can_undo(fixture.stack)) {
        ok = ok && vellum_command_undo(fixture.stack, &error);
        if (!ok) {
            fprintf(stderr, "bounded undo failed: %s\n", vellum_error_message(&error));
            break;
        }
    }
    ok = ok && vellum_command_validate(fixture.stack, &error) &&
         vellum_command_memory_usage(fixture.stack) <= 256u;

    fixture_destroy(&fixture);
    return ok;
}

static uint32_t next_random(uint32_t *state)
{
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static void save_state(char (*states)[MAX_TEXT], size_t *lengths, size_t *count, const char *text,
                       size_t length)
{
    memcpy(states[*count], text, length);
    states[*count][length] = '\0';
    lengths[*count] = length;
    ++*count;
}

static void restore_state(char *text, size_t *length, char (*states)[MAX_TEXT], size_t *lengths,
                          size_t index)
{
    *length = lengths[index];
    memcpy(text, states[index], *length);
    text[*length] = '\0';
}

static bool differential_fuzz(void)
{
    static const char *snippets[] = {"a", "b", "\n", "\r\n", "xy", "世界"};
    const size_t snippet_count = sizeof(snippets) / sizeof(snippets[0u]);
    Fixture fixture;
    if (!fixture_init(&fixture, "seed\nline2\n", 11u, 0u)) {
        return false;
    }
    VellumErrorInfo error;
    char model[MAX_TEXT] = "seed\nline2\n";
    size_t model_length = 11u;
    size_t undo_count = 0u;
    size_t redo_count = 0u;
    uint32_t state = 0x5EED1234u;
    bool ok = true;

    for (uint32_t op = 0u; op < FUZZ_OPERATIONS && ok; ++op) {
        const uint32_t choice = next_random(&state) % 100u;
        bool applied = false;

        if (choice < 80u && undo_count < MAX_HISTORY && model_length < MAX_TEXT / 2u) {
            save_state(undo_states, undo_lengths, &undo_count, model, model_length);
            redo_count = 0u;
            const size_t position = next_random(&state) % (model_length + 1u);
            const char *snippet = snippets[next_random(&state) % snippet_count];
            const size_t insert_length = strlen(snippet);

            if (choice < 50u) {
                applied =
                    vellum_command_insert(fixture.stack, position, snippet, insert_length, &error);
                memmove(model + position + insert_length, model + position,
                        model_length - position);
                memcpy(model + position, snippet, insert_length);
                model_length += insert_length;
            } else if (choice < 65u && model_length > 0u) {
                const size_t start = position < model_length ? position : model_length - 1u;
                size_t count = 1u + (next_random(&state) % 4u);
                if (count > model_length - start) {
                    count = model_length - start;
                }
                applied = vellum_command_delete(fixture.stack, start, count, &error);
                memmove(model + start, model + start + count, model_length - start - count);
                model_length -= count;
            } else {
                const size_t start = position < model_length ? position : model_length;
                size_t count = 1u + (next_random(&state) % 3u);
                if (count > model_length - start) {
                    count = model_length - start;
                }
                applied = vellum_command_replace(fixture.stack, start, count, snippet,
                                                 insert_length, &error);
                memmove(model + start + insert_length, model + start + count,
                        model_length - start - count);
                memcpy(model + start, snippet, insert_length);
                model_length = model_length - count + insert_length;
            }
        } else if (choice < 90u && undo_count > 0u) {
            save_state(redo_states, redo_lengths, &redo_count, model, model_length);
            restore_state(model, &model_length, undo_states, undo_lengths, undo_count - 1u);
            --undo_count;
            applied = vellum_command_undo(fixture.stack, &error);
        } else if (redo_count > 0u) {
            save_state(undo_states, undo_lengths, &undo_count, model, model_length);
            restore_state(model, &model_length, redo_states, redo_lengths, redo_count - 1u);
            --redo_count;
            applied = vellum_command_redo(fixture.stack, &error);
        } else {
            applied = true;
        }

        model[model_length] = '\0';
        VellumErrorInfo check_error;
        char *actual = vellum_buffer_snapshot(fixture.buffer, &check_error);
        const bool text_ok = applied && actual != NULL && strlen(actual) == model_length &&
                             memcmp(actual, model, model_length) == 0;
        const bool structure_ok = vellum_command_validate(fixture.stack, &check_error);
        ok = text_ok && structure_ok;
        if (!structure_ok) {
            fprintf(stderr, "command fuzz validation failed at op %u: %s\n", op,
                    vellum_error_message(&check_error));
        }
        if (!ok) {
            fprintf(
                stderr,
                "command fuzz failed at op %u (choice %u, applied %d): model='%s' actual='%s'\n",
                op, choice, applied ? 1 : 0, model, actual == NULL ? "NULL" : actual);
        }
        free(actual);
    }

    fixture_destroy(&fixture);
    return ok;
}

bool test_command(void)
{
    if (!basic_edits()) {
        fprintf(stderr, "command basic_edits failed\n");
        return false;
    }
    if (!transactions()) {
        fprintf(stderr, "command transactions failed\n");
        return false;
    }
    if (!bounded_memory()) {
        fprintf(stderr, "command bounded_memory failed\n");
        return false;
    }
    if (!differential_fuzz()) {
        fprintf(stderr, "command differential_fuzz failed\n");
        return false;
    }
    return true;
}
