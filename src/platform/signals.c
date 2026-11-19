/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#define _POSIX_C_SOURCE 200809L

#include "vellum/platform/signals.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t g_signal_number = 0;
static volatile int g_pipe_write = -1;
static int g_pipe[2] = {-1, -1};
static struct sigaction g_previous_int;
static struct sigaction g_previous_term;
static bool g_installed = false;

static void signal_handler(int signal_number)
{
    const int saved_errno = errno;
    g_signal_number = (sig_atomic_t)signal_number;
    const int fd = g_pipe_write;
    if (fd >= 0) {
        const unsigned char byte = 1u;
        (void)write(fd, &byte, 1u);
    }
    errno = saved_errno;
}

static bool set_nonblocking(int fd, VellumErrorInfo *error)
{
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        vellum_error_set(error, VELLUM_ERR_PLATFORM, "fcntl(O_NONBLOCK): %s", strerror(errno));
        return false;
    }
    return true;
}

bool vellum_signals_install(VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (g_installed) {
        return true;
    }
    if (pipe(g_pipe) != 0) {
        vellum_error_set(error, VELLUM_ERR_PLATFORM, "pipe: %s", strerror(errno));
        return false;
    }
    if (!set_nonblocking(g_pipe[0], error) || !set_nonblocking(g_pipe[1], error)) {
        (void)close(g_pipe[0]);
        (void)close(g_pipe[1]);
        g_pipe[0] = -1;
        g_pipe[1] = -1;
        return false;
    }

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = signal_handler;
    action.sa_flags = SA_RESTART;
    (void)sigemptyset(&action.sa_mask);

    if (sigaction(SIGINT, &action, &g_previous_int) != 0) {
        vellum_error_set(error, VELLUM_ERR_PLATFORM, "sigaction(SIGINT): %s", strerror(errno));
        (void)close(g_pipe[0]);
        (void)close(g_pipe[1]);
        g_pipe[0] = -1;
        g_pipe[1] = -1;
        return false;
    }
    if (sigaction(SIGTERM, &action, &g_previous_term) != 0) {
        vellum_error_set(error, VELLUM_ERR_PLATFORM, "sigaction(SIGTERM): %s", strerror(errno));
        (void)sigaction(SIGINT, &g_previous_int, NULL);
        (void)close(g_pipe[0]);
        (void)close(g_pipe[1]);
        g_pipe[0] = -1;
        g_pipe[1] = -1;
        return false;
    }

    g_pipe_write = g_pipe[1];
    g_installed = true;
    return true;
}

bool vellum_signals_pending(int *signal_number)
{
    const int pending = (int)g_signal_number;
    if (signal_number != NULL && pending != 0) {
        *signal_number = pending;
    }
    g_signal_number = 0;

    if (g_pipe[0] >= 0) {
        unsigned char buffer[16];
        while (read(g_pipe[0], buffer, sizeof(buffer)) > 0) {
            /* Drain the notification pipe. */
        }
    }
    return pending != 0;
}

void vellum_signals_shutdown(void)
{
    if (!g_installed) {
        return;
    }

    (void)sigaction(SIGINT, &g_previous_int, NULL);
    (void)sigaction(SIGTERM, &g_previous_term, NULL);
    g_installed = false;
    g_pipe_write = -1;
    g_signal_number = 0;

    if (g_pipe[0] >= 0) {
        (void)close(g_pipe[0]);
    }
    if (g_pipe[1] >= 0) {
        (void)close(g_pipe[1]);
    }
    g_pipe[0] = -1;
    g_pipe[1] = -1;
}
