#ifndef VELLUM_PLATFORM_SIGNALS_H
#define VELLUM_PLATFORM_SIGNALS_H

#include <stdbool.h>

#include "vellum/core/error.h"

#ifdef __cplusplus
extern "C" {
#endif

bool vellum_signals_install(VellumErrorInfo *error);
bool vellum_signals_pending(int *signal_number);
void vellum_signals_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* VELLUM_PLATFORM_SIGNALS_H */
