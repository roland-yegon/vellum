/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#include "vellum/app.h"

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vellum/core/log.h"
#include "vellum/core/trace.h"
#include "vellum/graphics/draw_list.h"
#include "vellum/graphics/font.h"
#include "vellum/graphics/renderer.h"
#include "vellum/platform/events.h"
#include "vellum/platform/paths.h"
#include "vellum/platform/signals.h"
#include "vellum/platform/window.h"

#define ATLAS_PAGE_SIZE 512u
#define FONT_PIXEL_SIZE 24u

#ifndef VELLUM_FONT_DIR
#define VELLUM_FONT_DIR "."
#endif

struct VellumApp {
    VellumWindow *window;
    VellumRenderer *renderer;
    VellumDrawList *draw_list;
    VellumFontSystem *fonts;
    VellumTextureHandle atlas_texture;
    const VellumGlyph *sample_glyphs[10u];
    VellumTrace trace;
    uint32_t app_event_base;
    Uint64 last_log_tick;
    int width;
    int height;
    bool quit;
    bool dirty;
};

static void log_startup(void)
{
    const int version = SDL_GetVersion();
    VELLUM_LOG_INFO("app", "SDL %d.%d.%d", SDL_VERSIONNUM_MAJOR(version),
                    SDL_VERSIONNUM_MINOR(version), SDL_VERSIONNUM_MICRO(version));

    char preferences[512];
    VellumErrorInfo error;
    if (vellum_paths_preferences(preferences, sizeof(preferences), &error)) {
        VELLUM_LOG_INFO("app", "preferences: %s", preferences);
    } else {
        VELLUM_LOG_WARN("app", "preferences unavailable: %s", vellum_error_message(&error));
    }
}

static bool post_startup_message(VellumApp *app, VellumErrorInfo *error)
{
    SDL_Event event;
    SDL_zero(event);
    event.type = app->app_event_base;
    event.user.code = 1;
    if (!SDL_PushEvent(&event)) {
        vellum_error_set_sdl(error, VELLUM_ERR_PLATFORM, "SDL_PushEvent");
        return false;
    }
    return true;
}

static bool add_bundled_font(VellumApp *app, const char *name, VellumErrorInfo *error)
{
    char path[512];
    const int written = snprintf(path, sizeof(path), "%s/%s", VELLUM_FONT_DIR, name);
    if (written < 0 || (size_t)written >= sizeof(path)) {
        vellum_error_set(error, VELLUM_ERR_INTERNAL, "font path is too long");
        return false;
    }
    return vellum_font_system_add_face(app->fonts, path, error) != VELLUM_FONT_NO_FACE;
}

static bool prepare_fonts(VellumApp *app, VellumErrorInfo *error)
{
    app->fonts = vellum_font_system_create(ATLAS_PAGE_SIZE, error);
    if (app->fonts == NULL) {
        return false;
    }
    if (!add_bundled_font(app, "JetBrainsMono-Variable.ttf", error) ||
        !add_bundled_font(app, "NotoSansSC-Variable.ttf", error) ||
        !add_bundled_font(app, "NotoSansArabic-Variable.ttf", error)) {
        return false;
    }

    static const uint32_t faces[] = {1u, 1u, 1u, 2u, 2u, 3u, 3u, 3u, 3u, 3u};
    static const uint32_t codepoints[] = {'A',     '!',     '=',     0x4E2Du, 0x6587u,
                                          0x0645u, 0x0631u, 0x062Du, 0x0628u, 0x0627u};
    for (size_t i = 0u; i < sizeof(faces) / sizeof(faces[0]); ++i) {
        app->sample_glyphs[i] = vellum_font_system_glyph_for_char(
            app->fonts, faces[i], codepoints[i], FONT_PIXEL_SIZE, error);
        if (app->sample_glyphs[i] == NULL) {
            return false;
        }
    }

    uint32_t width = 0u;
    uint32_t height = 0u;
    const uint8_t *pixels = vellum_font_system_atlas_pixels(app->fonts, &width, &height);
    app->atlas_texture =
        vellum_renderer_create_texture(app->renderer, width, height, pixels, error);
    if (app->atlas_texture == VELLUM_TEXTURE_NONE) {
        return false;
    }
    vellum_font_system_atlas_clear_dirty(app->fonts);
    return true;
}

