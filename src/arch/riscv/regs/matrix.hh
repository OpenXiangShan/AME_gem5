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

// Matrix Extension
#ifndef __ARCH_RISCV_REGS_MATRIX_HH__
#define __ARCH_RISCV_REGS_MATRIX_HH__

#include <cstdint>
#include <string>
#include <vector>

#include "arch/generic/mat_reg.hh"
#include "arch/riscv/types.hh"
#include "base/bitunion.hh"
#include "cpu/reg_class.hh"
#include "debug/MatRegs.hh"

namespace gem5
{

namespace RiscvISA
{

using MatRegContainer =
    gem5::MatRegContainer<MaxMatLenInBytes, MaxMatRowLenInBytes>;
using mreg_t = MatRegContainer;

// Ztt stays RISC-V-specific by reserving non-overlapping ranges in the
// existing RISC-V matrix register file. The instruction fields are bank-local
// selectors: e.g., md=5 selects ZttMD5, not physical index 53.
const int ZttMatRegBase = 0;
const int NumZttMatRegs = 32;
const int ZttAccRegBase = ZttMatRegBase + NumZttMatRegs;
const int NumZttAccRegs = 4;
const int ZttMatDataRegBase = ZttAccRegBase + NumZttAccRegs;
const int NumZttMatDataRegs = NumZttMatRegs;
const int ZttAccDataRegBase = ZttMatDataRegBase + NumZttMatDataRegs;
const int NumZttAccDataRegs = NumZttAccRegs;
const int NumMatRegs = ZttAccDataRegBase + NumZttAccDataRegs;

// Ztt helpers use structural register-file indices after their operand
// accessors have translated the encoded selector to the relevant bank.
const int ZttMatStructuralIndexBase = ZttMatRegBase;
const int ZttAccStructuralIndexBase = ZttAccRegBase;

const std::vector<std::string> MatRegNames = {
    // Ztt structural matrix registers, selected by ms/md fields.
    "ztt_m0", "ztt_m1", "ztt_m2", "ztt_m3",
    "ztt_m4", "ztt_m5", "ztt_m6", "ztt_m7",
    "ztt_m8", "ztt_m9", "ztt_m10", "ztt_m11",
    "ztt_m12", "ztt_m13", "ztt_m14", "ztt_m15",
    "ztt_m16", "ztt_m17", "ztt_m18", "ztt_m19",
    "ztt_m20", "ztt_m21", "ztt_m22", "ztt_m23",
    "ztt_m24", "ztt_m25", "ztt_m26", "ztt_m27",
    "ztt_m28", "ztt_m29", "ztt_m30", "ztt_m31",

    // Ztt structural accumulators, selected by ad/acc fields.
    "ztt_acc0", "ztt_acc1", "ztt_acc2", "ztt_acc3",

    // Ztt datatype registers. Only their low RegVal is architecturally
    // meaningful; their matrix-container storage keeps these physical
    // registers local to the RISC-V matrix register file.
    "ztt_md0", "ztt_md1", "ztt_md2", "ztt_md3",
    "ztt_md4", "ztt_md5", "ztt_md6", "ztt_md7",
    "ztt_md8", "ztt_md9", "ztt_md10", "ztt_md11",
    "ztt_md12", "ztt_md13", "ztt_md14", "ztt_md15",
    "ztt_md16", "ztt_md17", "ztt_md18", "ztt_md19",
    "ztt_md20", "ztt_md21", "ztt_md22", "ztt_md23",
    "ztt_md24", "ztt_md25", "ztt_md26", "ztt_md27",
    "ztt_md28", "ztt_md29", "ztt_md30", "ztt_md31",

    // Ztt accumulator datatype registers.
    "ztt_ad0", "ztt_ad1", "ztt_ad2", "ztt_ad3"
};

inline TypedRegClassOps<RiscvISA::MatRegContainer> matRegClassOps;

inline constexpr RegClass matRegClass =
    RegClass(MatRegClass, MatRegClassName, NumMatRegs, debug::MatRegs).
        ops(matRegClassOps).
        regType<MatRegContainer>();

// BitUnion64(MTYPE)
// Bitfield<63>    mill;      // Illegal value if set
// Bitfield<62,17> reserved;  // Must be zero
// Bitfield<16>    mma;
// Bitfield<15>    mba;       // Matrix out-of-bound agnostic
// Bitfield<14>    mfp64;     // Enable FP64
// Bitfield<13,12> mfp32;     // Enable FP32 / TF32
// Bitfield<11,10> mfp16;     // Enable FP16 / BF16
// Bitfield<9,8>   mfp8;      // Enable FP8
// Bitfield<7>     mint64;    // Enable INT64
// Bitfield<6>     mint32;    // Enable INT32
// Bitfield<5>     mint16;    // Enable INT16
// Bitfield<4>     mint8;     // Enable INT8
// Bitfield<3>     mint4;     // Enable INT4
// Bitfield<2,0>   msew;      // Selected element width (SEW)
// EndBitUnion(MTYPE)

} // namespace RiscvISA
} // namespace gem5

#endif // __ARCH_RISCV_REGS_MATRIX_HH__
