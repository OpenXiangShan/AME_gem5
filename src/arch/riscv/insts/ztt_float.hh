/*
 * Copyright (c) 2026 BOSC & ICT, CAS
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __ARCH_RISCV_INSTS_ZTT_FLOAT_HH__
#define __ARCH_RISCV_INSTS_ZTT_FLOAT_HH__

#include <softfloat.h>

#include <cstdint>

namespace gem5::RiscvISA
{

enum class ZttNarrowFloatFormat : uint8_t
{
    Bf16,
    E4M3,
    E5M2
};

enum class ZttFloatRoundingMode : uint8_t
{
    Rne = 0,
    Rtz = 1,
    Rdn = 2,
    Rup = 3,
    Rmm = 4,
    Rno = 5
};

enum class ZttFloatBinaryOp : uint8_t
{
    Add,
    Sub,
    Mul,
    Div
};

/**
 * Bit-exact backend for the standard narrow floating-point types used by
 * Ztt. BF16 conversion is delegated to gem5's SoftFloat extension. OCP E4M3
 * and E5M2 use the same field layout as the AMDGPU MXFP types, with Ztt's six
 * architectural rounding modes and SoftFloat exception flags.
 */
class ZttFloatBackend
{
  public:
    static bool isNaN(uint16_t raw, ZttNarrowFloatFormat format);
    static bool isSignalingNaN(uint16_t raw,
                               ZttNarrowFloatFormat format);
    static bool isInf(uint16_t raw, ZttNarrowFloatFormat format);

    static uint16_t canonicalNaN(ZttNarrowFloatFormat format);
    static uint16_t maxFinite(ZttNarrowFloatFormat format, bool negative);
    static uint16_t one(ZttNarrowFloatFormat format);

    static float32_t toF32(uint16_t raw, ZttNarrowFloatFormat format);
    static uint16_t fromF32(float32_t value, ZttNarrowFloatFormat format,
                            ZttFloatRoundingMode rounding);
    static uint16_t fromInteger(unsigned __int128 magnitude, bool negative,
                                ZttNarrowFloatFormat format,
                                ZttFloatRoundingMode rounding);

    static uint16_t convert(uint16_t raw, ZttNarrowFloatFormat source,
                            ZttNarrowFloatFormat dest,
                            ZttFloatRoundingMode rounding);
    static uint16_t binary(uint16_t lhs, uint16_t rhs,
                           ZttNarrowFloatFormat format,
                           ZttFloatRoundingMode rounding,
                           ZttFloatBinaryOp op);
    static uint16_t mulAdd(uint16_t lhs, uint16_t rhs, uint16_t addend,
                           ZttNarrowFloatFormat format,
                           ZttFloatRoundingMode rounding);
    static uint16_t halfSum(uint16_t lhs, uint16_t rhs,
                            ZttNarrowFloatFormat format,
                            ZttFloatRoundingMode rounding,
                            bool difference);
    static uint16_t scale(uint16_t raw, int exponent,
                          ZttNarrowFloatFormat format,
                          ZttFloatRoundingMode rounding);
    static uint16_t scaleAdd(uint16_t raw, int exponent, uint16_t addend,
                             ZttNarrowFloatFormat format,
                             ZttFloatRoundingMode rounding);
    static uint16_t sqrt(uint16_t raw, ZttNarrowFloatFormat format,
                         ZttFloatRoundingMode rounding);
    static uint16_t roundToInt(uint16_t raw, ZttNarrowFloatFormat format,
                               ZttFloatRoundingMode rounding);
    static bool less(uint16_t lhs, uint16_t rhs,
                     ZttNarrowFloatFormat format);
    static bool lessEqual(uint16_t lhs, uint16_t rhs,
                          ZttNarrowFloatFormat format);
};

} // namespace gem5::RiscvISA

#endif // __ARCH_RISCV_INSTS_ZTT_FLOAT_HH__
