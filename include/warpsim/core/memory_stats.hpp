#pragma once

#include <cstdint>

namespace warpsim::core {

/// Byte size of one global memory segment for the coalescing analyzer.
inline constexpr std::uint32_t segment_bytes = 128;
/// Shared memory bank geometry: 32 banks, 4 bytes wide.
inline constexpr std::uint32_t bank_count = 32;
inline constexpr std::uint32_t bank_width_bytes = 4;

/// Memory observables of one launch. Every number is a count of events
/// defined below, never a time.
///
/// Coalescing rule: for one warp-level global instruction, the executing
/// lanes' addresses are mapped to `address / 128`; `global_segments` adds
/// the number of distinct values. A perfectly coalesced 32-word access adds
/// 1; a 32-word stride adds 32.
///
/// Bank rule: for one warp-level shared instruction, each executing lane's
/// address maps to bank `(address / 4) % 32`; the access degree is the
/// maximum over banks of the number of distinct addresses in that bank
/// (identical addresses broadcast and count once). `shared_wavefronts` adds
/// the degree (the number of serialized passes) and
/// `shared_conflicted_accesses` adds 1 when the degree exceeds 1.
struct MemoryStats {
    std::uint64_t global_loads = 0;         ///< warp-level ld.global instructions
    std::uint64_t global_stores = 0;        ///< warp-level st.global instructions
    std::uint64_t global_lane_accesses = 0; ///< executing lanes summed over those
    std::uint64_t global_segments = 0;      ///< distinct 128-byte segments, summed

    std::uint64_t shared_accesses = 0;            ///< warp-level ld/st.shared instructions
    std::uint64_t shared_lane_accesses = 0;       ///< executing lanes summed over those
    std::uint64_t shared_wavefronts = 0;          ///< sum of conflict degrees
    std::uint64_t shared_conflicted_accesses = 0; ///< accesses with degree above 1

    [[nodiscard]] constexpr bool operator==(const MemoryStats&) const noexcept = default;
};

} // namespace warpsim::core
