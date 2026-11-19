/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#include <SDL3/SDL.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tests/benchmarks/benchmarks.h"
#include "vellum/config/theme.h"
#include "vellum/core/arena.h"
#include "vellum/core/error.h"
#include "vellum/graphics/draw_list.h"
#include "vellum/graphics/font.h"
#include "vellum/graphics/line_cache.h"
#include "vellum/graphics/renderer.h"
#include "vellum/graphics/shape.h"
#include "vellum/graphics/text_layout.h"
#include "vellum/platform/window.h"

#ifndef VELLUM_FONT_DIR
#define VELLUM_FONT_DIR "."
#endif

#ifndef VELLUM_CONFIG_DIR
#define VELLUM_CONFIG_DIR "."
#endif

/* Fixed configuration recorded with every result (ADR-004). */
#define BENCH_VIEW_WIDTH 1280.0f
#define BENCH_VIEW_HEIGHT 720.0f
#define BENCH_EDITOR_X 48.0f
#define BENCH_EDITOR_Y 36.0f
#define BENCH_EDITOR_WIDTH 1184.0f
#define BENCH_EDITOR_HEIGHT 636.0f
#define BENCH_SIDEBAR_WIDTH 220.0f
#define BENCH_STATUS_HEIGHT 26.0f
#define BENCH_ROWS 40u
#define BENCH_LINE_HEIGHT 16.0f
#define BENCH_FONT_SIZE 14u
#define BENCH_DISPLAY_SCALE 1.0f
#define BENCH_ATLAS_PAGE 1024u
#define BENCH_LINE_CACHE 256u
#define BENCH_ARENA_CHUNK (1u * 1024u * 1024u)
#define BENCH_WARMUP_FRAMES 5u
#define BENCH_MEASURE_FRAMES 30u
#define BENCH_INVESTIGATION_MS 8.0

/* ASCII-only so the shaped glyph count matches the byte count exactly. */
static const char bench_line_base[] = "const int value = compute(42); abcXYZ0123456789_ ; ";

typedef struct {
    const char *label;
    size_t columns;
    size_t glyph_target;
} BenchWorkload;

typedef struct {
    VellumWindow *window;
    VellumRenderer *renderer;
    VellumDrawList *list;
    VellumFontSystem *fonts;
    VellumShaper *shaper;
    VellumLineCache *cache;
    VellumArena *arena;
    VellumTheme theme;
    VellumTextureHandle atlas;
    char **templates;
    size_t template_count;
    uint64_t atlas_uploads;
    uint64_t glyphs;
} BenchContext;

typedef struct {
    uint64_t rasterizations;
    uint64_t line_hits;
    uint64_t line_misses;
    uint64_t line_evictions;
    uint64_t arena_resets;
    size_t arena_capacity;
    size_t arena_used;
    size_t arena_allocations;
    uint32_t draw_calls;
    size_t vertex_capacity;
    size_t quads;
    size_t rects;
    uint64_t atlas_uploads;
    uint64_t glyphs;
} BenchSnapshot;

static bool bench_headless(void)
{
    const char *value = SDL_getenv("VELLUM_BENCH_HEADLESS");
    return value != NULL && strcmp(value, "1") == 0;
}

static double elapsed_ms(uint64_t from, uint64_t to, uint64_t frequency)
{
    if (frequency == 0u || to < from) {
        return 0.0;
    }
    return (double)(to - from) * 1000.0 / (double)frequency;
}

static bool add_bundled_font(VellumFontSystem *system, const char *name, VellumErrorInfo *error)
{
    char path[512];
    const int written = snprintf(path, sizeof(path), "%s/%s", VELLUM_FONT_DIR, name);
    if (written < 0 || (size_t)written >= sizeof(path)) {
        vellum_error_set(error, VELLUM_ERR_INTERNAL, "font path is too long");
        return false;
    }
    return vellum_font_system_add_face(system, path, error) != VELLUM_FONT_NO_FACE;
}

static char *make_template(size_t columns, uint32_t row)
{
    char *text = malloc(columns + 1u);
    if (text == NULL) {
        return NULL;
    }
    size_t position = 0u;
    if (columns >= 4u) {
        const int written = snprintf(text, columns + 1u, "%02u ", row);
        if (written < 0 || (size_t)written > columns) {
            free(text);
            return NULL;
        }
        position = (size_t)written;
    }
    while (position < columns) {
        const size_t remaining = columns - position;
        const size_t length = sizeof(bench_line_base) - 1u;
        const size_t chunk = remaining < length ? remaining : length;
        memcpy(text + position, bench_line_base, chunk);
        position += chunk;
    }
    text[columns] = '\0';
    return text;
}

