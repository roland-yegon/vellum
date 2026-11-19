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

#include "vellum/config/config.h"
#include "vellum/config/file_watcher.h"
#include "vellum/config/theme.h"
#include "vellum/core/log.h"
#include "vellum/core/trace.h"
#include "vellum/graphics/draw_list.h"
#include "vellum/graphics/font.h"
#include "vellum/graphics/line_cache.h"
#include "vellum/graphics/renderer.h"
#include "vellum/graphics/shape.h"
#include "vellum/graphics/text_layout.h"
#include "vellum/platform/events.h"
#include "vellum/platform/paths.h"
#include "vellum/platform/signals.h"
#include "vellum/platform/window.h"
#include "vellum/ui/panels.h"
#include "vellum/ui/widget.h"

#define ATLAS_PAGE_SIZE 512u
#define VELLUM_MAX_PIXEL_SIZE 512u
#define VELLUM_MAX_DISPLAY_SCALE 1000.0f

#ifndef VELLUM_FONT_DIR
#define VELLUM_FONT_DIR "."
#endif

#ifndef VELLUM_CONFIG_DIR
#define VELLUM_CONFIG_DIR "."
#endif

struct VellumApp {
    VellumWindow *window;
    VellumRenderer *renderer;
    VellumDrawList *draw_list;
    VellumSettings settings;
    VellumTheme theme;
    VellumFontSystem *fonts;
    VellumShaper *shaper;
    VellumLineCache *line_cache;
    VellumTextureHandle atlas_texture;
    VellumUiState ui_state;
    VellumWidget *ui_root;
    VellumFileWatcher *watcher;
    char settings_user_path[512];
    char theme_dark_user_path[512];
    char theme_light_user_path[512];
    VellumTrace trace;
    uint32_t app_event_base;
    uint64_t atlas_uploads;
    uint32_t text_pixel_size;
    float display_scale;
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
    event.user.code = VELLUM_APPMSG_STARTUP;
    if (!SDL_PushEvent(&event)) {
        vellum_error_set_sdl(error, VELLUM_ERR_PLATFORM, "SDL_PushEvent");
        return false;
    }
    return true;
}

static void load_settings(VellumApp *app)
{
    VellumErrorInfo error;
    vellum_error_init(&error);
    app->settings = vellum_settings_default();

    char path[512];
    const int written = snprintf(path, sizeof(path), "%s/settings.json", VELLUM_CONFIG_DIR);
    const bool path_ok = written >= 0 && (size_t)written < sizeof(path);
    if (!path_ok || !vellum_settings_load(&app->settings, path, &error)) {
        VELLUM_LOG_WARN("config", "shipped settings unavailable (%s); using compiled-in defaults",
                        path_ok ? vellum_error_message(&error) : "path is too long");
    } else {
        VELLUM_LOG_INFO("config", "settings loaded from %s", path);
    }

    if (app->settings_user_path[0] == '\0' || !vellum_path_exists(app->settings_user_path)) {
        return;
    }
    if (!vellum_settings_load(&app->settings, app->settings_user_path, &error)) {
        VELLUM_LOG_WARN("config", "user settings ignored (%s); keeping shipped values",
                        vellum_error_message(&error));
    } else {
        VELLUM_LOG_INFO("config", "settings overridden by %s", app->settings_user_path);
    }
}

