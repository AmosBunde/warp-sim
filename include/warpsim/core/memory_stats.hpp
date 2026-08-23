#pragma once

#include <cstdint>

namespace warpsim::core {

/// Byte size of one global memory segment for the coalescing analyzer.
inline constexpr std::uint32_t segment_bytes = 128;

/// Memory observables of one launch. Every number is a count of events
/// defined below, never a time.
///
/// Coalescing rule: for one warp-level global instruction, the executing
/// lanes' addresses are mapped to `address / 128`; `global_segments` adds
/// the number of distinct values. A perfectly coalesced 32-word access adds
/// 1; a 32-word stride adds 32.
struct MemoryStats {
    std::uint64_t global_loads = 0;         ///< warp-level ld.global instructions
    std::uint64_t global_stores = 0;        ///< warp-level st.global instructions
    std::uint64_t global_lane_accesses = 0; ///< executing lanes summed over those
    std::uint64_t global_segments = 0;      ///< distinct 128-byte segments, summed

    [[nodiscard]] constexpr bool operator==(const MemoryStats&) const noexcept = default;
};

} // namespace warpsim::core
