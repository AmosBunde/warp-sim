#include "warpsim/isa/instruction.hpp"

#include <cstdint>
#include <string_view>

namespace warpsim::isa {

namespace {

// Bit positions of docs/wisa-spec.md section 3.
constexpr unsigned opcode_shift = 56;
constexpr unsigned guard_shift = 55;
constexpr unsigned negate_shift = 54;
constexpr unsigned pred_shift = 51;
constexpr unsigned dst_shift = 45;
constexpr unsigned src0_shift = 39;
constexpr unsigned src1_shift = 33;
constexpr unsigned imm_flag_shift = 32;

constexpr std::uint64_t mask8 = 0xFFU;
constexpr std::uint64_t mask6 = 0x3FU;
constexpr std::uint64_t mask3 = 0x7U;
constexpr std::uint64_t mask1 = 0x1U;
constexpr std::uint64_t mask32 = 0xFFFFFFFFU;

// Which fields a shape uses. A field that is not used must be zero.
struct FieldUse {
    bool dst;
    bool src0;
    bool src1;
    bool imm_optional; // imm_flag may be set; src1 (or src0 for Rr) must then be zero
    bool imm_always;   // imm_flag must be set
    bool imm_raw;      // imm is used with imm_flag clear (bra)
};

constexpr FieldUse field_use(Shape shape) noexcept {
    switch (shape) {
    case Shape::Rrr:
    case Shape::Prr:
        return {true, true, true, true, false, false};
    case Shape::Rr:
        return {true, true, false, true, false, false};
    case Shape::Acc:
        return {true, true, true, false, false, false};
    case Shape::Sreg:
        return {true, true, false, false, false, false};
    case Shape::Bra:
        return {false, false, false, false, false, true};
    case Shape::None:
        return {false, false, false, false, false, false};
    case Shape::Ld:
        return {true, true, false, false, true, false};
    case Shape::St:
        return {false, true, true, false, true, false};
    case Shape::Ldp:
        return {true, false, false, false, true, false};
    }
    return {false, false, false, false, false, false};
}

constexpr std::uint8_t special_register_count =
    static_cast<std::uint8_t>(special_register_table.size());

} // namespace

std::string_view to_string(IsaError error) noexcept {
    switch (error) {
    case IsaError::InvalidOpcode:
        return "invalid opcode";
    case IsaError::GuardFieldsWithoutGuard:
        return "guard fields set without a guard";
    case IsaError::UnusedFieldNotZero:
        return "unused field is not zero";
    case IsaError::PredicateOutOfRange:
        return "predicate register out of range";
    case IsaError::SpecialRegisterOutOfRange:
        return "special register index out of range";
    case IsaError::ImmediateFlagMismatch:
        return "immediate flag does not match the operand shape";
    }
    return "unknown error";
}

Result<void, IsaError> validate(const Instruction& instruction) noexcept {
    const auto info = opcode_info(static_cast<std::uint8_t>(instruction.opcode));
    if (!info.has_value()) {
        return fail(IsaError::InvalidOpcode);
    }
    const auto& guard = instruction.guard;
    if (!guard.present && (guard.negate || guard.pred != 0)) {
        return fail(IsaError::GuardFieldsWithoutGuard);
    }
    if (guard.pred >= predicate_register_count) {
        return fail(IsaError::PredicateOutOfRange);
    }
    if (instruction.dst >= general_register_count || instruction.src0 >= general_register_count ||
        instruction.src1 >= general_register_count) {
        // Six-bit fields cannot hold 64 or more; a struct with such a value has no encoding.
        return fail(IsaError::UnusedFieldNotZero);
    }

    const FieldUse use = field_use(info->shape);
    if (!use.dst && instruction.dst != 0) {
        return fail(IsaError::UnusedFieldNotZero);
    }
    if (!use.src0 && instruction.src0 != 0) {
        return fail(IsaError::UnusedFieldNotZero);
    }
    if (!use.src1 && instruction.src1 != 0) {
        return fail(IsaError::UnusedFieldNotZero);
    }

    if (use.imm_always) {
        if (!instruction.imm_flag) {
            return fail(IsaError::ImmediateFlagMismatch);
        }
    } else if (use.imm_raw) {
        if (instruction.imm_flag) {
            return fail(IsaError::ImmediateFlagMismatch);
        }
    } else if (use.imm_optional) {
        if (instruction.imm_flag) {
            const std::uint8_t replaced =
                info->shape == Shape::Rr ? instruction.src0 : instruction.src1;
            if (replaced != 0) {
                return fail(IsaError::UnusedFieldNotZero);
            }
        } else if (instruction.imm != 0) {
            return fail(IsaError::UnusedFieldNotZero);
        }
    } else {
        if (instruction.imm_flag) {
            return fail(IsaError::ImmediateFlagMismatch);
        }
        if (instruction.imm != 0) {
            return fail(IsaError::UnusedFieldNotZero);
        }
    }

    if (info->shape == Shape::Prr && instruction.dst >= predicate_register_count) {
        return fail(IsaError::PredicateOutOfRange);
    }
    if (info->shape == Shape::Sreg && instruction.src0 >= special_register_count) {
        return fail(IsaError::SpecialRegisterOutOfRange);
    }
    return {};
}

Result<std::uint64_t, IsaError> encode(const Instruction& instruction) noexcept {
    if (const auto valid = validate(instruction); !valid.has_value()) {
        return fail(valid.error());
    }
    std::uint64_t word = 0;
    word |= static_cast<std::uint64_t>(instruction.opcode) << opcode_shift;
    word |= static_cast<std::uint64_t>(instruction.guard.present) << guard_shift;
    word |= static_cast<std::uint64_t>(instruction.guard.negate) << negate_shift;
    word |= static_cast<std::uint64_t>(instruction.guard.pred) << pred_shift;
    word |= static_cast<std::uint64_t>(instruction.dst) << dst_shift;
    word |= static_cast<std::uint64_t>(instruction.src0) << src0_shift;
    word |= static_cast<std::uint64_t>(instruction.src1) << src1_shift;
    word |= static_cast<std::uint64_t>(instruction.imm_flag) << imm_flag_shift;
    word |= static_cast<std::uint64_t>(instruction.imm);
    return word;
}

Result<Instruction, IsaError> decode(std::uint64_t word) noexcept {
    const auto number = static_cast<std::uint8_t>((word >> opcode_shift) & mask8);
    const auto info = opcode_info(number);
    if (!info.has_value()) {
        return fail(IsaError::InvalidOpcode);
    }
    Instruction instruction;
    instruction.opcode = info->opcode;
    instruction.guard.present = ((word >> guard_shift) & mask1) != 0;
    instruction.guard.negate = ((word >> negate_shift) & mask1) != 0;
    instruction.guard.pred = static_cast<std::uint8_t>((word >> pred_shift) & mask3);
    instruction.dst = static_cast<std::uint8_t>((word >> dst_shift) & mask6);
    instruction.src0 = static_cast<std::uint8_t>((word >> src0_shift) & mask6);
    instruction.src1 = static_cast<std::uint8_t>((word >> src1_shift) & mask6);
    instruction.imm_flag = ((word >> imm_flag_shift) & mask1) != 0;
    instruction.imm = static_cast<std::uint32_t>(word & mask32);
    if (const auto valid = validate(instruction); !valid.has_value()) {
        return fail(valid.error());
    }
    return instruction;
}

} // namespace warpsim::isa