static bool bench_init(BenchContext *bench, size_t columns, VellumErrorInfo *error)
{
    memset(bench, 0, sizeof(*bench));
    bench->window = vellum_window_create((int)BENCH_VIEW_WIDTH, (int)BENCH_VIEW_HEIGHT,
                                         "vellum benchmark", error);
    if (bench->window == NULL) {
        return false;
    }
    bench->renderer = vellum_renderer_create(vellum_window_handle(bench->window), error);
    if (bench->renderer == NULL) {
        return false;
    }
    if (!vellum_renderer_set_vsync(bench->renderer, 0)) {
        vellum_error_set_sdl(error, VELLUM_ERR_PLATFORM, "SDL_SetRenderVSync");
        return false;
    }

    char theme_path[512];
    const int written =
        snprintf(theme_path, sizeof(theme_path), "%s/themes/dark.json", VELLUM_CONFIG_DIR);
    VellumErrorInfo theme_error;
    if (written < 0 || (size_t)written >= sizeof(theme_path) ||
        !vellum_theme_load(&bench->theme, theme_path, &theme_error)) {
        bench->theme = vellum_theme_dark();
    }

    bench->fonts = vellum_font_system_create(BENCH_ATLAS_PAGE, error);
    if (bench->fonts == NULL ||
        !add_bundled_font(bench->fonts, "JetBrainsMono-Variable.ttf", error) ||
        !add_bundled_font(bench->fonts, "NotoSansSC-Variable.ttf", error) ||
        !add_bundled_font(bench->fonts, "NotoSansArabic-Variable.ttf", error)) {
        return false;
    }
    bench->shaper = vellum_shaper_create(bench->fonts, error);
    bench->cache = vellum_line_cache_create(bench->fonts, bench->shaper, BENCH_LINE_CACHE, error);
    bench->arena = vellum_arena_create(BENCH_ARENA_CHUNK);
    if (bench->shaper == NULL || bench->cache == NULL || bench->arena == NULL) {
        vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not allocate benchmark caches");
        return false;
    }

    uint32_t width = 0u;
    uint32_t height = 0u;
    const uint8_t *pixels = vellum_font_system_atlas_pixels(bench->fonts, &width, &height);
    bench->atlas = vellum_renderer_create_texture(bench->renderer, width, height, pixels, error);
    if (bench->atlas == VELLUM_TEXTURE_NONE) {
        return false;
    }
    vellum_font_system_atlas_clear_dirty(bench->fonts);

    const size_t quad_capacity = columns * BENCH_ROWS + 128u;
    bench->list = vellum_draw_list_create(64u, quad_capacity);
    if (bench->list == NULL) {
        vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not allocate benchmark draw list");
        return false;
    }

    bench->templates = calloc(BENCH_ROWS, sizeof(*bench->templates));
    if (bench->templates == NULL) {
        vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not allocate line templates");
        return false;
    }
    for (uint32_t row = 0u; row < BENCH_ROWS; ++row) {
        bench->templates[row] = make_template(columns, row);
        if (bench->templates[row] == NULL) {
            vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not build a line template");
            return false;
        }
    }
    bench->template_count = BENCH_ROWS;
    return true;
}

static void bench_destroy(BenchContext *bench)
{
    if (bench->templates != NULL) {
        for (size_t i = 0u; i < bench->template_count; ++i) {
            free(bench->templates[i]);
        }
    }
    free(bench->templates);
    if (bench->list != NULL) {
        vellum_draw_list_destroy(bench->list);
    }
    if (bench->atlas != VELLUM_TEXTURE_NONE && bench->renderer != NULL) {
        vellum_renderer_destroy_texture(bench->renderer, bench->atlas);
    }
    if (bench->cache != NULL) {
        vellum_line_cache_destroy(bench->cache);
    }
    if (bench->shaper != NULL) {
        vellum_shaper_destroy(bench->shaper);
    }
    if (bench->fonts != NULL) {
        vellum_font_system_destroy(bench->fonts);
    }
    if (bench->arena != NULL) {
        vellum_arena_destroy(bench->arena);
    }
    if (bench->renderer != NULL) {
        vellum_renderer_destroy(bench->renderer);
    }
    if (bench->window != NULL) {
        vellum_window_destroy(bench->window);
    }
}

