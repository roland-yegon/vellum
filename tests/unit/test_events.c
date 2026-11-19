/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "tests/unit/tests.h"
#include "vellum/platform/events.h"

bool test_events(void)
{
    SDL_Event event;
    VellumInput input;
    SDL_zero(event);

    event.type = SDL_EVENT_QUIT;
    if (!vellum_input_from_sdl(&event, 0u, &input) || input.type != VELLUM_INPUT_QUIT) {
        return false;
    }

    event.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
    if (!vellum_input_from_sdl(&event, 0u, &input) || input.type != VELLUM_INPUT_QUIT) {
        return false;
    }

    event.type = SDL_EVENT_WINDOW_RESIZED;
    event.window.data1 = 800;
    event.window.data2 = 600;
    if (!vellum_input_from_sdl(&event, 0u, &input) || input.type != VELLUM_INPUT_RESIZE ||
        input.data.resize.width != 800 || input.data.resize.height != 600) {
        return false;
    }

    SDL_zero(event);
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = SDLK_ESCAPE;
    event.key.mod = SDL_KMOD_SHIFT | SDL_KMOD_CTRL;
    event.key.repeat = true;
    if (!vellum_input_from_sdl(&event, 0u, &input) || input.type != VELLUM_INPUT_KEY ||
        input.data.key.key != VELLUM_KEY_ESCAPE || !input.data.key.repeat ||
        input.data.key.modifiers != (VELLUM_MOD_SHIFT | VELLUM_MOD_CTRL)) {
        return false;
    }

    SDL_zero(event);
    event.type = SDL_EVENT_TEXT_INPUT;
    event.text.text = "hi";
    if (!vellum_input_from_sdl(&event, 0u, &input) || input.type != VELLUM_INPUT_TEXT ||
        strcmp(input.data.text.text, "hi") != 0) {
        return false;
    }

    SDL_zero(event);
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.x = 12.5f;
    event.motion.y = 44.0f;
    if (!vellum_input_from_sdl(&event, 0u, &input) || input.type != VELLUM_INPUT_POINTER_MOTION ||
        input.data.pointer.x != 12.5f || input.data.pointer.y != 44.0f) {
        return false;
    }

    SDL_zero(event);
    event.type = 0x8000u;
    event.user.code = 7;
    if (!vellum_input_from_sdl(&event, 0x8000u, &input) || input.type != VELLUM_INPUT_APP_MESSAGE ||
        input.data.message.type != 7u) {
        return false;
    }

    SDL_zero(event);
    event.type = SDL_EVENT_WINDOW_SHOWN;
    if (vellum_input_from_sdl(&event, 0x8000u, &input)) {
        return false;
    }
    if (vellum_input_from_sdl(NULL, 0u, &input) || vellum_input_from_sdl(&event, 0u, NULL)) {
        return false;
    }

    return true;
}
