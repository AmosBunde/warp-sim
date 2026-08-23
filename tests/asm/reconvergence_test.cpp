#include "warpsim/asm/assembler.hpp"
#include "warpsim/isa/instruction.hpp"
#include "warpsim/isa/opcode.hpp"

#include <cstdint>
#include <map>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "asm/reconvergence.hpp"

namespace {

using warpsim::isa::Instruction;
using warpsim::isa::no_reconvergence;
using warpsim::isa::Opcode;

/// Assembles and returns {pc -> reconvergence} for every guarded branch.
std::map<std::uint16_t, std::uint16_t> reconvergence_points(std::string_view source) {
    const auto program = warpsim::assembler::assemble(source);
    if (!program.has_value()) {
        ADD_FAILURE() << program.error().line << ": " << program.error().message;
        return {};
    }
    std::map<std::uint16_t, std::uint16_t> out;
    for (std::size_t pc = 0; pc < program->words.size(); ++pc) {
        const auto i = warpsim::isa::decode(program->words[pc]).value();
        if (i.opcode == Opcode::Bra) {
            out[static_cast<std::uint16_t>(pc)] = i.reconvergence_pc();
        }
    }
    return out;
}

using Expected = std::map<std::uint16_t, std::uint16_t>;

TEST(Reconvergence, SpecificationWorkedExample) {
    // docs/wisa-spec.md section 8.5: branch at PC 1 targets 4, reconverges at 5.
    const auto points = reconvergence_points(R"(.entry k
        setp.lt.s32 p0, r0, r1
    @p0 bra then
        mov r2, 10
        bra join
then:   mov r2, 20
join:   add r2, r2, 1
        exit
)");
    EXPECT_EQ(points, (Expected{{1, 5}, {3, no_reconvergence}}));
}

TEST(Reconvergence, IfWithoutElse) {
    const auto points = reconvergence_points(R"(.entry k
    @p0 bra skip
        mov r1, 1
skip:   mov r2, 2
        exit
)");
    EXPECT_EQ(points, (Expected{{0, 2}}));
}

TEST(Reconvergence, NestedDepthThree) {
    const auto points = reconvergence_points(R"(.entry k
    @p0 bra l1          // 0: reconverges at j1 (9)
    @p1 bra l2          // 1: reconverges at j2 (7)
    @p2 bra l3          // 2: reconverges at j3 (5)
        mov r1, 1       // 3
l3:     mov r2, 2       // 4 (leader because it is a target)
        add r2, r2, 1   // 5? no: j3 is l3 itself
l2:     mov r3, 3
        add r3, r3, 1
l1:     mov r4, 4
        exit
)");
    // PCs: 0 bra, 1 bra, 2 bra, 3 mov, 4 l3, 5 add, 6 l2, 7 add, 8 l1, 9 exit.
    EXPECT_EQ(points, (Expected{{0, 8}, {1, 6}, {2, 4}}));
}

TEST(Reconvergence, LoopBackEdgeReconvergesAtLoopExit) {
    const auto points = reconvergence_points(R"(.entry k
        mov r0, 0
top:    add r0, r0, 1
        setp.lt.s32 p0, r0, r1
    @p0 bra top
        mov r2, r0
        exit
)");
    EXPECT_EQ(points, (Expected{{3, 4}}));
}

TEST(Reconvergence, EarlyExitFromLoopReconvergesAfterLoop) {
    const auto points = reconvergence_points(R"(.entry k
        mov r0, 0
top:    add r0, r0, 1       // 1
        setp.eq.s32 p1, r0, r3
    @p1 bra done            // 3: early exit, reconverges at done (7)
        setp.lt.s32 p0, r0, r1
    @p0 bra top             // 5: back edge; 6 is bypassed by the early exit, so 7
        mov r2, 1           // 6
done:   mov r4, r0          // 7
        exit
)");
    EXPECT_EQ(points, (Expected{{3, 7}, {5, 7}}));
}

TEST(Reconvergence, ExitInsideDivergentCode) {
    const auto points = reconvergence_points(R"(.entry k
    @p0 bra other       // 0: taken path retires; both paths reach exit node only
        mov r1, 1
        exit
other:  mov r1, 2
        exit
)");
    EXPECT_EQ(points, (Expected{{0, no_reconvergence}}));
}

TEST(Reconvergence, GuardedExitOnOnePathPreventsReconvergence) {
    // The guarded exit at PC 1 has an edge to the virtual exit node, so no block
    // post-dominates the branch at PC 0 (specification 8.1 and 8.4). The core
    // resumes the deferred path by rule 4 once the taken path has retired.
    const auto points = reconvergence_points(R"(.entry k
    @p0 bra skip        // 0: no reconvergence
    @p1 exit            // 1: retires some lanes, the rest fall through
        mov r1, 1       // 2
skip:   mov r2, 2       // 3
        exit
)");
    EXPECT_EQ(points, (Expected{{0, no_reconvergence}}));
}

TEST(Reconvergence, InfiniteLoopHasNoReconvergence) {
    const auto points = reconvergence_points(R"(.entry k
top:    add r0, r0, 1
    @p0 bra top
        bra top
)");
    EXPECT_EQ(points, (Expected{{1, no_reconvergence}, {2, no_reconvergence}}));
}

TEST(Reconvergence, BranchTargetOutOfRangeIsAnError) {
    std::vector<Instruction> program(1);
    program[0].opcode = Opcode::Bra;
    program[0].guard = {.present = true, .negate = false, .pred = 0};
    program[0].imm = Instruction::make_branch_imm(5, no_reconvergence);
    const auto result = warpsim::assembler::annotate_reconvergence(program);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().message, "branch target out of range");
}

} // namespace
