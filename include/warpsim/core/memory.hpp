#pragma once

#include "warpsim/core/memory_port.hpp"
#include "warpsim/result.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace warpsim::core {

/// A flat byte-addressed memory with 32-bit word access, bounds and
/// alignment checked (specification section 2.5). Used for both the global
/// space (one per device) and the shared space (one per block).
class ByteMemory {
public:
    explicit ByteMemory(std::size_t bytes) : bytes_(bytes, std::byte{0}) {}

    [[nodiscard]] Result<std::uint32_t, AccessFault> load32(std::uint32_t address,
                                                            unsigned lane) const;
    [[nodiscard]] Result<void, AccessFault> store32(std::uint32_t address, std::uint32_t value,
                                                    unsigned lane);

    [[nodiscard]] std::span<std::byte> bytes() noexcept { return bytes_; }
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept { return bytes_; }
    [[nodiscard]] std::size_t size() const noexcept { return bytes_.size(); }

private:
    [[nodiscard]] Result<void, AccessFault> check(std::uint32_t address, unsigned lane) const;

    std::vector<std::byte> bytes_;
};

/// The memory port of one block: the device's global memory, the block's own
/// shared memory, and the launch's parameter table.
class BlockMemory : public MemoryPort {
public:
    BlockMemory(ByteMemory& global, std::uint32_t shared_bytes,
                std::span<const std::uint32_t> params)
        : global_(&global), shared_(shared_bytes), params_(params) {}

    [[nodiscard]] Result<std::uint32_t, AccessFault> load(Space space, std::uint32_t address,
                                                          unsigned lane) override;
    [[nodiscard]] Result<void, AccessFault> store(Space space, std::uint32_t address,
                                                  std::uint32_t value, unsigned lane) override;

    [[nodiscard]] ByteMemory& shared() noexcept { return shared_; }

private:
    ByteMemory* global_; ///< non-owning; the device outlives every block
    ByteMemory shared_;
    std::span<const std::uint32_t> params_;
};

} // namespace warpsim::core
