#pragma once

#include "warpsim/asm/error.hpp"
#include "warpsim/result.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace warpsim::assembler {

enum class TokenKind : std::uint8_t {
    Identifier, ///< mnemonics, labels, registers, parameter names (may contain dots)
    Directive,  ///< `.entry`, `.param`, `.shared`
    Guard,      ///< `@p3` or `@!p3`; text holds the predicate name, negate flag separate
    Special,    ///< `%tid.x` and friends, text includes the percent sign
    Integer,    ///< unsigned decimal or hexadecimal digits; sign is a separate token
    Float,      ///< decimal with a point or an exponent
    Comma,
    LBracket,
    RBracket,
    Plus,
    Minus,
    Colon,
    Newline,
    End,
};

struct Token {
    TokenKind kind = TokenKind::End;
    std::string text;
    bool negate = false; ///< for Guard only
    std::uint32_t line = 0;
    std::uint32_t column = 0;
};

/// Splits source text into tokens. Comments (`//` to end of line) are dropped,
/// every line ends with a Newline token, and the stream ends with End.
[[nodiscard]] Result<std::vector<Token>, AssemblyError> lex(std::string_view source);

} // namespace warpsim::assembler
