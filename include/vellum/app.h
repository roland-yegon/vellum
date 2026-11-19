#ifndef VELLUM_APP_H
#define VELLUM_APP_H

#include "vellum/core/error.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VellumApp VellumApp;

VellumApp *vellum_app_create(VellumErrorInfo *error);
int vellum_app_run(VellumApp *app);
void vellum_app_destroy(VellumApp *app);

#ifdef __cplusplus
}
#endif

#endif /* VELLUM_APP_H */
