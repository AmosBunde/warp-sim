#include "warpsim/instr/timing.hpp"

#include "warpsim/core/device.hpp"

namespace warpsim::instr {

CostBreakdown estimate(const core::LaunchStats& stats, const CostModel& model) noexcept {
    CostBreakdown cost;
    cost.issue = stats.instructions_issued * model.issue_slot;
    cost.global = stats.memory.global_segments * model.global_segment;
    cost.shared = stats.memory.shared_wavefronts * model.shared_wavefront;
    cost.total = cost.issue + cost.global + cost.shared;
    return cost;
}

} // namespace warpsim::instr
