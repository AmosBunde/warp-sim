#pragma once

#include "warpsim/core/types.hpp"
#include "warpsim/isa/instruction.hpp"

#include <array>
#include <cassert>
#include <cstdint>

namespace warpsim::core {

/// Per-warp register storage: 64 general 32-bit registers and 8 predicates
/// for each of the 32 lanes, zero initialized. Registers are untyped bits;
/// the ALU interprets them. Indices are asserted in debug builds; the
/// encoding's six-bit and three-bit fields make larger values unrepresentable.
class RegisterFile {
public:
    [[nodiscard]] std::uint32_t read(unsigned lane, std::uint8_t reg) const noexcept {
        assert(lane < warp_size && reg < isa::general_register_count);
        return general_[lane][reg];
    }

    void write(unsigned lane, std::uint8_t reg, std::uint32_t value) noexcept {
        assert(lane < warp_size && reg < isa::general_register_count);
        general_[lane][reg] = value;
    }

    [[nodiscard]] bool read_pred(unsigned lane, std::uint8_t pred) const noexcept {
        assert(lane < warp_size && pred < isa::predicate_register_count);
        return predicates_[lane][pred];
    }

    void write_pred(unsigned lane, std::uint8_t pred, bool value) noexcept {
        assert(lane < warp_size && pred < isa::predicate_register_count);
        predicates_[lane][pred] = value;
    }

private:
    std::array<std::array<std::uint32_t, isa::general_register_count>, warp_size> general_{};
    std::array<std::array<bool, isa::predicate_register_count>, warp_size> predicates_{};
};

} // namespace warpsim::core
