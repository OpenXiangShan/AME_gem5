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
    float four[ZTT_LLM_TILE_ELEMENTS] ZTT_LLM_ALIGNED;
    float zero[ZTT_LLM_TILE_ELEMENTS] ZTT_LLM_ALIGNED;
    float one[ZTT_LLM_TILE_ELEMENTS] ZTT_LLM_ALIGNED;
    for (unsigned i = 0; i < ZTT_LLM_TILE_ELEMENTS; ++i) {
        input[i] = (float)((int)(i % 5) - 2);
        four[i] = 4.0f;
        zero[i] = 0.0f;
        one[i] = 1.0f;
    }
#if !defined(__riscv)
    (void)four;
    (void)zero;
    (void)one;
#endif

#if defined(__riscv)
    const uint64_t fp32 = ZTT_DTYPE_FP32_RNE;
    ztt_llm_acquire();
    __asm__ volatile(
        "msettyp m0, %[fp32]\n\t"
        "msettyp m1, %[fp32]\n\t"
        "msettyp m2, %[fp32]\n\t"
        "msettyp m3, %[fp32]\n\t"
        "msettyp m4, %[fp32]\n\t"
        "msettyp m5, %[fp32]\n\t"
        "msettyp m6, %[fp32]\n\t"
        "mls.rm m0, %[input]\n\t"
        "mls.rm m1, %[four]\n\t"
        "mls.rm m4, %[zero]\n\t"
        "mls.rm m5, %[one]\n\t"
        "msqrt.ew m2, m1\n\t"
        "mmul.ew m3, m0, m2\n\t"
        "msub.ew m3, m3, m4\n\t"
        "mexp2.ew m3, m3\n\t"
        "madd.ew m3, m3, m5\n\t"
        "mrec.ew m3, m3\n\t"
        "mmul.ew m6, m0, m3\n\t"
        "mss.rm m6, %[output]"
        :: [fp32] "r"(fp32), [input] "r"(input), [four] "r"(four),
           [zero] "r"(zero), [one] "r"(one),
           [output] "r"(ztt_llm_output)
        : "memory");
    result->status = ztt_llm_release();
#else
    for (unsigned i = 0; i < ZTT_LLM_TILE_ELEMENTS; ++i) {
        const float exponent = 0.0f - input[i] * 2.0f;
        const float denominator = 1.0f +
            ztt_llm_pow2_integer((int)exponent);
        ztt_llm_output[i] = input[i] * (1.0f / denominator);
    }
    result->status = 0;
#endif

    result->output = ztt_llm_output;
    result->element_count = ZTT_LLM_TILE_ELEMENTS;
    result->element_bytes = sizeof(ztt_llm_output[0]);
    return result->status;
}
