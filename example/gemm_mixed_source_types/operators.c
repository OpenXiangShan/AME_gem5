/* Copyright (c) 2026 BOSC & ICT, CAS. SPDX-License-Identifier: BSD-3-Clause */
#include <stdint.h>
#include <string.h>

#include "span_checks.h"
#include "unsupported_checks.h"
#include "ztt_gemm_test.h"

/* Default AMENLEN=4 and AMEUDSZ=32 give 4x4 squares and 64 bytes per M. */
enum
{
    Side = 4,
    Elements = 16,
    MaxInput = 128,
    Operations = 6
};
enum
{
    I8,
    I4,
    U8,
    U4,
    E4M3,
    E5M2
};
enum
{
    Saturate = 1U << 29,
    Inexact = 1,
    Invalid = 16
};

struct tuple
{
    unsigned a, b;
    uint64_t a_type, b_type, c_type;
    const char *name;
};

static const struct tuple tuples[] = {
    {I8, I4, ZTT_DTYPE_INT8, ZTT_DTYPE_INT4, ZTT_DTYPE_INT32,
     "Int8 x Int4 -> Int32"},
    {I4, I8, ZTT_DTYPE_INT4, ZTT_DTYPE_INT8, ZTT_DTYPE_INT32,
     "Int4 x Int8 -> Int32"},
    {E4M3, E5M2, ZTT_DTYPE_OCP_E4M3_RNE, ZTT_DTYPE_OCP_E5M2_RNE,
     ZTT_DTYPE_FP32_RNE, "OCP_E4M3 x OCP_E5M2 -> FP32"},
    {E5M2, E4M3, ZTT_DTYPE_OCP_E5M2_RNE, ZTT_DTYPE_OCP_E4M3_RNE,
     ZTT_DTYPE_FP32_RNE, "OCP_E5M2 x OCP_E4M3 -> FP32"},
    {I8, U8, ZTT_DTYPE_INT8, ZTT_DTYPE_UINT8, ZTT_DTYPE_INT32,
     "Int8 x Uint8 -> Int32"},
    {U8, I8, ZTT_DTYPE_UINT8, ZTT_DTYPE_INT8, ZTT_DTYPE_INT32,
     "Uint8 x Int8 -> Int32"},
    {I8, U4, ZTT_DTYPE_INT8, ZTT_DTYPE_UINT4, ZTT_DTYPE_INT32,
     "Int8 x Uint4 -> Int32"},
    {U4, I8, ZTT_DTYPE_UINT4, ZTT_DTYPE_INT8, ZTT_DTYPE_INT32,
     "Uint4 x Int8 -> Int32"},
};

struct input
{
    uint8_t a[MaxInput], b[MaxInput];
    uint32_t c[Elements];
    uint64_t a_type, b_type, c_type;
    uint32_t initial_flags, initial_sat;
    unsigned square_count, rounding, saturating, unsupported;
};

struct output
{
    uint32_t c[Elements];
    uint64_t status, flags, saturated;
};

static struct ztt_gemm_report report;
static const char *case_names[ZTT_GEMM_MAX_CASES];

static unsigned
bits(unsigned kind)
{
    return kind == I4 || kind == U4 ? 4 : 8;
}

static int
floating(const struct tuple *type)
{
    return type->a == E4M3 || type->a == E5M2;
}

static uint8_t
pattern(unsigned kind, unsigned i)
{
    static const uint8_t integer[] = {0x80, 0xff, 0,    1,
                                      0x7f, 0x81, 0x37, 0xc9};
    static const uint8_t nibble[] = {8, 15, 0, 1, 7, 9, 3, 14};
    static const uint8_t e4[] = {0,    0x80, 1,    0x81, 0x38, 0xb8,
                                 0x7e, 0xfe, 0x3b, 0xbd, 0x11, 0x22,
                                 3,    0x84, 0x40, 0xc1};
    static const uint8_t e5[] = {0x3c, 0xbc, 1, 0x81, 0x7b, 0xfb, 0,    0x80,
                                 0x41, 0xc2, 7, 0x89, 0x23, 0xa2, 0x54, 0xd5};
    if (kind == E4M3) {
        return e4[i % 16];
    }
    if (kind == E5M2) {
        return e5[i % 16];
    }
    return bits(kind) == 4 ? nibble[i % 8] : integer[i % 8];
}

