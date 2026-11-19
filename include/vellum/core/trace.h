/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#ifndef VELLUM_CORE_TRACE_H
#define VELLUM_CORE_TRACE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VELLUM_TRACE_WINDOW 120

typedef struct {
    uint64_t frequency;
    uint64_t last_frame_counter;
    uint64_t frame_start_counter;
    uint64_t frame_count;
    double last_frame_ms;
    double total_frame_ms;
    double max_frame_ms;
    double last_work_ms;
    double total_work_ms;
    double max_work_ms;
    double window[VELLUM_TRACE_WINDOW];
    size_t window_length;
    size_t window_next;
} VellumTrace;

void vellum_trace_init(VellumTrace *trace);
void vellum_trace_begin_frame(VellumTrace *trace);
void vellum_trace_end_frame(VellumTrace *trace);
uint64_t vellum_trace_frame_count(const VellumTrace *trace);
double vellum_trace_last_ms(const VellumTrace *trace);
double vellum_trace_average_ms(const VellumTrace *trace);
double vellum_trace_max_ms(const VellumTrace *trace);
double vellum_trace_last_work_ms(const VellumTrace *trace);
double vellum_trace_average_work_ms(const VellumTrace *trace);
void vellum_trace_reset(VellumTrace *trace);

#ifdef __cplusplus
}
#endif

#endif /* VELLUM_CORE_TRACE_H */
