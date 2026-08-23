#pragma once

#include "warpsim/core/lane_context.hpp"
#include "warpsim/core/register_file.hpp"
#include "warpsim/core/types.hpp"
#include "warpsim/isa/instruction.hpp"

namespace warpsim::core {

/// True for opcodes the ALU executes: everything except control flow,
/// barriers, and memory.
[[nodiscard]] bool is_alu_opcode(isa::Opcode opcode) noexcept;

/// Executes one ALU instruction on every lane in `exec`. The caller has
/// already applied the active mask and the guard. Semantics follow
/// docs/wisa-spec.md section 4 exactly; no input can fault.
void execute_alu(const isa::Instruction& i, LaneMask exec, RegisterFile& rf,
                 const LaneContext& ctx) noexcept;

} // namespace warpsim::core
