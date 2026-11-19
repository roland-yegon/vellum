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

static bool push_event(SDL_Event *event)
{
    if (!SDL_PushEvent(event)) {
        fprintf(stderr, "SDL_PushEvent failed: %s\n", SDL_GetError());
        return false;
    }
    return true;
}

static bool run_until_quit(const char *name, SDL_Event *first, SDL_Event *second)
{
    VellumErrorInfo error;
    VellumApp *app = vellum_app_create(&error);
    if (app == NULL) {
        fprintf(stderr, "%s: create failed: %s\n", name, vellum_error_message(&error));
        return false;
    }
    if (!push_event(first) || (second != NULL && !push_event(second))) {
        vellum_app_destroy(app);
        return false;
    }

    const int result = vellum_app_run(app);
    vellum_app_destroy(app);
    if (result != 0) {
        fprintf(stderr, "%s: run returned %d\n", name, result);
    }
    return result == 0;
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

    bool ok = true;
    ok = run_until_quit("escape", &escape, NULL) && ok;
    ok = run_until_quit("close request", &close_request, NULL) && ok;
    ok = run_until_quit("resize then escape", &resize, &escape) && ok;

    vellum_log_shutdown();
    printf("%s\n", ok ? "app event integration: PASS" : "app event integration: FAIL");
    return ok ? 0 : 1;
}
