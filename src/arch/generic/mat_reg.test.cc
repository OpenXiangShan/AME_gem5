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

#include "arch/generic/mat_reg.hh"

#include <sstream>

#include <gtest/gtest.h>

namespace gem5
{

TEST(MatRegContainer, ParseAcrossRows)
{
    using TestMatReg = MatRegContainer<16, 4>;
    TestMatReg reg;

    ASSERT_TRUE(ParseParam<TestMatReg>::parse(
        "000102030405060708090a0b0c0d0e0f", reg));

    auto rows = reg.as<uint8_t>();
    for (size_t i = 0; i < reg.size(); ++i)
        EXPECT_EQ(rows[i / 4][i % 4], i);

    std::ostringstream output;
    ShowParam<TestMatReg>::show(output, reg);
    EXPECT_EQ(output.str(), "000102030405060708090a0b0c0d0e0f");
}

TEST(MatRegContainer, ParseZeroFillsMissingBytes)
{
    using TestMatReg = MatRegContainer<8, 2>;
    TestMatReg reg;
    reg.set(0xff);

    ASSERT_TRUE(ParseParam<TestMatReg>::parse("1234", reg));

    auto rows = reg.as<uint8_t>();
    EXPECT_EQ(rows[0][0], 0x12);
    EXPECT_EQ(rows[0][1], 0x34);
    for (size_t i = 2; i < reg.size(); ++i)
        EXPECT_EQ(rows[i / 2][i % 2], 0);
}

} // namespace gem5
