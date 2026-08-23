#pragma once

#include "warpsim/asm/program.hpp"
#include "warpsim/core/fault.hpp"
#include "warpsim/core/memory.hpp"
#include "warpsim/core/memory_stats.hpp"
#include "warpsim/core/types.hpp"
#include "warpsim/result.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace warpsim::core {

/// Observable totals of one launch. Counts, never cycles (README scope).
struct LaunchStats {
    std::uint64_t instructions_issued = 0;
    std::uint64_t divergent_branches = 0;
    std::uint64_t barriers_completed = 0;
    std::uint32_t blocks_executed = 0;
    std::uint32_t warps_launched = 0;
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
