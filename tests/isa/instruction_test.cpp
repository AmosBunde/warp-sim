#include "warpsim/isa/instruction.hpp"
#include "warpsim/isa/opcode.hpp"

#include <cstdint>
#include <random>

#include <gtest/gtest.h>

namespace {

using warpsim::isa::decode;
using warpsim::isa::encode;
using warpsim::isa::Instruction;
using warpsim::isa::IsaError;
using warpsim::isa::Opcode;
using warpsim::isa::opcode_table;
using warpsim::isa::Shape;
using warpsim::isa::validate;

// Produces a valid random instruction for the given opcode, so that the round
// trip is exercised over the whole legal operand space of every shape.
Instruction random_instruction(const warpsim::isa::OpcodeInfo& info, std::mt19937& rng) {
    std::uniform_int_distribution<int> reg(0, 63);
    std::uniform_int_distribution<int> pred(0, 7);
    std::uniform_int_distribution<int> coin(0, 1);
    std::uniform_int_distribution<std::uint32_t> word(0, 0xFFFFFFFFU);
    std::uniform_int_distribution<int> sreg(0, 9);

    Instruction i;
    i.opcode = info.opcode;
    if (coin(rng) == 1) {
        i.guard.present = true;
        i.guard.negate = coin(rng) == 1;
        i.guard.pred = static_cast<std::uint8_t>(pred(rng));
    }
    const bool use_imm = coin(rng) == 1;
    switch (info.shape) {
    case Shape::Rrr:
        i.dst = static_cast<std::uint8_t>(reg(rng));
        i.src0 = static_cast<std::uint8_t>(reg(rng));
        if (use_imm) {
            i.imm_flag = true;
            i.imm = word(rng);
        } else {
            i.src1 = static_cast<std::uint8_t>(reg(rng));
        }
        break;
    case Shape::Prr:
        i.dst = static_cast<std::uint8_t>(pred(rng));
        i.src0 = static_cast<std::uint8_t>(reg(rng));
        if (use_imm) {
            i.imm_flag = true;
            i.imm = word(rng);
        } else {
            i.src1 = static_cast<std::uint8_t>(reg(rng));
        }
        break;
    case Shape::Rr:
        i.dst = static_cast<std::uint8_t>(reg(rng));
        if (use_imm) {
            i.imm_flag = true;
            i.imm = word(rng);
        } else {
            i.src0 = static_cast<std::uint8_t>(reg(rng));
        }
        break;
    case Shape::Acc:
        i.dst = static_cast<std::uint8_t>(reg(rng));
        i.src0 = static_cast<std::uint8_t>(reg(rng));
        i.src1 = static_cast<std::uint8_t>(reg(rng));
        break;
    case Shape::Sreg:
        i.dst = static_cast<std::uint8_t>(reg(rng));
        i.src0 = static_cast<std::uint8_t>(sreg(rng));
        break;
    case Shape::Bra:
        i.imm = word(rng);
        break;
    case Shape::None:
        break;
    case Shape::Ld:
        i.dst = static_cast<std::uint8_t>(reg(rng));
        i.src0 = static_cast<std::uint8_t>(reg(rng));
        i.imm_flag = true;
        i.imm = word(rng);
        break;
    case Shape::St:
        i.src0 = static_cast<std::uint8_t>(reg(rng));
        i.src1 = static_cast<std::uint8_t>(reg(rng));
        i.imm_flag = true;
        i.imm = word(rng);
        break;
    case Shape::Ldp:
        i.dst = static_cast<std::uint8_t>(reg(rng));
        i.imm_flag = true;
        i.imm = word(rng);
        break;
    }
    return i;
}

TEST(Encoding, WorkedExampleFromSpecification) {
    // `@p1 add r3, r4, 7` from docs/wisa-spec.md section 3.1.
    Instruction i;
    i.opcode = Opcode::Add;
    i.guard = {.present = true, .negate = false, .pred = 1};
    i.dst = 3;
    i.src0 = 4;
    i.imm_flag = true;
    i.imm = 7;
    const auto word = encode(i);
    ASSERT_TRUE(word.has_value());
    EXPECT_EQ(*word, 0x0188620100000007ULL);
    const auto back = decode(*word);
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(*back, i);
}

TEST(Encoding, FieldPositions) {
    Instruction i;
    i.opcode = Opcode::FFma;
    i.guard = {.present = true, .negate = true, .pred = 7};
    i.dst = 63;
    i.src0 = 62;
    i.src1 = 61;
    const auto word = encode(i);
    ASSERT_TRUE(word.has_value());
    EXPECT_EQ((*word >> 56U) & 0xFFU, 0x14U);
    EXPECT_EQ((*word >> 55U) & 1U, 1U);
    EXPECT_EQ((*word >> 54U) & 1U, 1U);
    EXPECT_EQ((*word >> 51U) & 7U, 7U);
    EXPECT_EQ((*word >> 45U) & 0x3FU, 63U);
    EXPECT_EQ((*word >> 39U) & 0x3FU, 62U);
    EXPECT_EQ((*word >> 33U) & 0x3FU, 61U);
    EXPECT_EQ((*word >> 32U) & 1U, 0U);
    EXPECT_EQ(*word & 0xFFFFFFFFU, 0U);
}

TEST(Encoding, BranchHalves) {
    Instruction i;
    i.opcode = Opcode::Bra;
    i.imm = Instruction::make_branch_imm(0x1234, 0xABCD);
    EXPECT_EQ(i.branch_target(), 0x1234);
    EXPECT_EQ(i.reconvergence_pc(), 0xABCD);
    const auto word = encode(i);
    ASSERT_TRUE(word.has_value());
    EXPECT_EQ(*word & 0xFFFFFFFFU, 0xABCD1234U);
}

TEST(Encoding, RoundTripEveryOpcode) {
    std::mt19937 rng(0x5EED);
    for (const auto& info : opcode_table) {
        for (int n = 0; n < 1000; ++n) {
            const Instruction i = random_instruction(info, rng);
            const auto word = encode(i);
            ASSERT_TRUE(word.has_value()) << info.mnemonic << ": " << to_string(word.error());
            const auto back = decode(*word);
            ASSERT_TRUE(back.has_value()) << info.mnemonic << ": " << to_string(back.error());
            EXPECT_EQ(*back, i) << info.mnemonic;
            const auto again = encode(*back);
            ASSERT_TRUE(again.has_value());
            EXPECT_EQ(*again, *word) << info.mnemonic;
        }
    }
}

TEST(Decoding, RejectsInvalidOpcodes) {
    EXPECT_EQ(decode(0).error(), IsaError::InvalidOpcode);
    EXPECT_EQ(decode(0x2FULL << 56U).error(), IsaError::InvalidOpcode);
    EXPECT_EQ(decode(0xFFULL << 56U).error(), IsaError::InvalidOpcode);
}

TEST(Decoding, RejectsGuardFieldsWithoutGuard) {
    const std::uint64_t exit_word = 0x28ULL << 56U;
    EXPECT_TRUE(decode(exit_word).has_value());
    EXPECT_EQ(decode(exit_word | (1ULL << 54U)).error(), IsaError::GuardFieldsWithoutGuard);
    EXPECT_EQ(decode(exit_word | (3ULL << 51U)).error(), IsaError::GuardFieldsWithoutGuard);
}

TEST(Decoding, RejectsNonZeroUnusedFields) {
    const std::uint64_t exit_word = 0x28ULL << 56U;
    EXPECT_EQ(decode(exit_word | (1ULL << 45U)).error(), IsaError::UnusedFieldNotZero);
    EXPECT_EQ(decode(exit_word | 1ULL).error(), IsaError::UnusedFieldNotZero);
    EXPECT_EQ(decode(exit_word | (1ULL << 32U)).error(), IsaError::ImmediateFlagMismatch);

    // add r1, r2, imm with src1 also set: two encodings would describe one instruction.
    const std::uint64_t add_imm =
        (0x01ULL << 56U) | (1ULL << 45U) | (2ULL << 39U) | (3ULL << 33U) | (1ULL << 32U) | 5ULL;
    EXPECT_EQ(decode(add_imm).error(), IsaError::UnusedFieldNotZero);
    // add r1, r2, r3 with a stray immediate.
    const std::uint64_t add_reg =
        (0x01ULL << 56U) | (1ULL << 45U) | (2ULL << 39U) | (3ULL << 33U) | 5ULL;
    EXPECT_EQ(decode(add_reg).error(), IsaError::UnusedFieldNotZero);
}

TEST(Decoding, RejectsRangeViolations) {
    // setp with predicate destination 8.
    const std::uint64_t setp = (0x1AULL << 56U) | (8ULL << 45U);
    EXPECT_EQ(decode(setp).error(), IsaError::PredicateOutOfRange);
    // mov.sreg with special index 10.
    const std::uint64_t sreg = (0x26ULL << 56U) | (10ULL << 39U);
    EXPECT_EQ(decode(sreg).error(), IsaError::SpecialRegisterOutOfRange);
    // ld.global without the immediate flag.
    const std::uint64_t ld = (0x2AULL << 56U) | (1ULL << 45U) | (2ULL << 39U);
    EXPECT_EQ(decode(ld).error(), IsaError::ImmediateFlagMismatch);
    // bra with the immediate flag.
    const std::uint64_t bra = (0x27ULL << 56U) | (1ULL << 32U);
    EXPECT_EQ(decode(bra).error(), IsaError::ImmediateFlagMismatch);
}

TEST(Validation, RejectsStructValuesWithNoEncoding) {
    Instruction i;
    i.opcode = Opcode::Add;
    i.dst = 64;
    EXPECT_FALSE(validate(i).has_value());
    i.dst = 0;
    i.guard.pred = 8;
    EXPECT_FALSE(validate(i).has_value());
}

TEST(OpcodeTable, NumbersAreContiguousAndUnique) {
    std::uint8_t expected = 1;
    for (const auto& info : opcode_table) {
        EXPECT_EQ(static_cast<std::uint8_t>(info.opcode), expected) << info.mnemonic;
        ++expected;
        const auto by_name = warpsim::isa::opcode_info(info.mnemonic);
        ASSERT_TRUE(by_name.has_value()) << info.mnemonic;
        EXPECT_EQ(by_name.value_or(opcode_table.front()).opcode, info.opcode);
    }
    EXPECT_FALSE(warpsim::isa::opcode_info("nope").has_value());
}

} // namespace
