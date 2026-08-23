#include "warpsim/core/alu.hpp"
#include "warpsim/core/lane_context.hpp"
#include "warpsim/core/register_file.hpp"
#include "warpsim/core/types.hpp"
#include "warpsim/isa/instruction.hpp"
#include "warpsim/isa/opcode.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

namespace {

using warpsim::core::execute_alu;
using warpsim::core::LaneContext;
using warpsim::core::RegisterFile;
using warpsim::isa::Instruction;
using warpsim::isa::Opcode;

constexpr std::uint32_t int_min = 0x80000000U;
constexpr std::uint32_t int_max = 0x7FFFFFFFU;

std::uint32_t f(float v) {
    return std::bit_cast<std::uint32_t>(v);
}

/// Runs `op r0, r1, r2` (or `op r0, r1` for RR) on lane 0 with the given
/// register values and returns r0.
std::uint32_t run(Opcode op, std::uint32_t a, std::uint32_t b = 0, std::uint32_t dst_old = 0) {
    RegisterFile rf;
    rf.write(0, 0, dst_old);
    rf.write(0, 1, a);
    rf.write(0, 2, b);
    Instruction i;
    i.opcode = op;
    i.dst = 0;
    i.src0 = 1;
    if (warpsim::isa::opcode_info(op).shape != warpsim::isa::Shape::Rr) {
        i.src1 = 2;
    }
    execute_alu(i, 1U, rf, LaneContext{});
    return rf.read(0, 0);
}

bool run_setp(Opcode op, std::uint32_t a, std::uint32_t b) {
    RegisterFile rf;
    rf.write(0, 1, a);
    rf.write(0, 2, b);
    Instruction i;
    i.opcode = op;
    i.dst = 3;
    i.src0 = 1;
    i.src1 = 2;
    execute_alu(i, 1U, rf, LaneContext{});
    return rf.read_pred(0, 3);
}

TEST(Alu, IntegerWraparound) {
    EXPECT_EQ(run(Opcode::Add, int_max, 1), int_min);
    EXPECT_EQ(run(Opcode::Sub, int_min, 1), int_max);
    EXPECT_EQ(run(Opcode::Mul, 0x10000U, 0x10000U), 0U);
    EXPECT_EQ(run(Opcode::Mul, 0xFFFFFFFFU, 0xFFFFFFFFU), 1U);
    EXPECT_EQ(run(Opcode::Neg, int_min), int_min);
    EXPECT_EQ(run(Opcode::Neg, 5), 0xFFFFFFFBU);
}

TEST(Alu, DivisionRules) {
    EXPECT_EQ(run(Opcode::Div, 7, 2), 3U);
    EXPECT_EQ(run(Opcode::Div, 0xFFFFFFF9U, 2), 0xFFFFFFFDU); // -7 / 2 = -3
    EXPECT_EQ(run(Opcode::Div, 7, 0), 0U);
    EXPECT_EQ(run(Opcode::Div, int_min, 0xFFFFFFFFU), int_min);
    EXPECT_EQ(run(Opcode::Rem, 7, 2), 1U);
    EXPECT_EQ(run(Opcode::Rem, 0xFFFFFFF9U, 2), 0xFFFFFFFFU); // -7 rem 2 = -1
    EXPECT_EQ(run(Opcode::Rem, 7, 0), 0U);
    EXPECT_EQ(run(Opcode::Rem, int_min, 0xFFFFFFFFU), 0U);
}

TEST(Alu, MinMaxAreSigned) {
    EXPECT_EQ(run(Opcode::Min, 0xFFFFFFFFU, 1), 0xFFFFFFFFU);
    EXPECT_EQ(run(Opcode::Max, 0xFFFFFFFFU, 1), 1U);
}

TEST(Alu, LogicAndShifts) {
    EXPECT_EQ(run(Opcode::And, 0xF0F0U, 0xFF00U), 0xF000U);
    EXPECT_EQ(run(Opcode::Or, 0xF0F0U, 0xFF00U), 0xFFF0U);
    EXPECT_EQ(run(Opcode::Xor, 0xF0F0U, 0xFF00U), 0x0FF0U);
    EXPECT_EQ(run(Opcode::Not, 0xF0F0U), 0xFFFF0F0FU);
    EXPECT_EQ(run(Opcode::Shl, 1, 33), 2U); // count masked to 5 bits
    EXPECT_EQ(run(Opcode::Shl, 1, 31), int_min);
    EXPECT_EQ(run(Opcode::Shr, int_min, 31), 1U);
    EXPECT_EQ(run(Opcode::Shr, int_min, 32), int_min);
    EXPECT_EQ(run(Opcode::Sra, int_min, 31), 0xFFFFFFFFU);
    EXPECT_EQ(run(Opcode::Sra, int_min, 1), 0xC0000000U);
    EXPECT_EQ(run(Opcode::Sra, 0x40000000U, 1), 0x20000000U);
    EXPECT_EQ(run(Opcode::Sra, 0xFFFFFFF0U, 0), 0xFFFFFFF0U);
}

TEST(Alu, FloatArithmetic) {
    EXPECT_EQ(run(Opcode::FAdd, f(1.5F), f(2.25F)), f(3.75F));
    EXPECT_EQ(run(Opcode::FSub, f(1.5F), f(2.25F)), f(-0.75F));
    EXPECT_EQ(run(Opcode::FMul, f(1.5F), f(2.0F)), f(3.0F));
    // Single rounding: fma(a, b, c) differs from a*b+c for these values.
    const float a = 1.0F + 0x1p-23F;
    const float b = 1.0F - 0x1p-23F;
    const float c = -1.0F;
    EXPECT_EQ(run(Opcode::FFma, f(a), f(b), f(c)), f(std::fma(a, b, c)));
    EXPECT_NE(run(Opcode::FFma, f(a), f(b), f(c)), f((a * b) + c));
    EXPECT_EQ(run(Opcode::FNeg, f(1.0F)), f(-1.0F));
    EXPECT_EQ(run(Opcode::FNeg, f(0.0F)), f(-0.0F));
}

TEST(Alu, FloatMinMaxNanRules) {
    const std::uint32_t nan = 0x7FC00000U;
    EXPECT_EQ(run(Opcode::FMin, nan, f(2.0F)), f(2.0F));
    EXPECT_EQ(run(Opcode::FMin, f(2.0F), nan), f(2.0F));
    EXPECT_EQ(run(Opcode::FMax, nan, f(2.0F)), f(2.0F));
    EXPECT_TRUE(std::isnan(std::bit_cast<float>(run(Opcode::FMax, nan, nan))));
    EXPECT_EQ(run(Opcode::FMin, f(-1.0F), f(1.0F)), f(-1.0F));
    EXPECT_EQ(run(Opcode::FMax, f(-1.0F), f(1.0F)), f(1.0F));
}

TEST(Alu, Conversions) {
    EXPECT_EQ(run(Opcode::CvtF32S32, 0xFFFFFFFFU), f(-1.0F));
    EXPECT_EQ(run(Opcode::CvtF32S32, 16777217U), f(16777216.0F)); // round to nearest even
    EXPECT_EQ(run(Opcode::CvtS32F32, f(-2.9F)), 0xFFFFFFFEU);     // truncate toward zero
    EXPECT_EQ(run(Opcode::CvtS32F32, f(2.9F)), 2U);
    EXPECT_EQ(run(Opcode::CvtS32F32, f(3.0e9F)), int_max); // saturate
    EXPECT_EQ(run(Opcode::CvtS32F32, f(-3.0e9F)), int_min);
    EXPECT_EQ(run(Opcode::CvtS32F32, 0x7F800000U), int_max);       // +inf
    EXPECT_EQ(run(Opcode::CvtS32F32, 0x7FC00000U), 0U);            // NaN
    EXPECT_EQ(run(Opcode::CvtS32F32, f(-2147483648.0F)), int_min); // exact boundary
}

TEST(Alu, Compares) {
    EXPECT_TRUE(run_setp(Opcode::SetpLtS32, 0xFFFFFFFFU, 0)); // -1 < 0 signed
    EXPECT_FALSE(run_setp(Opcode::SetpGtS32, 0xFFFFFFFFU, 0));
    EXPECT_TRUE(run_setp(Opcode::SetpEqS32, 5, 5));
    EXPECT_TRUE(run_setp(Opcode::SetpNeS32, 5, 6));
    EXPECT_TRUE(run_setp(Opcode::SetpLeS32, 5, 5));
    EXPECT_TRUE(run_setp(Opcode::SetpGeS32, 6, 5));
    const std::uint32_t nan = 0x7FC00000U;
    EXPECT_FALSE(run_setp(Opcode::SetpEqF32, nan, nan));
    EXPECT_FALSE(run_setp(Opcode::SetpLtF32, nan, f(1.0F)));
    EXPECT_FALSE(run_setp(Opcode::SetpGeF32, nan, f(1.0F)));
    EXPECT_TRUE(run_setp(Opcode::SetpNeF32, nan, f(1.0F)));
    EXPECT_TRUE(run_setp(Opcode::SetpEqF32, f(0.0F), f(-0.0F)));
    EXPECT_TRUE(run_setp(Opcode::SetpLtF32, f(-1.0F), f(1.0F)));
    EXPECT_TRUE(run_setp(Opcode::SetpLeF32, f(1.0F), f(1.0F)));
    EXPECT_TRUE(run_setp(Opcode::SetpGtF32, f(2.0F), f(1.0F)));
}

TEST(Alu, ImmediatesAndRrShape) {
    RegisterFile rf;
    rf.write(0, 1, 10);
    Instruction add;
    add.opcode = Opcode::Add;
    add.dst = 0;
    add.src0 = 1;
    add.imm_flag = true;
    add.imm = static_cast<std::uint32_t>(-3);
    execute_alu(add, 1U, rf, LaneContext{});
    EXPECT_EQ(rf.read(0, 0), 7U);

    Instruction mov;
    mov.opcode = Opcode::Mov;
    mov.dst = 5;
    mov.imm_flag = true;
    mov.imm = 0x3F800000U;
    execute_alu(mov, 1U, rf, LaneContext{});
    EXPECT_EQ(rf.read(0, 5), 0x3F800000U);
}

TEST(Alu, ExecMaskSelectsLanes) {
    RegisterFile rf;
    for (unsigned lane = 0; lane < 32; ++lane) {
        rf.write(lane, 1, lane);
    }
    Instruction add;
    add.opcode = Opcode::Add;
    add.dst = 0;
    add.src0 = 1;
    add.imm_flag = true;
    add.imm = 100;
    execute_alu(add, 0x0000FFFFU, rf, LaneContext{});
    for (unsigned lane = 0; lane < 32; ++lane) {
        EXPECT_EQ(rf.read(lane, 0), lane < 16 ? lane + 100 : 0U) << lane;
    }
}

TEST(Alu, SpecialRegisters) {
    const LaneContext ctx{.ntid = {.x = 8, .y = 4},
                          .nctaid = {.x = 3, .y = 2},
                          .ctaid = {.x = 2, .y = 1},
                          .warp_id = 1};
    RegisterFile rf;
    Instruction sreg;
    sreg.opcode = Opcode::MovSreg;
    sreg.dst = 0;
    using warpsim::isa::SpecialRegister;
    const auto read = [&](SpecialRegister r, unsigned lane) {
        sreg.src0 = static_cast<std::uint8_t>(r);
        execute_alu(sreg, warpsim::core::full_mask, rf, ctx);
        return rf.read(lane, 0);
    };
    // warp 1, lane 5: linear 37 -> tid.x = 37 % 8 = 5, tid.y = 37 / 8 = 4.
    EXPECT_EQ(read(SpecialRegister::TidX, 5), 5U);
    EXPECT_EQ(read(SpecialRegister::TidY, 5), 4U);
    EXPECT_EQ(read(SpecialRegister::NtidX, 0), 8U);
    EXPECT_EQ(read(SpecialRegister::NtidY, 0), 4U);
    EXPECT_EQ(read(SpecialRegister::CtaidX, 0), 2U);
    EXPECT_EQ(read(SpecialRegister::CtaidY, 0), 1U);
    EXPECT_EQ(read(SpecialRegister::NctaidX, 0), 3U);
    EXPECT_EQ(read(SpecialRegister::NctaidY, 0), 2U);
    EXPECT_EQ(read(SpecialRegister::LaneId, 17), 17U);
    EXPECT_EQ(read(SpecialRegister::WarpId, 3), 1U);
}

TEST(Alu, OpcodeClassification) {
    EXPECT_TRUE(warpsim::core::is_alu_opcode(Opcode::Add));
    EXPECT_TRUE(warpsim::core::is_alu_opcode(Opcode::MovSreg));
    EXPECT_FALSE(warpsim::core::is_alu_opcode(Opcode::Bra));
    EXPECT_FALSE(warpsim::core::is_alu_opcode(Opcode::LdParam));
}

} // namespace
