#include "warpsim/core/types.hpp"
#include "warpsim/core/warp.hpp"
#include "warpsim/core/warp_state.hpp"

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "support/warp_harness.hpp"

namespace {

using warpsim::core::DivergenceEntry;
using warpsim::core::full_mask;
using warpsim::core::LaneContext;
using warpsim::core::LaneMask;
using warpsim::core::StepOutcome;
using warpsim::core::Warp;
using warpsim::testing::decode_program;
using warpsim::testing::NullMemory;
using warpsim::testing::run_to_end;

// Every kernel below starts by loading %laneid into r0 so that control flow is
// data dependent per lane. Results are left in r10 and compared lane by lane.

LaneContext context_for(std::uint32_t ntid_x) {
    LaneContext ctx;
    ctx.ntid.x = ntid_x;
    return ctx;
}

LaneContext block_one_context() {
    LaneContext ctx = context_for(32);
    ctx.ctaid.x = 1;
    ctx.nctaid.x = 2;
    return ctx;
}

std::vector<std::uint32_t> r10_of(const Warp& warp) {
    std::vector<std::uint32_t> out;
    out.reserve(32);
    for (unsigned lane = 0; lane < 32; ++lane) {
        out.push_back(warp.state().registers.read(lane, 10));
    }
    return out;
}

TEST(Divergence, SpecificationWorkedExampleStackContents) {
    // Section 8.5 with p0 true on lanes 0..15: r0 = laneid, r1 = 16.
    const auto program = decode_program(R"(.entry k
        mov.sreg r0, %laneid
        mov r1, 16
        setp.lt.s32 p0, r0, r1
    @p0 bra then
        mov r2, 10
        bra join
then:   mov r2, 20
join:   add r2, r2, 1
        exit
)");
    // PCs: 0 sreg, 1 mov, 2 setp, 3 bra(then=6, reconv=7), 4 mov, 5 bra join, 6 then, 7 join, 8
    // exit
    Warp warp(0, full_mask, context_for(32));
    NullMemory memory;
    const auto step = [&] {
        const auto r = warp.step(program, memory);
        ASSERT_TRUE(r.has_value()) << r.error().describe();
    };
    step(); // mov.sreg
    step(); // mov
    step(); // setp
    step(); // bra: diverges
    EXPECT_EQ(warp.state().pc, 6U);
    EXPECT_EQ(warp.state().active, 0x0000FFFFU);
    ASSERT_EQ(warp.state().stack.size(), 1U);
    EXPECT_EQ(warp.state().stack[0],
              (DivergenceEntry{.reconvergence_pc = 7, .resume_pc = 4, .mask = 0xFFFF0000U}));
    EXPECT_EQ(warp.divergent_branches(), 1U);
    step(); // then: mov r2, 20 on lanes 0..15
    EXPECT_EQ(warp.state().pc, 7U);
    step(); // at join: rule 3 switches to the deferred path, then issues mov r2, 10
    EXPECT_EQ(warp.state().pc, 5U);
    EXPECT_EQ(warp.state().active, 0xFFFF0000U);
    ASSERT_EQ(warp.state().stack.size(), 1U);
    EXPECT_EQ(warp.state().stack[0],
              (DivergenceEntry{.reconvergence_pc = 7, .resume_pc = 7, .mask = 0x0000FFFFU}));
    step(); // bra join (unguarded)
    EXPECT_EQ(warp.state().pc, 7U);
    step(); // at join again: rule 3 unions, then issues add
    EXPECT_EQ(warp.state().active, full_mask);
    EXPECT_TRUE(warp.state().stack.empty());
    EXPECT_EQ(warp.state().pc, 8U);
    step(); // exit
    EXPECT_TRUE(warp.state().finished());
    EXPECT_EQ(warp.step(program, memory).value(), StepOutcome::Finished);
    for (unsigned lane = 0; lane < 32; ++lane) {
        EXPECT_EQ(warp.state().registers.read(lane, 2), lane < 16 ? 21U : 11U) << lane;
    }
}

TEST(Divergence, IfWithoutElse) {
    const auto program = decode_program(R"(.entry k
        mov.sreg r0, %laneid
        and r1, r0, 1
        setp.eq.s32 p0, r1, 0
    @p0 bra skip
        add r10, r10, 100
skip:   add r10, r10, r0
        exit
)");
    Warp warp(0, full_mask, context_for(32));
    NullMemory memory;
    run_to_end(warp, program, memory);
    const auto r = r10_of(warp);
    for (unsigned lane = 0; lane < 32; ++lane) {
        EXPECT_EQ(r[lane], (lane % 2 == 1 ? 100U : 0U) + lane) << lane;
    }
}

TEST(Divergence, NestedDepthThree) {
    // Three nested data-dependent branches on bits 0, 1, 2 of the lane id.
    const auto program = decode_program(R"(.entry k
        mov.sreg r0, %laneid
        and r1, r0, 1
        and r2, r0, 2
        and r3, r0, 4
        setp.eq.s32 p1, r1, 0
        setp.eq.s32 p2, r2, 0
        setp.eq.s32 p3, r3, 0
    @p1 bra l1
        add r10, r10, 1
    @p2 bra l2
        add r10, r10, 10
    @p3 bra l3
        add r10, r10, 100
l3:     add r10, r10, 1000
l2:     add r10, r10, 10000
l1:     add r10, r10, 100000
        exit
)");
    Warp warp(0, full_mask, context_for(32));
    NullMemory memory;
    run_to_end(warp, program, memory);
    const auto r = r10_of(warp);
    for (unsigned lane = 0; lane < 32; ++lane) {
        std::uint32_t expected = 100000;
        if ((lane & 1U) != 0) {
            expected += 1 + 10000;
            if ((lane & 2U) != 0) {
                expected += 10 + 1000;
                if ((lane & 4U) != 0) {
                    expected += 100;
                }
            }
        }
        EXPECT_EQ(r[lane], expected) << lane;
    }
    EXPECT_TRUE(warp.state().stack.empty());
}

TEST(Divergence, LoopWithDivergentTripCounts) {
    // Each lane loops laneid times, summing its counter.
    const auto program = decode_program(R"(.entry k
        mov.sreg r0, %laneid
        mov r1, 0
top:    setp.ge.s32 p0, r1, r0
    @p0 bra done
        add r10, r10, r1
        add r1, r1, 1
        bra top
done:   exit
)");
    Warp warp(0, full_mask, context_for(32));
    NullMemory memory;
    run_to_end(warp, program, memory);
    const auto r = r10_of(warp);
    for (unsigned lane = 0; lane < 32; ++lane) {
        EXPECT_EQ(r[lane], lane * (lane - 1) / 2) << lane;
    }
}

TEST(Divergence, EarlyExitFromLoop) {
    // Loop to 40 but lanes whose id is a multiple of 5 leave when r1 == laneid.
    const auto program = decode_program(R"(.entry k
        mov.sreg r0, %laneid
        rem r2, r0, 5
        setp.eq.s32 p2, r2, 0
        mov r1, 0
top:    setp.eq.s32 p1, r1, r0
    @p1 bra check
        add r1, r1, 1
        setp.lt.s32 p0, r1, 40
    @p0 bra top
        mov r10, 40
        exit
check:
    @!p2 bra back
        mov r10, r1
        exit
back:   add r1, r1, 1
        bra top
)");
    Warp warp(0, full_mask, context_for(32));
    NullMemory memory;
    run_to_end(warp, program, memory);
    const auto r = r10_of(warp);
    for (unsigned lane = 0; lane < 32; ++lane) {
        EXPECT_EQ(r[lane], lane % 5 == 0 ? lane : 40U) << lane;
    }
}

TEST(Divergence, ExitInsideDivergentCode) {
    const auto program = decode_program(R"(.entry k
        mov.sreg r0, %laneid
        setp.lt.s32 p0, r0, 8
    @p0 bra early
        mov r10, 2
        exit
early:  mov r10, 1
        exit
)");
    Warp warp(0, full_mask, context_for(32));
    NullMemory memory;
    run_to_end(warp, program, memory);
    const auto r = r10_of(warp);
    for (unsigned lane = 0; lane < 32; ++lane) {
        EXPECT_EQ(r[lane], lane < 8 ? 1U : 2U) << lane;
    }
    EXPECT_TRUE(warp.state().finished());
}

TEST(Divergence, GuardedExitWithCommonTail) {
    const auto program = decode_program(R"(.entry k
        mov.sreg r0, %laneid
        setp.lt.s32 p0, r0, 4
        setp.lt.s32 p1, r0, 12
    @p0 bra skip
    @p1 exit
        add r10, r10, 5
skip:   add r10, r10, 1
        exit
)");
    Warp warp(0, full_mask, context_for(32));
    NullMemory memory;
    run_to_end(warp, program, memory);
    const auto r = r10_of(warp);
    for (unsigned lane = 0; lane < 32; ++lane) {
        std::uint32_t expected = 6;
        if (lane < 4) {
            expected = 1;
        } else if (lane < 12) {
            expected = 0;
        }
        EXPECT_EQ(r[lane], expected) << lane;
    }
}

TEST(Divergence, PartialWarpAndUniformBranchesDoNotDiverge) {
    const auto program = decode_program(R"(.entry k
        mov r1, 1
        setp.eq.s32 p0, r1, 1
    @p0 bra there
        mov r10, 99
there:  mov r10, 7
        exit
)");
    Warp warp(2, 0x000000FFU, context_for(72));
    NullMemory memory;
    run_to_end(warp, program, memory);
    EXPECT_EQ(warp.divergent_branches(), 0U);
    for (unsigned lane = 0; lane < 32; ++lane) {
        EXPECT_EQ(warp.state().registers.read(lane, 10), lane < 8 ? 7U : 0U) << lane;
    }
}

TEST(Divergence, FallingOffTheEndRetires) {
    const auto program = decode_program(".entry k\n mov r10, 3\n");
    Warp warp(0, full_mask, context_for(32));
    NullMemory memory;
    EXPECT_EQ(run_to_end(warp, program, memory), 1U);
    EXPECT_TRUE(warp.state().finished());
}

TEST(Divergence, BarrierInDivergentCodeFaults) {
    const auto program = decode_program(R"(.entry k
        mov.sreg r0, %laneid
        setp.lt.s32 p0, r0, 8
    @p0 bra a
        bar.sync
        exit
a:      bar.sync
        exit
)");
    Warp warp(0, full_mask, block_one_context());
    NullMemory memory;
    for (int n = 0; n < 3; ++n) {
        ASSERT_TRUE(warp.step(program, memory).has_value());
    }
    const auto r = warp.step(program, memory);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().message, "bar.sync executed in divergent code");
    EXPECT_EQ(r.error().pc, 5U);
    EXPECT_EQ(r.error().block, 1U);
    EXPECT_NE(r.error().describe().find("pc 5"), std::string::npos);
}

TEST(Divergence, MemoryFaultCarriesLaneAndAddress) {
    const auto program = decode_program(".entry k\n mov r1, 64\n ld.global r2, [r1+4]\n");
    Warp warp(0, 0x1U, context_for(32));
    NullMemory memory;
    ASSERT_TRUE(warp.step(program, memory).has_value());
    const auto r = warp.step(program, memory);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().message, "no memory");
    EXPECT_EQ(r.error().address, 68U);
    EXPECT_EQ(r.error().lane, 0U);
}

} // namespace
