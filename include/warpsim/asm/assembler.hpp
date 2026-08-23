#pragma once

#include "warpsim/asm/error.hpp"
#include "warpsim/asm/program.hpp"
#include "warpsim/result.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace warpsim::assembler {

/// Assembles WISA source text (docs/wisa-spec.md section 7) into a Program.
/// Reconvergence points of guarded branches are annotated per section 8.
[[nodiscard]] Result<Program, AssemblyError> assemble(std::string_view source);

} // namespace warpsim::assembler
