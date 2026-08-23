#include "warpsim/core/memory_analysis.hpp"

#include "warpsim/core/memory_stats.hpp"
#include "warpsim/core/types.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

namespace warpsim::core {

std::uint32_t distinct_segments(std::span<const std::uint32_t, warp_size> addresses,
                                LaneMask exec) noexcept {
    std::array<std::uint32_t, warp_size> segments{};
    std::uint32_t count = 0;
    for (unsigned lane = 0; lane < warp_size; ++lane) {
        if (!has_lane(exec, lane)) {
            continue;
        }
        const std::uint32_t segment = addresses[lane] / segment_bytes;
        const std::span<const std::uint32_t> seen(segments.data(), count);
        if (std::ranges::find(seen, segment) == seen.end()) {
            segments[count] = segment;
            ++count;
        }
    }
    return count;
}

void MemoryAnalyzer::on_global(std::span<const std::uint32_t, warp_size> addresses, LaneMask exec,
                               bool is_store) noexcept {
    if (exec == 0) {
        return;
    }
    if (is_store) {
        ++stats_.global_stores;
    } else {
        ++stats_.global_loads;
    }
    stats_.global_lane_accesses += lane_count(exec);
    stats_.global_segments += distinct_segments(addresses, exec);
}

} // namespace warpsim::core
