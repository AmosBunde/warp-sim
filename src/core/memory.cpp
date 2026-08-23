#include "warpsim/core/memory.hpp"

#include "warpsim/core/memory_port.hpp"
#include "warpsim/result.hpp"

#include <cstdint>
#include <cstring>

namespace warpsim::core {

Result<void, AccessFault> ByteMemory::check(std::uint32_t address, unsigned lane) const {
    if (address % 4 != 0) {
        return fail(
            AccessFault{.message = "misaligned 32-bit access", .lane = lane, .address = address});
    }
    if (static_cast<std::size_t>(address) + 4 > bytes_.size()) {
        return fail(
            AccessFault{.message = "access out of bounds", .lane = lane, .address = address});
    }
    return {};
}

Result<std::uint32_t, AccessFault> ByteMemory::load32(std::uint32_t address, unsigned lane) const {
    if (const auto ok = check(address, lane); !ok.has_value()) {
        return fail(ok.error());
    }
    std::uint32_t value = 0;
    std::memcpy(&value, &bytes_[address], sizeof value);
    return value;
}

Result<void, AccessFault> ByteMemory::store32(std::uint32_t address, std::uint32_t value,
                                              unsigned lane) {
    if (const auto ok = check(address, lane); !ok.has_value()) {
        return fail(ok.error());
    }
    std::memcpy(&bytes_[address], &value, sizeof value);
    return {};
}

Result<std::uint32_t, AccessFault> BlockMemory::load(Space space, std::uint32_t address,
                                                     unsigned lane) {
    switch (space) {
    case Space::Global:
        return global_->load32(address, lane);
    case Space::Shared:
        return shared_.load32(address, lane);
    case Space::Param:
        if (address >= params_.size()) {
            return fail(AccessFault{
                .message = "parameter ordinal out of range", .lane = lane, .address = address});
        }
        return params_[address];
    }
    return fail(AccessFault{.message = "unknown memory space", .lane = lane, .address = address});
}

Result<void, AccessFault> BlockMemory::store(Space space, std::uint32_t address,
                                             std::uint32_t value, unsigned lane) {
    switch (space) {
    case Space::Global:
        return global_->store32(address, value, lane);
    case Space::Shared:
        return shared_.store32(address, value, lane);
    case Space::Param:
        return fail(
            AccessFault{.message = "store to parameter space", .lane = lane, .address = address});
    }
    return fail(AccessFault{.message = "unknown memory space", .lane = lane, .address = address});
}

} // namespace warpsim::core

namespace warpsim::core {

void BlockMemory::on_warp_access(Space space, bool is_store,
                                 std::span<const std::uint32_t, warp_size> addresses,
                                 LaneMask exec) {
    if (space == Space::Global) {
        analyzer_.on_global(addresses, exec, is_store);
    }
}

} // namespace warpsim::core
