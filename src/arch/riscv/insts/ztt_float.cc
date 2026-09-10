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

#include "arch/riscv/insts/ztt_float.hh"

#include <algorithm>
#include <cassert>
#include <cstdint>

#include <boost/multiprecision/cpp_int.hpp>

namespace gem5::RiscvISA
{

namespace
{

using boost::multiprecision::cpp_int;

struct Fp8Info
{
    uint8_t exponentBits;
    uint8_t fractionBits;
    int bias;
    bool hasInfinity;
    uint8_t canonicalNaN;
    uint8_t maxFinite;
};

constexpr Fp8Info E4M3Info{4, 3, 7, false, 0x7f, 0x7e};
constexpr Fp8Info E5M2Info{5, 2, 15, true, 0x7e, 0x7b};
constexpr int MaxScaleExponent = 4096;

struct Dyadic
{
    cpp_int coefficient;
    int exponent;
};

const Fp8Info &
fp8Info(ZttNarrowFloatFormat format)
{
    assert(format != ZttNarrowFloatFormat::Bf16);
    return format == ZttNarrowFloatFormat::E4M3 ? E4M3Info : E5M2Info;
}

uint_fast8_t
softRoundingMode(ZttFloatRoundingMode mode)
{
    switch (mode) {
      case ZttFloatRoundingMode::Rne:
        return softfloat_round_near_even;
      case ZttFloatRoundingMode::Rtz:
        return softfloat_round_minMag;
      case ZttFloatRoundingMode::Rdn:
        return softfloat_round_min;
      case ZttFloatRoundingMode::Rup:
        return softfloat_round_max;
      case ZttFloatRoundingMode::Rmm:
        return softfloat_round_near_maxMag;
      case ZttFloatRoundingMode::Rno:
        return softfloat_round_odd;
    }
    return softfloat_round_near_even;
}

class ScopedRoundingMode
{
  private:
    const uint_fast8_t saved;

  public:
    explicit ScopedRoundingMode(uint_fast8_t mode)
        : saved(softfloat_roundingMode)
    {
        softfloat_roundingMode = mode;
    }

    ~ScopedRoundingMode()
    {
        softfloat_roundingMode = saved;
    }

