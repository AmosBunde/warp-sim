#pragma once

#include <cstdint>
#include <string>

namespace warpsim::core {

/// A simulator fault: a kernel did something the specification defines as an
/// error (memory bounds, alignment, barrier misuse). The launch stops and the
/// fault names the block, warp, lane, PC, and address involved.
struct Fault {
    std::string message;
    std::uint32_t block = 0;
    unsigned warp = 0;
    unsigned lane = 0;
    std::uint16_t pc = 0;
    std::uint32_t address = 0;

    [[nodiscard]] std::string describe() const;
};

} // namespace warpsim::core