static bool theme_name_is_safe(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return false;
    }
    for (const char *c = name; *c != '\0'; ++c) {
        const unsigned char value = (unsigned char)*c;
        if (!((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') || value == '-' || value == '_')) {
            return false;
        }
    }
    return true;
}

static const char *user_theme_path(VellumApp *app, const char *name)
{
    if (strcmp(name, "light") == 0) {
        return app->theme_light_user_path;
    }
    if (strcmp(name, "dark") == 0) {
        return app->theme_dark_user_path;
    }
    return NULL;
}

static void load_theme(VellumApp *app, const char *name)
{
    VellumErrorInfo error;
    vellum_error_init(&error);

    char path[512];
    int written = -1;
    if (theme_name_is_safe(name)) {
        written = snprintf(path, sizeof(path), "%s/themes/%s.json", VELLUM_CONFIG_DIR, name);
    }
    const bool path_ok = written >= 0 && (size_t)written < sizeof(path);
    if (!path_ok || !vellum_theme_load(&app->theme, path, &error)) {
        VELLUM_LOG_WARN("config", "theme '%s' unavailable (%s); using built-in fallback", name,
                        path_ok ? vellum_error_message(&error) : "unsafe theme name");
        app->theme = strcmp(name, "light") == 0 ? vellum_theme_light() : vellum_theme_dark();
    } else {
        VELLUM_LOG_INFO("config", "theme '%s' loaded from %s", vellum_theme_name(&app->theme),
                        path);
    }

    const char *override = user_theme_path(app, name);
    if (override == NULL || override[0] == '\0' || !vellum_path_exists(override)) {
        return;
    }
    VellumTheme override_theme = app->theme;
    if (!vellum_theme_load(&override_theme, override, &error)) {
        VELLUM_LOG_WARN("config", "user theme '%s' ignored (%s); keeping shipped values", name,
                        vellum_error_message(&error));
        return;
    }
    app->theme = override_theme;
    VELLUM_LOG_INFO("config", "theme '%s' overridden by %s", vellum_theme_name(&app->theme),
                    override);
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
    app->shaper = vellum_shaper_create(app->fonts, error);
    if (app->shaper == NULL) {
        return false;
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

static bool compute_user_paths(VellumApp *app, VellumErrorInfo *error)
{
    if (!vellum_paths_user_config(app->settings_user_path, sizeof(app->settings_user_path),
                                  "settings.json", error) ||
        !vellum_paths_user_config(app->theme_dark_user_path, sizeof(app->theme_dark_user_path),
                                  "themes/dark.json", error) ||
        !vellum_paths_user_config(app->theme_light_user_path, sizeof(app->theme_light_user_path),
                                  "themes/light.json", error)) {
        vellum_error_set(error, VELLUM_ERR_INTERNAL, "could not compose user config paths");
        return false;
    }
    return true;
}

static bool setup_watcher(VellumApp *app, VellumErrorInfo *error)
{
    app->watcher = vellum_file_watcher_create(app->app_event_base, VELLUM_APPMSG_CONFIG_RELOAD,
                                              VELLUM_WATCH_DEFAULT_POLL_MS, error);
    if (app->watcher == NULL) {
        return false;
    }
    if (!vellum_file_watcher_add(app->watcher, app->settings_user_path, error) ||
        !vellum_file_watcher_add(app->watcher, app->theme_dark_user_path, error) ||
        !vellum_file_watcher_add(app->watcher, app->theme_light_user_path, error)) {
        return false;
    }
    VELLUM_LOG_INFO("config", "watching user config files for changes");
    return true;
}

static void reload_config(VellumApp *app)
{
    load_settings(app);
    load_theme(app, app->settings.theme);
    /* A new font size changes the rasterization size even if the scale is fixed. */
    vellum_app_set_display_scale(app, app->display_scale);
    if (app->ui_root != NULL) {
        vellum_ui_apply_settings(app->ui_root, &app->ui_state, app->settings.show_sidebar,
                                 app->settings.show_status_bar);
    }
    VELLUM_LOG_INFO("config", "configuration reloaded (theme=%s, font size=%u)",
                    vellum_theme_name(&app->theme), app->settings.font_size);
    app->dirty = true;
}

uint32_t vellum_app_pixel_size_for_scale(uint32_t font_size, float scale)
{
    const float scaled = (float)font_size * scale + 0.5f;
    /* Reject NaN and nonpositive results so layout never receives a bad size. */
    if (!(scaled >= 1.0f)) {
        return 1u;
    }
    if (scaled >= (float)VELLUM_MAX_PIXEL_SIZE) {
        return VELLUM_MAX_PIXEL_SIZE;
    }
    return (uint32_t)scaled;
}

static float window_display_scale(const VellumApp *app)
{
    if (app->window != NULL) {
        const float queried = SDL_GetWindowDisplayScale(vellum_window_handle(app->window));
        if (queried > 0.0f && queried < VELLUM_MAX_DISPLAY_SCALE) {
            return queried;
        }
    }
    return 1.0f;
}

float vellum_app_display_scale(const VellumApp *app)
{
    return app == NULL ? 1.0f : app->display_scale;
}

uint32_t vellum_app_text_pixel_size(const VellumApp *app)
{
    return app == NULL ? 0u : app->text_pixel_size;
}

void vellum_app_set_display_scale(VellumApp *app, float scale)
{
    if (app == NULL) {
        return;
    }
    if (!(scale > 0.0f) || scale >= VELLUM_MAX_DISPLAY_SCALE) {
        /* Tests and settings can override the scale; nonpositive means "query SDL". */
        scale = window_display_scale(app);
    }
    const uint32_t next = vellum_app_pixel_size_for_scale(app->settings.font_size, scale);
    if (app->display_scale == scale && app->text_pixel_size == next) {
        return;
    }
    const float previous_scale = app->display_scale;
    const uint32_t previous_size = app->text_pixel_size;
    app->display_scale = scale;
    app->text_pixel_size = next;
    app->dirty = true;
    VELLUM_LOG_INFO("app", "display scale %.2f -> %.2f, text pixel size %u -> %u", previous_scale,
                    scale, previous_size, next);
}

static void refresh_display_scale(VellumApp *app)
{
    vellum_app_set_display_scale(app, window_display_scale(app));
}

static bool prepare_line_cache(VellumApp *app, VellumErrorInfo *error)
{
    app->line_cache = vellum_line_cache_create(app->fonts, app->shaper, 256u, error);
    return app->line_cache != NULL;
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
    app->settings = vellum_settings_default();
    app->theme = vellum_theme_dark();
    app->fonts = NULL;
    app->shaper = NULL;
    app->line_cache = NULL;
    app->atlas_texture = VELLUM_TEXTURE_NONE;
    app->ui_state = vellum_ui_state_default();
    app->ui_root = NULL;
    app->watcher = NULL;
    app->settings_user_path[0] = '\0';
    app->theme_dark_user_path[0] = '\0';
    app->theme_light_user_path[0] = '\0';
    app->app_event_base = 0u;
    app->atlas_uploads = 0u;
    app->text_pixel_size = 0u;
    app->display_scale = 1.0f;
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
    if (!compute_user_paths(app, error)) {
        goto fail;
    }
    load_settings(app);
    load_theme(app, app->settings.theme);
    refresh_display_scale(app);
    app->ui_root = vellum_ui_create(&app->ui_state, error);
    if (app->ui_root == NULL) {
        goto fail;
    }
    vellum_ui_apply_settings(app->ui_root, &app->ui_state, app->settings.show_sidebar,
                             app->settings.show_status_bar);
    if (!prepare_fonts(app, error)) {
        goto fail;
    }
    if (!prepare_line_cache(app, error)) {
        goto fail;
    }
    if (!setup_watcher(app, error)) {
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
    VELLUM_LOG_INFO("app", "theme: %s, font size: %u", vellum_theme_name(&app->theme),
                    app->settings.font_size);
    VELLUM_LOG_INFO("app", "atlas texture handle: %u", app->atlas_texture);
    VELLUM_LOG_INFO("app", "display scale: %.2f, text pixel size: %u", app->display_scale,
                    app->text_pixel_size);
    VELLUM_LOG_INFO("app", "window: %dx%d", app->width, app->height);
    return app;

fail:
    vellum_app_destroy(app);
    return NULL;
}

static void app_ui_context(VellumApp *app, VellumUiContext *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->draw_list = app->draw_list;
    ctx->theme = &app->theme;
    ctx->shaper = app->shaper;
    ctx->fonts = app->fonts;
    ctx->atlas_texture = app->atlas_texture;
    /* Panel labels and the caret rasterize at the effective (scaled) size. */
    ctx->font_size = app->text_pixel_size;
}

static void handle_input(VellumApp *app, const VellumInput *input)
{
    if (app->ui_root != NULL) {
        VellumUiContext ui_ctx;
        app_ui_context(app, &ui_ctx);
        if (vellum_widget_dispatch_event(app->ui_root, input, &ui_ctx)) {
            app->dirty = true;
        }
    }

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
    case VELLUM_INPUT_SCALE_CHANGED:
        /* SDL sends no value with this event; query the window for the new scale. */
        refresh_display_scale(app);
        app->dirty = true;
        break;
    case VELLUM_INPUT_APP_MESSAGE:
        if (input->data.message.type == VELLUM_APPMSG_CONFIG_RELOAD) {
            reload_config(app);
        } else {
            VELLUM_LOG_INFO("input", "application message %u", input->data.message.type);
            app->dirty = true;
        }
        break;
    case VELLUM_INPUT_TEXT:
        if (input->data.text.text[0] == 't' || input->data.text.text[0] == 'T') {
            const char *next = strcmp(app->settings.theme, "light") == 0 ? "dark" : "light";
            memcpy(app->settings.theme, next, strlen(next) + 1u);
            load_theme(app, next);
            VellumErrorInfo error;
            if (app->settings_user_path[0] != '\0' &&
                !vellum_settings_save(&app->settings, app->settings_user_path, &error)) {
                VELLUM_LOG_WARN("config", "could not persist theme: %s",
                                vellum_error_message(&error));
            }
            app->dirty = true;
            VELLUM_LOG_INFO("config", "theme switched to %s", vellum_theme_name(&app->theme));
        }
        break;
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

static bool layout_demo_line(VellumApp *app, const char *text, float x, float baseline_y,
                             float box_width, VellumThemeToken token, uint32_t font_size,
                             VellumErrorInfo *error)
{
    VellumTextLayoutParams params;
    memset(&params, 0, sizeof(params));
    params.texture = app->atlas_texture;
    params.color = vellum_theme_color(&app->theme, token);
    params.x = x;
    params.baseline_y = baseline_y;
    params.box_width = box_width;
    params.auto_direction = true; /* ICU first-strong detection drives RTL shaping. */
    /* The cache key includes the effective pixel size, so a scale change
     * re-rasterizes glyphs and re-lays out every line automatically. */
    const uint32_t pixel_size = vellum_app_pixel_size_for_scale(font_size, app->display_scale);
    return vellum_line_cache_layout(app->line_cache, app->draw_list, &params, text, strlen(text),
                                    pixel_size, NULL, error);
}

static bool build_demo_text(VellumApp *app, VellumBounds area, VellumErrorInfo *error)
{
    static const char title[] = "Vellum Text Foundation";
    static const char subtitle[] = "ICU + HarfBuzz + FreeType + SDL3";
    static const char latin_label[] = "LATIN";
    static const char latin[] = "Hello, Vellum! != =>";
    static const char cjk_label[] = "CJK";
    static const char cjk[] = "\xE4\xB8\xAD\xE6\x96\x87\xE7\xBC\x96\xE8\xBE\x91\xE5\x99\xA8"
                              "\xE6\xB5\x8B\xE8\xAF\x95";
    static const char arabic_label[] = "ARABIC";
    static const char arabic[] = "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7\x20\xD8\xA8\xD8\xA7"
                                 "\xD9\x84\xD8\xB9\xD8\xA7\xD9\x84\xD9\x85";
    static const char fallback_label[] = "FALLBACK";
    static const char fallback[] = "Vellum \xE4\xB8\xAD\xE6\x96\x87 editor";
    static const char hint[] = "T: theme    Esc: quit";

    const uint32_t size = app->settings.font_size;
    const bool wide = area.width > 360.0f;
    const float label_x = area.x + 16.0f;
    const float text_x = wide ? area.x + 150.0f : label_x;
    const float right_edge = area.x + area.width - 16.0f;
    const float text_box = right_edge > text_x ? right_edge - text_x : 0.0f;
    const float y = area.y + 48.0f;

    if (!layout_demo_line(app, title, label_x, y, 0.0f, VELLUM_THEME_ACCENT, size + 4u, error) ||
        !layout_demo_line(app, subtitle, label_x, y + 28.0f, 0.0f, VELLUM_THEME_TEXT, size,
                          error)) {
        return false;
    }

    const struct {
        const char *label;
        const char *text;
        float offset;
        float box_width;
    } rows[] = {{latin_label, latin, 88.0f, 0.0f},
                {cjk_label, cjk, 136.0f, 0.0f},
                {arabic_label, arabic, 184.0f, text_box},
                {fallback_label, fallback, 232.0f, 0.0f}};
    for (size_t i = 0u; i < sizeof(rows) / sizeof(rows[0]); ++i) {
        if (wide && !layout_demo_line(app, rows[i].label, label_x, y + rows[i].offset, 0.0f,
                                      VELLUM_THEME_ACCENT, size, error)) {
            return false;
        }
        if (!layout_demo_line(app, rows[i].text, text_x, y + rows[i].offset, rows[i].box_width,
                              VELLUM_THEME_TEXT, size, error)) {
            return false;
        }
    }
    return layout_demo_line(app, hint, label_x, y + 312.0f, 0.0f, VELLUM_THEME_TEXT, size, error);
}

static bool build_scene(VellumApp *app, VellumErrorInfo *error)
{
    VellumDrawList *list = app->draw_list;
    vellum_draw_list_clear(list);
    vellum_draw_list_set_clear_color(list,
                                     vellum_theme_color(&app->theme, VELLUM_THEME_BACKGROUND));

    VellumUiContext ctx;
    app_ui_context(app, &ctx);
    const VellumBounds window = {0.0f, 0.0f, (float)app->width, (float)app->height};
    vellum_widget_layout(app->ui_root, window, &ctx);
    if (!vellum_widget_paint(app->ui_root, &ctx, error)) {
        return false;
    }

    /* Demo text is positioned relative to the editor panel. The renderer's scissor is
     * draw-list wide, so clipping here would also clip the surrounding panels. */
    const VellumBounds editor = vellum_widget_bounds(vellum_ui_editor(app->ui_root));
    if (editor.width > 0.0f && editor.height > 0.0f && !build_demo_text(app, editor, error)) {
        return false;
    }

    /* Shaping and rasterizing happens first: it may add glyphs to the atlas, so the
     * texture upload must follow it but still precede the renderer submission. */
    if (vellum_font_system_atlas_is_dirty(app->fonts)) {
        uint32_t width = 0u;
        uint32_t height = 0u;
        const uint8_t *pixels = vellum_font_system_atlas_pixels(app->fonts, &width, &height);
        if (!vellum_renderer_update_texture(app->renderer, app->atlas_texture, pixels, error)) {
            return false;
        }
        vellum_font_system_atlas_clear_dirty(app->fonts);
        ++app->atlas_uploads;
        VellumFontStats stats = {0u, 0u, 0u};
        (void)vellum_font_system_stats(app->fonts, &stats);
        VELLUM_LOG_INFO("render", "atlas upload #%llu (pixel size %u, rasterizations %llu)",
                        (unsigned long long)app->atlas_uploads, app->text_pixel_size,
                        (unsigned long long)stats.rasterizations);
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
        VellumLineCacheStats cache = {0u, 0u, 0u, 0u, 0u, 0u, 0u};
        (void)vellum_line_cache_stats(app->line_cache, &cache);
        VELLUM_LOG_INFO("frame",
                        "frames=%llu avg_interval=%.3fms work=%.3fms max_interval=%.3fms "
                        "geometry_calls=%u line_hits=%llu line_misses=%llu",
                        (unsigned long long)frames, vellum_trace_average_ms(&app->trace),
                        vellum_trace_average_work_ms(&app->trace), vellum_trace_max_ms(&app->trace),
                        vellum_renderer_draw_calls(app->renderer), (unsigned long long)cache.hits,
                        (unsigned long long)cache.misses);
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

uint32_t vellum_app_event_base(const VellumApp *app)
{
    return app == NULL ? 0u : app->app_event_base;
}

void vellum_app_destroy(VellumApp *app)
{
    if (app == NULL) {
        return;
    }
    if (app->watcher != NULL) {
        vellum_file_watcher_destroy(app->watcher);
    }
    if (app->ui_root != NULL) {
        vellum_widget_destroy(app->ui_root);
    }
    if (app->draw_list != NULL) {
        vellum_draw_list_destroy(app->draw_list);
    }
    if (app->renderer != NULL) {
        vellum_renderer_destroy(app->renderer);
    }
    if (app->line_cache != NULL) {
        vellum_line_cache_destroy(app->line_cache);
    }
    if (app->shaper != NULL) {
        vellum_shaper_destroy(app->shaper);
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
