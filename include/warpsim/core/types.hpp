#pragma once

#include <bit>
#include <cstdint>

namespace warpsim::core {

inline constexpr unsigned warp_size = 32;

/// One bit per lane of a warp; bit i is lane i.
using LaneMask = std::uint32_t;

inline constexpr LaneMask full_mask = 0xFFFFFFFFU;

[[nodiscard]] constexpr LaneMask lane_bit(unsigned lane) noexcept {
    return LaneMask{1} << lane;
}

[[nodiscard]] constexpr bool has_lane(LaneMask mask, unsigned lane) noexcept {
    return (mask & lane_bit(lane)) != 0;
}

[[nodiscard]] constexpr unsigned lane_count(LaneMask mask) noexcept {
    return static_cast<unsigned>(std::popcount(mask));
}

/// Two-dimensional launch geometry (specification section 1).
struct Dim2 {
    std::uint32_t x = 1;
    std::uint32_t y = 1;

    [[nodiscard]] constexpr std::uint32_t count() const noexcept { return x * y; }
    [[nodiscard]] constexpr bool operator==(const Dim2&) const noexcept = default;
};

} // namespace warpsim::core
