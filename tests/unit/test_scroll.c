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
#include "vellum/text/line_index.h"
#include "vellum/text/scroll.h"

typedef struct {
    VellumBuffer *buffer;
    VellumLineIndex *lines;
    VellumScroll *scroll;
} Fixture;

/* "abc\n", "def\r\n", "ghi\r", "jkl" -> starts {0,4,9,13}. */
static const char DOC[] = "abc\ndef\r\nghi\rjkl";
static const size_t DOC_LENGTH = sizeof(DOC) - 1u;

static bool fixture_init(Fixture *fixture, const char *text, size_t length, float line_height,
                         float view_width, float view_height)
{
    VellumErrorInfo error;
    fixture->buffer = vellum_buffer_create(text, length, &error);
    if (fixture->buffer == NULL) {
        fprintf(stderr, "scroll fixture buffer: %s\n", vellum_error_message(&error));
        return false;
    }
    fixture->lines = vellum_line_index_create(&error);
    if (fixture->lines == NULL ||
        !vellum_line_index_rebuild(fixture->lines, fixture->buffer, &error)) {
        fprintf(stderr, "scroll fixture line index: %s\n", vellum_error_message(&error));
        vellum_line_index_destroy(fixture->lines);
        vellum_buffer_destroy(fixture->buffer);
        return false;
    }
    fixture->scroll = vellum_scroll_create(fixture->buffer, fixture->lines, line_height, &error);
    if (fixture->scroll == NULL) {
        fprintf(stderr, "scroll fixture scroll: %s\n", vellum_error_message(&error));
        vellum_line_index_destroy(fixture->lines);
        vellum_buffer_destroy(fixture->buffer);
        return false;
    }
    vellum_scroll_set_view_size(fixture->scroll, view_width, view_height);
    return true;
}

static void fixture_destroy(Fixture *fixture)
{
    vellum_scroll_destroy(fixture->scroll);
    vellum_line_index_destroy(fixture->lines);
    vellum_buffer_destroy(fixture->buffer);
}

static bool near(float a, float b)
{
    const float epsilon = 0.0001f;
    return a - b < epsilon && b - a < epsilon;
}

static bool position_is(const Fixture *fixture, float x, float y, const char *name)
{
    float actual_x = -1.0f;
    float actual_y = -1.0f;
    vellum_scroll_position(fixture->scroll, &actual_x, &actual_y);
    if (!near(actual_x, x) || !near(actual_y, y)) {
        fprintf(stderr, "scroll %s: expected (%.3f, %.3f), got (%.3f, %.3f)\n", name, x, y,
                actual_x, actual_y);
        return false;
    }
    return true;
}

static bool lines_are(const Fixture *fixture, size_t first, size_t last, const char *name)
{
    size_t actual_first = SIZE_MAX;
    size_t actual_last = SIZE_MAX;
    vellum_scroll_visible_lines(fixture->scroll, &actual_first, &actual_last);
    if (actual_first != first || actual_last != last) {
        fprintf(stderr, "scroll %s: expected [%zu, %zu], got [%zu, %zu]\n", name, first, last,
                actual_first, actual_last);
        return false;
    }
    return true;
}

static bool bytes_are(const Fixture *fixture, size_t start, size_t end, const char *name)
{
    size_t actual_start = SIZE_MAX;
    size_t actual_end = SIZE_MAX;
    vellum_scroll_visible_byte_range(fixture->scroll, &actual_start, &actual_end);
    if (actual_start != start || actual_end != end) {
        fprintf(stderr, "scroll %s: expected [%zu, %zu), got [%zu, %zu)\n", name, start, end,
                actual_start, actual_end);
        return false;
    }
    return true;
}

#define SCROLL_STEP(name, expr)                                                                    \
    do {                                                                                           \
        if (ok && !(expr)) {                                                                       \
            fprintf(stderr, "scroll %s failed\n", name);                                           \
            ok = false;                                                                            \
        }                                                                                          \
    } while (0)

