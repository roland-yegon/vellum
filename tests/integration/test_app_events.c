/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdio.h>

#include "vellum/app.h"
#include "vellum/core/error.h"
#include "vellum/core/log.h"
#include "vellum/platform/events.h"

static bool push_event(SDL_Event *event)
{
    if (!SDL_PushEvent(event)) {
        fprintf(stderr, "SDL_PushEvent failed: %s\n", SDL_GetError());
        return false;
    }
    return true;
}

static bool test_scale_math(void)
{
    bool ok = vellum_app_pixel_size_for_scale(14u, 1.0f) == 14u &&
              vellum_app_pixel_size_for_scale(14u, 1.5f) == 21u &&
              vellum_app_pixel_size_for_scale(14u, 2.0f) == 28u &&
              vellum_app_pixel_size_for_scale(15u, 1.5f) == 23u; /* round to nearest */
    ok = ok && vellum_app_pixel_size_for_scale(0u, 1.0f) == 1u &&
         vellum_app_pixel_size_for_scale(14u, 0.0f) == 1u &&
         vellum_app_pixel_size_for_scale(14u, -1.0f) == 1u &&
         vellum_app_pixel_size_for_scale(512u, 4.0f) == 512u; /* clamped */
    if (!ok) {
        fprintf(stderr, "scale math: unexpected pixel size\n");
    }
    return ok;
}

/* `scale_override` of 0 means use the value SDL reports. */
static bool run_until_quit(const char *name, SDL_Event *first, SDL_Event *second, bool reload,
                           float scale_override)
{
    VellumErrorInfo error;
    VellumApp *app = vellum_app_create(&error);
    if (app == NULL) {
        fprintf(stderr, "%s: create failed: %s\n", name, vellum_error_message(&error));
        return false;
    }

    bool scale_ok = true;
    if (scale_override > 0.0f) {
        const uint32_t original = vellum_app_text_pixel_size(app);
        vellum_app_set_display_scale(app, scale_override);
        scale_ok = vellum_app_display_scale(app) == scale_override &&
                   vellum_app_text_pixel_size(app) == 2u * original;
        if (!scale_ok) {
            fprintf(stderr, "%s: scale override did not change text pixel size\n", name);
        }
    }

    if (!push_event(first) || (second != NULL && !push_event(second))) {
        vellum_app_destroy(app);
        return false;
    }
    if (reload) {
        SDL_Event message;
        SDL_zero(message);
        message.type = vellum_app_event_base(app);
        message.user.code = VELLUM_APPMSG_CONFIG_RELOAD;
        if (!push_event(&message)) {
            vellum_app_destroy(app);
            return false;
        }
    }

    const int result = vellum_app_run(app);
    vellum_app_destroy(app);
    if (result != 0) {
        fprintf(stderr, "%s: run returned %d\n", name, result);
    }
    return result == 0 && scale_ok;
}

int main(void)
{
    vellum_log_init();
    vellum_log_set_level(VELLUM_LOG_ERROR);

    /* Headless drivers keep this test runnable without a desktop session. */
    (void)SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    (void)SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");

    SDL_Event escape;
    SDL_zero(escape);
    escape.type = SDL_EVENT_KEY_DOWN;
    escape.key.key = SDLK_ESCAPE;

    SDL_Event close_request;
    SDL_zero(close_request);
    close_request.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;

    SDL_Event resize;
    SDL_zero(resize);
    resize.type = SDL_EVENT_WINDOW_RESIZED;
    resize.window.data1 = 1024;
    resize.window.data2 = 768;

    SDL_Event toggle_theme;
    SDL_zero(toggle_theme);
    toggle_theme.type = SDL_EVENT_TEXT_INPUT;
    toggle_theme.text.text = "t";

    /* SDL sends no scale value with this event; the app must re-query the window. */
    SDL_Event scale_changed;
    SDL_zero(scale_changed);
    scale_changed.type = SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED;

    bool ok = test_scale_math();
    ok = run_until_quit("escape", &escape, NULL, false, 0.0f) && ok;
    ok = run_until_quit("close request", &close_request, NULL, false, 0.0f) && ok;
    ok = run_until_quit("resize then escape", &resize, &escape, false, 0.0f) && ok;
    ok = run_until_quit("theme toggle then escape", &toggle_theme, &escape, false, 0.0f) && ok;
    ok = run_until_quit("config reload then escape", &escape, NULL, true, 0.0f) && ok;
    ok = run_until_quit("scale event then escape", &scale_changed, &escape, false, 0.0f) && ok;
    ok = run_until_quit("scale override then escape", &escape, NULL, false, 2.0f) && ok;

    vellum_log_shutdown();
    printf("%s\n", ok ? "app event integration: PASS" : "app event integration: FAIL");
    return ok ? 0 : 1;
}
