#pragma once

#include "warpsim/isa/instruction.hpp"
#include "warpsim/result.hpp"

#include <cstdint>
#include <span>
#include <string>

namespace warpsim::assembler {

struct ReconvergenceError {
    std::size_t pc = 0;
    std::string message;
};

/// Writes the reconvergence PC of every guarded `bra` (docs/wisa-spec.md
/// section 8): the first instruction of the immediate post-dominator of the
/// branch's basic block, or no_reconvergence when that is the virtual exit
/// node. Unguarded branches are set to no_reconvergence. Every other
/// instruction is left untouched.
[[nodiscard]] Result<void, ReconvergenceError>
annotate_reconvergence(std::span<isa::Instruction> program);

} // namespace warpsim::assembler
