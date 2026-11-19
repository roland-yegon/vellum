#include "vellum/platform/events.h"

#include <string.h>

static VellumKey translate_key(SDL_Keycode key)
{
    switch (key) {
    case SDLK_ESCAPE:
        return VELLUM_KEY_ESCAPE;
    case SDLK_UP:
        return VELLUM_KEY_UP;
    case SDLK_DOWN:
        return VELLUM_KEY_DOWN;
    case SDLK_LEFT:
        return VELLUM_KEY_LEFT;
    case SDLK_RIGHT:
        return VELLUM_KEY_RIGHT;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        return VELLUM_KEY_ENTER;
    case SDLK_SPACE:
        return VELLUM_KEY_SPACE;
    default:
        return key == SDLK_UNKNOWN ? VELLUM_KEY_UNKNOWN : VELLUM_KEY_OTHER;
    }
}

static uint16_t translate_modifiers(SDL_Keymod modifiers)
{
    uint16_t result = 0u;
    if ((modifiers & SDL_KMOD_SHIFT) != 0u) {
        result |= VELLUM_MOD_SHIFT;
    }
    if ((modifiers & SDL_KMOD_CTRL) != 0u) {
        result |= VELLUM_MOD_CTRL;
    }
    if ((modifiers & SDL_KMOD_ALT) != 0u) {
        result |= VELLUM_MOD_ALT;
    }
    if ((modifiers & SDL_KMOD_GUI) != 0u) {
        result |= VELLUM_MOD_GUI;
    }
    return result;
}

static void copy_text(char destination[VELLUM_TEXT_MAX], const char *source)
{
    size_t i = 0u;
    for (; i + 1u < VELLUM_TEXT_MAX && source[i] != '\0'; ++i) {
        destination[i] = source[i];
    }
    destination[i] = '\0';
}

bool vellum_input_from_sdl(const SDL_Event *event, uint32_t app_event_base, VellumInput *out)
{
    if (event == NULL || out == NULL) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    switch (event->type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        out->type = VELLUM_INPUT_QUIT;
        return true;
    case SDL_EVENT_WINDOW_RESIZED:
        out->type = VELLUM_INPUT_RESIZE;
        out->data.resize.width = event->window.data1;
        out->data.resize.height = event->window.data2;
        return true;
    case SDL_EVENT_KEY_DOWN:
        out->type = VELLUM_INPUT_KEY;
        out->data.key.key = translate_key(event->key.key);
        out->data.key.modifiers = translate_modifiers(event->key.mod);
        out->data.key.repeat = event->key.repeat;
        return true;
    case SDL_EVENT_TEXT_INPUT:
        out->type = VELLUM_INPUT_TEXT;
        copy_text(out->data.text.text, event->text.text);
        return true;
    case SDL_EVENT_MOUSE_MOTION:
        out->type = VELLUM_INPUT_POINTER_MOTION;
        out->data.pointer.x = event->motion.x;
        out->data.pointer.y = event->motion.y;
        return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        out->type = VELLUM_INPUT_POINTER_BUTTON;
        out->data.pointer.x = event->button.x;
        out->data.pointer.y = event->button.y;
        return true;
    case SDL_EVENT_MOUSE_WHEEL:
        out->type = VELLUM_INPUT_WHEEL;
        out->data.wheel.x = event->wheel.x;
        out->data.wheel.y = event->wheel.y;
        return true;
    default:
        break;
    }

    if (app_event_base != 0u && event->type == app_event_base) {
        out->type = VELLUM_INPUT_APP_MESSAGE;
        out->data.message.type = (uint32_t)event->user.code;
        out->data.message.ptr = event->user.data1;
        return true;
    }
    return false;
}