static bool bench_frame(BenchContext *bench, VellumErrorInfo *error, double *layout_ms,
                        double *upload_ms, double *submit_ms)
{
    const uint64_t frequency = SDL_GetPerformanceFrequency();
    const uint64_t start = SDL_GetPerformanceCounter();

    SDL_PumpEvents();
    vellum_arena_reset(bench->arena);
    vellum_draw_list_clear(bench->list);
    vellum_draw_list_set_clear_color(bench->list,
                                     vellum_theme_color(&bench->theme, VELLUM_THEME_BACKGROUND));

    /* Representative editor chrome: sidebar, status bar, current-line highlight,
     * selection, and alternating syntax colours across the visible lines. */
    if (!vellum_draw_list_rect(bench->list, 0.0f, 0.0f, BENCH_SIDEBAR_WIDTH,
                               BENCH_VIEW_HEIGHT - BENCH_STATUS_HEIGHT,
                               vellum_theme_color(&bench->theme, VELLUM_THEME_SIDEBAR)) ||
        !vellum_draw_list_rect(bench->list, 0.0f, BENCH_VIEW_HEIGHT - BENCH_STATUS_HEIGHT,
                               BENCH_VIEW_WIDTH, BENCH_STATUS_HEIGHT,
                               vellum_theme_color(&bench->theme, VELLUM_THEME_TAB_BAR)) ||
        !vellum_draw_list_rect(bench->list, BENCH_EDITOR_X,
                               BENCH_EDITOR_Y + 12.0f * BENCH_LINE_HEIGHT, BENCH_EDITOR_WIDTH,
                               BENCH_LINE_HEIGHT,
                               vellum_theme_color(&bench->theme, VELLUM_THEME_TAB_BAR)) ||
        !vellum_draw_list_rect(bench->list, BENCH_EDITOR_X + 48.0f,
                               BENCH_EDITOR_Y + 5.0f * BENCH_LINE_HEIGHT + 2.0f, 180.0f,
                               BENCH_LINE_HEIGHT - 4.0f,
                               vellum_theme_color(&bench->theme, VELLUM_THEME_ACCENT))) {
        return false;
    }

    const VellumThemeToken palette[] = {VELLUM_THEME_TEXT, VELLUM_THEME_ACCENT,
                                        VELLUM_THEME_TAB_BAR, VELLUM_THEME_SIDEBAR};
    uint64_t glyphs = 0u;
    for (uint32_t row = 0u; row < BENCH_ROWS; ++row) {
        /* Per-frame text is built in the frame arena, as a live editor would. */
        char *line = vellum_arena_strdup(bench->arena, bench->templates[row]);
        if (line == NULL) {
            vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "frame arena exhausted");
            return false;
        }
        VellumTextLayoutParams params;
        memset(&params, 0, sizeof(params));
        params.texture = bench->atlas;
        params.color = vellum_theme_color(&bench->theme, palette[row % 4u]);
        params.x = BENCH_EDITOR_X;
        params.baseline_y = BENCH_EDITOR_Y + (float)(row + 1u) * BENCH_LINE_HEIGHT;
        params.direction = VELLUM_TEXT_LTR;
        VellumTextLayoutMetrics metrics;
        if (!vellum_line_cache_layout(bench->cache, bench->list, &params, line, strlen(line),
                                      BENCH_FONT_SIZE, &metrics, error)) {
            return false;
        }
        glyphs += metrics.glyph_count;
    }
    bench->glyphs = glyphs;
    const uint64_t after_layout = SDL_GetPerformanceCounter();

    if (vellum_font_system_atlas_is_dirty(bench->fonts)) {
        uint32_t width = 0u;
        uint32_t height = 0u;
        const uint8_t *pixels = vellum_font_system_atlas_pixels(bench->fonts, &width, &height);
        if (!vellum_renderer_update_texture(bench->renderer, bench->atlas, pixels, error)) {
            return false;
        }
        vellum_font_system_atlas_clear_dirty(bench->fonts);
        ++bench->atlas_uploads;
    }
    const uint64_t after_upload = SDL_GetPerformanceCounter();

    const bool ok = vellum_renderer_submit(bench->renderer, bench->list, error);
    const uint64_t end = SDL_GetPerformanceCounter();
    if (layout_ms != NULL) {
        *layout_ms = elapsed_ms(start, after_layout, frequency);
    }
    if (upload_ms != NULL) {
        *upload_ms = elapsed_ms(after_layout, after_upload, frequency);
    }
    if (submit_ms != NULL) {
        *submit_ms = elapsed_ms(after_upload, end, frequency);
    }
    return ok;
}