static bool extents_and_clamping(void)
{
    Fixture fixture;
    if (!fixture_init(&fixture, DOC, DOC_LENGTH, 10.0f, 100.0f, 25.0f)) {
        return false;
    }
    bool ok = true;
    float max_x = -1.0f;
    float max_y = -1.0f;

    SCROLL_STEP("content height", near(vellum_scroll_content_height(fixture.scroll), 40.0f));
    SCROLL_STEP("content width default", near(vellum_scroll_content_width(fixture.scroll), 0.0f));
    vellum_scroll_max_scroll(fixture.scroll, &max_x, &max_y);
    SCROLL_STEP("max x default", near(max_x, 0.0f));
    SCROLL_STEP("max y", near(max_y, 15.0f));

    /* Offsets clamp to [0, max] in both axes. */
    vellum_scroll_set(fixture.scroll, 50.0f, 100.0f);
    SCROLL_STEP("set clamped", position_is(&fixture, 0.0f, 15.0f, "set clamped"));
    vellum_scroll_set(fixture.scroll, -50.0f, -100.0f);
    SCROLL_STEP("set negative clamped", position_is(&fixture, 0.0f, 0.0f, "set negative"));

    /* A wider document enables horizontal scrolling. */
    vellum_scroll_set_content_width(fixture.scroll, 200.0f);
    vellum_scroll_max_scroll(fixture.scroll, &max_x, &max_y);
    SCROLL_STEP("max x after width", near(max_x, 100.0f));
    vellum_scroll_set(fixture.scroll, 50.0f, 0.0f);
    SCROLL_STEP("horizontal set", position_is(&fixture, 50.0f, 0.0f, "horizontal set"));
    vellum_scroll_by(fixture.scroll, 1000.0f, 1000.0f);
    SCROLL_STEP("delta clamped", position_is(&fixture, 100.0f, 15.0f, "delta clamped"));

    /* Growing the view past the content pulls the scroll offsets back to zero. */
    vellum_scroll_set_view_size(fixture.scroll, 400.0f, 100.0f);
    SCROLL_STEP("grow view clamps", position_is(&fixture, 0.0f, 0.0f, "grow view"));
    vellum_scroll_max_scroll(fixture.scroll, &max_x, &max_y);
    SCROLL_STEP("max after grow", near(max_x, 0.0f) && near(max_y, 0.0f));

    /* Changing line height re-derives the vertical extent. */
    vellum_scroll_set_view_size(fixture.scroll, 100.0f, 25.0f);
    vellum_scroll_set_line_height(fixture.scroll, 20.0f);
    SCROLL_STEP("new content height", near(vellum_scroll_content_height(fixture.scroll), 80.0f));
    vellum_scroll_max_scroll(fixture.scroll, &max_x, &max_y);
    SCROLL_STEP("new max y", near(max_y, 55.0f));
    SCROLL_STEP("line height getter", near(vellum_scroll_line_height(fixture.scroll), 20.0f));

    SCROLL_STEP("validate", vellum_scroll_validate(fixture.scroll));
    fixture_destroy(&fixture);
    return ok;
}

