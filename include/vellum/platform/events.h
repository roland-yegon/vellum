/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#ifndef VELLUM_PLATFORM_EVENTS_H
#define VELLUM_PLATFORM_EVENTS_H

#include <SDL3/SDL_events.h>
#include <stdbool.h>
#include <stdint.h>

#include "vellum/core/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VELLUM_INPUT_NONE = 0,
    VELLUM_INPUT_QUIT,
    VELLUM_INPUT_KEY,
    VELLUM_INPUT_TEXT,
    VELLUM_INPUT_POINTER_MOTION,
    VELLUM_INPUT_POINTER_BUTTON,
    VELLUM_INPUT_WHEEL,
    VELLUM_INPUT_RESIZE,
    VELLUM_INPUT_APP_MESSAGE
} VellumInputType;

typedef enum {
    VELLUM_KEY_UNKNOWN = 0,
    VELLUM_KEY_ESCAPE,
    VELLUM_KEY_UP,
    VELLUM_KEY_DOWN,
    VELLUM_KEY_LEFT,
    VELLUM_KEY_RIGHT,
    VELLUM_KEY_ENTER,
    VELLUM_KEY_SPACE,
    VELLUM_KEY_OTHER
} VellumKey;

#define VELLUM_MOD_SHIFT 0x01u
#define VELLUM_MOD_CTRL 0x02u
#define VELLUM_MOD_ALT 0x04u
#define VELLUM_MOD_GUI 0x08u

#define VELLUM_TEXT_MAX 8

typedef struct {
    VellumInputType type;
    union {
        struct {
            VellumKey key;
            uint16_t modifiers;
            bool repeat;
        } key;
        struct {
            char text[VELLUM_TEXT_MAX];
        } text;
        struct {
            float x;
            float y;
        } pointer;
        struct {
            float x;
            float y;
        } wheel;
        struct {
            int width;
            int height;
        } resize;
        VellumMessage message;
    } data;
} VellumInput;

bool vellum_input_from_sdl(const SDL_Event *event, uint32_t app_event_base, VellumInput *out);

#ifdef __cplusplus
}
#endif

#endif /* VELLUM_PLATFORM_EVENTS_H */
