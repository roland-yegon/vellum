/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#include <stdbool.h>
#include <stdio.h>

#include "tests/benchmarks/benchmarks.h"

int main(void)
{
    const bool ok = bench_draw_list();
    printf("%s\n", ok ? "benchmark completed" : "benchmark failed");
    return ok ? 0 : 1;
}