static BenchSnapshot bench_snapshot(const BenchContext *bench)
{
    BenchSnapshot snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    VellumFontStats font_stats = {0u, 0u, 0u};
    VellumLineCacheStats cache_stats = {0u, 0u, 0u, 0u, 0u, 0u, 0u};
    (void)vellum_font_system_stats(bench->fonts, &font_stats);
    (void)vellum_line_cache_stats(bench->cache, &cache_stats);
    snapshot.rasterizations = font_stats.rasterizations;
    snapshot.line_hits = cache_stats.hits;
    snapshot.line_misses = cache_stats.misses;
    snapshot.line_evictions = cache_stats.evictions;
    snapshot.arena_resets = vellum_arena_reset_count(bench->arena);
    snapshot.arena_capacity = vellum_arena_capacity(bench->arena);
    snapshot.arena_used = vellum_arena_used(bench->arena);
    snapshot.arena_allocations = vellum_arena_allocation_count(bench->arena);
    snapshot.draw_calls = vellum_renderer_draw_calls(bench->renderer);
    snapshot.vertex_capacity = vellum_renderer_vertex_capacity(bench->renderer);
    snapshot.quads = 0u;
    (void)vellum_draw_list_quads(bench->list, &snapshot.quads);
    snapshot.rects = 0u;
    (void)vellum_draw_list_rects(bench->list, &snapshot.rects);
    snapshot.atlas_uploads = bench->atlas_uploads;
    snapshot.glyphs = bench->glyphs;
    return snapshot;
}

static int compare_double(const void *left, const void *right)
{
    const double a = *(const double *)left;
    const double b = *(const double *)right;
    return a < b ? -1 : (a > b ? 1 : 0);
}

static void report_distribution(double *times, size_t count)
{
    qsort(times, count, sizeof(*times), compare_double);
    double total = 0.0;
    for (size_t i = 0u; i < count; ++i) {
        total += times[i];
    }
    const double mean = total / (double)count;
    double variance = 0.0;
    for (size_t i = 0u; i < count; ++i) {
        const double delta = times[i] - mean;
        variance += delta * delta;
    }
    const double p50 = times[count / 2u];
    const size_t p95_index = (95u * count) / 100u;
    const double p95 = times[p95_index < count ? p95_index : count - 1u];
    printf("  min / p50 / p95 / max: %.3f / %.3f / %.3f / %.3f ms\n", times[0u], p50, p95,
           times[count - 1u]);
    printf("  mean / stddev:         %.3f / %.3f ms\n", mean,
           variance > 0.0 ? sqrt(variance / (double)count) : 0.0);
}

static void report_phase(const char *label, const double *times, size_t count, double frame_mean)
{
    double total = 0.0;
    double maximum = 0.0;
    for (size_t i = 0u; i < count; ++i) {
        total += times[i];
        if (times[i] > maximum) {
            maximum = times[i];
        }
    }
    const double mean = total / (double)count;
    printf("  %-22s %.3f ms mean, %.3f ms max (%.1f%% of frame)\n", label, mean, maximum,
           frame_mean > 0.0 ? 100.0 * mean / frame_mean : 0.0);
}

static double distribution_mean(const double *times, size_t count)
{
    double total = 0.0;
    for (size_t i = 0u; i < count; ++i) {
        total += times[i];
    }
    return total / (double)count;
}

