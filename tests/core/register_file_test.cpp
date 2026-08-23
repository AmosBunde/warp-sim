#include "warpsim/core/register_file.hpp"
#include "warpsim/core/types.hpp"
#include "warpsim/core/warp_state.hpp"

#include <gtest/gtest.h>

namespace {

using warpsim::core::DivergenceEntry;
using warpsim::core::RegisterFile;

TEST(RegisterFile, ZeroInitialized) {
    const RegisterFile rf;
    for (unsigned lane = 0; lane < warpsim::core::warp_size; ++lane) {
        for (std::uint8_t r = 0; r < 64; ++r) {
            EXPECT_EQ(rf.read(lane, r), 0U);
        }
        for (std::uint8_t p = 0; p < 8; ++p) {
            EXPECT_FALSE(rf.read_pred(lane, p));
        }
    }
}

TEST(RegisterFile, LanesAreIsolated) {
    RegisterFile rf;
    rf.write(3, 7, 0xDEADBEEFU);
    rf.write(4, 7, 1U);
    rf.write_pred(3, 2, true);
    EXPECT_EQ(rf.read(3, 7), 0xDEADBEEFU);
    EXPECT_EQ(rf.read(4, 7), 1U);
    EXPECT_EQ(rf.read(2, 7), 0U);
    EXPECT_EQ(rf.read(3, 8), 0U);
    EXPECT_TRUE(rf.read_pred(3, 2));
    EXPECT_FALSE(rf.read_pred(4, 2));
    EXPECT_FALSE(rf.read_pred(3, 3));
}

TEST(LaneMask, Helpers) {
    using namespace warpsim::core;
    EXPECT_EQ(lane_bit(0), 1U);
    EXPECT_EQ(lane_bit(31), 0x80000000U);
    EXPECT_TRUE(has_lane(0x5U, 2));
    EXPECT_FALSE(has_lane(0x5U, 1));
    EXPECT_EQ(lane_count(full_mask), 32U);
    EXPECT_EQ(lane_count(0), 0U);
    EXPECT_EQ((Dim2{.x = 4, .y = 3}).count(), 12U);
}

TEST(WarpState, JoinEntryIsRecognized) {
    EXPECT_TRUE((DivergenceEntry{.reconvergence_pc = 5, .resume_pc = 5, .mask = 1}).is_join());
    EXPECT_FALSE((DivergenceEntry{.reconvergence_pc = 5, .resume_pc = 2, .mask = 1}).is_join());
    warpsim::core::WarpState w;
    EXPECT_TRUE(w.finished());
    w.live = 1;
    EXPECT_FALSE(w.finished());
}

} // namespace
