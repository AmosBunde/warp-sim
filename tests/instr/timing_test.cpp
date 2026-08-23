#include "warpsim/core/device.hpp"
#include "warpsim/instr/timing.hpp"

#include <gtest/gtest.h>

namespace {

using warpsim::core::LaunchStats;
using warpsim::instr::CostModel;
using warpsim::instr::estimate;

TEST(Timing, ComposesCountsWithStatedWeights) {
    LaunchStats stats;
    stats.instructions_issued = 100;
    stats.memory.global_segments = 10;
    stats.memory.shared_wavefronts = 7;
    const auto cost = estimate(stats);
    EXPECT_EQ(cost.issue, 100U);
    EXPECT_EQ(cost.global, 80U);
    EXPECT_EQ(cost.shared, 7U);
    EXPECT_EQ(cost.total, 187U);

    const CostModel custom{.issue_slot = 2, .global_segment = 1, .shared_wavefront = 3};
    const auto other = estimate(stats, custom);
    EXPECT_EQ(other.total, 200U + 10U + 21U);
}

TEST(Timing, EmptyLaunchCostsNothing) {
    EXPECT_EQ(estimate(LaunchStats{}).total, 0U);
}

} // namespace
