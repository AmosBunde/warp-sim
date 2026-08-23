#include "warpsim/core/device.hpp"

#include "warpsim/asm/program.hpp"
#include "warpsim/core/fault.hpp"
#include "warpsim/core/lane_context.hpp"
#include "warpsim/core/memory.hpp"
#include "warpsim/core/types.hpp"
#include "warpsim/core/warp.hpp"
#include "warpsim/isa/instruction.hpp"
#include "warpsim/result.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace warpsim::core {

namespace {

/// Executes one block to completion under round-robin issue. Every pass over
/// the warps issues at most one instruction per runnable warp; when every
/// unfinished warp is parked, the barrier completes and all are released.
Result<void, Fault> run_block(std::span<const isa::Instruction> program, BlockMemory& memory,
                              const LaneContext& base_context, std::uint32_t lanes_in_block,
                              LaunchStats& stats) {
    const std::uint32_t warp_count = (lanes_in_block + warp_size - 1) / warp_size;
    std::vector<Warp> warps;
    warps.reserve(warp_count);
    for (std::uint32_t w = 0; w < warp_count; ++w) {
        const std::uint32_t first = w * warp_size;
        const std::uint32_t in_warp =
            lanes_in_block - first < warp_size ? lanes_in_block - first : warp_size;
        const LaneMask live = in_warp == warp_size ? full_mask : (lane_bit(in_warp) - 1);
        LaneContext context = base_context;
        context.warp_id = w;
        warps.emplace_back(w, live, context);
    }
    stats.warps_launched += warp_count;

    while (true) {
        bool any_runnable = false;
        for (auto& warp : warps) {
            if (warp.state().finished() || warp.state().parked) {
                continue;
            }
            any_runnable = true;
            const auto outcome = warp.step(program, memory);
            if (!outcome.has_value()) {
                return fail(outcome.error());
            }
            if (*outcome != StepOutcome::Finished) {
                ++stats.instructions_issued;
            }
        }
        // Barrier completion: every unfinished warp has arrived.
        bool any_unfinished = false;
        bool all_parked = true;
        for (const auto& warp : warps) {
            if (!warp.state().finished()) {
                any_unfinished = true;
                all_parked = all_parked && warp.state().parked;
            }
        }
        if (!any_unfinished) {
            break;
        }
        if (all_parked) {
            for (auto& warp : warps) {
                warp.state().parked = false;
            }
            ++stats.barriers_completed;
            continue;
        }
        if (!any_runnable) {
            // Cannot happen: an unfinished warp that is not parked is runnable.
            return fail(Fault{.message = "scheduler made no progress"});
        }
    }
    for (const auto& warp : warps) {
        stats.divergent_branches += warp.divergent_branches();
    }
    return {};
}

} // namespace

Result<LaunchStats, Fault> Device::launch(const assembler::Program& program, Dim2 grid, Dim2 block,
                                          std::span<const std::uint32_t> params) {
    std::vector<isa::Instruction> decoded;
    decoded.reserve(program.words.size());
    for (std::size_t pc = 0; pc < program.words.size(); ++pc) {
        const auto i = isa::decode(program.words[pc]);
        if (!i.has_value()) {
            return fail(Fault{.message = "invalid instruction word: " +
                                         std::string(isa::to_string(i.error())),
                              .pc = static_cast<std::uint16_t>(pc)});
        }
        decoded.push_back(*i);
    }
    if (block.count() == 0 || grid.count() == 0) {
        return fail(Fault{.message = "empty grid or block"});
    }

    LaunchStats stats;
    for (std::uint32_t by = 0; by < grid.y; ++by) {
        for (std::uint32_t bx = 0; bx < grid.x; ++bx) {
            BlockMemory memory(global_, program.shared_bytes, params);
            LaneContext context;
            context.ntid = block;
            context.nctaid = grid;
            context.ctaid = Dim2{.x = bx, .y = by};
            if (auto r = run_block(decoded, memory, context, block.count(), stats);
                !r.has_value()) {
                return fail(r.error());
            }
            ++stats.blocks_executed;
            const MemoryStats& m = memory.stats();
            stats.memory.global_loads += m.global_loads;
            stats.memory.global_stores += m.global_stores;
            stats.memory.global_lane_accesses += m.global_lane_accesses;
            stats.memory.global_segments += m.global_segments;
            stats.memory.shared_accesses += m.shared_accesses;
            stats.memory.shared_lane_accesses += m.shared_lane_accesses;
            stats.memory.shared_wavefronts += m.shared_wavefronts;
            stats.memory.shared_conflicted_accesses += m.shared_conflicted_accesses;
        }
    }
    return stats;
}

} // namespace warpsim::core