VellumApp *vellum_app_create(VellumErrorInfo *error)
{
    vellum_error_init(error);

    VellumApp *app = malloc(sizeof(*app));
    if (app == NULL) {
        vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not allocate application state");
        return NULL;
    }
    app->window = NULL;
    app->renderer = NULL;
    app->draw_list = NULL;
    app->fonts = NULL;
    app->atlas_texture = VELLUM_TEXTURE_NONE;
    memset(app->sample_glyphs, 0, sizeof(app->sample_glyphs));
    app->app_event_base = 0u;
    app->last_log_tick = 0u;
    app->width = 0;
    app->height = 0;
    app->quit = false;
    app->dirty = true;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        vellum_error_set_sdl(error, VELLUM_ERR_PLATFORM, "SDL_Init");
        free(app);
        return NULL;
    }

    app->app_event_base = SDL_RegisterEvents(1);
    if (app->app_event_base == UINT32_MAX) {
        vellum_error_set(error, VELLUM_ERR_PLATFORM, "SDL_RegisterEvents failed");
        goto fail;
    }
    if (!vellum_signals_install(error)) {
        goto fail;
    }
    app->window = vellum_window_create(1280, 720, "Vellum", error);
    if (app->window == NULL) {
        goto fail;
    }
    app->renderer = vellum_renderer_create(vellum_window_handle(app->window), error);
    if (app->renderer == NULL) {
        goto fail;
    }
    app->draw_list = vellum_draw_list_create(64u, 16u);
    if (app->draw_list == NULL) {
        vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not allocate draw list");
        goto fail;
    }
    if (!prepare_fonts(app, error)) {
        goto fail;
    }
    if (!post_startup_message(app, error)) {
        goto fail;
    }

    vellum_trace_init(&app->trace);
    vellum_window_get_size(app->window, &app->width, &app->height);
    app->last_log_tick = SDL_GetTicks();
    log_startup();
    VELLUM_LOG_INFO("app", "renderer: %s", vellum_renderer_name(app->renderer));
    VELLUM_LOG_INFO("app", "atlas texture handle: %u", app->atlas_texture);
    VELLUM_LOG_INFO("app", "window: %dx%d", app->width, app->height);
    return app;

fail:
    vellum_app_destroy(app);
    return NULL;
}

static void handle_input(VellumApp *app, const VellumInput *input)
{
    switch (input->type) {
    case VELLUM_INPUT_QUIT:
        app->quit = true;
        break;
    case VELLUM_INPUT_KEY:
        if (input->data.key.key == VELLUM_KEY_ESCAPE) {
            VELLUM_LOG_INFO("input", "escape pressed");
            app->quit = true;
        }
        break;
    case VELLUM_INPUT_RESIZE:
        app->width = input->data.resize.width;
        app->height = input->data.resize.height;
        app->dirty = true;
        VELLUM_LOG_DEBUG("input", "resized to %dx%d", app->width, app->height);
        break;
    case VELLUM_INPUT_APP_MESSAGE:
        VELLUM_LOG_INFO("input", "application message %u", input->data.message.type);
        app->dirty = true;
        break;
    case VELLUM_INPUT_TEXT:
    case VELLUM_INPUT_POINTER_MOTION:
    case VELLUM_INPUT_POINTER_BUTTON:
    case VELLUM_INPUT_WHEEL:
    case VELLUM_INPUT_NONE:
        break;
    }
}

static void process_events(VellumApp *app)
{
    SDL_Event event;
    if (SDL_PollEvent(&event)) {
        do {
            VellumInput input;
            if (vellum_input_from_sdl(&event, app->app_event_base, &input)) {
                handle_input(app, &input);
            }
        } while (!app->quit && SDL_PollEvent(&event));
        return;
    }

    /* Bounded wait: no idle spin, and SIGINT/SIGTERM is noticed within 50 ms. */
    if (SDL_WaitEventTimeout(&event, 50)) {
        VellumInput input;
        if (vellum_input_from_sdl(&event, app->app_event_base, &input)) {
            handle_input(app, &input);
        }
    }
}