    void set(uint_fast8_t mode) const { softfloat_roundingMode = mode; }
};

int
highestBit(uint32_t value)
{
    assert(value);
    return 31 - __builtin_clz(value);
}

int
highestBit(unsigned __int128 value)
{
    assert(value);
    const uint64_t high = value >> 64;
    return high ? 127 - __builtin_clzll(high) :
                  63 - __builtin_clzll(static_cast<uint64_t>(value));
}

float32_t
integerToF32RoundOdd(unsigned __int128 magnitude, bool negative)
{
    if (!magnitude)
        return float32_t{uint32_t(negative) << 31};

    const int top = highestBit(magnitude);
    uint32_t significand;
    bool inexact = false;
    if (top <= 23) {
        significand = static_cast<uint32_t>(magnitude) << (23 - top);
    } else {
        const int shift = top - 23;
        significand = static_cast<uint32_t>(magnitude >> shift);
        inexact = magnitude !=
            (static_cast<unsigned __int128>(significand) << shift);
        if (inexact) {
            significand |= 1;
            softfloat_exceptionFlags |= softfloat_flag_inexact;
        }
    }
    return float32_t{
        (uint32_t(negative) << 31) |
        (uint32_t(top + 127) << 23) |
        (significand & 0x7fffff)
    };
}

uint32_t
roundInteger(uint32_t significand, int shift, bool negative,
             ZttFloatRoundingMode mode, bool &inexact)
{
    if (shift <= 0) {
        inexact = false;
        return significand << -shift;
    }

    uint32_t truncated;
    uint32_t remainder;
    if (shift >= 32) {
        truncated = 0;
        remainder = significand;
    } else {
        truncated = significand >> shift;
        remainder = significand & ((uint32_t(1) << shift) - 1);
    }
    inexact = remainder != 0;
    if (!inexact)
        return truncated;

    if (mode == ZttFloatRoundingMode::Rno)
        return truncated | 1;

    bool increment = false;
    switch (mode) {
      case ZttFloatRoundingMode::Rne:
      case ZttFloatRoundingMode::Rmm:
        if (shift < 32) {
            const uint32_t half = uint32_t(1) << (shift - 1);
            increment = remainder > half ||
                (remainder == half &&
                 (mode == ZttFloatRoundingMode::Rmm || (truncated & 1)));
        }
        break;
      case ZttFloatRoundingMode::Rdn:
        increment = negative;
        break;
      case ZttFloatRoundingMode::Rup:
        increment = !negative;
        break;
      case ZttFloatRoundingMode::Rtz:
      case ZttFloatRoundingMode::Rno:
        break;
    }
    return truncated + increment;
}

uint8_t
fp8Overflow(const Fp8Info &info, bool negative,
            ZttFloatRoundingMode rounding)
{
    softfloat_raiseFlags(softfloat_flag_overflow | softfloat_flag_inexact);
    const bool toInfinity = info.hasInfinity &&
        (rounding == ZttFloatRoundingMode::Rne ||
         rounding == ZttFloatRoundingMode::Rmm ||
         (rounding == ZttFloatRoundingMode::Rup && !negative) ||
         (rounding == ZttFloatRoundingMode::Rdn && negative));
    const uint8_t magnitude = toInfinity ?
        uint8_t(((uint16_t(1) << info.exponentBits) - 1) <<
                info.fractionBits) : info.maxFinite;
    return magnitude | (negative ? 0x80 : 0);
}

uint8_t
f32ToFp8(float32_t value, ZttNarrowFloatFormat format,
         ZttFloatRoundingMode rounding)
{
    const Fp8Info &info = fp8Info(format);
    const uint32_t raw = value.v;
    const bool negative = raw >> 31;
    const uint32_t exponent = (raw >> 23) & 0xff;
    const uint32_t fraction = raw & 0x7fffff;
    const uint8_t sign = negative ? 0x80 : 0;

    if (exponent == 0xff) {
        if (fraction) {
            if (!(fraction & 0x400000))
                softfloat_raiseFlags(softfloat_flag_invalid);
            return info.canonicalNaN;
        }
        if (info.hasInfinity)
            return sign | uint8_t(((uint16_t(1) << info.exponentBits) - 1)
                                  << info.fractionBits);
        return fp8Overflow(info, negative, rounding);
    }
    if (!(exponent | fraction))
        return sign;

    const uint32_t significand = exponent ? 0x800000 | fraction : fraction;
    const int binaryExponent = exponent ? int(exponent) - 127 - 23 : -149;
    const int valueExponent = highestBit(significand) + binaryExponent;
    const int minNormalExponent = 1 - info.bias;
    const int maxExponentField =
        (uint16_t(1) << info.exponentBits) - 1;
    const int maxNormalExponent =
        (info.hasInfinity ? maxExponentField - 1 : maxExponentField) -
        info.bias;
    bool inexact = false;
    if (valueExponent < minNormalExponent) {
        const int quantumExponent =
            minNormalExponent - info.fractionBits;
        const uint32_t rounded = roundInteger(
            significand, quantumExponent - binaryExponent,
            negative, rounding, inexact);
        if (inexact)
            softfloat_exceptionFlags |= softfloat_flag_inexact;
        if (rounded >= (uint32_t(1) << info.fractionBits))
            return sign | (uint8_t(1) << info.fractionBits);
        if (inexact)
            softfloat_raiseFlags(softfloat_flag_underflow);
        return sign | uint8_t(rounded);
    }

    int resultExponent = valueExponent;
    uint32_t rounded = roundInteger(
        significand,
        resultExponent - info.fractionBits - binaryExponent,
        negative, rounding, inexact);
    if (rounded == (uint32_t(1) << (info.fractionBits + 1))) {
        rounded >>= 1;
        ++resultExponent;
    }
    const uint32_t maxSignificand =
        (uint32_t(1) << info.fractionBits) |
        (info.maxFinite & ((uint32_t(1) << info.fractionBits) - 1));
    if (resultExponent > maxNormalExponent ||
        (resultExponent == maxNormalExponent &&
         rounded > maxSignificand)) {
        return fp8Overflow(info, negative, rounding);
    }
    if (inexact)
        softfloat_exceptionFlags |= softfloat_flag_inexact;
    const uint8_t resultExp = resultExponent + info.bias;
    const uint8_t resultFrac = rounded -
        (uint32_t(1) << info.fractionBits);
    return sign | (resultExp << info.fractionBits) | resultFrac;
}

float32_t
fp8ToF32(uint8_t raw, ZttNarrowFloatFormat format)
{
    const Fp8Info &info = fp8Info(format);
    const bool negative = raw >> 7;
    const uint8_t fractionMask =
        (uint16_t(1) << info.fractionBits) - 1;
    const uint8_t exponentMask =
        (uint16_t(1) << info.exponentBits) - 1;
    const uint8_t fraction = raw & fractionMask;
    const uint8_t exponent = (raw >> info.fractionBits) & exponentMask;
    uint32_t result = uint32_t(negative) << 31;

    const bool nan = format == ZttNarrowFloatFormat::E4M3 ?
        exponent == exponentMask && fraction == fractionMask :
        exponent == exponentMask && fraction != 0;
    if (nan) {
        if (format == ZttNarrowFloatFormat::E5M2 &&
            !(fraction & (uint8_t(1) << (info.fractionBits - 1)))) {
            softfloat_raiseFlags(softfloat_flag_invalid);
        }
        return float32_t{0x7fc00000};
    }
    if (info.hasInfinity && exponent == exponentMask)
        return float32_t{result | 0x7f800000};
    if (!exponent) {
        if (!fraction)
            return float32_t{result};
        const int top = highestBit(uint32_t(fraction));
        const int unbiased =
            1 - info.bias - info.fractionBits + top;
        result |= uint32_t(unbiased + 127) << 23;
        result |= (uint32_t(fraction) << (23 - top)) & 0x7fffff;
        return float32_t{result};
    }
    result |= uint32_t(int(exponent) - info.bias + 127) << 23;
    result |= uint32_t(fraction) << (23 - info.fractionBits);
    return float32_t{result};
}

int
narrowFractionBits(ZttNarrowFloatFormat format)
{
    return format == ZttNarrowFloatFormat::Bf16 ? 7 :
        fp8Info(format).fractionBits;
}

int
narrowExponentBits(ZttNarrowFloatFormat format)
{
    return format == ZttNarrowFloatFormat::Bf16 ? 8 :
        fp8Info(format).exponentBits;
}

int
narrowBias(ZttNarrowFloatFormat format)
{
    return format == ZttNarrowFloatFormat::Bf16 ? 127 :
        fp8Info(format).bias;
}

bool
narrowHasInfinity(ZttNarrowFloatFormat format)
{
    return format == ZttNarrowFloatFormat::Bf16 ||
        fp8Info(format).hasInfinity;
}

uint16_t
narrowSignMask(ZttNarrowFloatFormat format)
{
    return format == ZttNarrowFloatFormat::Bf16 ? 0x8000 : 0x80;
}

uint16_t
narrowMaximum(ZttNarrowFloatFormat format)
{
    return format == ZttNarrowFloatFormat::Bf16 ? 0x7f7f :
        fp8Info(format).maxFinite;
}

Dyadic
finiteDyadic(uint16_t raw, ZttNarrowFloatFormat format)
{
    const int fractionBits = narrowFractionBits(format);
    const uint16_t fractionMask = (uint16_t(1) << fractionBits) - 1;
    const uint16_t exponentMask =
        (uint16_t(1) << narrowExponentBits(format)) - 1;
    const uint16_t magnitude = raw & (narrowSignMask(format) - 1);
    const uint16_t fraction = magnitude & fractionMask;
    const uint16_t exponent =
        (magnitude >> fractionBits) & exponentMask;
    Dyadic result;
    if (exponent) {
        result.coefficient = (uint16_t(1) << fractionBits) | fraction;
        result.exponent = int(exponent) - narrowBias(format) - fractionBits;
    } else {
        result.coefficient = fraction;
        result.exponent = 1 - narrowBias(format) - fractionBits;
    }
    if (raw & narrowSignMask(format))
        result.coefficient = -result.coefficient;
    return result;
}

cpp_int
roundDyadicInteger(const cpp_int &magnitude, int shift, bool negative,
                   ZttFloatRoundingMode rounding, bool &inexact)
{
    if (shift <= 0) {
        inexact = false;
        return magnitude << -shift;
    }

    cpp_int truncated = magnitude >> shift;
    const cpp_int remainder = magnitude - (truncated << shift);
    inexact = remainder != 0;
    if (!inexact)
        return truncated;
    if (rounding == ZttFloatRoundingMode::Rno)
        return truncated | 1;

    bool increment = false;
    switch (rounding) {
      case ZttFloatRoundingMode::Rne:
      case ZttFloatRoundingMode::Rmm: {
        const int remainderTop = static_cast<int>(
            boost::multiprecision::msb(remainder));
        const bool atLeastHalf = remainderTop == shift - 1;
        const bool exactlyHalf = atLeastHalf &&
            !(remainder & (remainder - 1));
        increment = (atLeastHalf && !exactlyHalf) ||
            (exactlyHalf &&
             (rounding == ZttFloatRoundingMode::Rmm ||
              static_cast<bool>(truncated & 1)));
        break;
      }
      case ZttFloatRoundingMode::Rdn:
        increment = negative;
        break;
      case ZttFloatRoundingMode::Rup:
        increment = !negative;
        break;
      case ZttFloatRoundingMode::Rtz:
      case ZttFloatRoundingMode::Rno:
        break;
    }
    return truncated + increment;
}

uint16_t
narrowOverflow(ZttNarrowFloatFormat format, bool negative,
               ZttFloatRoundingMode rounding)
{
    softfloat_raiseFlags(softfloat_flag_overflow | softfloat_flag_inexact);
    const bool toInfinity = narrowHasInfinity(format) &&
        (rounding == ZttFloatRoundingMode::Rne ||
         rounding == ZttFloatRoundingMode::Rmm ||
         (rounding == ZttFloatRoundingMode::Rup && !negative) ||
         (rounding == ZttFloatRoundingMode::Rdn && negative));
    const uint16_t magnitude = toInfinity ?
        ((uint16_t(1) << narrowExponentBits(format)) - 1) <<
            narrowFractionBits(format) :
        narrowMaximum(format);
    return magnitude | (negative ? narrowSignMask(format) : 0);
}

uint16_t
roundDyadic(cpp_int coefficient, int binaryExponent,
            ZttNarrowFloatFormat format, ZttFloatRoundingMode rounding)
{
    if (!coefficient)
        return 0;
    const bool negative = coefficient < 0;
    const cpp_int magnitude = negative ? -coefficient : coefficient;
    const int top = static_cast<int>(boost::multiprecision::msb(magnitude));
    const int valueExponent = top + binaryExponent;
    const int fractionBits = narrowFractionBits(format);
    const int bias = narrowBias(format);
    const int minNormalExponent = 1 - bias;
    const int exponentMask =
        (uint16_t(1) << narrowExponentBits(format)) - 1;
    const int maxNormalExponent =
        (narrowHasInfinity(format) ? exponentMask - 1 : exponentMask) - bias;
    const uint32_t hidden = uint32_t(1) << fractionBits;
    const uint32_t maxSignificand = hidden |
        (narrowMaximum(format) & (hidden - 1));
    const uint16_t sign = negative ? narrowSignMask(format) : 0;

    bool inexact = false;
    if (valueExponent < minNormalExponent) {
        const int quantumExponent = minNormalExponent - fractionBits;
        const cpp_int roundedValue = roundDyadicInteger(
            magnitude, quantumExponent - binaryExponent, negative,
            rounding, inexact);
        const uint32_t rounded = roundedValue.convert_to<uint32_t>();
        if (inexact)
            softfloat_exceptionFlags |= softfloat_flag_inexact;
        if (rounded >= hidden)
            return sign | hidden;
        if (inexact)
            softfloat_raiseFlags(softfloat_flag_underflow);
        return sign | rounded;
    }

    int resultExponent = valueExponent;
    cpp_int roundedValue = roundDyadicInteger(
        magnitude, resultExponent - fractionBits - binaryExponent,
        negative, rounding, inexact);
    uint32_t rounded = roundedValue.convert_to<uint32_t>();
    if (rounded == (hidden << 1)) {
        rounded >>= 1;
        ++resultExponent;
    }
    if (resultExponent > maxNormalExponent ||
        (resultExponent == maxNormalExponent &&
         rounded > maxSignificand)) {
        return narrowOverflow(format, negative, rounding);
    }
    if (inexact)
        softfloat_exceptionFlags |= softfloat_flag_inexact;
    const uint16_t exponent = resultExponent + bias;
    return sign | (exponent << fractionBits) | (rounded - hidden);
}

uint16_t
zeroForRounding(ZttNarrowFloatFormat format,
                ZttFloatRoundingMode rounding)
{
    if (rounding != ZttFloatRoundingMode::Rdn)
        return 0;
    return format == ZttNarrowFloatFormat::Bf16 ? 0x8000 : 0x80;
}

bool
exactBinaryCancellation(float32_t lhs, float32_t rhs,
                        ZttFloatBinaryOp op)
{
    const uint32_t lhsMagnitude = lhs.v & 0x7fffffff;
    const uint32_t rhsMagnitude = rhs.v & 0x7fffffff;
    if (lhsMagnitude >= 0x7f800000 || rhsMagnitude >= 0x7f800000)
        return false;
    if (op == ZttFloatBinaryOp::Add) {
        return lhsMagnitude == rhsMagnitude &&
               ((lhs.v ^ rhs.v) & 0x80000000);
    }
    if (op == ZttFloatBinaryOp::Sub)
        return lhs.v == rhs.v;
    return false;
}

} // anonymous namespace

bool
ZttFloatBackend::isNaN(uint16_t raw, ZttNarrowFloatFormat format)
{
    if (format == ZttNarrowFloatFormat::Bf16)
        return (raw & 0x7f80) == 0x7f80 && (raw & 0x007f);
    const Fp8Info &info = fp8Info(format);
    const uint8_t value = raw;
    const uint8_t fractionMask =
        (uint16_t(1) << info.fractionBits) - 1;
    const uint8_t exponentMask =
        (uint16_t(1) << info.exponentBits) - 1;
    const uint8_t fraction = value & fractionMask;
    const uint8_t exponent = (value >> info.fractionBits) & exponentMask;
    return format == ZttNarrowFloatFormat::E4M3 ?
        exponent == exponentMask && fraction == fractionMask :
        exponent == exponentMask && fraction != 0;
}

bool
ZttFloatBackend::isSignalingNaN(uint16_t raw,
                                ZttNarrowFloatFormat format)
{
    if (!isNaN(raw, format) || format == ZttNarrowFloatFormat::E4M3)
        return false;
    const int quietBit = format == ZttNarrowFloatFormat::Bf16 ? 6 : 1;
    return !(raw & (uint16_t(1) << quietBit));
}

bool
ZttFloatBackend::isInf(uint16_t raw, ZttNarrowFloatFormat format)
{
    if (format == ZttNarrowFloatFormat::E4M3)
        return false;
    return format == ZttNarrowFloatFormat::Bf16 ?
        (raw & 0x7fff) == 0x7f80 : (raw & 0x7f) == 0x7c;
}

uint16_t
ZttFloatBackend::canonicalNaN(ZttNarrowFloatFormat format)
{
    switch (format) {
      case ZttNarrowFloatFormat::Bf16: return 0x7fc0;
      case ZttNarrowFloatFormat::E4M3: return E4M3Info.canonicalNaN;
      case ZttNarrowFloatFormat::E5M2: return E5M2Info.canonicalNaN;
    }
    return 0;
}

uint16_t
ZttFloatBackend::maxFinite(ZttNarrowFloatFormat format, bool negative)
{
    const uint16_t magnitude = format == ZttNarrowFloatFormat::Bf16 ?
        0x7f7f : fp8Info(format).maxFinite;
    return magnitude | (negative ?
        (format == ZttNarrowFloatFormat::Bf16 ? 0x8000 : 0x80) : 0);
}

uint16_t
ZttFloatBackend::one(ZttNarrowFloatFormat format)
{
    switch (format) {
      case ZttNarrowFloatFormat::Bf16: return 0x3f80;
      case ZttNarrowFloatFormat::E4M3: return 0x38;
      case ZttNarrowFloatFormat::E5M2: return 0x3c;
    }
    return 0;
}

float32_t
ZttFloatBackend::toF32(uint16_t raw, ZttNarrowFloatFormat format)
{
    if (format == ZttNarrowFloatFormat::Bf16)
        return bf16_to_f32(bfloat16_t{raw});
    return fp8ToF32(raw, format);
}

uint16_t
ZttFloatBackend::fromF32(float32_t value, ZttNarrowFloatFormat format,
                         ZttFloatRoundingMode rounding)
{
    ScopedRoundingMode scope(softRoundingMode(rounding));
    if (format == ZttNarrowFloatFormat::Bf16)
        return f32_to_bf16(value).v;
    return f32ToFp8(value, format, rounding);
}

uint16_t
ZttFloatBackend::fromInteger(unsigned __int128 magnitude, bool negative,
                             ZttNarrowFloatFormat format,
                             ZttFloatRoundingMode rounding)
{
    const float32_t carrier = integerToF32RoundOdd(magnitude, negative);
    return fromF32(carrier, format, rounding);
}

uint16_t
ZttFloatBackend::convert(uint16_t raw, ZttNarrowFloatFormat source,
                         ZttNarrowFloatFormat dest,
                         ZttFloatRoundingMode rounding)
{
    if (source == dest) {
        if (isSignalingNaN(raw, source))
            softfloat_raiseFlags(softfloat_flag_invalid);
        return isNaN(raw, source) ? canonicalNaN(dest) : raw;
    }
    return fromF32(toF32(raw, source), dest, rounding);
}

uint16_t
ZttFloatBackend::binary(uint16_t lhs, uint16_t rhs,
                        ZttNarrowFloatFormat format,
                        ZttFloatRoundingMode rounding,
                        ZttFloatBinaryOp op)
{
    const float32_t a = toF32(lhs, format);
    const float32_t b = toF32(rhs, format);
    if (exactBinaryCancellation(a, b, op))
        return zeroForRounding(format, rounding);
    ScopedRoundingMode scope(softfloat_round_odd);
    float32_t result{};
    switch (op) {
      case ZttFloatBinaryOp::Add: result = f32_add(a, b); break;
      case ZttFloatBinaryOp::Sub: result = f32_sub(a, b); break;
      case ZttFloatBinaryOp::Mul: result = f32_mul(a, b); break;
      case ZttFloatBinaryOp::Div: result = f32_div(a, b); break;
    }
    scope.set(softRoundingMode(rounding));
    if (format == ZttNarrowFloatFormat::Bf16)
        return f32_to_bf16(result).v;
    return f32ToFp8(result, format, rounding);
}

uint16_t
ZttFloatBackend::mulAdd(uint16_t lhs, uint16_t rhs, uint16_t addend,
                        ZttNarrowFloatFormat format,
                        ZttFloatRoundingMode rounding)
{
    const float32_t a = toF32(lhs, format);
    const float32_t b = toF32(rhs, format);
    const float32_t c = toF32(addend, format);
    ScopedRoundingMode scope(softfloat_round_odd);
    const uint_fast8_t savedFlags = softfloat_exceptionFlags;
    scope.set(softfloat_round_near_even);
    softfloat_exceptionFlags = 0;
    const float32_t product = f32_mul(a, b);
    const bool cancellation = !softfloat_exceptionFlags &&
        (product.v & 0x7fffffff) != 0 &&
        (product.v & 0x7fffffff) < 0x7f800000 &&
        (product.v & 0x7fffffff) == (c.v & 0x7fffffff) &&
        ((product.v ^ c.v) & 0x80000000);
    softfloat_exceptionFlags = savedFlags;
    if (cancellation)
        return zeroForRounding(format, rounding);
    scope.set(softfloat_round_odd);
    const float32_t result = f32_mulAdd(a, b, c);
    scope.set(softRoundingMode(rounding));
    if (format == ZttNarrowFloatFormat::Bf16)
        return f32_to_bf16(result).v;
    return f32ToFp8(result, format, rounding);
}

uint16_t
ZttFloatBackend::halfSum(uint16_t lhs, uint16_t rhs,
                         ZttNarrowFloatFormat format,
                         ZttFloatRoundingMode rounding, bool difference)
{
    if (isNaN(lhs, format) || isNaN(rhs, format) ||
        isInf(lhs, format) || isInf(rhs, format)) {
        const uint16_t combined = difference ?
            binary(rhs, lhs, format, rounding, ZttFloatBinaryOp::Sub) :
            binary(lhs, rhs, format, rounding, ZttFloatBinaryOp::Add);
        return scale(combined, -1, format, rounding);
    }

    const Dyadic left = finiteDyadic(lhs, format);
    const Dyadic right = finiteDyadic(rhs, format);
    const int commonExponent = std::min(left.exponent, right.exponent);
    const cpp_int leftValue =
        left.coefficient << (left.exponent - commonExponent);
    const cpp_int rightValue =
        right.coefficient << (right.exponent - commonExponent);
    cpp_int sum = leftValue + rightValue;
    if (difference)
        sum = rightValue - leftValue;
    if (!sum) {
        return difference ?
            binary(rhs, lhs, format, rounding, ZttFloatBinaryOp::Sub) :
            binary(lhs, rhs, format, rounding, ZttFloatBinaryOp::Add);
    }
    return roundDyadic(sum, commonExponent - 1, format, rounding);
}

uint16_t
ZttFloatBackend::scale(uint16_t raw, int exponent,
                       ZttNarrowFloatFormat format,
                       ZttFloatRoundingMode rounding)
{
    if (isNaN(raw, format)) {
        if (isSignalingNaN(raw, format))
            softfloat_raiseFlags(softfloat_flag_invalid);
        return canonicalNaN(format);
    }
    if (isInf(raw, format))
        return raw;

    Dyadic value = finiteDyadic(raw, format);
    if (!value.coefficient)
        return raw & narrowSignMask(format);
    exponent = std::clamp(
        exponent, -MaxScaleExponent, MaxScaleExponent);
    value.exponent += exponent;
    return roundDyadic(value.coefficient, value.exponent, format, rounding);
}

uint16_t
ZttFloatBackend::scaleAdd(uint16_t raw, int exponent, uint16_t addend,
                          ZttNarrowFloatFormat format,
                          ZttFloatRoundingMode rounding)
{
    const bool rawNaN = isNaN(raw, format);
    const bool addendNaN = isNaN(addend, format);
    if (rawNaN || addendNaN) {
        if ((rawNaN && isSignalingNaN(raw, format)) ||
            (addendNaN && isSignalingNaN(addend, format))) {
            softfloat_raiseFlags(softfloat_flag_invalid);
        }
        return canonicalNaN(format);
    }

    const bool rawInf = isInf(raw, format);
    const bool addendInf = isInf(addend, format);
    if (rawInf && addendInf &&
        ((raw ^ addend) & narrowSignMask(format))) {
        softfloat_raiseFlags(softfloat_flag_invalid);
        return canonicalNaN(format);
    }
    if (rawInf)
        return raw;
    if (addendInf)
        return addend;

    exponent = std::clamp(
        exponent, -MaxScaleExponent, MaxScaleExponent);
    const Dyadic value = finiteDyadic(raw, format);
    const Dyadic addendValue = finiteDyadic(addend, format);
    const int valueExponent = value.exponent + exponent;
    const int commonExponent =
        std::min(valueExponent, addendValue.exponent);
    const cpp_int scaledValue =
        value.coefficient << (valueExponent - commonExponent);
    const cpp_int alignedAddend =
        addendValue.coefficient <<
            (addendValue.exponent - commonExponent);
    const cpp_int sum = scaledValue + alignedAddend;
    if (!sum) {
        if (!value.coefficient && !addendValue.coefficient) {
            const bool valueNegative = raw & narrowSignMask(format);
            const bool addendNegative = addend & narrowSignMask(format);
            if (valueNegative == addendNegative)
                return valueNegative ? narrowSignMask(format) : 0;
        }
        return zeroForRounding(format, rounding);
    }
    return roundDyadic(sum, commonExponent, format, rounding);
}

uint16_t
ZttFloatBackend::sqrt(uint16_t raw, ZttNarrowFloatFormat format,
                      ZttFloatRoundingMode rounding)
{
    ScopedRoundingMode scope(softfloat_round_odd);
    const float32_t result = f32_sqrt(toF32(raw, format));
    scope.set(softRoundingMode(rounding));
    if (format == ZttNarrowFloatFormat::Bf16)
        return f32_to_bf16(result).v;
    return f32ToFp8(result, format, rounding);
}

uint16_t
ZttFloatBackend::roundToInt(uint16_t raw, ZttNarrowFloatFormat format,
                            ZttFloatRoundingMode rounding)
{
    const float32_t result = f32_roundToInt(
        toF32(raw, format), softRoundingMode(rounding), true);
    return fromF32(result, format, rounding);
}

bool
ZttFloatBackend::less(uint16_t lhs, uint16_t rhs,
                      ZttNarrowFloatFormat format)
{
    return f32_lt_quiet(toF32(lhs, format), toF32(rhs, format));
}

bool
ZttFloatBackend::lessEqual(uint16_t lhs, uint16_t rhs,
                           ZttNarrowFloatFormat format)
{
    return f32_le_quiet(toF32(lhs, format), toF32(rhs, format));
}

} // namespace gem5::RiscvISA
