#include "vellum/core/trace.h"

#include <SDL3/SDL.h>

static double elapsed_ms(uint64_t from, uint64_t to, uint64_t frequency)
{
    if (frequency == 0u || to < from) {
        return 0.0;
    }
    return (double)(to - from) * 1000.0 / (double)frequency;
}

void vellum_trace_init(VellumTrace *trace)
{
    if (trace == NULL) {
        return;
    }

    trace->frequency = SDL_GetPerformanceFrequency();
    trace->last_frame_counter = 0u;
    trace->frame_start_counter = 0u;
    trace->frame_count = 0u;
    trace->last_frame_ms = 0.0;
    trace->total_frame_ms = 0.0;
    trace->max_frame_ms = 0.0;
    trace->last_work_ms = 0.0;
    trace->total_work_ms = 0.0;
    trace->max_work_ms = 0.0;
    trace->window_length = 0u;
    trace->window_next = 0u;
    for (size_t i = 0u; i < VELLUM_TRACE_WINDOW; ++i) {
        trace->window[i] = 0.0;
    }
}

void vellum_trace_begin_frame(VellumTrace *trace)
{
    if (trace == NULL) {
        return;
    }

    const uint64_t now = SDL_GetPerformanceCounter();
    const double interval = elapsed_ms(trace->last_frame_counter, now, trace->frequency);
    if (trace->last_frame_counter != 0u && interval > 0.0) {
        trace->last_frame_ms = interval;
        trace->total_frame_ms += interval;
        if (interval > trace->max_frame_ms) {
            trace->max_frame_ms = interval;
        }
        trace->window[trace->window_next] = interval;
        trace->window_next = (trace->window_next + 1u) % VELLUM_TRACE_WINDOW;
        if (trace->window_length < VELLUM_TRACE_WINDOW) {
            trace->window_length++;
        }
    }

    trace->last_frame_counter = now;
    trace->frame_start_counter = now;
    trace->frame_count++;
}

void vellum_trace_end_frame(VellumTrace *trace)
{
    if (trace == NULL || trace->frame_start_counter == 0u) {
        return;
    }

    const double work = elapsed_ms(trace->frame_start_counter,
                                   SDL_GetPerformanceCounter(),
                                   trace->frequency);
    trace->last_work_ms = work;
    trace->total_work_ms += work;
    if (work > trace->max_work_ms) {
        trace->max_work_ms = work;
    }
}

uint64_t vellum_trace_frame_count(const VellumTrace *trace)
{
    return trace == NULL ? 0u : trace->frame_count;
}

double vellum_trace_last_ms(const VellumTrace *trace)
{
    return trace == NULL ? 0.0 : trace->last_frame_ms;
}

double vellum_trace_average_ms(const VellumTrace *trace)
{
    if (trace == NULL || trace->window_length == 0u) {
        return 0.0;
    }

    double sum = 0.0;
    for (size_t i = 0u; i < trace->window_length; ++i) {
        sum += trace->window[i];
    }
    return sum / (double)trace->window_length;
}

double vellum_trace_max_ms(const VellumTrace *trace)
{
    return trace == NULL ? 0.0 : trace->max_frame_ms;
}

double vellum_trace_last_work_ms(const VellumTrace *trace)
{
    return trace == NULL ? 0.0 : trace->last_work_ms;
}

double vellum_trace_average_work_ms(const VellumTrace *trace)
{
    if (trace == NULL || trace->frame_count == 0u) {
        return 0.0;
    }
    return trace->total_work_ms / (double)trace->frame_count;
}

void vellum_trace_reset(VellumTrace *trace)
{
    vellum_trace_init(trace);
}
