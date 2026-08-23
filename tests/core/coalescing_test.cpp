#include "warpsim/asm/assembler.hpp"
#include "warpsim/core/device.hpp"
#include "warpsim/core/memory_analysis.hpp"
#include "warpsim/core/memory_stats.hpp"
#include "warpsim/core/types.hpp"

#include <array>
#include <cstdint>

#include <gtest/gtest.h>

namespace {

using warpsim::core::distinct_segments;
using warpsim::core::full_mask;
using warpsim::core::MemoryAnalyzer;

std::array<std::uint32_t, 32> pattern(std::uint32_t base, std::uint32_t stride_words) {
    std::array<std::uint32_t, 32> a{};
    for (std::uint32_t lane = 0; lane < 32; ++lane) {
        a[lane] = base + (lane * stride_words * 4);
    }
    return a;
}

TEST(Coalescing, SegmentCounts) {
    EXPECT_EQ(distinct_segments(pattern(0, 1), full_mask), 1U);
    EXPECT_EQ(distinct_segments(pattern(64, 1), full_mask), 2U);  // straddles a boundary
    EXPECT_EQ(distinct_segments(pattern(128, 1), full_mask), 1U); // aligned again
    EXPECT_EQ(distinct_segments(pattern(0, 2), full_mask), 2U);
    EXPECT_EQ(distinct_segments(pattern(0, 32), full_mask), 32U);
    EXPECT_EQ(distinct_segments(pattern(0, 0), full_mask), 1U); // broadcast
    EXPECT_EQ(distinct_segments(pattern(0, 32), 0x1U), 1U);     // single lane
    EXPECT_EQ(distinct_segments(pattern(0, 32), 0x3U), 2U);
    EXPECT_EQ(distinct_segments(pattern(0, 32), 0U), 0U);
}

TEST(Coalescing, AnalyzerAccumulates) {
    MemoryAnalyzer analyzer;
    analyzer.on_global(pattern(0, 1), full_mask, false);
    analyzer.on_global(pattern(0, 32), full_mask, true);
    analyzer.on_global(pattern(0, 1), 0x0000FFFFU, false);
    analyzer.on_global(pattern(0, 1), 0U, false); // fully masked: nothing
    const auto& s = analyzer.stats();
    EXPECT_EQ(s.global_loads, 2U);
    EXPECT_EQ(s.global_stores, 1U);
    EXPECT_EQ(s.global_lane_accesses, 32U + 32U + 16U);
    EXPECT_EQ(s.global_segments, 1U + 32U + 1U);
}

TEST(Coalescing, DeviceReportsSegmentsForVecadd) {
    // 64 lanes, one block: each warp performs 2 coalesced loads and 1 store on
    // 128-byte aligned buffers: 3 segments per warp, 6 in total.
    const auto program = warpsim::assembler::assemble(R"(.entry k
.param a
.param b
.param c
.param n
        mov.sreg r0, %tid.x
        shl r4, r0, 2
        ld.param r5, [a]
        add r5, r5, r4
        ld.global r6, [r5]
        ld.param r7, [b]
        add r7, r7, r4
        ld.global r8, [r7]
        add r6, r6, r8
        ld.param r9, [c]
        add r9, r9, r4
        st.global [r9], r6
        exit
)");
    ASSERT_TRUE(program.has_value());
    warpsim::core::Device device(4096);
    const std::array<std::uint32_t, 4> params = {0, 1024, 2048, 64};
    const auto stats = device.launch(*program, warpsim::core::Dim2{},
                                     warpsim::core::Dim2{.x = 64, .y = 1}, params);
    ASSERT_TRUE(stats.has_value()) << stats.error().describe();
    EXPECT_EQ(stats->memory.global_loads, 4U);
    EXPECT_EQ(stats->memory.global_stores, 2U);
    EXPECT_EQ(stats->memory.global_lane_accesses, 6U * 32U);
    EXPECT_EQ(stats->memory.global_segments, 6U);
}

} // namespace
