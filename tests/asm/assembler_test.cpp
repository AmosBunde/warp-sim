#include "warpsim/asm/assembler.hpp"
#include "warpsim/isa/instruction.hpp"
#include "warpsim/isa/opcode.hpp"

#include <bit>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

namespace {

using warpsim::assembler::assemble;
using warpsim::assembler::AssemblyError;
using warpsim::assembler::Program;
using warpsim::isa::Instruction;
using warpsim::isa::Opcode;

std::string read_file(const std::string& path) {
    std::ifstream in(path);
    EXPECT_TRUE(in.is_open()) << path;
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::vector<Instruction> decode_all(const Program& program) {
    std::vector<Instruction> out;
    for (const auto word : program.words) {
        const auto decoded = warpsim::isa::decode(word);
        EXPECT_TRUE(decoded.has_value());
        out.push_back(decoded.value());
    }
    return out;
}

Program must_assemble(std::string_view source) {
    const auto result = assemble(source);
    if (!result.has_value()) {
        ADD_FAILURE() << result.error().line << ":" << result.error().column << ": "
                      << result.error().message;
        return {};
    }
    return result.value();
}

AssemblyError must_fail(std::string_view source) {
    const auto result = assemble(source);
    if (result.has_value()) {
        ADD_FAILURE() << "expected an error for:\n" << source;
        return {};
    }
    return result.error();
}

TEST(Assembler, VecaddExampleAssembles) {
    const Program program = must_assemble(read_file(WARPSIM_KERNELS_DIR "/vecadd.wisa"));
    EXPECT_EQ(program.entry, "vecadd");
    EXPECT_EQ(program.params, (std::vector<std::string>{"a", "b", "c", "n"}));
    EXPECT_EQ(program.shared_bytes, 0U);
    ASSERT_EQ(program.words.size(), 20U);
    const auto ins = decode_all(program);
    EXPECT_EQ(ins[0].opcode, Opcode::MovSreg);
    EXPECT_EQ(ins[0].src0, static_cast<std::uint8_t>(warpsim::isa::SpecialRegister::TidX));
    EXPECT_EQ(ins[5].opcode, Opcode::LdParam);
    EXPECT_EQ(ins[5].imm, 3U);
    EXPECT_EQ(ins[7].opcode, Opcode::Bra);
    EXPECT_TRUE(ins[7].guard.present);
    EXPECT_EQ(ins[7].branch_target(), 19U);
    EXPECT_EQ(program.labels.at(19), "done");
    EXPECT_EQ(ins[19].opcode, Opcode::Exit);
}

TEST(Assembler, BranchesExampleCoversEveryOperandForm) {
    const Program program = must_assemble(read_file(WARPSIM_KERNELS_DIR "/examples/branches.wisa"));
    EXPECT_EQ(program.shared_bytes, 128U);
    const auto ins = decode_all(program);
    ASSERT_EQ(ins.size(), 21U);
    EXPECT_EQ(ins[1].imm, static_cast<std::uint32_t>(-5));
    EXPECT_EQ(ins[2].imm, 0x7FFFFFFFU);
    EXPECT_EQ(std::bit_cast<float>(ins[3].imm), 1.5F);
    EXPECT_EQ(std::bit_cast<float>(ins[4].imm), -25.0F);
    EXPECT_EQ(ins[6].branch_target(), 9U);
    EXPECT_EQ(ins[6].reconvergence_pc(), 10U); // join
    EXPECT_FALSE(ins[8].guard.present);
    EXPECT_EQ(ins[8].branch_target(), 10U);
    EXPECT_EQ(ins[11].opcode, Opcode::StShared);
    EXPECT_EQ(ins[11].imm, 64U);
    EXPECT_EQ(ins[12].opcode, Opcode::LdShared);
    EXPECT_EQ(ins[12].imm, 0U);
    EXPECT_EQ(ins[16].branch_target(), 14U);
    EXPECT_TRUE(ins[17].guard.negate);
    EXPECT_EQ(ins[17].opcode, Opcode::Exit);
    EXPECT_EQ(ins[19].opcode, Opcode::StGlobal);
    EXPECT_EQ(ins[19].imm, 4U);
}

TEST(Assembler, NegativeOffsetWrapsToTwosComplement) {
    const Program p = must_assemble(".entry k\n ld.global r1, [r2-8]\n");
    EXPECT_EQ(decode_all(p)[0].imm, static_cast<std::uint32_t>(-8));
}

TEST(Assembler, LabelOnItsOwnLine) {
    const Program p = must_assemble(".entry k\nstart:\n bra start\n");
    EXPECT_EQ(decode_all(p)[0].branch_target(), 0U);
}

TEST(Assembler, ImmediateExtremes) {
    EXPECT_EQ(decode_all(must_assemble(".entry k\n mov r1, -2147483648\n"))[0].imm, 0x80000000U);
    EXPECT_EQ(decode_all(must_assemble(".entry k\n mov r1, 4294967295\n"))[0].imm, 0xFFFFFFFFU);
}

TEST(AssemblerErrors, ReportPositions) {
    const auto e = must_fail(".entry k\n add r1, r2, r3\n add r1, r2\n");
    EXPECT_EQ(e.line, 3U);
    EXPECT_EQ(e.message, "expected ','");
}

TEST(AssemblerErrors, Catalog) {
    EXPECT_EQ(must_fail(".entry k\n bra nowhere\n").message, "undefined label 'nowhere'");
    EXPECT_EQ(must_fail(".entry k\na:\na:\n").message, "duplicate label 'a'");
    EXPECT_EQ(must_fail(".entry k\n frobnicate r1\n").message, "unknown mnemonic 'frobnicate'");
    EXPECT_EQ(must_fail(".entry k\n add r1, r2, r64\n").message,
              "expected a general register r0..r63");
    EXPECT_EQ(must_fail(".entry k\n setp.eq.s32 p8, r1, r2\n").message,
              "expected a predicate register p0..p7");
    EXPECT_EQ(must_fail(".entry k\n mov r1, 4294967296\n").message,
              "integer does not fit in 32 bits: '4294967296'");
    EXPECT_EQ(must_fail(".entry k\n mov r1, -2147483649\n").message,
              "immediate does not fit in 32 bits");
    EXPECT_EQ(must_fail(".entry k\n add r1, r2, 1.0\n").message,
              "float immediate is not valid for this instruction");
    EXPECT_EQ(must_fail(".entry k\n ld.param r1, [x]\n").message, "undeclared parameter 'x'");
    EXPECT_EQ(must_fail(".entry k\n mov.sreg r1, %nope\n").message,
              "unknown special register '%nope'");
    EXPECT_EQ(must_fail(".entry k\n @q1 exit\n").message, "invalid guard predicate 'q1'");
    EXPECT_EQ(must_fail(".entry k\n exit r1\n").message, "unexpected token 'r1' at end of line");
    EXPECT_EQ(must_fail(" exit\n").message, "missing .entry");
    EXPECT_EQ(must_fail(".entry k\n.entry k\n").message, "duplicate .entry");
    EXPECT_EQ(must_fail(".entry k\n.param a\n.param a\n").message, "duplicate parameter 'a'");
    EXPECT_EQ(must_fail(".entry k\n.bogus 1\n").message, "unknown directive '.bogus'");
    EXPECT_EQ(must_fail(".entry k\n add r1, r2, $\n").message, "unexpected character '$'");
    EXPECT_EQ(must_fail(".entry k\n mov r1, 0x\n").message, "expected hexadecimal digits after 0x");
    EXPECT_EQ(must_fail(".entry k\n mov r1, 12abc\n").message, "unexpected character in number");
    EXPECT_EQ(must_fail(".entry k\n mov r01, 1\n").message, "expected a general register r0..r63");
}

TEST(AssemblerErrors, ProgramSizeLimit) {
    std::string source = ".entry k\n";
    for (int i = 0; i < 65536; ++i) {
        source += " exit\n";
    }
    EXPECT_EQ(must_fail(source).message, "program exceeds 65535 instructions");
}

} // namespace
