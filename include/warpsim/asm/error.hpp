#pragma once

#include <cstdint>
#include <string>

namespace warpsim::assembler {

/// A diagnostic with a one-based source position, produced by the lexer and
/// the assembler.
struct AssemblyError {
    std::uint32_t line = 0;
    std::uint32_t column = 0;
    std::string message;
};

} // namespace warpsim::assembler
