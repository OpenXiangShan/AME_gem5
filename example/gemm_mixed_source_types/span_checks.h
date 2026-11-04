/*
 * Copyright (c) 2026 BOSC & ICT, CAS
 * All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ZTT_MIXED_SPAN_CHECKS_H
#define ZTT_MIXED_SPAN_CHECKS_H

#include "ztt_gemm_test.h"

#if defined(__riscv)
static volatile uint64_t ztt_mixed_trap_count;
static volatile uint64_t ztt_mixed_trap_cause;

/* Only the tested 32-bit matrix instruction may fault. Resume after it so
 * that the test can inspect both the exception and the unchanged ACC. */
__attribute__((interrupt("machine"), aligned(4))) static void
ztt_mixed_span_trap(void)
{
    uintptr_t pc;
    __asm__ volatile("csrr %0, mcause" : "=r"(ztt_mixed_trap_cause));
    ++ztt_mixed_trap_count;
    __asm__ volatile("csrr %0, mepc" : "=r"(pc));
    pc += 4;
    __asm__ volatile("csrw mepc, %0" ::"r"(pc));
}
#endif

static int
ztt_mixed_check_spans(void)
{
#if defined(__riscv)
    uint8_t first[64] __attribute__((aligned(64)));
    uint8_t second[64] __attribute__((aligned(64)));
    uint8_t narrow[64] __attribute__((aligned(64)));
    int32_t seed[16] __attribute__((aligned(64)));
    int32_t output[16] __attribute__((aligned(64)));
    for (unsigned i = 0; i < 64; ++i) {
        first[i] = 1;
        second[i] = 2;
        narrow[i] = 0x11;
    }
    for (unsigned i = 0; i < 16; ++i) {
        seed[i] = (int32_t)i - 8;
    }

    uintptr_t saved_mtvec;
    const uintptr_t handler = (uintptr_t)ztt_mixed_span_trap;
    __asm__ volatile("csrr %0, mtvec\n\tcsrw mtvec, %1"
                     : "=&r"(saved_mtvec)
                     : "r"(handler)
                     : "memory");
    int failures = 0;
    for (unsigned type = 0; type < 2; ++type) {
        const uint64_t narrow_type = type ? ZTT_DTYPE_UINT4 : ZTT_DTYPE_INT4;
        for (unsigned scenario = 0; scenario < 6; ++scenario) {
            ztt_mixed_trap_count = 0;
            ztt_mixed_trap_cause = 0;
            __asm__ volatile("csrw amestatus, zero\n\t"
                             "csrw amefflags, zero\n\t"
                             "csrw amexsat, zero\n\t"
                             "msettyp m0, %[i8]\n\t"
                             "msettyp m1, %[i8]\n\t"
                             "msettyp m30, %[i8]\n\t"
                             "msettyp m31, %[i8]\n\t"
                             "msettyp m4, %[i4]\n\t"
                             "msettyp m8, %[i32]\n\t"
                             "asettyp acc0, %[i32]\n\t"
                             "mls.rm m0, %[first]\n\t"
                             "mls.rm m1, %[second]\n\t"
                             "mls.rm m30, %[first]\n\t"
                             "mls.rm m31, %[second]\n\t"
                             "mls.rm m4, %[narrow]\n\t"
                             "mls.rm m8, %[seed]\n\t"
                             "mmov.a.m acc0, m8" ::[i8] "r"(ZTT_DTYPE_INT8),
                             [i4] "r"(narrow_type), [i32] "r"(ZTT_DTYPE_INT32),
                             [first] "r"(first), [second] "r"(second),
                             [narrow] "r"(narrow), [seed] "r"(seed)
                             : "memory");
            if (scenario >= 4) {
                __asm__ volatile(
                    "msettyp m31, %[type]\n\tmls.rm m31, %[data]" ::[type] "r"(
                        narrow_type),
                    [data] "r"(narrow)
                    : "memory");
            }
            switch (scenario) {
                case 0:
                    __asm__ volatile("mmulacc.2d acc0, m30, m4" ::: "memory");
                    break;
                case 1:
                    __asm__ volatile("mmulacc.2d acc0, m31, m4" ::: "memory");
                    break;
                case 2:
                    __asm__ volatile("mmulacc.2d acc0, m4, m30" ::: "memory");
                    break;
                case 3:
                    __asm__ volatile("mmulacc.2d acc0, m4, m31" ::: "memory");
                    break;
                case 4:
                    __asm__ volatile("mmulacc.2d acc0, m0, m31" ::: "memory");
                    break;
                case 5:
                    __asm__ volatile("mmulacc.2d acc0, m31, m0" ::: "memory");
                    break;
            }
            uint64_t status, flags, saturation;
            __asm__ volatile("mmov.m.a m8, acc0\n\tmss.rm m8, %[output]\n\t"
                             "csrr %[status], amestatus\n\t"
                             "csrr %[flags], amefflags\n\t"
                             "csrr %[sat], amexsat"
                             : [status] "=r"(status), [flags] "=r"(flags),
                               [sat] "=r"(saturation)
                             : [output] "r"(output)
                             : "memory");
            const int invalid = scenario == 1 || scenario == 3;
            if (ztt_mixed_trap_count != (unsigned)invalid ||
                (invalid && ztt_mixed_trap_cause != 2) || status || flags ||
                saturation) {
                ++failures;
            }
            for (unsigned i = 0; i < 16; ++i) {
                if (output[i] != seed[i] + (invalid ? 0 : 48)) {
                    ++failures;
                }
            }
        }
    }
    __asm__ volatile("csrw mtvec, %0" ::"r"(saved_mtvec) : "memory");
    return failures;
#else
    return 0;
#endif
}

#endif
