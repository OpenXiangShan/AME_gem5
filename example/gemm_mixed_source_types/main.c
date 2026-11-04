/* Copyright (c) 2026 BOSC & ICT, CAS. SPDX-License-Identifier: BSD-3-Clause */
#include <stdio.h>

int ztt_mixed_run_suite(const char *path);

int
main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s REPORT_PATH\n", argv[0]);
        return 2;
    }
    if (!ztt_mixed_run_suite(argv[1])) {
        fprintf(stderr, "mixed GEMM report failed: %s\n", argv[1]);
        return 1;
    }
    return 0;
}
