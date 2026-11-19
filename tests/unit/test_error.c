#include <SDL3/SDL.h>
#include <stdbool.h>
#include <string.h>

#include "tests/unit/tests.h"
#include "vellum/core/error.h"

bool test_error(void)
{
    VellumErrorInfo info;
    vellum_error_init(&info);
    if (!vellum_error_is_ok(&info) || info.message[0] != '\0') {
        return false;
    }

    vellum_error_set(&info, VELLUM_ERR_INVALID_ARGUMENT, "bad value %d", 7);
    if (vellum_error_is_ok(&info) || info.code != VELLUM_ERR_INVALID_ARGUMENT) {
        return false;
    }
    if (strstr(info.message, "bad value 7") == NULL) {
        return false;
    }
    if (strcmp(vellum_error_message(&info), info.message) != 0) {
        return false;
    }
    if (vellum_error_name(VELLUM_OK) == NULL ||
        strstr(vellum_error_name(VELLUM_ERR_PARSE), "parse") == NULL) {
        return false;
    }

    /* Exercise the SDL_GetError() capture path without requiring a window. */
    (void)SDL_SetError("synthetic negative test");
    vellum_error_set_sdl(&info, VELLUM_ERR_PLATFORM, "context");
    if (info.code != VELLUM_ERR_PLATFORM) {
        return false;
    }
    if (strstr(info.message, "context") == NULL ||
        strstr(info.message, "synthetic negative test") == NULL) {
        return false;
    }
    const char *remaining = SDL_GetError();
    if (remaining != NULL && remaining[0] != '\0') {
        return false;
    }

    vellum_error_init(&info);
    return vellum_error_is_ok(&info);
}
