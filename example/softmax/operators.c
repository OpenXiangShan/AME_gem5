#include <stdint.h>

#include "ztt_llm_case.h"

static float ztt_llm_output[ZTT_LLM_TILE_ELEMENTS] ZTT_LLM_ALIGNED;

#if !defined(__riscv)
static float
ztt_llm_pow2_integer(int exponent)
{
    uint32_t bits;
    if (exponent < -126)
        bits = 0;
    else if (exponent > 127)
        bits = UINT32_C(0x7f800000);
    else
        bits = (uint32_t)(exponent + 127) << 23;
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}
#endif

uint64_t
ztt_llm_run_case(struct ztt_llm_case_result *result)
{
    float input[ZTT_LLM_TILE_ELEMENTS] ZTT_LLM_ALIGNED;
    for (unsigned row = 0; row < ZTT_LLM_TILE; ++row)
        for (unsigned col = 0; col < ZTT_LLM_TILE; ++col)
            input[row * ZTT_LLM_TILE + col] =
                (float)((int)col - (int)row);

#if defined(__riscv)
    const uint64_t fp32 = ZTT_DTYPE_FP32_RNE;
    ztt_llm_acquire();
    __asm__ volatile(
        "msettyp m0, %[fp32]\n\t"
        "msettyp m1, %[fp32]\n\t"
        "msettyp m2, %[fp32]\n\t"
        "msettyp m3, %[fp32]\n\t"
        "msettyp m4, %[fp32]\n\t"
        "mls.rm m0, %[input]\n\t"
        "mreducemax.row m1, m0\n\t"
        "mmax.ew m1, m1, m0\n\t"
        "msub.ew m2, m1, m0\n\t"
        "mexp2.ew m2, m2\n\t"
        "mreduceadd.row m3, m2\n\t"
        "mrec.ew m3, m3\n\t"
        "mmul.ew m4, m2, m3\n\t"
        "mss.rm m4, %[output]"
        :: [fp32] "r"(fp32), [input] "r"(input),
           [output] "r"(ztt_llm_output)
        : "memory");
    result->status = ztt_llm_release();
#else
    for (unsigned row = 0; row < ZTT_LLM_TILE; ++row) {
        float maximum = input[row * ZTT_LLM_TILE];
        for (unsigned col = 1; col < ZTT_LLM_TILE; ++col) {
            const float value = input[row * ZTT_LLM_TILE + col];
            if (value > maximum)
                maximum = value;
        }
        float exponentials[ZTT_LLM_TILE];
        float sum = 0.0f;
        for (unsigned col = 0; col < ZTT_LLM_TILE; ++col) {
            const int shifted = (int)(
                input[row * ZTT_LLM_TILE + col] - maximum);
            exponentials[col] = ztt_llm_pow2_integer(shifted);
            sum += exponentials[col];
        }
        const float reciprocal = 1.0f / sum;
        for (unsigned col = 0; col < ZTT_LLM_TILE; ++col)
            ztt_llm_output[row * ZTT_LLM_TILE + col] =
                exponentials[col] * reciprocal;
    }
    result->status = 0;
#endif

    result->output = ztt_llm_output;
    result->element_count = ZTT_LLM_TILE_ELEMENTS;
    result->element_bytes = sizeof(ztt_llm_output[0]);
    return result->status;
}