static bool run_workload(const BenchWorkload *workload)
{
    VellumErrorInfo error;
    BenchContext bench;
    if (!bench_init(&bench, workload->columns, &error)) {
        fprintf(stderr, "%s: init failed: %s\n", workload->label, vellum_error_message(&error));
        bench_destroy(&bench);
        return false;
    }

    double times[BENCH_MEASURE_FRAMES] = {0.0};
    double layout_times[BENCH_MEASURE_FRAMES] = {0.0};
    double upload_times[BENCH_MEASURE_FRAMES] = {0.0};
    double submit_times[BENCH_MEASURE_FRAMES] = {0.0};
    const uint64_t frequency = SDL_GetPerformanceFrequency();
    bool ok = true;
    for (uint32_t frame = 0u; frame < BENCH_WARMUP_FRAMES + BENCH_MEASURE_FRAMES && ok; ++frame) {
        double layout_ms = 0.0;
        double upload_ms = 0.0;
        double submit_ms = 0.0;
        const uint64_t start = SDL_GetPerformanceCounter();
        ok = bench_frame(&bench, &error, &layout_ms, &upload_ms, &submit_ms);
        const double ms = elapsed_ms(start, SDL_GetPerformanceCounter(), frequency);
        if (frame >= BENCH_WARMUP_FRAMES) {
            const size_t slot = frame - BENCH_WARMUP_FRAMES;
            times[slot] = ms;
            layout_times[slot] = layout_ms;
            upload_times[slot] = upload_ms;
            submit_times[slot] = submit_ms;
        }
    }
    if (!ok) {
        fprintf(stderr, "%s: frame failed: %s\n", workload->label, vellum_error_message(&error));
        bench_destroy(&bench);
        return false;
    }

    const BenchSnapshot after = bench_snapshot(&bench);
    /* Space glyphs have no bitmap, so they advance the pen but emit no quad. */
    ok = after.glyphs == (uint64_t)workload->glyph_target && after.quads > 0u &&
         after.quads <= after.glyphs;

    int version = SDL_GetVersion();
    printf("\n=== %s ===\n", workload->label);
    printf("configuration\n");
    printf("  viewport / editor:    %.0fx%.0f / %.0fx%.0f\n", BENCH_VIEW_WIDTH, BENCH_VIEW_HEIGHT,
           BENCH_EDITOR_WIDTH, BENCH_EDITOR_HEIGHT);
    printf("  scene:                %u rows x %zu columns = %zu glyphs; sidebar, status bar,\n",
           BENCH_ROWS, workload->columns, workload->glyph_target);
    printf("                        current-line highlight, selection, 4 syntax colours\n");
    printf("  font / scale / theme: JetBrains Mono %u px, scale %.2f, dark\n", BENCH_FONT_SIZE,
           BENCH_DISPLAY_SCALE);
    printf("  SDL / renderer / OS:  %d.%d.%d / %s / %s\n", SDL_VERSIONNUM_MAJOR(version),
           SDL_VERSIONNUM_MINOR(version), SDL_VERSIONNUM_MICRO(version),
           vellum_renderer_name(bench.renderer), SDL_GetPlatform());
#ifdef NDEBUG
    printf("  build / vsync:        release / off\n");
#else
    printf("  build / vsync:        debug / off\n");
#endif
    printf("  warmup / measured:    %u / %u frames\n", BENCH_WARMUP_FRAMES, BENCH_MEASURE_FRAMES);
    printf("  investigation limit:  %.1f ms (ADR-004; profile before architecture change)\n",
           BENCH_INVESTIGATION_MS);
    printf("frame time (CPU, ms)\n");
    report_distribution(times, BENCH_MEASURE_FRAMES);
    const double frame_mean = distribution_mean(times, BENCH_MEASURE_FRAMES);
    printf("phase breakdown (CPU, ms)\n");
    report_phase("arena + scene layout", layout_times, BENCH_MEASURE_FRAMES, frame_mean);
    report_phase("atlas upload", upload_times, BENCH_MEASURE_FRAMES, frame_mean);
    report_phase("render submit", submit_times, BENCH_MEASURE_FRAMES, frame_mean);
    printf("scene per frame\n");
    printf("  glyphs / quads:       %llu / %zu\n", (unsigned long long)after.glyphs, after.quads);
    printf("  rects / geometry:     %zu / %u\n", after.rects, after.draw_calls);
    printf("  vertex capacity:      %zu\n", after.vertex_capacity);
    printf("caches and atlas\n");
    printf("  line hits / misses:   %llu / %llu (evictions %llu)\n",
           (unsigned long long)after.line_hits, (unsigned long long)after.line_misses,
           (unsigned long long)after.line_evictions);
    printf("  rasterizations:       %llu total (uploads %llu)\n",
           (unsigned long long)after.rasterizations, (unsigned long long)after.atlas_uploads);
    printf("arena (frame lifetime)\n");
    printf("  resets:               %llu (one per rendered frame)\n",
           (unsigned long long)after.arena_resets);
    printf("  capacity / used:      %zu / %zu bytes\n", after.arena_capacity, after.arena_used);
    printf("  allocations/frame:    %zu\n", after.arena_allocations);
    printf("result:                 %s\n",
           ok ? "ok" : "INVALID GLYPH/QUAD COUNT (whitespace emits no quad)");

    bench_destroy(&bench);
    return ok;
}

bool bench_text_frame(void)
{
    if (bench_headless()) {
        (void)SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        (void)SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    const BenchWorkload workloads[] = {
        {"editor (1,000 glyphs)", 25u, 25u * BENCH_ROWS},
        {"stress (200,000 glyphs)", 5000u, 5000u * BENCH_ROWS},
    };
    bool ok = true;
    for (size_t i = 0u; i < sizeof(workloads) / sizeof(workloads[0]); ++i) {
        ok = run_workload(&workloads[i]) && ok;
    }
    SDL_Quit();
    return ok;
}
