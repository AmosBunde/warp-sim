#pragma once

#include "warpsim/isa/opcode.hpp"
#include "warpsim/result.hpp"

#include <cstdint>
#include <string_view>

namespace warpsim::isa {

inline constexpr std::uint8_t general_register_count = 64;
inline constexpr std::uint8_t predicate_register_count = 8;
/// Reconvergence value meaning "no reconvergence point" (specification section 3).
inline constexpr std::uint16_t no_reconvergence = 0xFFFF;

/// A guard `@p` or `@!p`. When `present` is false the other fields are zero.
struct Guard {
    bool present = false;
    bool negate = false;
    std::uint8_t pred = 0;

    [[nodiscard]] constexpr bool operator==(const Guard&) const noexcept = default;
};

/// One decoded instruction. Field meaning follows docs/wisa-spec.md section 3.
struct Instruction {
    Opcode opcode = Opcode::Exit;
    Guard guard;
    std::uint8_t dst = 0;
    std::uint8_t src0 = 0;
    std::uint8_t src1 = 0;
    bool imm_flag = false;
    std::uint32_t imm = 0;

    [[nodiscard]] constexpr bool operator==(const Instruction&) const noexcept = default;

    /// Branch target PC, valid only for `bra`.
    [[nodiscard]] constexpr std::uint16_t branch_target() const noexcept {
        return static_cast<std::uint16_t>(imm & 0xFFFFU);
    }

    /// Reconvergence PC, valid only for `bra`; `no_reconvergence` when absent.
    [[nodiscard]] constexpr std::uint16_t reconvergence_pc() const noexcept {
        return static_cast<std::uint16_t>(imm >> 16U);
    }

    /// Builds the `bra` immediate from its two halves.
    [[nodiscard]] static constexpr std::uint32_t
    make_branch_imm(std::uint16_t target, std::uint16_t reconvergence) noexcept {
        return (static_cast<std::uint32_t>(reconvergence) << 16U) | target;
    }
};

/// Every way an instruction or a 64-bit word can fail validation.
enum class IsaError : std::uint8_t {
    InvalidOpcode,
    GuardFieldsWithoutGuard,
    UnusedFieldNotZero,
    PredicateOutOfRange,
    SpecialRegisterOutOfRange,
    ImmediateFlagMismatch,
};

[[nodiscard]] std::string_view to_string(IsaError error) noexcept;

/// Checks that `instruction` has exactly one valid encoding under its shape.
[[nodiscard]] Result<void, IsaError> validate(const Instruction& instruction) noexcept;

/// Encodes a valid instruction into its 64-bit word.
[[nodiscard]] Result<std::uint64_t, IsaError> encode(const Instruction& instruction) noexcept;

/// Decodes a 64-bit word. Any word that `encode` cannot produce is an error.
[[nodiscard]] Result<Instruction, IsaError> decode(std::uint64_t word) noexcept;

} // namespace warpsim::isa
