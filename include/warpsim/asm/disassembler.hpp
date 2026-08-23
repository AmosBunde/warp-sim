#pragma once

#include "warpsim/asm/program.hpp"
#include "warpsim/isa/instruction.hpp"
#include "warpsim/result.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace warpsim::assembler {

struct DisassemblyError {
    std::size_t pc = 0;
    std::string message;
};

/// Prints one instruction in canonical form (no label prefix, no newline).
/// `labels` supplies branch target names; a target without a label prints as
/// `L<pc>`. `params` supplies parameter names for `ld.param`.
[[nodiscard]] Result<std::string, DisassemblyError>
format_instruction(const isa::Instruction& instruction,
                   const std::map<std::uint16_t, std::string>& labels,
                   const std::vector<std::string>& params);

/// Prints a whole program in canonical form. The output reassembles to the
/// same words: assemble(disassemble(p)).words == p.words.
[[nodiscard]] Result<std::string, DisassemblyError> disassemble(const Program& program);

} // namespace warpsim::assembler