static bool visible_range(void)
{
    Fixture fixture;
    if (!fixture_init(&fixture, DOC, DOC_LENGTH, 10.0f, 100.0f, 25.0f)) {
        return false;
    }
    bool ok = true;

    SCROLL_STEP("top lines", lines_are(&fixture, 0u, 2u, "top"));
    SCROLL_STEP("top bytes", bytes_are(&fixture, 0u, 12u, "top"));

    vellum_scroll_set(fixture.scroll, 0.0f, 10.0f);
    SCROLL_STEP("middle lines", lines_are(&fixture, 1u, 3u, "middle"));
    SCROLL_STEP("middle bytes", bytes_are(&fixture, 4u, 16u, "middle"));

    vellum_scroll_set(fixture.scroll, 0.0f, 15.0f);
    SCROLL_STEP("bottom lines", lines_are(&fixture, 1u, 3u, "bottom"));

    /* A one-line-tall view shows exactly one line when aligned to a row. */
    vellum_scroll_set_view_size(fixture.scroll, 100.0f, 10.0f);
    vellum_scroll_set(fixture.scroll, 0.0f, 0.0f);
    SCROLL_STEP("one line", lines_are(&fixture, 0u, 0u, "one line"));
    vellum_scroll_set(fixture.scroll, 0.0f, 20.0f);
    SCROLL_STEP("one line at row two", lines_are(&fixture, 2u, 2u, "one line row two"));
    /* An unaligned one-line view overlaps two rows. */
    vellum_scroll_set(fixture.scroll, 0.0f, 15.0f);
    SCROLL_STEP("unaligned spans two", lines_are(&fixture, 1u, 2u, "unaligned"));

    /* An empty buffer still has one empty line and an empty byte range. */
    fixture_destroy(&fixture);
    if (!fixture_init(&fixture, "", 0u, 10.0f, 100.0f, 25.0f)) {
        return false;
    }
    SCROLL_STEP("empty lines", lines_are(&fixture, 0u, 0u, "empty"));
    SCROLL_STEP("empty bytes", bytes_are(&fixture, 0u, 0u, "empty bytes"));
    SCROLL_STEP("empty content height", near(vellum_scroll_content_height(fixture.scroll), 10.0f));

    fixture_destroy(&fixture);
    return ok;
}

static bool ensure_visible(void)
{
    Fixture fixture;
    if (!fixture_init(&fixture, DOC, DOC_LENGTH, 10.0f, 100.0f, 25.0f)) {
        return false;
    }
    bool ok = true;

    SCROLL_STEP("ensure row three", vellum_scroll_ensure_line_visible(fixture.scroll, 3u));
    SCROLL_STEP("scrolled to row three", position_is(&fixture, 0.0f, 15.0f, "row three"));
    SCROLL_STEP("row three visible", lines_are(&fixture, 1u, 3u, "row three visible"));

    /* Row two is fully inside [15, 40) already, so this is a no-op. */
    SCROLL_STEP("ensure row two no-op", vellum_scroll_ensure_line_visible(fixture.scroll, 2u));
    SCROLL_STEP("still row three", position_is(&fixture, 0.0f, 15.0f, "still three"));
    /* Row one is clipped at the top, so ensuring it scrolls back up. */
    SCROLL_STEP("ensure clipped row one", vellum_scroll_ensure_line_visible(fixture.scroll, 1u));
    SCROLL_STEP("scrolled up", position_is(&fixture, 0.0f, 10.0f, "scrolled up"));

    SCROLL_STEP("ensure row zero", vellum_scroll_ensure_line_visible(fixture.scroll, 0u));
    SCROLL_STEP("back to top", position_is(&fixture, 0.0f, 0.0f, "top"));

    SCROLL_STEP("ensure offset in row three",
                vellum_scroll_ensure_offset_visible(fixture.scroll, 13u));
    SCROLL_STEP("offset row three", position_is(&fixture, 0.0f, 15.0f, "offset row three"));
    SCROLL_STEP("ensure offset in row zero",
                vellum_scroll_ensure_offset_visible(fixture.scroll, 2u));
    SCROLL_STEP("offset row zero", position_is(&fixture, 0.0f, 0.0f, "offset row zero"));

    if (vellum_scroll_ensure_line_visible(fixture.scroll, 4u) ||
        vellum_scroll_ensure_line_visible(NULL, 0u) ||
        vellum_scroll_ensure_offset_visible(NULL, 0u)) {
        fprintf(stderr, "scroll ensure should reject out-of-range line and NULL\n");
        ok = false;
    }

    fixture_destroy(&fixture);
    return ok;
}