static uint8_t
one(unsigned kind)
{
    return kind == E4M3 ? 0x38 : 0x3c;
}

static void
prepare(const struct tuple *type, unsigned scenario, struct input *in)
{
    memset(in, 0, sizeof(*in));
    in->a_type = type->a_type;
    in->b_type = type->b_type;
    in->c_type = type->c_type;
    in->square_count =
        32 / (bits(type->a) < bits(type->b) ? bits(type->a) : bits(type->b));
    for (unsigned i = 0; i < in->square_count * Elements; ++i) {
        /* Different row/column/square patterns expose transpose and packing.
         */
        const unsigned square = i / Elements;
        const unsigned row = i / Side % Side, col = i % Side;
        in->a[i] = pattern(type->a, square * 7 + row * 3 + col);
        in->b[i] = pattern(type->b, square * 5 + row + col * 7 + 3);
        if (scenario == 1 && square < in->square_count / 2) {
            in->a[i] = in->b[i] = 0;
        }
        if (scenario == 2 || scenario == 3) {
            in->a[i] = type->a == I4   ? 7
                       : type->a == U4 ? 15
                       : type->a == U8 ? 255
                                       : 127;
            in->b[i] = type->b == I4   ? 7
                       : type->b == U4 ? 15
                       : type->b == U8 ? 255
                                       : 127;
        }
    }
    for (unsigned i = 0; i < Elements; ++i) {
        in->c[i] = floating(type) ? (i % 2 ? 0xbf000000U : 0x3f800000U)
                                  : (uint32_t)((int32_t)i * 17 - 97);
        if (scenario == 2 || scenario == 3) {
            in->c[i] = i % 2 ? 0x80000005U : 0x7ffffffaU;
        }
    }
    if (scenario == 3) {
        in->c_type |= Saturate;
        in->saturating = 1;
    }
    if (scenario == 4) {
        /* Input controls do not determine accumulator arithmetic. */
        in->a_type |= Saturate | (UINT64_C(1) << 27);
        in->b_type |= UINT64_C(3) << 27;
        in->initial_flags = 8;
        in->initial_sat = 1;
    }
}

#if !defined(__riscv)

static int64_t
integer_value(uint8_t raw, unsigned kind)
{
    const unsigned width = bits(kind), sign = 1U << (width - 1);
    const unsigned value = raw & ((1U << width) - 1);
    if ((kind == I8 || kind == I4) && (value & sign)) {
        return (int64_t)value - (INT64_C(1) << width);
    }
    return value;
}

struct fp8
{
    int64_t units; /* Exact multiples of 2^-16. */
    unsigned negative, nan, signaling, infinity;
};

static struct fp8
decode_fp8(uint8_t raw, unsigned kind)
{
    struct fp8 value = {0, raw >> 7, 0, 0, 0};
    const unsigned fraction_bits = kind == E4M3 ? 3 : 2;
    const unsigned fraction_mask = (1U << fraction_bits) - 1;
    const unsigned fraction = raw & fraction_mask;
    const unsigned exponent_mask = kind == E4M3 ? 15 : 31;
    const unsigned exponent = (raw >> fraction_bits) & exponent_mask;
    value.nan = exponent == exponent_mask &&
                (kind == E4M3 ? fraction == fraction_mask : fraction != 0);
    value.signaling = value.nan && kind == E5M2 && !(fraction & 2);
    value.infinity = kind == E5M2 && exponent == 31 && fraction == 0;
    if (value.nan || value.infinity) {
        return value;
    }
    const int bias = kind == E4M3 ? 7 : 15;
    const int shift =
        (exponent ? (int)exponent - bias : 1 - bias) - (int)fraction_bits + 16;
    const int64_t significand =
        fraction + (exponent ? 1U << fraction_bits : 0);
    value.units = significand << shift;
    if (value.negative) {
        value.units = -value.units;
    }
    return value;
}

