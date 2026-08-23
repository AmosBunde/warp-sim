#pragma once

#include "warpsim/core/types.hpp"
#include "warpsim/result.hpp"

#include <cstdint>
#include <span>
#include <string>

namespace warpsim::core {

enum class Space : std::uint8_t { Global, Shared, Param };

/// A failed lane access; the warp fills in block, warp, and PC.
struct AccessFault {
    std::string message;
    unsigned lane = 0;
    std::uint32_t address = 0;
};

/// The services a warp needs from its block's memory system. Implemented by
/// the device in #29; analyzers (M3) observe the warp-level hook.
class MemoryPort {
public:
    MemoryPort() = default;
    MemoryPort(const MemoryPort&) = default;
    MemoryPort(MemoryPort&&) = default;
    MemoryPort& operator=(const MemoryPort&) = default;
    MemoryPort& operator=(MemoryPort&&) = default;
    virtual ~MemoryPort() = default;

    [[nodiscard]] virtual Result<std::uint32_t, AccessFault>
    load(Space space, std::uint32_t address, unsigned lane) = 0;
    [[nodiscard]] virtual Result<void, AccessFault> store(Space space, std::uint32_t address,
                                                          std::uint32_t value, unsigned lane) = 0;

    /// Called once per warp memory instruction with the address of every
    /// executing lane (entries for inactive lanes are unspecified), before the
    /// lane accesses are performed. Default does nothing.
    virtual void on_warp_access(Space /*space*/, bool /*is_store*/,
                                std::span<const std::uint32_t, warp_size> /*addresses*/,
                                LaneMask /*exec*/) {}
};

} // namespace warpsim::core