static bool horizontal_ensure(void)
{
    Fixture fixture;
    if (!fixture_init(&fixture, DOC, DOC_LENGTH, 10.0f, 100.0f, 25.0f)) {
        return false;
    }
    bool ok = true;
    vellum_scroll_set_content_width(fixture.scroll, 200.0f);

    vellum_scroll_ensure_x_visible(fixture.scroll, 150.0f, 20.0f);
    SCROLL_STEP("ensure far right", position_is(&fixture, 70.0f, 0.0f, "far right"));
    vellum_scroll_ensure_x_visible(fixture.scroll, 10.0f, 20.0f);
    SCROLL_STEP("ensure far left", position_is(&fixture, 10.0f, 0.0f, "far left"));
    vellum_scroll_ensure_x_visible(fixture.scroll, 50.0f, 200.0f);
    SCROLL_STEP("wide range aligns left", position_is(&fixture, 50.0f, 0.0f, "wide range"));
    /* Clamping still applies after an oversized request. */
    vellum_scroll_ensure_x_visible(fixture.scroll, 500.0f, 20.0f);
    SCROLL_STEP("wide clamped", position_is(&fixture, 100.0f, 0.0f, "wide clamped"));

    fixture_destroy(&fixture);
    return ok;
}

static bool edits_and_invariants(void)
{
    Fixture fixture;
    if (!fixture_init(&fixture, DOC, DOC_LENGTH, 10.0f, 100.0f, 25.0f)) {
        return false;
    }
    VellumErrorInfo error;
    bool ok = true;
    vellum_scroll_set(fixture.scroll, 0.0f, 15.0f);

    /* Remove all but "abc": the line count drops to one and the offset must
     * re-clamp to zero after the caller refreshes the index and scroll. */
    static const char removed[] = "\ndef\r\nghi\rjkl";
    if (!vellum_buffer_delete(fixture.buffer, 3u, sizeof(removed) - 1u, &error) ||
        !vellum_line_index_notify_delete(fixture.lines, fixture.buffer, 3u, sizeof(removed) - 1u,
                                         &error)) {
        fprintf(stderr, "scroll edit setup: %s\n", vellum_error_message(&error));
        fixture_destroy(&fixture);
        return false;
    }
    vellum_scroll_clamp(fixture.scroll);
    SCROLL_STEP("line index still valid",
                vellum_line_index_validate(fixture.lines, fixture.buffer));
    SCROLL_STEP("shrunk content", near(vellum_scroll_content_height(fixture.scroll), 10.0f));
    SCROLL_STEP("reclamped", position_is(&fixture, 0.0f, 0.0f, "reclamped"));
    SCROLL_STEP("one visible line", lines_are(&fixture, 0u, 0u, "one line"));
    SCROLL_STEP("validate after edit", vellum_scroll_validate(fixture.scroll));

    /* Sweep a range of scroll offsets and view heights, checking that every
     * reported visible line's band intersects the view and that the first
     * reported line is the one containing the top edge. */
    fixture_destroy(&fixture);
    if (!fixture_init(&fixture, DOC, DOC_LENGTH, 10.0f, 100.0f, 25.0f)) {
        return false;
    }
    const float heights[] = {1.0f, 10.0f, 25.0f, 40.0f, 100.0f};
    for (size_t h = 0u; h < sizeof(heights) / sizeof(heights[0u]) && ok; ++h) {
        vellum_scroll_set_view_size(fixture.scroll, 100.0f, heights[h]);
        for (float y = 0.0f; y <= 40.0f && ok; y += 2.5f) {
            vellum_scroll_set(fixture.scroll, 0.0f, y);
            size_t first = SIZE_MAX;
            size_t last = SIZE_MAX;
            vellum_scroll_visible_lines(fixture.scroll, &first, &last);
            float actual_y = 0.0f;
            vellum_scroll_position(fixture.scroll, NULL, &actual_y);
            const size_t expected_first =
                vellum_line_index_line_count(fixture.lines) - 1u < (size_t)(actual_y / 10.0f)
                    ? vellum_line_index_line_count(fixture.lines) - 1u
                    : (size_t)(actual_y / 10.0f);
            if (first != expected_first || last < first || last >= 4u) {
                fprintf(stderr, "scroll invariant failed at y=%.1f height=%.1f: [%zu,%zu]\n", y,
                        heights[h], first, last);
                ok = false;
                break;
            }
            const float top_of_first = (float)first * 10.0f;
            const float bottom_of_last = (float)(last + 1u) * 10.0f;
            if (top_of_first > actual_y + heights[h] || bottom_of_last <= actual_y) {
                fprintf(stderr, "scroll visible band outside view at y=%.1f\n", y);
                ok = false;
            }
        }
    }

    fixture_destroy(&fixture);
    return ok;
}

