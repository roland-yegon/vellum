#include "vellum/core/log.h"

#include <SDL3/SDL.h>
#include <stdarg.h>
#include <stdio.h>

static SDL_Mutex *g_log_mutex;
static VellumLogLevel g_log_level = VELLUM_LOG_INFO;

static const char *level_name(VellumLogLevel level)
{
    switch (level) {
    case VELLUM_LOG_DEBUG:
        return "DEBUG";
    case VELLUM_LOG_INFO:
        return "INFO";
    case VELLUM_LOG_WARN:
        return "WARN";
    case VELLUM_LOG_ERROR:
        return "ERROR";
    }
    return "?";
}

void vellum_log_init(void)
{
    if (g_log_mutex == NULL) {
        g_log_mutex = SDL_CreateMutex();
    }
}

void vellum_log_shutdown(void)
{
    if (g_log_mutex != NULL) {
        SDL_DestroyMutex(g_log_mutex);
        g_log_mutex = NULL;
    }
}

void vellum_log_set_level(VellumLogLevel level)
{
    g_log_level = level;
}

VellumLogLevel vellum_log_get_level(void)
{
    return g_log_level;
}

bool vellum_log_enabled(VellumLogLevel level)
{
    return level >= g_log_level;
}

void vellum_log(VellumLogLevel level, const char *subsystem, const char *fmt, ...)
{
    if (fmt == NULL || !vellum_log_enabled(level)) {
        return;
    }

    if (g_log_mutex != NULL) {
        SDL_LockMutex(g_log_mutex);
    }

    const Uint64 ticks = SDL_GetTicks();
    const unsigned int milliseconds = (unsigned int)(ticks % 1000u);
    const unsigned int seconds = (unsigned int)((ticks / 1000u) % 60u);
    const unsigned int minutes = (unsigned int)((ticks / 60000u) % 60u);

    char message[1024];
    va_list args;
    va_start(args, fmt);
    (void)vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    (void)fprintf(stderr, "%02u:%02u.%03u [%-5s] [%s] %s\n", minutes, seconds, milliseconds,
                  level_name(level), subsystem == NULL ? "app" : subsystem, message);
    (void)fflush(stderr);

    if (g_log_mutex != NULL) {
        SDL_UnlockMutex(g_log_mutex);
    }
}
