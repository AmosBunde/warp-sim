#pragma once

#include "warpsim/asm/assembler.hpp"
#include "warpsim/core/memory_port.hpp"
#include "warpsim/core/warp.hpp"
#include "warpsim/isa/instruction.hpp"

#include <string_view>
#include <vector>

#include <gtest/gtest.h>

namespace warpsim::testing {

/// A memory port that faults on every access, for control-flow-only tests.
class NullMemory : public core::MemoryPort {
public:
    Result<std::uint32_t, core::AccessFault> load(core::Space /*space*/, std::uint32_t address,
                                                  unsigned lane) override {
        return fail(core::AccessFault{.message = "no memory", .lane = lane, .address = address});
    }
    Result<void, core::AccessFault> store(core::Space /*space*/, std::uint32_t address,
                                          std::uint32_t /*value*/, unsigned lane) override {
        return fail(core::AccessFault{.message = "no memory", .lane = lane, .address = address});
    }
};

inline std::vector<isa::Instruction> decode_program(std::string_view source) {
    const auto program = assembler::assemble(source);
    if (!program.has_value()) {
        ADD_FAILURE() << program.error().line << ": " << program.error().message;
        return {};
    }
    std::vector<isa::Instruction> out;
    for (const auto word : program->words) {
        out.push_back(isa::decode(word).value());
    }
    return out;
}

/// Runs a warp to completion (or up to `max_steps`) and returns the number of
/// issued instructions. Fails the test on a fault.
inline std::size_t run_to_end(core::Warp& warp, const std::vector<isa::Instruction>& program,
                              core::MemoryPort& memory, std::size_t max_steps = 100000) {
    std::size_t issued = 0;
    for (std::size_t n = 0; n < max_steps; ++n) {
        const auto outcome = warp.step(program, memory);
        if (!outcome.has_value()) {
            ADD_FAILURE() << outcome.error().describe();
            return issued;
        }
        if (*outcome == core::StepOutcome::Finished) {
            return issued;
        }
        ++issued;
    }
    ADD_FAILURE() << "warp did not finish within " << max_steps << " steps";
    return issued;
}

} // namespace warpsim::testing
