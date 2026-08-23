#pragma once

#include <string_view>

namespace warpsim {

/// Returns the semantic version of the simulator library, for example "0.1.0".
[[nodiscard]] std::string_view version() noexcept;

} // namespace warpsim
