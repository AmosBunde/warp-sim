#pragma once

#include "warpsim/isa/instruction.hpp"
#include "warpsim/isa/opcode.hpp"

#include <cstdint>
#include <random>

namespace warpsim::testing {

using isa::Instruction;
using isa::Shape;

/// Limits on the random operand space. `max_pc` bounds branch targets so that
/// a generated program is well formed; `max_param` bounds parameter ordinals.
struct RandomLimits {
    std::uint16_t max_pc = 0xFFFF;
    std::uint32_t max_param = 0xFFFFFFFFU;
};

// Produces a valid random instruction for the given opcode, so that the round
// trip is exercised over the whole legal operand space of every shape.
inline Instruction random_instruction(const isa::OpcodeInfo& info, std::mt19937& rng,
                                      RandomLimits limits = {}) {
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
    case Shape::Bra: {
        std::uniform_int_distribution<int> pc(0, limits.max_pc);
        // The reconvergence half is whatever the caller's pass computes; a
        // random value here exercises encode/decode, and callers that
        // reassemble run the analysis pass to make it consistent.
        i.imm = Instruction::make_branch_imm(static_cast<std::uint16_t>(pc(rng)),
                                             static_cast<std::uint16_t>(word(rng) & 0xFFFFU));
        break;
    }
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
    case Shape::Ldp: {
        std::uniform_int_distribution<std::uint32_t> ordinal(0, limits.max_param);
        i.dst = static_cast<std::uint8_t>(reg(rng));
        i.imm_flag = true;
        i.imm = ordinal(rng);
        break;
    }
    }
    return i;
}

} // namespace warpsim::testing
