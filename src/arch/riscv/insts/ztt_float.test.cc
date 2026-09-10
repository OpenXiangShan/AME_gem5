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

#include <gtest/gtest.h>

#include "arch/riscv/insts/ztt_float.hh"

namespace gem5::RiscvISA
{

namespace
{

class ZttFloatTest : public testing::Test
{
  protected:
    uint_fast8_t savedRounding = softfloat_roundingMode;
    uint_fast8_t savedTininess = softfloat_detectTininess;
    uint_fast8_t savedFlags = softfloat_exceptionFlags;

    void SetUp() override
    {
        softfloat_roundingMode = softfloat_round_near_even;
        softfloat_detectTininess = softfloat_tininess_afterRounding;
        softfloat_exceptionFlags = 0;
    }

    void TearDown() override
    {
        softfloat_roundingMode = savedRounding;
        softfloat_detectTininess = savedTininess;
        softfloat_exceptionFlags = savedFlags;
    }
};

TEST_F(ZttFloatTest, Fp8RoundTripAllEncodings)
{
    for (const auto format : {ZttNarrowFloatFormat::E4M3,
                              ZttNarrowFloatFormat::E5M2}) {
        for (unsigned raw = 0; raw < 256; ++raw) {
            softfloat_exceptionFlags = 0;
            const uint16_t converted = ZttFloatBackend::fromF32(
                ZttFloatBackend::toF32(raw, format), format,
                ZttFloatRoundingMode::Rne);
            const uint16_t expected = ZttFloatBackend::isNaN(raw, format) ?
                ZttFloatBackend::canonicalNaN(format) : raw;
            EXPECT_EQ(converted, expected)
                << "format=" << static_cast<int>(format)
                << " raw=" << raw;
        }
    }
}

TEST_F(ZttFloatTest, Bf16UsesSoftFloatConversion)
{
    for (const uint16_t raw : {uint16_t(0x0000), uint16_t(0x8000),
                               uint16_t(0x0001), uint16_t(0x3f80),
                               uint16_t(0x7f7f), uint16_t(0x7f80)}) {
        EXPECT_EQ(ZttFloatBackend::fromF32(
                      ZttFloatBackend::toF32(
                          raw, ZttNarrowFloatFormat::Bf16),
                      ZttNarrowFloatFormat::Bf16,
                      ZttFloatRoundingMode::Rne),
                  raw);
    }
    EXPECT_EQ(ZttFloatBackend::fromF32(
                  ZttFloatBackend::toF32(
                      0x7f81, ZttNarrowFloatFormat::Bf16),
                  ZttNarrowFloatFormat::Bf16,
                  ZttFloatRoundingMode::Rne),
              0x7fc0);
    EXPECT_TRUE(softfloat_exceptionFlags & softfloat_flag_invalid);
}

TEST_F(ZttFloatTest, E4M3SixRoundingModes)
{
    const float32_t positiveHalf{0x3f880000}; // 1.0625
    const float32_t negativeHalf{0xbf880000}; // -1.0625
    struct Expected
    {
        ZttFloatRoundingMode mode;
        uint8_t positive;
        uint8_t negative;
    };
    const Expected expected[] = {
        {ZttFloatRoundingMode::Rne, 0x38, 0xb8},
        {ZttFloatRoundingMode::Rtz, 0x38, 0xb8},
        {ZttFloatRoundingMode::Rdn, 0x38, 0xb9},
        {ZttFloatRoundingMode::Rup, 0x39, 0xb8},
        {ZttFloatRoundingMode::Rmm, 0x39, 0xb9},
        {ZttFloatRoundingMode::Rno, 0x39, 0xb9},
    };
    for (const auto &item : expected) {
        softfloat_exceptionFlags = 0;
        EXPECT_EQ(ZttFloatBackend::fromF32(
                      positiveHalf, ZttNarrowFloatFormat::E4M3, item.mode),
                  item.positive);
        EXPECT_TRUE(softfloat_exceptionFlags & softfloat_flag_inexact);
        softfloat_exceptionFlags = 0;
        EXPECT_EQ(ZttFloatBackend::fromF32(
                      negativeHalf, ZttNarrowFloatFormat::E4M3, item.mode),
                  item.negative);
        EXPECT_TRUE(softfloat_exceptionFlags & softfloat_flag_inexact);
    }
}

TEST_F(ZttFloatTest, E5M2SixRoundingModes)
{
    const float32_t positiveHalf{0x3f900000}; // 1.125
    const float32_t negativeHalf{0xbf900000}; // -1.125
    const uint8_t positive[] = {0x3c, 0x3c, 0x3c, 0x3d, 0x3d, 0x3d};
    const uint8_t negative[] = {0xbc, 0xbc, 0xbd, 0xbc, 0xbd, 0xbd};
    for (unsigned mode = 0; mode < 6; ++mode) {
        softfloat_exceptionFlags = 0;
        EXPECT_EQ(ZttFloatBackend::fromF32(
                      positiveHalf, ZttNarrowFloatFormat::E5M2,
                      static_cast<ZttFloatRoundingMode>(mode)),
                  positive[mode]);
        EXPECT_TRUE(softfloat_exceptionFlags & softfloat_flag_inexact);
        softfloat_exceptionFlags = 0;
        EXPECT_EQ(ZttFloatBackend::fromF32(
                      negativeHalf, ZttNarrowFloatFormat::E5M2,
                      static_cast<ZttFloatRoundingMode>(mode)),
                  negative[mode]);
        EXPECT_TRUE(softfloat_exceptionFlags & softfloat_flag_inexact);
    }
}

TEST_F(ZttFloatTest, Bf16SixRoundingModes)
{
    const float32_t positiveHalf{0x3f808000}; // 1 + 2^-8
    const float32_t negativeHalf{0xbf808000}; // -(1 + 2^-8)
    const uint16_t positive[] = {
        0x3f80, 0x3f80, 0x3f80, 0x3f81, 0x3f81, 0x3f81
    };
    const uint16_t negative[] = {
        0xbf80, 0xbf80, 0xbf81, 0xbf80, 0xbf81, 0xbf81
    };
    for (unsigned mode = 0; mode < 6; ++mode) {
        softfloat_exceptionFlags = 0;
        EXPECT_EQ(ZttFloatBackend::fromF32(
                      positiveHalf, ZttNarrowFloatFormat::Bf16,
                      static_cast<ZttFloatRoundingMode>(mode)),
                  positive[mode]);
        EXPECT_TRUE(softfloat_exceptionFlags & softfloat_flag_inexact);
        softfloat_exceptionFlags = 0;
        EXPECT_EQ(ZttFloatBackend::fromF32(
                      negativeHalf, ZttNarrowFloatFormat::Bf16,
                      static_cast<ZttFloatRoundingMode>(mode)),
                  negative[mode]);
        EXPECT_TRUE(softfloat_exceptionFlags & softfloat_flag_inexact);
    }
}

TEST_F(ZttFloatTest, Fp8OverflowIsDecidedAfterRounding)
{
    // 450 is above E4M3's maximum finite value (448), but RNE rounds back
    // to 448.  RUP rounds to the reserved 0x7f encoding and therefore
    // overflows instead.
    const float32_t e4NearMaximum{0x43e10000}; // 450
    EXPECT_EQ(ZttFloatBackend::fromF32(
                  e4NearMaximum, ZttNarrowFloatFormat::E4M3,
                  ZttFloatRoundingMode::Rne),
              0x7e);
    EXPECT_EQ(softfloat_exceptionFlags, softfloat_flag_inexact);

    softfloat_exceptionFlags = 0;
    EXPECT_EQ(ZttFloatBackend::fromF32(
                  e4NearMaximum, ZttNarrowFloatFormat::E4M3,
                  ZttFloatRoundingMode::Rup),
              0x7e);
    EXPECT_EQ(softfloat_exceptionFlags &
                  (softfloat_flag_overflow | softfloat_flag_inexact),
              softfloat_flag_overflow | softfloat_flag_inexact);

    // The same distinction exists at the top of E5M2's finite range.
    const float32_t e5NearMaximum{0x47600100}; // 57345
    softfloat_exceptionFlags = 0;
    EXPECT_EQ(ZttFloatBackend::fromF32(
                  e5NearMaximum, ZttNarrowFloatFormat::E5M2,
                  ZttFloatRoundingMode::Rne),
              0x7b);
    EXPECT_EQ(softfloat_exceptionFlags, softfloat_flag_inexact);

    softfloat_exceptionFlags = 0;
    EXPECT_EQ(ZttFloatBackend::fromF32(
                  e5NearMaximum, ZttNarrowFloatFormat::E5M2,
                  ZttFloatRoundingMode::Rup),
              0x7c);
    EXPECT_EQ(softfloat_exceptionFlags &
                  (softfloat_flag_overflow | softfloat_flag_inexact),
              softfloat_flag_overflow | softfloat_flag_inexact);
}

TEST_F(ZttFloatTest, OverflowResultsFollowAllRoundingModes)
{
    constexpr ZttFloatRoundingMode modes[] = {
        ZttFloatRoundingMode::Rne, ZttFloatRoundingMode::Rtz,
        ZttFloatRoundingMode::Rdn, ZttFloatRoundingMode::Rup,
        ZttFloatRoundingMode::Rmm, ZttFloatRoundingMode::Rno
    };
    constexpr uint16_t e5Expected[] = {
        0x7c, 0x7b, 0x7b, 0x7c, 0x7c, 0x7b
    };
    constexpr uint16_t bf16Expected[] = {
        0x7f80, 0x7f7f, 0x7f7f, 0x7f80, 0x7f80, 0x7f7f
    };
    for (unsigned i = 0; i < 6; ++i) {
        softfloat_exceptionFlags = 0;
        EXPECT_EQ(ZttFloatBackend::fromF32(
                      float32_t{0x7f7fffff}, ZttNarrowFloatFormat::E4M3,
                      modes[i]),
                  0x7e);
        EXPECT_EQ(softfloat_exceptionFlags &
                      (softfloat_flag_overflow | softfloat_flag_inexact),
                  softfloat_flag_overflow | softfloat_flag_inexact);

        softfloat_exceptionFlags = 0;
        EXPECT_EQ(ZttFloatBackend::fromF32(
                      float32_t{0x7f7fffff}, ZttNarrowFloatFormat::E5M2,
                      modes[i]),
                  e5Expected[i]);
        EXPECT_EQ(softfloat_exceptionFlags &
                      (softfloat_flag_overflow | softfloat_flag_inexact),
                  softfloat_flag_overflow | softfloat_flag_inexact);

        softfloat_exceptionFlags = 0;
        EXPECT_EQ(ZttFloatBackend::fromF32(
                      float32_t{0x7f7fffff}, ZttNarrowFloatFormat::Bf16,
                      modes[i]),
                  bf16Expected[i]);
        const uint_fast8_t bf16Flags = i == 0 || i == 3 || i == 4 ?
            softfloat_flag_overflow | softfloat_flag_inexact :
            softfloat_flag_inexact;
        EXPECT_EQ(softfloat_exceptionFlags, bf16Flags);
    }
}

TEST_F(ZttFloatTest, TinyResultsUseTininessAfterRounding)
{
    const float32_t halfE4Subnormal{0x3a800000}; // 2^-10
    EXPECT_EQ(ZttFloatBackend::fromF32(
                  halfE4Subnormal, ZttNarrowFloatFormat::E4M3,
                  ZttFloatRoundingMode::Rne),
              0x00);
    EXPECT_EQ(softfloat_exceptionFlags &
                  (softfloat_flag_underflow | softfloat_flag_inexact),
              softfloat_flag_underflow | softfloat_flag_inexact);

    softfloat_exceptionFlags = 0;
    EXPECT_EQ(ZttFloatBackend::fromF32(
                  halfE4Subnormal, ZttNarrowFloatFormat::E4M3,
                  ZttFloatRoundingMode::Rup),
              0x01);
    EXPECT_EQ(softfloat_exceptionFlags &
                  (softfloat_flag_underflow | softfloat_flag_inexact),
              softfloat_flag_underflow | softfloat_flag_inexact);

    // An inexact value that rounds up to the minimum normal is not tiny
    // after rounding, so only NX is accrued.
    softfloat_exceptionFlags = 0;
    EXPECT_EQ(ZttFloatBackend::fromF32(
                  float32_t{0x3c780000}, ZttNarrowFloatFormat::E4M3,
                  ZttFloatRoundingMode::Rne),
              0x08);
    EXPECT_EQ(softfloat_exceptionFlags, softfloat_flag_inexact);
}

TEST_F(ZttFloatTest, InvalidAndDivideByZeroFlags)
{
    EXPECT_EQ(ZttFloatBackend::toF32(
                  0x7d, ZttNarrowFloatFormat::E5M2).v,
              0x7fc00000);
    EXPECT_TRUE(softfloat_exceptionFlags & softfloat_flag_invalid);

    softfloat_exceptionFlags = 0;
    EXPECT_EQ(ZttFloatBackend::binary(
                  0x00, 0x7c, ZttNarrowFloatFormat::E5M2,
                  ZttFloatRoundingMode::Rne, ZttFloatBinaryOp::Mul),
              0x7e);
    EXPECT_TRUE(softfloat_exceptionFlags & softfloat_flag_invalid);

    softfloat_exceptionFlags = 0;
    EXPECT_EQ(ZttFloatBackend::sqrt(
                  0xbc, ZttNarrowFloatFormat::E5M2,
                  ZttFloatRoundingMode::Rne),
              0x7e);
    EXPECT_TRUE(softfloat_exceptionFlags & softfloat_flag_invalid);

    softfloat_exceptionFlags = 0;
    EXPECT_EQ(ZttFloatBackend::binary(
                  0x3c, 0x00, ZttNarrowFloatFormat::E5M2,
                  ZttFloatRoundingMode::Rne, ZttFloatBinaryOp::Div),
              0x7c);
    EXPECT_EQ(softfloat_exceptionFlags, softfloat_flag_infinite);
}

TEST_F(ZttFloatTest, IntegerConversionUsesTheNarrowBackend)
{
    constexpr ZttFloatRoundingMode modes[] = {
        ZttFloatRoundingMode::Rne, ZttFloatRoundingMode::Rtz,
        ZttFloatRoundingMode::Rdn, ZttFloatRoundingMode::Rup,
        ZttFloatRoundingMode::Rmm, ZttFloatRoundingMode::Rno
    };
    constexpr uint16_t e4Expected[] = {
        0x70, 0x70, 0x70, 0x71, 0x71, 0x71
    };
    for (unsigned i = 0; i < 6; ++i) {
        softfloat_exceptionFlags = 0;
        EXPECT_EQ(ZttFloatBackend::fromInteger(
                      136, false, ZttNarrowFloatFormat::E4M3, modes[i]),
                  e4Expected[i]);
        EXPECT_EQ(softfloat_exceptionFlags, softfloat_flag_inexact);
    }

    const unsigned __int128 uint128Max =
        ~static_cast<unsigned __int128>(0);
    constexpr uint16_t bf16Expected[] = {
        0x7f80, 0x7f7f, 0x7f7f, 0x7f80, 0x7f80, 0x7f7f
    };
    for (unsigned i = 0; i < 6; ++i) {
        softfloat_exceptionFlags = 0;
        EXPECT_EQ(ZttFloatBackend::fromInteger(
                      uint128Max, false, ZttNarrowFloatFormat::Bf16,
                      modes[i]),
                  bf16Expected[i]);
        EXPECT_TRUE(softfloat_exceptionFlags & softfloat_flag_inexact);
        EXPECT_EQ(bool(softfloat_exceptionFlags & softfloat_flag_overflow),
                  i == 0 || i == 3 || i == 4);
    }
}

TEST_F(ZttFloatTest, ExactCancellationUsesTheFinalRoundingMode)
{
    struct FormatValues
    {
        ZttNarrowFloatFormat format;
        uint16_t one;
        uint16_t negativeOne;
        uint16_t negativeZero;
    };
    constexpr FormatValues values[] = {
        {ZttNarrowFloatFormat::Bf16, 0x3f80, 0xbf80, 0x8000},
        {ZttNarrowFloatFormat::E4M3, 0x38, 0xb8, 0x80},
        {ZttNarrowFloatFormat::E5M2, 0x3c, 0xbc, 0x80}
    };
    for (const auto &value : values) {
        EXPECT_EQ(ZttFloatBackend::binary(
                      value.one, value.negativeOne, value.format,
                      ZttFloatRoundingMode::Rne, ZttFloatBinaryOp::Add),
                  0);
        EXPECT_EQ(ZttFloatBackend::binary(
                      value.one, value.negativeOne, value.format,
                      ZttFloatRoundingMode::Rdn, ZttFloatBinaryOp::Add),
                  value.negativeZero);
        EXPECT_EQ(ZttFloatBackend::binary(
                      value.one, value.one, value.format,
                      ZttFloatRoundingMode::Rdn, ZttFloatBinaryOp::Sub),
                  value.negativeZero);
        EXPECT_EQ(ZttFloatBackend::mulAdd(
                      value.one, value.one, value.negativeOne, value.format,
                      ZttFloatRoundingMode::Rdn),
                  value.negativeZero);
        EXPECT_EQ(softfloat_exceptionFlags, 0);
    }
}

TEST_F(ZttFloatTest, Fp8SpecialValuesAndFlags)
{
    EXPECT_EQ(ZttFloatBackend::fromF32(
                  float32_t{0x7f800000}, ZttNarrowFloatFormat::E4M3,
                  ZttFloatRoundingMode::Rne),
              0x7e);
    EXPECT_EQ(softfloat_exceptionFlags &
                  (softfloat_flag_overflow | softfloat_flag_inexact),
              softfloat_flag_overflow | softfloat_flag_inexact);

    softfloat_exceptionFlags = 0;
    EXPECT_EQ(ZttFloatBackend::fromF32(
                  float32_t{0x7f800000}, ZttNarrowFloatFormat::E5M2,
                  ZttFloatRoundingMode::Rne),
              0x7c);
    EXPECT_EQ(softfloat_exceptionFlags, 0);

    softfloat_exceptionFlags = 0;
    EXPECT_EQ(ZttFloatBackend::fromF32(
                  float32_t{0x3a800000}, ZttNarrowFloatFormat::E4M3,
                  ZttFloatRoundingMode::Rne),
              0x00);
    EXPECT_EQ(softfloat_exceptionFlags &
                  (softfloat_flag_underflow | softfloat_flag_inexact),
              softfloat_flag_underflow | softfloat_flag_inexact);

    softfloat_exceptionFlags = 0;
    EXPECT_EQ(ZttFloatBackend::binary(
                  0x38, 0x00, ZttNarrowFloatFormat::E4M3,
                  ZttFloatRoundingMode::Rne, ZttFloatBinaryOp::Div),
              0x7e);
    EXPECT_TRUE(softfloat_exceptionFlags & softfloat_flag_infinite);
}

TEST_F(ZttFloatTest, HalfSumRetainsTinyContributions)
{
    struct FormatValues
    {
        ZttNarrowFloatFormat format;
        uint16_t one;
        uint16_t minSubnormal;
        uint16_t rne;
        uint16_t rup;
    };
    constexpr FormatValues values[] = {
        {ZttNarrowFloatFormat::Bf16, 0x3f80, 0x0001, 0x3f00, 0x3f01},
        {ZttNarrowFloatFormat::E4M3, 0x38, 0x01, 0x30, 0x31},
        {ZttNarrowFloatFormat::E5M2, 0x3c, 0x01, 0x38, 0x39}
    };
    for (const auto &value : values) {
        softfloat_exceptionFlags = 0;
        EXPECT_EQ(ZttFloatBackend::halfSum(
                      value.one, value.minSubnormal, value.format,
                      ZttFloatRoundingMode::Rne, false),
                  value.rne);
        EXPECT_EQ(softfloat_exceptionFlags, softfloat_flag_inexact);

        softfloat_exceptionFlags = 0;
        EXPECT_EQ(ZttFloatBackend::halfSum(
                      value.one, value.minSubnormal, value.format,
                      ZttFloatRoundingMode::Rup, false),
                  value.rup);
        EXPECT_EQ(softfloat_exceptionFlags, softfloat_flag_inexact);
    }
}

TEST_F(ZttFloatTest, ScaleReportsExtremeRangeExceptions)
{
    struct FormatValues
    {
        ZttNarrowFloatFormat format;
        uint16_t one;
        uint16_t overflow;
    };
    constexpr FormatValues values[] = {
        {ZttNarrowFloatFormat::Bf16, 0x3f80, 0x7f80},
        {ZttNarrowFloatFormat::E4M3, 0x38, 0x7e},
        {ZttNarrowFloatFormat::E5M2, 0x3c, 0x7c}
    };
    for (const auto &value : values) {
        softfloat_exceptionFlags = 0;
        EXPECT_EQ(ZttFloatBackend::scale(
                      value.one, 1000, value.format,
                      ZttFloatRoundingMode::Rne),
                  value.overflow);
        EXPECT_EQ(softfloat_exceptionFlags &
                      (softfloat_flag_overflow | softfloat_flag_inexact),
                  softfloat_flag_overflow | softfloat_flag_inexact);

        softfloat_exceptionFlags = 0;
        EXPECT_EQ(ZttFloatBackend::scale(
                      value.one, -1000, value.format,
                      ZttFloatRoundingMode::Rup),
                  0x0001);
        EXPECT_EQ(softfloat_exceptionFlags &
                      (softfloat_flag_underflow | softfloat_flag_inexact),
                  softfloat_flag_underflow | softfloat_flag_inexact);
    }
}

TEST_F(ZttFloatTest, ScaleAddIsFusedAtNarrowPrecision)
{
    struct FormatValues
    {
        ZttNarrowFloatFormat format;
        uint16_t one;
        uint16_t roundedUp;
    };
    constexpr FormatValues values[] = {
        {ZttNarrowFloatFormat::Bf16, 0x3f80, 0x3f81},
        {ZttNarrowFloatFormat::E4M3, 0x38, 0x39},
        {ZttNarrowFloatFormat::E5M2, 0x3c, 0x3d}
    };
    for (const auto &value : values) {
        softfloat_exceptionFlags = 0;
        EXPECT_EQ(ZttFloatBackend::scaleAdd(
                      0x0001, 0, value.one, value.format,
                      ZttFloatRoundingMode::Rup),
                  value.roundedUp);
        EXPECT_EQ(softfloat_exceptionFlags, softfloat_flag_inexact);
    }

    softfloat_exceptionFlags = 0;
    EXPECT_EQ(ZttFloatBackend::scaleAdd(
                  0x3f80, 128, 0xff7f, ZttNarrowFloatFormat::Bf16,
                  ZttFloatRoundingMode::Rne),
              0x7b80);
    EXPECT_EQ(softfloat_exceptionFlags, 0);
}

} // anonymous namespace
} // namespace gem5::RiscvISA