/* Independent exact reference: every finite FP8 product is a multiple of
 * 2^-25. Chosen FP32 addends/results fit this __int128 fixed-point domain.
 * This code uses neither the simulator backend nor host floating arithmetic.
 */
static __int128
f32_units(uint32_t raw)
{
    const unsigned exponent = (raw >> 23) & 255;
    const uint32_t significand = (raw & 0x7fffff) | (exponent ? 0x800000 : 0);
    if (!significand) {
        return 0;
    }
    const int shift = (int)exponent - 125;
    __int128 value =
        shift >= 0 ? (__int128)significand << shift : significand >> -shift;
    return raw >> 31 ? -value : value;
}

static uint32_t
round_f32(__int128 value, unsigned negative_zero, unsigned rounding,
          uint64_t *flags)
{
    if (!value) {
        return negative_zero ? 0x80000000U : 0;
    }
    const unsigned negative = value < 0;
    const unsigned __int128 magnitude =
        negative ? (unsigned __int128)-value : (unsigned __int128)value;
    unsigned high = 0;
    for (unsigned __int128 scan = magnitude; scan >>= 1;) {
        ++high;
    }
    unsigned exponent = high + 102; /* high - 25 + FP32 bias */
    unsigned __int128 significand;
    if (high <= 23) {
        significand = magnitude << (23 - high);
    } else {
        const unsigned shift = high - 23;
        const unsigned __int128 remainder =
            magnitude & (((unsigned __int128)1 << shift) - 1);
        const unsigned __int128 halfway = (unsigned __int128)1 << (shift - 1);
        significand = magnitude >> shift;
        if (remainder) {
            *flags |= Inexact;
            if (rounding == 5) {
                significand |= 1;
            } else if ((rounding == 0 &&
                        (remainder > halfway ||
                         (remainder == halfway && (significand & 1)))) ||
                       (rounding == 2 && negative) ||
                       (rounding == 3 && !negative) ||
                       (rounding == 4 && remainder >= halfway)) {
                ++significand;
            }
        }
        if (significand == 0x1000000) {
            significand >>= 1;
            ++exponent;
        }
    }
    return (negative << 31) | (exponent << 23) |
           ((uint32_t)significand & 0x7fffff);
}

static uint32_t
reference_fma(uint8_t a_raw, unsigned a_kind, uint8_t b_raw, unsigned b_kind,
              uint32_t addend, unsigned negate, unsigned rounding,
              uint64_t *flags)
{
    const struct fp8 a = decode_fp8(a_raw, a_kind);
    const struct fp8 b = decode_fp8(b_raw, b_kind);
    const unsigned c_nan = (addend & 0x7fffffff) > 0x7f800000;
    const unsigned c_infinity = (addend & 0x7fffffff) == 0x7f800000;
    const unsigned c_sign = addend >> 31;
    const unsigned product_sign = a.negative ^ b.negative ^ negate;
    const unsigned invalid_product =
        (a.infinity && !b.nan && !b.infinity && !b.units) ||
        (b.infinity && !a.nan && !a.infinity && !a.units);
    if (a.signaling || b.signaling || (c_nan && !(addend & 0x400000)) ||
        invalid_product) {
        *flags |= Invalid;
    }
    if (a.nan || b.nan || c_nan || invalid_product) {
        return 0x7fc00000;
    }
    if (a.infinity || b.infinity) {
        if (c_infinity && c_sign != product_sign) {
            *flags |= Invalid;
            return 0x7fc00000;
        }
        return (product_sign << 31) | 0x7f800000;
    }
    if (c_infinity) {
        return addend;
    }
    __int128 product = (__int128)a.units * b.units / 128;
    if (negate) {
        product = -product;
    }
    const __int128 current = f32_units(addend);
    const __int128 exact = current + product;
    const unsigned zero_sign = !current && !product && c_sign == product_sign
                                   ? c_sign
                                   : rounding == 2;
    return round_f32(exact, zero_sign, rounding, flags);
}

