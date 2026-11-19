#include "vellum/core/error.h"

#include <SDL3/SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void vellum_error_init(VellumErrorInfo *info)
{
    if (info == NULL) {
        return;
    }
    info->code = VELLUM_OK;
    info->message[0] = '\0';
}

bool vellum_error_is_ok(const VellumErrorInfo *info)
{
    return info == NULL || info->code == VELLUM_OK;
}

void vellum_error_set(VellumErrorInfo *info, VellumError code, const char *fmt, ...)
{
    if (info == NULL || fmt == NULL) {
        return;
    }

    info->code = code;
    va_list args;
    va_start(args, fmt);
    (void)vsnprintf(info->message, sizeof(info->message), fmt, args);
    va_end(args);
}

void vellum_error_set_sdl(VellumErrorInfo *info, VellumError code, const char *context)
{
    const char *detail = SDL_GetError();
    if (detail == NULL || detail[0] == '\0') {
        detail = "SDL did not provide an error message";
    }
    vellum_error_set(info, code, "%s: %s", context == NULL ? "SDL operation" : context, detail);
    (void)SDL_ClearError();
}

const char *vellum_error_name(VellumError code)
{
    switch (code) {
    case VELLUM_OK:
        return "OK";
    case VELLUM_ERR_INVALID_ARGUMENT:
        return "invalid argument";
    case VELLUM_ERR_ALLOC_FAILED:
        return "allocation failed";
    case VELLUM_ERR_CAPACITY:
        return "capacity exceeded";
    case VELLUM_ERR_PLATFORM:
        return "platform error";
    case VELLUM_ERR_NOT_FOUND:
        return "not found";
    case VELLUM_ERR_IO:
        return "I/O error";
    case VELLUM_ERR_PARSE:
        return "parse error";
    case VELLUM_ERR_UNSUPPORTED:
        return "unsupported";
    case VELLUM_ERR_INTERNAL:
        return "internal error";
    }
    return "unknown error";
}

const char *vellum_error_message(const VellumErrorInfo *info)
{
    if (info == NULL) {
        return "";
    }
    if (info->message[0] == '\0') {
        return vellum_error_name(info->code);
    }
    return info->message;
}
