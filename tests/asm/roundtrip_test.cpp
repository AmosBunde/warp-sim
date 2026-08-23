#include "warpsim/asm/assembler.hpp"
#include "warpsim/asm/disassembler.hpp"
#include "warpsim/asm/program.hpp"
#include "warpsim/isa/instruction.hpp"
#include "warpsim/isa/opcode.hpp"

#include <cstdint>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "asm/reconvergence.hpp"
#include "support/random_instruction.hpp"

namespace {

using warpsim::assembler::assemble;
using warpsim::assembler::disassemble;
using warpsim::assembler::Program;
using warpsim::isa::Instruction;
using warpsim::isa::Opcode;
using warpsim::isa::opcode_table;

constexpr int cases_per_opcode = 1000;
constexpr std::size_t program_length = 4;
constexpr std::uint32_t param_count = 8;

std::string read_file(const std::string& path) {
    std::ifstream in(path);
    EXPECT_TRUE(in.is_open()) << path;
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

/// Builds a well-formed program: the instruction under test followed by
/// filler, with reconvergence points made consistent by the analysis pass.
Program make_program(std::vector<Instruction> instructions) {
    const auto annotated = warpsim::assembler::annotate_reconvergence(instructions);
    EXPECT_TRUE(annotated.has_value());
    Program program;
    program.entry = "k";
    for (std::uint32_t p = 0; p < param_count; ++p) {
        program.params.push_back("param" + std::to_string(p));
    }
    program.shared_bytes = 256;
    for (const auto& i : instructions) {
        program.words.push_back(warpsim::isa::encode(i).value());
    }
    return program;
}

void expect_round_trip(const Program& program, const std::string& context) {
    const auto text = disassemble(program);
    ASSERT_TRUE(text.has_value()) << context << ": " << text.error().message;
    const auto back = assemble(*text);
    ASSERT_TRUE(back.has_value()) << context << ": " << back.error().line << ":"
                                  << back.error().column << ": " << back.error().message << "\n"
                                  << *text;
    EXPECT_EQ(back->words, program.words) << context << "\n" << *text;
    EXPECT_EQ(back->entry, program.entry);
    EXPECT_EQ(back->params, program.params);
    EXPECT_EQ(back->shared_bytes, program.shared_bytes);
}

TEST(RoundTrip, EveryOpcodeWithRandomOperands) {
    std::mt19937 rng(0xD15A);
    const warpsim::testing::RandomLimits limits{.max_pc = program_length - 1,
                                                .max_param = param_count - 1};
    for (const auto& info : opcode_table) {
        for (int n = 0; n < cases_per_opcode; ++n) {
            std::vector<Instruction> instructions;
            instructions.push_back(warpsim::testing::random_instruction(info, rng, limits));
            // Filler with a second branch so that labels, guards, and fall
            // through all appear in the same program.
            Instruction filler;
            filler.opcode = Opcode::Add;
            filler.dst = 1;
            filler.src0 = 2;
            filler.src1 = 3;
            instructions.push_back(filler);
            Instruction back_edge;
            back_edge.opcode = Opcode::Bra;
            back_edge.guard = {.present = true, .negate = true, .pred = 3};
            back_edge.imm = Instruction::make_branch_imm(1, warpsim::isa::no_reconvergence);
            instructions.push_back(back_edge);
            Instruction exit;
            exit.opcode = Opcode::Exit;
            instructions.push_back(exit);
            ASSERT_EQ(instructions.size(), program_length);
            expect_round_trip(make_program(std::move(instructions)),
                              std::string(info.mnemonic) + " case " + std::to_string(n));
        }
    }
}

TEST(RoundTrip, FloatImmediateExtremes) {
    const std::vector<std::uint32_t> bit_patterns = {
        0x00000000U, 0x80000000U, 0x3F800000U, 0xBF800000U, 0x00000001U, 0x007FFFFFU,
        0x7F7FFFFFU, 0xFF7FFFFFU, 0x7F800000U, 0xFF800000U, 0x7FC00000U, 0x7F800001U,
        0x3EAAAAABU, 0x4B7FFFFFU, 0x4B800000U, 0x33800000U,
    };
    for (const auto bits : bit_patterns) {
        Instruction i;
        i.opcode = Opcode::FAdd;
        i.dst = 1;
        i.src0 = 2;
        i.imm_flag = true;
        i.imm = bits;
        Instruction exit;
        exit.opcode = Opcode::Exit;
        expect_round_trip(make_program({i, exit}), "fadd imm 0x" + std::to_string(bits));
    }
}

TEST(RoundTrip, ExampleKernels) {
    for (const char* name : {"vecadd", "branches"}) {
        const auto source =
            read_file(std::string(WARPSIM_KERNELS_DIR "/examples/") + name + ".wisa");
        const auto first = assemble(source);
        ASSERT_TRUE(first.has_value()) << name;
        expect_round_trip(*first, name);
        // Labels from the source survive into the canonical text.
        const auto text = disassemble(*first).value();
        EXPECT_NE(text.find(std::string(name == std::string("vecadd") ? "done:" : "join:")),
                  std::string::npos);
    }
}

TEST(Disassembler, CanonicalTextOfSpecificationExample) {
    const auto program = assemble(R"(.entry k
        setp.lt.s32 p0, r0, r1
    @p0 bra then
        mov r2, 10
        bra join
then:   mov r2, 20
join:   add r2, r2, 1
        exit
)");
    ASSERT_TRUE(program.has_value());
    const auto text = disassemble(*program);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, ".entry k\n"
                     ".shared 0\n"
                     "\n"
                     "    setp.lt.s32 p0, r0, r1\n"
                     "    @p0 bra then    // reconverge join\n"
                     "    mov r2, 0xa\n"
                     "    bra join\n"
                     "then:\n"
                     "    mov r2, 0x14\n"
                     "join:\n"
                     "    add r2, r2, 1\n"
                     "    exit\n");
}

TEST(Disassembler, ReportsBadWords) {
    Program program;
    program.entry = "k";
    program.words.push_back(0);
    const auto text = disassemble(program);
    ASSERT_FALSE(text.has_value());
    EXPECT_EQ(text.error().pc, 0U);
    EXPECT_EQ(text.error().message, "invalid opcode");

    Instruction ldp;
    ldp.opcode = Opcode::LdParam;
    ldp.imm_flag = true;
    ldp.imm = 3;
    program.words[0] = warpsim::isa::encode(ldp).value();
    EXPECT_EQ(disassemble(program).error().message, "parameter ordinal out of range");
}

} // namespace
