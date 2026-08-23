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

std::uint32_t bank_conflict_degree(std::span<const std::uint32_t, warp_size> addresses,
                                   LaneMask exec) noexcept {
    // Distinct addresses seen per bank; at most 32 lanes contribute.
    std::array<std::array<std::uint32_t, warp_size>, bank_count> seen{};
    std::array<std::uint32_t, bank_count> counts{};
    std::uint32_t degree = 0;
    for (unsigned lane = 0; lane < warp_size; ++lane) {
        if (!has_lane(exec, lane)) {
            continue;
        }
        const std::uint32_t address = addresses[lane];
        const std::uint32_t bank = (address / bank_width_bytes) % bank_count;
        auto& bucket = seen[bank];
        auto& n = counts[bank];
        const std::span<const std::uint32_t> in_bank(bucket.data(), n);
        if (std::ranges::find(in_bank, address) == in_bank.end()) {
            bucket[n] = address;
            ++n;
            degree = std::max(degree, n);
        }
    }
    return degree;
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

void MemoryAnalyzer::on_shared(std::span<const std::uint32_t, warp_size> addresses, LaneMask exec,
                               bool /*is_store*/) noexcept {
    if (exec == 0) {
        return;
    }
    ++stats_.shared_accesses;
    stats_.shared_lane_accesses += lane_count(exec);
    const std::uint32_t degree = bank_conflict_degree(addresses, exec);
    stats_.shared_wavefronts += degree;
    if (degree > 1) {
        ++stats_.shared_conflicted_accesses;
    }
}

} // namespace warpsim::core
