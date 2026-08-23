#include "warpsim/asm/assembler.hpp"
#include "warpsim/core/device.hpp"
#include "warpsim/core/memory_analysis.hpp"
#include "warpsim/core/memory_stats.hpp"
#include "warpsim/core/types.hpp"

#include <array>
#include <cstdint>

#include <gtest/gtest.h>

namespace {

using warpsim::core::bank_conflict_degree;
using warpsim::core::full_mask;
using warpsim::core::MemoryAnalyzer;

std::array<std::uint32_t, 32> pattern(std::uint32_t base, std::uint32_t stride_words) {
    std::array<std::uint32_t, 32> a{};
    for (std::uint32_t lane = 0; lane < 32; ++lane) {
        a[lane] = base + (lane * stride_words * 4);
    }
    return a;
}

TEST(BankConflicts, Degrees) {
    EXPECT_EQ(bank_conflict_degree(pattern(0, 1), full_mask), 1U);
    EXPECT_EQ(bank_conflict_degree(pattern(0, 2), full_mask), 2U);
    EXPECT_EQ(bank_conflict_degree(pattern(0, 32), full_mask), 32U);
    EXPECT_EQ(bank_conflict_degree(pattern(0, 33), full_mask), 1U); // odd stride is conflict free
    EXPECT_EQ(bank_conflict_degree(pattern(0, 0), full_mask), 1U);  // broadcast
    EXPECT_EQ(bank_conflict_degree(pattern(4, 1), full_mask), 1U);  // rotation does not matter
    // Two groups of 16 lanes on two addresses that share bank 0: degree 2.
    std::array<std::uint32_t, 32> two{};
    for (unsigned lane = 0; lane < 32; ++lane) {
        two[lane] = lane < 16 ? 0U : 128U;
    }
    EXPECT_EQ(bank_conflict_degree(two, full_mask), 2U);
    EXPECT_EQ(bank_conflict_degree(two, 0x0000FFFFU), 1U); // only the first group
    EXPECT_EQ(bank_conflict_degree(pattern(0, 32), 0U), 0U);
}

TEST(BankConflicts, AnalyzerAccumulates) {
    MemoryAnalyzer analyzer;
    analyzer.on_shared(pattern(0, 1), full_mask, false);
    analyzer.on_shared(pattern(0, 32), full_mask, true);
    analyzer.on_shared(pattern(0, 2), 0x000000FFU,
                       false); // 8 lanes, stride 2: banks 0,2,..,14, degree 1
    analyzer.on_shared(pattern(0, 1), 0U, false);
    const auto& s = analyzer.stats();
    EXPECT_EQ(s.shared_accesses, 3U);
    EXPECT_EQ(s.shared_lane_accesses, 32U + 32U + 8U);
    EXPECT_EQ(s.shared_wavefronts, 1U + 32U + 1U);
    EXPECT_EQ(s.shared_conflicted_accesses, 1U);
}

TEST(BankConflicts, DeviceReportsWavefronts) {
    // One warp: a stride-1 store (degree 1) then a stride-32 load (degree 32).
    const auto program = warpsim::assembler::assemble(R"(.entry k
.shared 4096
        mov.sreg r0, %laneid
        shl r1, r0, 2
        st.shared [r1], r0
        shl r2, r0, 7
        ld.shared r3, [r2]
        exit
)");
    ASSERT_TRUE(program.has_value());
    warpsim::core::Device device(64);
    const auto stats =
        device.launch(*program, warpsim::core::Dim2{}, warpsim::core::Dim2{.x = 32, .y = 1}, {});
    ASSERT_TRUE(stats.has_value()) << stats.error().describe();
    EXPECT_EQ(stats->memory.shared_accesses, 2U);
    EXPECT_EQ(stats->memory.shared_wavefronts, 33U);
    EXPECT_EQ(stats->memory.shared_conflicted_accesses, 1U);
    EXPECT_EQ(stats->memory.global_loads, 0U);
}

} // namespace
