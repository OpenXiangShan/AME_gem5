/* Copyright (c) 2026 BOSC & ICT, CAS. SPDX-License-Identifier: BSD-3-Clause */

#ifndef ZTT_MIXED_UNSUPPORTED_CHECKS_H
#define ZTT_MIXED_UNSUPPORTED_CHECKS_H

#include <stdint.h>
#include <string.h>

#include "ztt_gemm_test.h"

static int
ztt_mixed_check_accumulator_types(void)
{
#if defined(__riscv)
    uint8_t a[128] __attribute__((aligned(64)));
    uint8_t b[128] __attribute__((aligned(64)));
    uint8_t seed[128] __attribute__((aligned(64)));
    uint8_t output[128] __attribute__((aligned(64)));
    int failures = 0;
    for (unsigned scenario = 0; scenario < 4; ++scenario) {
        const unsigned floating = scenario >= 2;
        const unsigned swap = scenario & 1;
        const uint64_t first =
            floating ? ZTT_DTYPE_OCP_E4M3_RNE : ZTT_DTYPE_INT8;
        const uint64_t second =
            floating ? ZTT_DTYPE_OCP_E5M2_RNE : ZTT_DTYPE_INT4;
        const uint64_t a_type = swap ? second : first;
        const uint64_t b_type = swap ? first : second;
        const uint64_t c_type =
            floating ? ZTT_DTYPE_FP16_RNE : ZTT_DTYPE_INT64;
        const unsigned accumulator_bytes = floating ? 32 : 128;
        for (unsigned i = 0; i < 128; ++i) {
            a[i] = floating ? (swap ? 0x3c : 0x38) : (swap ? 0x71 : 3);
            b[i] = floating ? (swap ? 0x38 : 0x3c) : (swap ? 5 : 0x29);
            seed[i] = (uint8_t)(i * 37 + 11);
        }
        if (floating) {
            /* Distinct finite FP16 values avoid unrelated NaN conversion
             * flags. */
            for (unsigned i = 0; i < 64; ++i) {
                seed[2 * i] = (uint8_t)(i * 3);
                seed[2 * i + 1] = (uint8_t)(0x3c | (i % 2 ? 0x80 : 0));
            }
        }
        for (unsigned operation = 0; operation < 6; ++operation) {
            memset(output, 0xa5, sizeof(output));
            __asm__ volatile("csrw amestatus, zero\n\t"
                             "msettyp m0, %[at]\n\t"
                             "msettyp m1, %[at]\n\t"
                             "msettyp m4, %[bt]\n\t"
                             "msettyp m5, %[bt]\n\t"
                             /* I64 uses m8/m9 as one group; m9 is not a legal
                                group base. */
                             "msettyp m8, %[ct]\n\t"
                             "asettyp acc0, %[ct]\n\t"
                             "mls.rm m0, %[a]\n\t"
                             "mls.rm m1, %[a_next]\n\t"
                             "mls.rm m4, %[b]\n\t"
                             "mls.rm m5, %[b_next]\n\t"
                             "mls.rm m8, %[seed]\n\t"
                             "mmov.a.m acc0, m8\n\t"
                             "csrw amefflags, %[flags]\n\t"
                             "csrw amexsat, %[sat]" ::[at] "r"(a_type),
                             [bt] "r"(b_type), [ct] "r"(c_type), [a] "r"(a),
                             [a_next] "r"(a + 64), [b] "r"(b),
                             [b_next] "r"(b + 64), [seed] "r"(seed),
                             [flags] "r"(UINT64_C(9)), [sat] "r"(UINT64_C(1))
                             : "memory");
            switch (operation) {
                case 0:
                    __asm__ volatile("mmulacc.2d acc0, m0, m4" ::: "memory");
                    break;
                case 1:
                    __asm__ volatile("mmulaccneg.2d acc0, m0, m4" ::
                                         : "memory");
                    break;
                case 2:
                    __asm__ volatile("mmulatacc.2d acc0, m0, m4" ::: "memory");
                    break;
                case 3:
                    __asm__ volatile("mmulataccneg.2d acc0, m0, m4" ::
                                         : "memory");
                    break;
                case 4:
                    __asm__ volatile("mmulbtacc.2d acc0, m0, m4" ::: "memory");
                    break;
                case 5:
                    __asm__ volatile("mmulbtaccneg.2d acc0, m0, m4" ::
                                         : "memory");
                    break;
            }
            uint64_t status, flags, saturated;
            __asm__ volatile("mmov.m.a m8, acc0\n\t"
                             "mss.rm m8, %[output]\n\t"
                             "csrr %[status], amestatus\n\t"
                             "csrr %[flags], amefflags\n\t"
                             "csrr %[sat], amexsat"
                             : [status] "=r"(status), [flags] "=r"(flags),
                               [sat] "=r"(saturated)
                             : [output] "r"(output)
                             : "memory");
            if (status != 1 || flags != 9 || saturated != 1) {
                ++failures;
            }
            for (unsigned i = 0; i < accumulator_bytes; ++i) {
                if (output[i] != seed[i]) {
                    ++failures;
                }
            }
        }
    }
    return failures;
#else
    return 0;
#endif
}

#endif
