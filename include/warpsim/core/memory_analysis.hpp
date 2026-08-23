#pragma once

#include "warpsim/core/memory_stats.hpp"
#include "warpsim/core/types.hpp"

#include <cstdint>
#include <span>

namespace warpsim::core {

/// Pure functions over one warp-level access, so that the counting rules can
/// be tested without a device.
[[nodiscard]] std::uint32_t distinct_segments(std::span<const std::uint32_t, warp_size> addresses,
                                              LaneMask exec) noexcept;

/// Accumulates MemoryStats from warp-level accesses.
class MemoryAnalyzer {
public:
    void on_global(std::span<const std::uint32_t, warp_size> addresses, LaneMask exec,
                   bool is_store) noexcept;

    [[nodiscard]] const MemoryStats& stats() const noexcept { return stats_; }

private:
    MemoryStats stats_;
};

} // namespace warpsim::core
