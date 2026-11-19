#include "vellum/app.h"
#include "vellum/core/error.h"
#include "vellum/core/log.h"

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    vellum_log_init();

    VellumErrorInfo error;
    vellum_error_init(&error);
    VellumApp *app = vellum_app_create(&error);
    if (app == NULL) {
        VELLUM_LOG_ERROR("main", "startup failed: %s", vellum_error_message(&error));
        vellum_log_shutdown();
        return 1;
    }

    const int result = vellum_app_run(app);
    vellum_app_destroy(app);
    vellum_log_shutdown();
    return result;
}
