/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#include <stdbool.h>
#include <stdio.h>

#include "tests/benchmarks/benchmarks.h"

int main(void)
{
    const bool ok = bench_text_buffer();
    printf("%s\n", ok ? "text-buffer benchmark completed" : "text-buffer benchmark failed");
    return ok ? 0 : 1;
}