static void
execute(const struct tuple *type, const struct input *in, unsigned op,
        struct output *out)
{
    memcpy(out->c, in->c, sizeof(out->c));
    out->status = in->unsupported ? 1 : 0;
    out->flags = in->initial_flags;
    out->saturated = in->initial_sat;
    if (in->unsupported) {
        return;
    }
    const unsigned negate = op & 1;
    for (unsigned row = 0; row < Side; ++row) {
        for (unsigned col = 0; col < Side; ++col) {
            const unsigned dest = row * Side + col;
            uint32_t sum = in->c[dest];
            for (unsigned square = 0; square < in->square_count; ++square) {
                for (unsigned k = 0; k < Side; ++k) {
                    const unsigned a_index =
                        square * Elements +
                        (op == 2 || op == 3 ? k * Side + row : row * Side + k);
                    const unsigned b_index =
                        square * Elements +
                        (op == 4 || op == 5 ? col * Side + k : k * Side + col);
                    if (floating(type)) {
                        sum = reference_fma(in->a[a_index], type->a,
                                            in->b[b_index], type->b, sum,
                                            negate, in->rounding, &out->flags);
                    } else {
                        const int64_t product =
                            integer_value(in->a[a_index], type->a) *
                            integer_value(in->b[b_index], type->b);
                        const int64_t current =
                            sum & 0x80000000U
                                ? (int64_t)sum - INT64_C(0x100000000)
                                : sum;
                        int64_t result =
                            current + (negate ? -product : product);
                        if (in->saturating && result > INT32_MAX) {
                            result = INT32_MAX;
                            out->saturated = 1;
                        } else if (in->saturating && result < INT32_MIN) {
                            result = INT32_MIN;
                            out->saturated = 1;
                        }
                        sum = (uint32_t)result;
                    }
                }
            }
            out->c[dest] = sum;
        }
    }
}

#else

static void
pack(const uint8_t *logical, unsigned kind, unsigned count, uint8_t *packed)
{
    memset(packed, 0, MaxInput);
    if (bits(kind) == 8) {
        memcpy(packed, logical, count);
    } else {
        for (unsigned i = 0; i < count; ++i) {
            packed[i / 2] |= (logical[i] & 15) << ((i % 2) * 4);
        }
    }
}

static void
execute(const struct tuple *type, const struct input *in, unsigned op,
        struct output *out)
{
    uint8_t a[MaxInput] __attribute__((aligned(64)));
    uint8_t b[MaxInput] __attribute__((aligned(64)));
    uint32_t c[Elements] __attribute__((aligned(64)));
    pack(in->a, type->a, in->square_count * Elements, a);
    pack(in->b, type->b, in->square_count * Elements, b);
    memcpy(c, in->c, sizeof(c));
    __asm__ volatile(
        "csrw amestatus, zero\n\t"
        "csrw 0xcc4, %[flags]\n\t"
        "csrw 0xcc5, %[sat]\n\t"
        "msettyp m0, %[at]\n\t"
        "msettyp m1, %[at]\n\t"
        "msettyp m4, %[bt]\n\t"
        "msettyp m5, %[bt]\n\t"
        "msettyp m8, %[ct]\n\t"
        "asettyp acc0, %[ct]\n\t"
        "mls.rm m0, %[a]\n\t"
        "mls.rm m4, %[b]\n\t"
        "mls.rm m8, %[c]\n\t"
        "mmov.a.m acc0, m8" ::[flags] "r"((uint64_t)in->initial_flags),
        [sat] "r"((uint64_t)in->initial_sat), [at] "r"(in->a_type),
        [bt] "r"(in->b_type), [ct] "r"(in->c_type), [a] "r"(a), [b] "r"(b),
        [c] "r"(c)
        : "memory");
    if (in->square_count == 8 && bits(type->a) == 8) {
        __asm__ volatile("mls.rm m1, %0" ::"r"(a + 64) : "memory");
    }
    if (in->square_count == 8 && bits(type->b) == 8) {
        __asm__ volatile("mls.rm m5, %0" ::"r"(b + 64) : "memory");
    }
    switch (op) {
        case 0:
            __asm__ volatile("mmulacc.2d acc0, m0, m4" ::: "memory");
            break;
        case 1:
            __asm__ volatile("mmulaccneg.2d acc0, m0, m4" ::: "memory");
            break;
        case 2:
            __asm__ volatile("mmulatacc.2d acc0, m0, m4" ::: "memory");
            break;
        case 3:
            __asm__ volatile("mmulataccneg.2d acc0, m0, m4" ::: "memory");
            break;
        case 4:
            __asm__ volatile("mmulbtacc.2d acc0, m0, m4" ::: "memory");
            break;
        case 5:
            __asm__ volatile("mmulbtaccneg.2d acc0, m0, m4" ::: "memory");
            break;
    }
    __asm__ volatile("mmov.m.a m8, acc0\n\t"
                     "mss.rm m8, %[c]\n\t"
                     "csrr %[status], amestatus\n\t"
                     "csrr %[flags], 0xcc4\n\t"
                     "csrr %[sat], 0xcc5"
                     : [status] "=r"(out->status), [flags] "=r"(out->flags),
                       [sat] "=r"(out->saturated)
                     : [c] "r"(c)
                     : "memory");
    memcpy(out->c, c, sizeof(c));
}

