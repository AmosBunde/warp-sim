#pragma once

#include "warpsim/asm/program.hpp"
#include "warpsim/core/fault.hpp"
#include "warpsim/core/memory.hpp"
#include "warpsim/core/memory_stats.hpp"
#include "warpsim/core/types.hpp"
#include "warpsim/result.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace warpsim::core {

/// Observable totals of one launch. Every field is a count of events; none
/// is a time. The rule producing each counter is in docs/counters.md.
struct LaunchStats {
    std::uint64_t instructions_issued = 0;  ///< warp-level issues of any class
    std::uint64_t alu_instructions = 0;     ///< issues of arithmetic, logic, compare, move
    std::uint64_t memory_instructions = 0;  ///< issues of ld and st of any space
    std::uint64_t control_instructions = 0; ///< issues of bra and exit
    std::uint64_t barrier_instructions = 0; ///< issues of bar.sync
    std::uint64_t divergent_branches = 0;   ///< guarded bra that split the active mask
    std::uint64_t reconvergence_events = 0; ///< arrivals at a reconvergence point
    std::uint64_t barriers_completed = 0;
    std::uint64_t active_lane_sum = 0; ///< lanes executing, summed over issues
    /// Issues by number of executing lanes, index 0 through 32.
    std::array<std::uint64_t, warp_size + 1> active_lane_histogram{};
    std::uint32_t blocks_executed = 0;
    std::uint32_t warps_launched = 0;
    std::uint32_t lanes_launched = 0; ///< block lanes times blocks
    MemoryStats memory;

    [[nodiscard]] constexpr bool operator==(const LaunchStats&) const noexcept = default;
};

/// One simulated device: a single streaming multiprocessor model with one
/// global memory. Blocks execute in order; warps of a block are issued under
/// a deterministic round-robin scheduler.
class Device {
public:
    explicit Device(std::size_t global_bytes) : global_(global_bytes) {}

    [[nodiscard]] ByteMemory& global() noexcept { return global_; }
    [[nodiscard]] const ByteMemory& global() const noexcept { return global_; }

    /// Runs `program` over `grid` blocks of `block` lanes with the given
    /// parameter words. Returns the launch totals or the first fault.
    [[nodiscard]] Result<LaunchStats, Fault> launch(const assembler::Program& program, Dim2 grid,
                                                    Dim2 block,
                                                    std::span<const std::uint32_t> params);

private:
    ByteMemory global_;
};

} // namespace warpsim::core
