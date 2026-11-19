#ifndef VELLUM_CORE_LOG_H
#define VELLUM_CORE_LOG_H

#include <stdbool.h>

#include "vellum/core/attributes.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VELLUM_LOG_DEBUG = 0,
    VELLUM_LOG_INFO,
    VELLUM_LOG_WARN,
    VELLUM_LOG_ERROR
} VellumLogLevel;

void vellum_log_init(void);
void vellum_log_shutdown(void);
void vellum_log_set_level(VellumLogLevel level);
VellumLogLevel vellum_log_get_level(void);
bool vellum_log_enabled(VellumLogLevel level);
void vellum_log(VellumLogLevel level, const char *subsystem, const char *fmt, ...)
    VELLUM_PRINTF_ATTR(3, 4);

#define VELLUM_LOG_DEBUG(subsystem, ...) \
    vellum_log(VELLUM_LOG_DEBUG, (subsystem), __VA_ARGS__)
#define VELLUM_LOG_INFO(subsystem, ...) \
    vellum_log(VELLUM_LOG_INFO, (subsystem), __VA_ARGS__)
#define VELLUM_LOG_WARN(subsystem, ...) \
    vellum_log(VELLUM_LOG_WARN, (subsystem), __VA_ARGS__)
#define VELLUM_LOG_ERROR(subsystem, ...) \
    vellum_log(VELLUM_LOG_ERROR, (subsystem), __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* VELLUM_CORE_LOG_H */