#endif

static void
record(unsigned test, const struct output *out)
{
    unsigned index = report.case_values[test];
    for (unsigned i = 0; i < Elements; ++i) {
        report.values[test][index++] = out->c[i];
    }
    report.values[test][index++] = out->status;
    report.values[test][index++] = out->flags;
    report.values[test][index++] = out->saturated;
    report.case_values[test] = index;
}

static void
run_operations(unsigned test, const struct tuple *type, const struct input *in)
{
    struct output out;
    for (unsigned op = 0; op < Operations; ++op) {
        execute(type, in, op, &out);
        record(test, &out);
    }
}

static void
prepare_rounding(const struct tuple *type, unsigned mode, struct input *in)
{
    prepare(type, 0, in);
    in->rounding = mode;
    in->a_type |= (uint64_t)((mode + 1) % 6) << 22;
    in->b_type |= (uint64_t)((mode + 2) % 6) << 22;
    in->c_type |= (uint64_t)mode << 22;
    in->initial_flags = 8; /* Existing DZ remains sticky, independent of NX. */
    in->initial_sat = 1;
    for (unsigned i = 0; i < in->square_count * Elements; ++i) {
        in->a[i] = one(type->a);
        in->b[i] = one(type->b);
    }
    for (unsigned i = 0; i < Elements; ++i) {
        in->c[i] = 0x4b800000U | (i % 2 ? 0x80000000U : 0) | (i / 2 % 2);
    }
}

static void
prepare_special(const struct tuple *type, unsigned scenario, struct input *in)
{
    prepare(type, 0, in);
    for (unsigned i = 0; i < in->square_count * Elements; ++i) {
        uint8_t e4 = one(E4M3), e5 = one(E5M2);
        if (scenario == 0) {
            e4 = i % 3 ? 0x38 : 0;
            e5 = i % 5 ? 0x7c : 0xfc; /* Inf, 0*Inf, opposite infinities. */
        } else if (scenario == 1) {
            e4 = i % 7 ? 0x38 : 0x7f; /* E4M3 has only quiet NaNs. */
            e5 = i % 5 ? 0x3c : 0x7e;
        } else if (scenario == 2) {
            e4 = i % 2 ? 0x80 : 1;
            e5 = i % 5 ? 0x3c
                       : 0x7d; /* Signaling NaN raises NV on conversion. */
        } else if (scenario == 4) {
            e4 = i % 2 ? 0x80 : 0;
            e5 = i % 3 ? 0 : 0x80;
        }
        in->a[i] = type->a == E4M3 ? e4 : e5;
        in->b[i] = type->b == E4M3 ? e4 : e5;
    }
    for (unsigned i = 0; i < Elements; ++i) {
        static const uint32_t special_c[] = {0x7f800000, 0xff800000,
                                             0x7fc12345, 0x7f800001};
        in->c[i] = scenario == 3   ? special_c[i % 4]
                   : scenario == 4 ? (i % 2 ? 0x80000000U : 0)
                                   : 0x3f800000;
    }
    if (scenario == 4) {
        in->rounding = 2;
        in->c_type |= UINT64_C(2) << 22;
    }
}

