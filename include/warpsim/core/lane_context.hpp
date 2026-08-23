#pragma once

#include "warpsim/core/types.hpp"
#include "warpsim/isa/opcode.hpp"

#include <cstdint>

namespace warpsim::core {

/// Launch geometry visible to a warp through the special registers
/// (specification section 2.4). Lane-specific values are derived from the
/// warp index and the lane index: linear block index = warp_id * 32 + lane.
struct LaneContext {
    Dim2 ntid;
    Dim2 nctaid;
    Dim2 ctaid;
    unsigned warp_id = 0;

    [[nodiscard]] constexpr std::uint32_t linear_index(unsigned lane) const noexcept {
        return (warp_id * warp_size) + lane;
    }

    [[nodiscard]] constexpr std::uint32_t special(isa::SpecialRegister reg,
                                                  unsigned lane) const noexcept {
        using isa::SpecialRegister;
        switch (reg) {
        case SpecialRegister::TidX:
            return linear_index(lane) % ntid.x;
        case SpecialRegister::TidY:
            return linear_index(lane) / ntid.x;
        case SpecialRegister::NtidX:
            return ntid.x;
        case SpecialRegister::NtidY:
            return ntid.y;
        case SpecialRegister::CtaidX:
            return ctaid.x;
        case SpecialRegister::CtaidY:
            return ctaid.y;
        case SpecialRegister::NctaidX:
            return nctaid.x;
        case SpecialRegister::NctaidY:
            return nctaid.y;
        case SpecialRegister::LaneId:
            return lane;
        case SpecialRegister::WarpId:
            return warp_id;
        }
        return 0;
    }
};

} // namespace warpsim::core
