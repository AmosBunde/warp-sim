#pragma once

#include "warpsim/core/fault.hpp"
#include "warpsim/core/lane_context.hpp"
#include "warpsim/core/memory_port.hpp"
#include "warpsim/core/types.hpp"
#include "warpsim/core/warp_state.hpp"
#include "warpsim/isa/instruction.hpp"
#include "warpsim/result.hpp"

#include <span>

namespace warpsim::core {

/// Result of one scheduling step.
enum class StepOutcome : std::uint8_t {
    Issued,   ///< one instruction executed
    Barrier,  ///< the warp executed bar.sync and is now parked
    Finished, ///< every lane has retired; nothing was issued
};

/// One warp of 32 lanes executing a program in lockstep under an active mask,
/// with the divergence stack of docs/wisa-spec.md section 8.3.
class Warp {
public:
    Warp(unsigned warp_id, LaneMask live, LaneContext context);

    /// Executes one scheduling step: resolves the divergence stack, then
    /// issues at most one instruction. `program` is the decoded kernel.
    [[nodiscard]] Result<StepOutcome, Fault> step(std::span<const isa::Instruction> program,
                                                  MemoryPort& memory);

    [[nodiscard]] const WarpState& state() const noexcept { return state_; }
    [[nodiscard]] WarpState& state() noexcept { return state_; }
    [[nodiscard]] const LaneContext& context() const noexcept { return context_; }

    /// Lanes that executed the most recently issued instruction.
    [[nodiscard]] LaneMask last_exec() const noexcept { return last_exec_; }

    /// Divergence events so far: branches that split the active mask.
    [[nodiscard]] std::uint64_t divergent_branches() const noexcept { return divergent_; }
    /// Rule 3 pops so far: arrivals at a reconvergence point.
    [[nodiscard]] std::uint64_t reconvergence_events() const noexcept { return reconverged_; }
    /// Class of the most recently issued instruction.
    [[nodiscard]] isa::Opcode last_opcode() const noexcept { return last_opcode_; }

private:
    /// Rules 3 and 4: pops entries until the warp has a non-empty active mask
    /// that is not at its reconvergence point. Returns false when finished.
    bool resolve_stack();
    void switch_to(const DivergenceEntry& entry, LaneMask arrived);
    void retire(LaneMask lanes);
    void check_invariants() const noexcept;

    [[nodiscard]] Result<void, Fault> memory_access(const isa::Instruction& i, LaneMask exec,
                                                    MemoryPort& memory);
    [[nodiscard]] Fault make_fault(std::string message, unsigned lane, std::uint32_t address) const;

    WarpState state_;
    LaneContext context_;
    LaneMask last_exec_ = 0;
    std::uint64_t divergent_ = 0;
    std::uint64_t reconverged_ = 0;
    isa::Opcode last_opcode_ = isa::Opcode::Exit;
};

} // namespace warpsim::core