int
ztt_mixed_run_suite(const char *path)
{
    ztt_gemm_report_init(&report, 14, 17);
#if defined(__riscv)
    __asm__ volatile("li t0, 0x3c\n\tame.acquire t1, t0" ::
                         : "t0", "t1", "memory");
#endif
    struct input in;
    for (unsigned test = 0; test < 8; ++test) {
        const struct tuple *type = &tuples[test];
        case_names[test] = type->name;
        const unsigned scenarios = floating(type) ? 2 : 5;
        for (unsigned scenario = 0; scenario < scenarios; ++scenario) {
            prepare(type, scenario, &in);
            run_operations(test, type, &in);
        }
    }
    static const char *rounding_names[] = {
        "E4M3 x E5M2 FP32 RNE/RTZ/RDN, source RM independent, sticky flags",
        "E4M3 x E5M2 FP32 RUP/RMM/RNO, source RM independent, sticky flags",
        "E5M2 x E4M3 FP32 RNE/RTZ/RDN, source RM independent, sticky flags",
        "E5M2 x E4M3 FP32 RUP/RMM/RNO, source RM independent, sticky flags"};
    for (unsigned test = 0; test < 4; ++test) {
        const struct tuple *type = &tuples[2 + test / 2];
        case_names[8 + test] = rounding_names[test];
        for (unsigned mode = test % 2 * 3; mode < test % 2 * 3 + 3; ++mode) {
            prepare_rounding(type, mode, &in);
            run_operations(8 + test, type, &in);
        }
    }
    case_names[12] = "E4M3 x E5M2 NaN/Inf/sNaN/signed zero and NV";
    case_names[13] = "E5M2 x E4M3 NaN/Inf/sNaN/signed zero and NV";
    for (unsigned direction = 0; direction < 2; ++direction) {
        const struct tuple *type = &tuples[2 + direction];
        for (unsigned scenario = 0; scenario < 5; ++scenario) {
            prepare_special(type, scenario, &in);
            run_operations(12 + direction, type, &in);
        }
    }
    case_names[14] = "Unsupported neighboring tuples preserve ACC and flags";
    static const struct tuple rejected[] = {
        {I8, U4, ZTT_DTYPE_INT8, ZTT_DTYPE_UINT4, ZTT_DTYPE_UINT32, ""},
        {I4, U8, ZTT_DTYPE_INT4, ZTT_DTYPE_UINT8, ZTT_DTYPE_INT32, ""},
        {E4M3, I8, ZTT_DTYPE_OCP_E4M3_RNE, ZTT_DTYPE_INT8, ZTT_DTYPE_FP32_RNE,
         ""},
        {E4M3, E5M2, ZTT_DTYPE_OCP_E4M3_RNE, ZTT_DTYPE_OCP_E5M2_RNE,
         ZTT_DTYPE_INT32, ""},
    };
    for (unsigned test = 0; test < 4; ++test) {
        prepare(&rejected[test], 0, &in);
        in.unsupported = 1;
        in.initial_flags = 9;
        in.initial_sat = 1;
        run_operations(14, &rejected[test], &in);
    }
    case_names[15] = "M30 legal spans and M31 illegal spans preserve ACC";
    report.values[15][0] = (uint64_t)ztt_mixed_check_spans();
    report.case_values[15] = 1;
    case_names[16] = "Mixed inputs reject I64/FP16 ACC and preserve all bits";
    report.values[16][0] = (uint64_t)ztt_mixed_check_accumulator_types();
    report.case_values[16] = 1;
#if defined(__riscv)
    __asm__ volatile("ame.release" ::: "memory");
#endif
    return ztt_gemm_report_write(path, &report, case_names);
}
