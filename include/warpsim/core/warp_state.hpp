#pragma once

#include "warpsim/core/register_file.hpp"
#include "warpsim/core/types.hpp"

#include <cstdint>
#include <vector>

namespace warpsim::core {

/// One entry of the divergence stack (specification section 8.3). A deferred
/// path entry resumes at `resume_pc` with `mask`; a join entry has
/// `resume_pc == reconvergence_pc` and its mask is unioned back in.
struct DivergenceEntry {
    std::uint16_t reconvergence_pc = 0;
    std::uint16_t resume_pc = 0;
    LaneMask mask = 0;

    [[nodiscard]] constexpr bool is_join() const noexcept { return resume_pc == reconvergence_pc; }
    [[nodiscard]] constexpr bool operator==(const DivergenceEntry&) const noexcept = default;
};

/// Everything a warp carries between instructions.
struct WarpState {
    std::uint16_t pc = 0;
    LaneMask active = 0; ///< lanes executing the current path
    LaneMask live = 0;   ///< lanes that have not retired
    std::vector<DivergenceEntry> stack;
    RegisterFile registers;
    unsigned warp_id = 0;
    bool parked = false; ///< waiting at a barrier

    [[nodiscard]] bool finished() const noexcept { return live == 0; }
};

} // namespace warpsim::core
