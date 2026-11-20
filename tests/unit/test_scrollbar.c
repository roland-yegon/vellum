/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tests/unit/tests.h"
#include "vellum/core/error.h"
#include "vellum/text/buffer.h"
#include "vellum/text/line_index.h"
#include "vellum/text/scroll.h"
#include "vellum/text/scrollbar.h"

typedef struct {
    VellumBuffer *buffer;
    VellumLineIndex *lines;
    VellumScroll *scroll;
} Fixture;

static bool fixture_init(Fixture *fixture, size_t line_count, float line_height, float view_width,
                         float view_height)
{
    memset(fixture, 0, sizeof(*fixture));
    char *text = malloc(2u * line_count + 1u);
    if (text == NULL) {
        return false;
    }
    size_t length = 0u;
    for (size_t i = 0u; i < line_count; ++i) {
        if (i > 0u) {
            text[length++] = '\n';
        }
        text[length++] = 'x';
    }
    text[length] = '\0';

    VellumErrorInfo error;
    fixture->buffer = vellum_buffer_create(text, length, &error);
    free(text);
    if (fixture->buffer == NULL) {
        return false;
    }
    fixture->lines = vellum_line_index_create(&error);
    if (fixture->lines == NULL ||
        !vellum_line_index_rebuild(fixture->lines, fixture->buffer, &error)) {
        vellum_line_index_destroy(fixture->lines);
        vellum_buffer_destroy(fixture->buffer);
        memset(fixture, 0, sizeof(*fixture));
        return false;
    }
    fixture->scroll = vellum_scroll_create(fixture->buffer, fixture->lines, line_height, &error);
    if (fixture->scroll == NULL) {
        vellum_line_index_destroy(fixture->lines);
        vellum_buffer_destroy(fixture->buffer);
        memset(fixture, 0, sizeof(*fixture));
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
    memset(fixture, 0, sizeof(*fixture));
}

static bool near(float a, float b)
{
    const float epsilon = 0.001f;
    return a - b < epsilon && b - a < epsilon;
}

static bool rect_is(VellumScrollbarRect rect, float x, float y, float width, float height,
                    const char *label)
{
    if (!near(rect.x, x) || !near(rect.y, y) || !near(rect.width, width) ||
        !near(rect.height, height)) {
        fprintf(stderr, "scrollbar %s: expected (%.3f,%.3f,%.3f,%.3f), got (%.3f,%.3f,%.3f,%.3f)\n",
                label, x, y, width, height, rect.x, rect.y, rect.width, rect.height);
        return false;
    }
    return true;
}

static bool geometry_is(const VellumScrollbar *bar, float tx, float ty, float tw, float th,
                        float hx, float hy, float hw, float hh, const char *label)
{
    VellumScrollbarGeometry geometry = {0};
    vellum_scrollbar_geometry(bar, &geometry);
    return rect_is(geometry.track, tx, ty, tw, th, label) &&
           rect_is(geometry.thumb, hx, hy, hw, hh, label);
}

static VellumScrollbarRect make_rect(float x, float y, float width, float height)
{
    VellumScrollbarRect rect = {x, y, width, height};
    return rect;
}

static bool vertical_geometry(void)
{
    /* 100 lines * 10 = 1000 content, 200 view -> max scroll 800. */
    Fixture fixture;
    if (!fixture_init(&fixture, 100u, 10.0f, 300.0f, 200.0f)) {
        return false;
    }
    VellumScrollbar bar;
    vellum_scrollbar_init(&bar, fixture.scroll, VELLUM_SCROLLBAR_VERTICAL,
                          make_rect(10.0f, 20.0f, 12.0f, 400.0f), 20.0f);
    bool ok = vellum_scrollbar_validate(&bar);

    /* thumb = 400 * 200 / 1000 = 80, at track start. */
    ok = geometry_is(&bar, 10.0f, 20.0f, 12.0f, 400.0f, 10.0f, 20.0f, 12.0f, 80.0f,
                     "vertical top") &&
         ok;
    vellum_scroll_set(fixture.scroll, 0.0f, 400.0f);
    ok = geometry_is(&bar, 10.0f, 20.0f, 12.0f, 400.0f, 10.0f, 180.0f, 12.0f, 80.0f,
                     "vertical middle") &&
         ok;
    vellum_scroll_set(fixture.scroll, 0.0f, 800.0f);
    ok = geometry_is(&bar, 10.0f, 20.0f, 12.0f, 400.0f, 10.0f, 340.0f, 12.0f, 80.0f,
                     "vertical bottom") &&
         ok;
    /* Clamped beyond the end: the thumb stays at the track end. */
    vellum_scroll_set(fixture.scroll, 0.0f, 5000.0f);
    ok = geometry_is(&bar, 10.0f, 20.0f, 12.0f, 400.0f, 10.0f, 340.0f, 12.0f, 80.0f,
                     "vertical clamped") &&
         ok;

    fixture_destroy(&fixture);
    return ok;
}

static bool horizontal_geometry(void)
{
    Fixture fixture;
    if (!fixture_init(&fixture, 100u, 10.0f, 200.0f, 100.0f)) {
        return false;
    }
    vellum_scroll_set_content_width(fixture.scroll, 1000.0f);
    VellumScrollbar bar;
    vellum_scrollbar_init(&bar, fixture.scroll, VELLUM_SCROLLBAR_HORIZONTAL,
                          make_rect(5.0f, 10.0f, 300.0f, 12.0f), 30.0f);
    bool ok = vellum_scrollbar_validate(&bar);

    /* thumb = 300 * 200 / 1000 = 60, at track start. */
    ok = geometry_is(&bar, 5.0f, 10.0f, 300.0f, 12.0f, 5.0f, 10.0f, 60.0f, 12.0f,
                     "horizontal top") &&
         ok;
    vellum_scroll_set(fixture.scroll, 400.0f, 0.0f);
    ok = geometry_is(&bar, 5.0f, 10.0f, 300.0f, 12.0f, 125.0f, 10.0f, 60.0f, 12.0f,
                     "horizontal middle") &&
         ok;
    vellum_scroll_set(fixture.scroll, 800.0f, 0.0f);
    ok = geometry_is(&bar, 5.0f, 10.0f, 300.0f, 12.0f, 245.0f, 10.0f, 60.0f, 12.0f,
                     "horizontal bottom") &&
         ok;

    fixture_destroy(&fixture);
    return ok;
}

static bool no_scroll_and_minimum(void)
{
    /* Content shorter than the view: the thumb fills the track. */
    Fixture fixture;
    if (!fixture_init(&fixture, 5u, 10.0f, 300.0f, 200.0f)) {
        return false;
    }
    VellumScrollbar bar;
    vellum_scrollbar_init(&bar, fixture.scroll, VELLUM_SCROLLBAR_VERTICAL,
                          make_rect(0.0f, 0.0f, 10.0f, 200.0f), 20.0f);
    bool ok = geometry_is(&bar, 0.0f, 0.0f, 10.0f, 200.0f, 0.0f, 0.0f, 10.0f, 200.0f,
                          "no vertical scroll");
    vellum_scrollbar_init(&bar, fixture.scroll, VELLUM_SCROLLBAR_HORIZONTAL,
                          make_rect(0.0f, 0.0f, 200.0f, 10.0f), 20.0f);
    ok = geometry_is(&bar, 0.0f, 0.0f, 200.0f, 10.0f, 0.0f, 0.0f, 200.0f, 10.0f,
                     "no horizontal scroll") &&
         ok;

    /* A tiny view/content ratio clamps the thumb to the configured minimum. */
    fixture_destroy(&fixture);
    if (!fixture_init(&fixture, 1000u, 10.0f, 300.0f, 100.0f)) {
        return false;
    }
    vellum_scrollbar_init(&bar, fixture.scroll, VELLUM_SCROLLBAR_VERTICAL,
                          make_rect(0.0f, 0.0f, 10.0f, 400.0f), 20.0f);
    ok = geometry_is(&bar, 0.0f, 0.0f, 10.0f, 400.0f, 0.0f, 0.0f, 10.0f, 20.0f, "minimum thumb") &&
         ok;
    vellum_scroll_set(fixture.scroll, 0.0f, 9900.0f); /* max = 10000 - 100 */
    ok = geometry_is(&bar, 0.0f, 0.0f, 10.0f, 400.0f, 0.0f, 380.0f, 10.0f, 20.0f,
                     "minimum thumb bottom") &&
         ok;

    fixture_destroy(&fixture);
    return ok;
}

static bool hit_testing(void)
{
    Fixture fixture;
    if (!fixture_init(&fixture, 100u, 10.0f, 300.0f, 200.0f)) {
        return false;
    }
    VellumScrollbar bar;
    vellum_scrollbar_init(&bar, fixture.scroll, VELLUM_SCROLLBAR_VERTICAL,
                          make_rect(10.0f, 20.0f, 12.0f, 400.0f), 20.0f);
    bool ok = true;

    /* At the top the thumb is [20,100); everything after it is "after". */
    ok = vellum_scrollbar_hit_test(&bar, 15.0f, 50.0f) == VELLUM_SCROLLBAR_HIT_THUMB && ok;
    ok = vellum_scrollbar_hit_test(&bar, 15.0f, 20.0f) == VELLUM_SCROLLBAR_HIT_THUMB && ok;
    ok = vellum_scrollbar_hit_test(&bar, 15.0f, 200.0f) == VELLUM_SCROLLBAR_HIT_AFTER && ok;
    ok = vellum_scrollbar_hit_test(&bar, 15.0f, 19.0f) == VELLUM_SCROLLBAR_HIT_NONE && ok;
    ok = vellum_scrollbar_hit_test(&bar, 5.0f, 50.0f) == VELLUM_SCROLLBAR_HIT_NONE && ok;
    ok = vellum_scrollbar_hit_test(&bar, 15.0f, 420.0f) == VELLUM_SCROLLBAR_HIT_NONE && ok;

    /* Mid-scroll the thumb is [180,260), with a "before" region above it. */
    vellum_scroll_set(fixture.scroll, 0.0f, 400.0f);
    ok = vellum_scrollbar_hit_test(&bar, 15.0f, 100.0f) == VELLUM_SCROLLBAR_HIT_BEFORE && ok;
    ok = vellum_scrollbar_hit_test(&bar, 15.0f, 200.0f) == VELLUM_SCROLLBAR_HIT_THUMB && ok;
    ok = vellum_scrollbar_hit_test(&bar, 15.0f, 300.0f) == VELLUM_SCROLLBAR_HIT_AFTER && ok;

    /* Horizontal hit testing uses the x axis. */
    vellum_scroll_set_content_width(fixture.scroll, 1000.0f);
    vellum_scrollbar_init(&bar, fixture.scroll, VELLUM_SCROLLBAR_HORIZONTAL,
                          make_rect(5.0f, 10.0f, 300.0f, 12.0f), 30.0f);
    vellum_scroll_set(fixture.scroll, 400.0f, 0.0f);
    ok = vellum_scrollbar_hit_test(&bar, 50.0f, 15.0f) == VELLUM_SCROLLBAR_HIT_BEFORE && ok;
    ok = vellum_scrollbar_hit_test(&bar, 150.0f, 15.0f) == VELLUM_SCROLLBAR_HIT_THUMB && ok;
    ok = vellum_scrollbar_hit_test(&bar, 250.0f, 15.0f) == VELLUM_SCROLLBAR_HIT_AFTER && ok;
    ok = vellum_scrollbar_hit_test(&bar, 150.0f, 5.0f) == VELLUM_SCROLLBAR_HIT_NONE && ok;

    fixture_destroy(&fixture);
    return ok;
}

static bool position_x_is(const Fixture *fixture, float expected, const char *label)
{
    float x = -1.0f;
    float y = -1.0f;
    vellum_scroll_position(fixture->scroll, &x, &y);
    if (!near(x, expected) || !near(y, 0.0f)) {
        fprintf(stderr, "scrollbar %s: expected x %.3f, got (%.3f, %.3f)\n", label, expected, x, y);
        return false;
    }
    return true;
}

static bool position_y_is(const Fixture *fixture, float expected, const char *label)
{
    float x = -1.0f;
    float y = -1.0f;
    vellum_scroll_position(fixture->scroll, &x, &y);
    if (!near(y, expected) || !near(x, 0.0f)) {
        fprintf(stderr, "scrollbar %s: expected y %.3f, got (%.3f, %.3f)\n", label, expected, x, y);
        return false;
    }
    return true;
}

static bool dragging_and_paging(void)
{
    /* max scroll 800, thumb length 80, travel 320, track starts at y=20. */
    Fixture fixture;
    if (!fixture_init(&fixture, 100u, 10.0f, 200.0f, 200.0f)) {
        return false;
    }
    VellumScrollbar bar;
    vellum_scrollbar_init(&bar, fixture.scroll, VELLUM_SCROLLBAR_VERTICAL,
                          make_rect(10.0f, 20.0f, 12.0f, 400.0f), 20.0f);
    bool ok = true;

    /* Centering the thumb at its start keeps scroll at zero. */
    vellum_scrollbar_scroll_to_pointer(&bar, 60.0f, true);
    ok = position_y_is(&fixture, 0.0f, "drag to top") && ok;
    /* Centering at 220 -> desired start 180 -> half of travel -> y=400. */
    vellum_scrollbar_scroll_to_pointer(&bar, 220.0f, true);
    ok = position_y_is(&fixture, 400.0f, "drag to middle") && ok;
    /* Beyond the end clamps to max. */
    vellum_scrollbar_scroll_to_pointer(&bar, 1000.0f, true);
    ok = position_y_is(&fixture, 800.0f, "drag past end") && ok;
    /* Leading-edge placement at 180 -> half of travel -> y=400. */
    vellum_scrollbar_scroll_to_pointer(&bar, 180.0f, false);
    ok = position_y_is(&fixture, 400.0f, "drag leading edge") && ok;

    /* Paging moves by one view (200) and clamps. */
    vellum_scroll_set(fixture.scroll, 0.0f, 0.0f);
    vellum_scrollbar_page(&bar, 1);
    ok = position_y_is(&fixture, 200.0f, "page down") && ok;
    vellum_scrollbar_page(&bar, -1);
    ok = position_y_is(&fixture, 0.0f, "page up") && ok;
    vellum_scrollbar_page(&bar, -1);
    ok = position_y_is(&fixture, 0.0f, "page up clamped") && ok;
    for (int i = 0; i < 10; ++i) {
        vellum_scrollbar_page(&bar, 1);
    }
    ok = position_y_is(&fixture, 800.0f, "page down clamped") && ok;
    vellum_scrollbar_page(&bar, 0);
    vellum_scrollbar_page(&bar, 2);
    ok = position_y_is(&fixture, 800.0f, "page bad direction") && ok;

    /* Cross-axis scroll stays untouched. */
    vellum_scroll_set_content_width(fixture.scroll, 1000.0f);
    vellum_scroll_set(fixture.scroll, 42.0f, 0.0f);
    vellum_scrollbar_page(&bar, 1);
    {
        float x = -1.0f;
        float y = -1.0f;
        vellum_scroll_position(fixture.scroll, &x, &y);
        if (!near(x, 42.0f)) {
            fprintf(stderr, "scrollbar paging changed x to %.3f\n", x);
            ok = false;
        }
    }

    /* Horizontal drag/paging uses x and preserves y. */
    vellum_scroll_set_content_width(fixture.scroll, 1000.0f);
    vellum_scrollbar_init(&bar, fixture.scroll, VELLUM_SCROLLBAR_HORIZONTAL,
                          make_rect(5.0f, 10.0f, 300.0f, 12.0f), 30.0f);
    vellum_scroll_set(fixture.scroll, 0.0f, 0.0f);
    vellum_scrollbar_page(&bar, 1);
    ok = position_x_is(&fixture, 200.0f, "horizontal page") && ok;
    /* thumb length 60, travel 240, track start 5: center at 155 -> x=400. */
    vellum_scrollbar_scroll_to_pointer(&bar, 155.0f, true);
    ok = position_x_is(&fixture, 400.0f, "horizontal drag") && ok;
    /* No horizontal range: dragging is a no-op. */
    vellum_scroll_set_content_width(fixture.scroll, 0.0f);
    vellum_scroll_set(fixture.scroll, 0.0f, 0.0f);
    vellum_scrollbar_scroll_to_pointer(&bar, 250.0f, true);
    ok = position_x_is(&fixture, 0.0f, "horizontal no range") && ok;

    fixture_destroy(&fixture);
    return ok;
}

static bool invariants(const VellumScrollbar *bar, const char *label)
{
    VellumScrollbarGeometry geometry = {0};
    vellum_scrollbar_geometry(bar, &geometry);
    const bool vertical = bar->orientation == VELLUM_SCROLLBAR_VERTICAL;
    const float track_start = vertical ? geometry.track.y : geometry.track.x;
    const float track_length = vertical ? geometry.track.height : geometry.track.width;
    const float thumb_start = vertical ? geometry.thumb.y : geometry.thumb.x;
    const float thumb_length = vertical ? geometry.thumb.height : geometry.thumb.width;
    if (thumb_length < 0.0f || thumb_length > track_length + 0.001f ||
        thumb_start < track_start - 0.001f ||
        thumb_start + thumb_length > track_start + track_length + 0.001f) {
        fprintf(stderr,
                "scrollbar %s: thumb outside track (start %.3f len %.3f, track %.3f %.3f)\n", label,
                thumb_start, thumb_length, track_start, track_length);
        return false;
    }
    const bool cross_ok = vertical ? near(geometry.thumb.x, geometry.track.x) &&
                                         near(geometry.thumb.width, geometry.track.width)
                                   : near(geometry.thumb.y, geometry.track.y) &&
                                         near(geometry.thumb.height, geometry.track.height);
    if (!cross_ok) {
        fprintf(stderr, "scrollbar %s: thumb cross-axis does not match track\n", label);
        return false;
    }
    return true;
}

static bool invariant_sweep(void)
{
    Fixture fixture;
    if (!fixture_init(&fixture, 100u, 10.0f, 200.0f, 120.0f)) {
        return false;
    }
    vellum_scroll_set_content_width(fixture.scroll, 900.0f);
    VellumScrollbar bar;
    bool ok = true;
    const VellumScrollbarOrientation orientations[] = {VELLUM_SCROLLBAR_VERTICAL,
                                                       VELLUM_SCROLLBAR_HORIZONTAL};
    for (size_t o = 0u; o < 2u && ok; ++o) {
        vellum_scrollbar_init(&bar, fixture.scroll, orientations[o],
                              make_rect(3.0f, 7.0f, 250.0f, 350.0f), 25.0f);
        for (size_t x = 0u; x <= 20u && ok; ++x) {
            for (size_t y = 0u; y <= 20u && ok; ++y) {
                vellum_scroll_set(fixture.scroll, (float)x * 60.0f, (float)y * 50.0f);
                if (!invariants(&bar, "sweep")) {
                    ok = false;
                    break;
                }
            }
        }
    }

    /* Skip the sweep when the track itself is degenerate. */
    vellum_scrollbar_init(&bar, fixture.scroll, VELLUM_SCROLLBAR_VERTICAL,
                          make_rect(0.0f, 0.0f, 0.0f, 0.0f), 0.0f);
    ok = invariants(&bar, "degenerate track") && ok;

    fixture_destroy(&fixture);
    return ok;
}

static bool null_and_validation(void)
{
    Fixture fixture;
    if (!fixture_init(&fixture, 10u, 10.0f, 100.0f, 50.0f)) {
        return false;
    }
    VellumScrollbar bar;
    vellum_scrollbar_init(&bar, fixture.scroll, VELLUM_SCROLLBAR_VERTICAL,
                          make_rect(0.0f, 0.0f, 10.0f, 100.0f), -5.0f);
    bool ok = vellum_scrollbar_validate(&bar) && bar.min_thumb_length == 0.0f;

    VellumScrollbarGeometry geometry = {0};
    vellum_scrollbar_geometry(NULL, &geometry);
    vellum_scrollbar_geometry(&bar, NULL);
    vellum_scrollbar_init(NULL, fixture.scroll, VELLUM_SCROLLBAR_VERTICAL,
                          make_rect(0.0f, 0.0f, 10.0f, 10.0f), 0.0f);
    vellum_scrollbar_init(&bar, NULL, VELLUM_SCROLLBAR_VERTICAL,
                          make_rect(0.0f, 0.0f, 10.0f, 10.0f), 0.0f);
    ok = !vellum_scrollbar_validate(&bar) && ok;
    ok = vellum_scrollbar_hit_test(&bar, 5.0f, 5.0f) == VELLUM_SCROLLBAR_HIT_NONE && ok;
    vellum_scrollbar_scroll_to_pointer(&bar, 5.0f, true);
    vellum_scrollbar_page(&bar, 1);
    ok = vellum_scrollbar_validate(NULL) == false && ok;

    vellum_scrollbar_init(&bar, fixture.scroll, (VellumScrollbarOrientation)99,
                          make_rect(0.0f, 0.0f, 10.0f, 10.0f), 0.0f);
    ok = !vellum_scrollbar_validate(&bar) && ok;
    vellum_scrollbar_init(&bar, fixture.scroll, VELLUM_SCROLLBAR_VERTICAL,
                          make_rect(0.0f, 0.0f, -1.0f, 10.0f), 0.0f);
    ok = !vellum_scrollbar_validate(&bar) && ok;
    vellum_scrollbar_init(&bar, fixture.scroll, VELLUM_SCROLLBAR_VERTICAL,
                          make_rect(0.0f, 0.0f, 10.0f, 10.0f), NAN);
    ok = !vellum_scrollbar_validate(&bar) && ok;

    fixture_destroy(&fixture);
    return ok;
}

bool test_scrollbar(void)
{
    if (!vertical_geometry()) {
        fprintf(stderr, "scrollbar vertical_geometry failed\n");
        return false;
    }
    if (!horizontal_geometry()) {
        fprintf(stderr, "scrollbar horizontal_geometry failed\n");
        return false;
    }
    if (!no_scroll_and_minimum()) {
        fprintf(stderr, "scrollbar no_scroll_and_minimum failed\n");
        return false;
    }
    if (!hit_testing()) {
        fprintf(stderr, "scrollbar hit_testing failed\n");
        return false;
    }
    if (!dragging_and_paging()) {
        fprintf(stderr, "scrollbar dragging_and_paging failed\n");
        return false;
    }
    if (!invariant_sweep()) {
        fprintf(stderr, "scrollbar invariant_sweep failed\n");
        return false;
    }
    if (!null_and_validation()) {
        fprintf(stderr, "scrollbar null_and_validation failed\n");
        return false;
    }
    return true;
}