static bool build_scene(VellumApp *app, VellumErrorInfo *error)
{
    if (vellum_font_system_atlas_is_dirty(app->fonts)) {
        uint32_t width = 0u;
        uint32_t height = 0u;
        const uint8_t *pixels = vellum_font_system_atlas_pixels(app->fonts, &width, &height);
        if (!vellum_renderer_update_texture(app->renderer, app->atlas_texture, pixels, error)) {
            return false;
        }
        vellum_font_system_atlas_clear_dirty(app->fonts);
    }

    VellumDrawList *list = app->draw_list;
    vellum_draw_list_clear(list);
    vellum_draw_list_set_clear_color(list, vellum_color(0.04f, 0.05f, 0.07f, 1.0f));
    vellum_draw_list_rect(list, 0.0f, 0.0f, 180.0f, (float)app->height,
                          vellum_color(0.08f, 0.09f, 0.12f, 1.0f));
    vellum_draw_list_rect(list, 180.0f, 0.0f, (float)(app->width - 180), 24.0f,
                          vellum_color(0.11f, 0.12f, 0.16f, 1.0f));
    vellum_draw_list_rect(list, 180.0f, 80.0f, (float)(app->width - 180), 18.0f,
                          vellum_color(0.10f, 0.12f, 0.18f, 1.0f));

    for (size_t i = 0u; i < sizeof(app->sample_glyphs) / sizeof(app->sample_glyphs[0]); ++i) {
        const VellumGlyph *glyph = app->sample_glyphs[i];
        if (glyph == NULL) {
            continue;
        }
        const VellumColor tint = i % 3u == 0u ? vellum_color(0.35f, 0.70f, 0.95f, 1.0f)
                                              : vellum_color(0.85f, 0.87f, 0.92f, 1.0f);
        if (!vellum_draw_list_textured_quad(list, app->atlas_texture,
                                            196.0f + (float)i * 20.0f + glyph->bearing_x,
                                            60.0f - glyph->bearing_y, glyph->width, glyph->height,
                                            glyph->u0, glyph->v0, glyph->u1, glyph->v1, tint)) {
            return false;
        }
    }
    return true;
}

static void log_frame_stats(VellumApp *app)
{
    const Uint64 now = SDL_GetTicks();
    const uint64_t frames = vellum_trace_frame_count(&app->trace);
    if (frames == 0u || (frames != 1u && now - app->last_log_tick < 2000u)) {
        return;
    }
    if (frames == 1u) {
        VELLUM_LOG_INFO("frame", "first frame work=%.3fms geometry_calls=%u",
                        vellum_trace_last_work_ms(&app->trace),
                        vellum_renderer_draw_calls(app->renderer));
    } else {
        VELLUM_LOG_INFO("frame",
                        "frames=%llu avg_interval=%.3fms work=%.3fms max_interval=%.3fms "
                        "geometry_calls=%u",
                        (unsigned long long)frames, vellum_trace_average_ms(&app->trace),
                        vellum_trace_average_work_ms(&app->trace), vellum_trace_max_ms(&app->trace),
                        vellum_renderer_draw_calls(app->renderer));
    }
    app->last_log_tick = now;
}

int vellum_app_run(VellumApp *app)
{
    if (app == NULL) {
        return 1;
    }

    while (!app->quit) {
        int signal_number = 0;
        if (vellum_signals_pending(&signal_number)) {
            VELLUM_LOG_INFO("app", "received signal %d", signal_number);
            app->quit = true;
            break;
        }

        process_events(app);
        if (app->quit) {
            break;
        }

        if (!app->dirty || vellum_window_is_minimized(app->window)) {
            continue;
        }

        vellum_trace_begin_frame(&app->trace);

        VellumErrorInfo error;
        if (!build_scene(app, &error) ||
            !vellum_renderer_submit(app->renderer, app->draw_list, &error)) {
            VELLUM_LOG_ERROR("render", "%s", vellum_error_message(&error));
            vellum_trace_end_frame(&app->trace);
            app->quit = true;
            return 1;
        }
        vellum_trace_end_frame(&app->trace);
        app->dirty = false;
        log_frame_stats(app);
    }
    return 0;
}

void vellum_app_destroy(VellumApp *app)
{
    if (app == NULL) {
        return;
    }
    if (app->draw_list != NULL) {
        vellum_draw_list_destroy(app->draw_list);
    }
    if (app->renderer != NULL) {
        vellum_renderer_destroy(app->renderer);
    }
    if (app->fonts != NULL) {
        vellum_font_system_destroy(app->fonts);
    }
    if (app->window != NULL) {
        vellum_window_destroy(app->window);
    }
    vellum_signals_shutdown();
    SDL_Quit();
    free(app);
}
