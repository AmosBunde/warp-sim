#pragma once

#include "warpsim/core/device.hpp"

#include <cstdint>

namespace warpsim::instr {

/// Weights of the coarse cost model, in issue slots. These are ordinal
/// weights chosen so that the observable mechanisms (issue count, global
/// segments, shared wavefronts) compose into one comparable number. They are
/// not calibrated to any hardware and the result is not a cycle count; the
/// only claim made of the model is the ranking it produces, and the
/// acceptance test in python/tests/test_timing_ordinal.py checks that the
/// ranking is attributable to the segment counts.
struct CostModel {
    std::uint64_t issue_slot = 1;       ///< one warp-level instruction issue
    std::uint64_t global_segment = 8;   ///< one distinct 128-byte segment of a global access
    std::uint64_t shared_wavefront = 1; ///< one serialized pass of a shared access
};

/// Ordinal cost units of one launch, split by mechanism. `total` is the sum.
/// Divergence does not appear as a separate term because its effect is
/// already present in `issue`: serialized paths issue more instructions,
/// which `LaunchStats::divergent_branches` and the active-lane histogram
/// attribute.
struct CostBreakdown {
    std::uint64_t issue = 0;
    std::uint64_t global = 0;
    std::uint64_t shared = 0;
    std::uint64_t total = 0;

    [[nodiscard]] constexpr bool operator==(const CostBreakdown&) const noexcept = default;
};

[[nodiscard]] CostBreakdown estimate(const core::LaunchStats& stats,
                                     const CostModel& model = {}) noexcept;

} // namespace warpsim::instr