static bool null_and_validation(void)
{
    VellumErrorInfo error;
    vellum_error_init(&error);

    if (vellum_scroll_create(NULL, NULL, 10.0f, &error) != NULL ||
        error.code != VELLUM_ERR_INVALID_ARGUMENT || vellum_scroll_validate(NULL)) {
        fprintf(stderr, "scroll create should reject NULL dependencies\n");
        return false;
    }

    Fixture fixture;
    if (!fixture_init(&fixture, DOC, DOC_LENGTH, 10.0f, 100.0f, 25.0f)) {
        return false;
    }
    VellumBuffer *buffer = fixture.buffer;
    VellumLineIndex *lines = fixture.lines;
    if (vellum_scroll_create(buffer, lines, 0.0f, &error) != NULL) {
        fprintf(stderr, "scroll create should reject a non-positive line height\n");
        fixture_destroy(&fixture);
        return false;
    }

    /* NULL-safe setters and getters. */
    vellum_scroll_set_view_size(NULL, 10.0f, 10.0f);
    vellum_scroll_set_line_height(NULL, 10.0f);
    vellum_scroll_set_content_width(NULL, 10.0f);
    vellum_scroll_set(NULL, 0.0f, 0.0f);
    vellum_scroll_by(NULL, 0.0f, 0.0f);
    vellum_scroll_clamp(NULL);
    vellum_scroll_ensure_x_visible(NULL, 0.0f, 0.0f);
    vellum_scroll_visible_lines(NULL, NULL, NULL);
    vellum_scroll_visible_byte_range(NULL, NULL, NULL);
    vellum_scroll_max_scroll(NULL, NULL, NULL);
    vellum_scroll_position(NULL, NULL, NULL);
    vellum_scroll_view_size(NULL, NULL, NULL);
    float width = -1.0f;
    float height = -1.0f;
    vellum_scroll_view_size(fixture.scroll, &width, &height);
    vellum_scroll_set_view_size(fixture.scroll, -5.0f, -5.0f);
    vellum_scroll_view_size(fixture.scroll, &width, &height);
    if (!near(width, 0.0f) || !near(height, 0.0f) || !vellum_scroll_validate(fixture.scroll)) {
        fprintf(stderr, "scroll NULL handling or view clamping failed\n");
        fixture_destroy(&fixture);
        return false;
    }
    fixture_destroy(&fixture);
    return true;
}

bool test_scroll(void)
{
    if (!extents_and_clamping()) {
        fprintf(stderr, "scroll extents_and_clamping failed\n");
        return false;
    }
    if (!visible_range()) {
        fprintf(stderr, "scroll visible_range failed\n");
        return false;
    }
    if (!ensure_visible()) {
        fprintf(stderr, "scroll ensure_visible failed\n");
        return false;
    }
    if (!horizontal_ensure()) {
        fprintf(stderr, "scroll horizontal_ensure failed\n");
        return false;
    }
    if (!edits_and_invariants()) {
        fprintf(stderr, "scroll edits_and_invariants failed\n");
        return false;
    }
    if (!null_and_validation()) {
        fprintf(stderr, "scroll null_and_validation failed\n");
        return false;
    }
    return true;
}
