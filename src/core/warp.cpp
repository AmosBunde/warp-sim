#include "warpsim/core/warp.hpp"

#include "warpsim/core/alu.hpp"
#include "warpsim/core/fault.hpp"
#include "warpsim/core/memory_port.hpp"
#include "warpsim/core/types.hpp"
#include "warpsim/core/warp_state.hpp"
#include "warpsim/isa/instruction.hpp"
#include "warpsim/isa/opcode.hpp"
#include "warpsim/result.hpp"

#include <array>
#include <cassert>
#include <span>
#include <string>
#include <utility>

namespace warpsim::core {

using isa::Instruction;
using isa::Opcode;

std::string Fault::describe() const {
    return message + " (block " + std::to_string(block) + ", warp " + std::to_string(warp) +
           ", lane " + std::to_string(lane) + ", pc " + std::to_string(pc) + ", address 0x" +
           [this] {
               std::string hex;
               for (int shift = 28; shift >= 0; shift -= 4) {
                   hex += "0123456789abcdef"[(address >> static_cast<unsigned>(shift)) & 0xFU];
               }
               return hex;
           }() +
           ")";
}

Warp::Warp(unsigned warp_id, LaneMask live, LaneContext context) : context_(context) {
    state_.warp_id = warp_id;
    state_.live = live;
    state_.active = live;
}

Fault Warp::make_fault(std::string message, unsigned lane, std::uint32_t address) const {
    return Fault{.message = std::move(message),
                 .block = (context_.ctaid.y * context_.nctaid.x) + context_.ctaid.x,
                 .warp = state_.warp_id,
                 .lane = lane,
                 .pc = state_.pc,
                 .address = address};
}

void Warp::switch_to(const DivergenceEntry& entry, LaneMask arrived) {
    if (entry.is_join()) {
        // Rule 3, second arrival: union the lanes that arrived first.
        state_.active = arrived | entry.mask;
        state_.pc = entry.reconvergence_pc;
        return;
    }
    // Rule 3, first arrival (or rule 4 with an empty arrival): remember who
    // arrived, then run the deferred path.
    if (arrived != 0) {
        state_.stack.push_back(DivergenceEntry{.reconvergence_pc = entry.reconvergence_pc,
                                               .resume_pc = entry.reconvergence_pc,
                                               .mask = arrived});
    }
    state_.active = entry.mask;
    state_.pc = entry.resume_pc;
}

bool Warp::resolve_stack() {
    while (true) {
        if (state_.active == 0) {
            // Rule 4: every active lane retired; resume the deferred path.
            if (state_.stack.empty()) {
                return false;
            }
            const DivergenceEntry entry = state_.stack.back();
            state_.stack.pop_back();
            switch_to(entry, 0);
            continue;
        }
        if (!state_.stack.empty() && state_.stack.back().reconvergence_pc == state_.pc) {
            // Rule 3: the current path reached its reconvergence point.
            const DivergenceEntry entry = state_.stack.back();
            state_.stack.pop_back();
            switch_to(entry, state_.active);
            continue;
        }
        return true;
    }
}

void Warp::retire(LaneMask lanes) {
    state_.live &= ~lanes;
    state_.active &= ~lanes;
    for (auto& entry : state_.stack) {
        entry.mask &= ~lanes;
    }
}

void Warp::check_invariants() const noexcept {
#ifndef NDEBUG
    LaneMask seen = state_.active;
    for (const auto& entry : state_.stack) {
        assert((seen & entry.mask) == 0 && "a lane is present in two masks");
        seen |= entry.mask;
    }
    assert((seen & ~state_.live) == 0 && "a retired lane is present in a mask");
#endif
}

Result<StepOutcome, Fault> Warp::step(std::span<const Instruction> program, MemoryPort& memory) {
    // Falling off the end is equivalent to exit (specification section 5); the
    // loop retires such paths until a path with instructions remains.
    while (true) {
        if (!resolve_stack()) {
            return StepOutcome::Finished;
        }
        if (state_.pc < program.size()) {
            break;
        }
        retire(state_.active);
        check_invariants();
    }

    const Instruction& i = program[state_.pc];

    // The guard is applied here and nowhere else.
    LaneMask exec = state_.active;
    if (i.guard.present) {
        LaneMask guard_mask = 0;
        for (unsigned lane = 0; lane < warp_size; ++lane) {
            if (has_lane(state_.active, lane) &&
                state_.registers.read_pred(lane, i.guard.pred) != i.guard.negate) {
                guard_mask |= lane_bit(lane);
            }
        }
        exec = guard_mask;
    }
    last_exec_ = exec;

    switch (i.opcode) {
    case Opcode::Bra: {
        const std::uint16_t target = i.branch_target();
        if (!i.guard.present) {
            state_.pc = target;
            break;
        }
        const LaneMask taken = exec;
        const LaneMask fall_through = state_.active & ~exec;
        if (taken == 0) {
            ++state_.pc;
        } else if (fall_through == 0) {
            state_.pc = target;
        } else {
            // Rule 2: diverge. Taken path first, fall-through path deferred.
            ++divergent_;
            state_.stack.push_back(
                DivergenceEntry{.reconvergence_pc = i.reconvergence_pc(),
                                .resume_pc = static_cast<std::uint16_t>(state_.pc + 1),
                                .mask = fall_through});
            state_.active = taken;
            state_.pc = target;
        }
        break;
    }
    case Opcode::Exit:
        retire(exec);
        ++state_.pc;
        break;
    case Opcode::BarSync:
        if (state_.active != state_.live) {
            return fail(make_fault("bar.sync executed in divergent code", 0, 0));
        }
        state_.parked = true;
        ++state_.pc;
        check_invariants();
        return StepOutcome::Barrier;
    case Opcode::LdGlobal:
    case Opcode::StGlobal:
    case Opcode::LdShared:
    case Opcode::StShared:
    case Opcode::LdParam:
        if (auto r = memory_access(i, exec, memory); !r.has_value()) {
            return fail(r.error());
        }
        ++state_.pc;
        break;
    default:
        execute_alu(i, exec, state_.registers, context_);
        ++state_.pc;
        break;
    }
    check_invariants();
    return StepOutcome::Issued;
}

Result<void, Fault> Warp::memory_access(const Instruction& i, LaneMask exec, MemoryPort& memory) {
    const Space space = [&] {
        switch (i.opcode) {
        case Opcode::LdShared:
        case Opcode::StShared:
            return Space::Shared;
        case Opcode::LdParam:
            return Space::Param;
        default:
            return Space::Global;
        }
    }();
    const bool is_store = i.opcode == Opcode::StGlobal || i.opcode == Opcode::StShared;

    std::array<std::uint32_t, warp_size> addresses{};
    for (unsigned lane = 0; lane < warp_size; ++lane) {
        if (has_lane(exec, lane)) {
            addresses[lane] =
                space == Space::Param ? i.imm : state_.registers.read(lane, i.src0) + i.imm;
        }
    }
    if (exec != 0) {
        memory.on_warp_access(space, is_store, addresses, exec);
    }
    // Lane order is part of the specification (section 5).
    for (unsigned lane = 0; lane < warp_size; ++lane) {
        if (!has_lane(exec, lane)) {
            continue;
        }
        if (is_store) {
            const auto r =
                memory.store(space, addresses[lane], state_.registers.read(lane, i.src1), lane);
            if (!r.has_value()) {
                return fail(make_fault(r.error().message, lane, addresses[lane]));
            }
        } else {
            const auto r = memory.load(space, addresses[lane], lane);
            if (!r.has_value()) {
                return fail(make_fault(r.error().message, lane, addresses[lane]));
            }
            state_.registers.write(lane, i.dst, *r);
        }
    }
    return {};
}

} // namespace warpsim::core
