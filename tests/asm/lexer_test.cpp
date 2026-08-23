#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "asm/lexer.hpp"

namespace {

using warpsim::assembler::lex;
using warpsim::assembler::Token;
using warpsim::assembler::TokenKind;

std::vector<Token> must_lex(std::string_view source) {
    auto result = lex(source);
    if (!result.has_value()) {
        ADD_FAILURE() << result.error().line << ":" << result.error().column << ": "
                      << result.error().message;
        return {};
    }
    return result.value();
}

std::string must_fail(std::string_view source) {
    auto result = lex(source);
    if (result.has_value()) {
        ADD_FAILURE() << "expected an error for: " << source;
        return {};
    }
    return result.error().message;
}

TEST(Lexer, EveryTokenKind) {
    const auto tokens =
        must_lex(".entry k @p1 @!p2 %tid.x setp.lt.s32 r1 , [ ] + - : 12 0x1F 1.5 2e3\n");
    const std::vector<TokenKind> kinds = {
        TokenKind::Directive, TokenKind::Identifier, TokenKind::Guard,      TokenKind::Guard,
        TokenKind::Special,   TokenKind::Identifier, TokenKind::Identifier, TokenKind::Comma,
        TokenKind::LBracket,  TokenKind::RBracket,   TokenKind::Plus,       TokenKind::Minus,
        TokenKind::Colon,     TokenKind::Integer,    TokenKind::Integer,    TokenKind::Float,
        TokenKind::Float,     TokenKind::Newline,    TokenKind::End};
    ASSERT_EQ(tokens.size(), kinds.size());
    for (std::size_t i = 0; i < kinds.size(); ++i) {
        EXPECT_EQ(tokens[i].kind, kinds[i]) << "token " << i;
    }
    EXPECT_EQ(tokens[0].text, ".entry");
    EXPECT_EQ(tokens[2].text, "p1");
    EXPECT_FALSE(tokens[2].negate);
    EXPECT_EQ(tokens[3].text, "p2");
    EXPECT_TRUE(tokens[3].negate);
    EXPECT_EQ(tokens[4].text, "%tid.x");
    EXPECT_EQ(tokens[5].text, "setp.lt.s32");
    EXPECT_EQ(tokens[13].text, "12");
    EXPECT_EQ(tokens[14].text, "0x1F");
    EXPECT_EQ(tokens[15].text, "1.5");
    EXPECT_EQ(tokens[16].text, "2e3");
}

TEST(Lexer, PositionsAreOneBased) {
    const auto tokens = must_lex("ab cd\n  ef");
    EXPECT_EQ(tokens[0].line, 1U);
    EXPECT_EQ(tokens[0].column, 1U);
    EXPECT_EQ(tokens[1].column, 4U);
    EXPECT_EQ(tokens[3].line, 2U);
    EXPECT_EQ(tokens[3].column, 3U);
}

TEST(Lexer, CommentsAndBlankLinesAndCarriageReturns) {
    const auto tokens = must_lex("// only a comment\r\n\r\nexit // trailing\n");
    ASSERT_EQ(tokens.size(), 5U);
    EXPECT_EQ(tokens[0].kind, TokenKind::Newline);
    EXPECT_EQ(tokens[1].kind, TokenKind::Newline);
    EXPECT_EQ(tokens[2].text, "exit");
    EXPECT_EQ(tokens[2].line, 3U);
    EXPECT_EQ(tokens[3].kind, TokenKind::Newline);
    EXPECT_EQ(tokens[4].kind, TokenKind::End);
}

TEST(Lexer, MissingFinalNewlineIsSupplied) {
    const auto tokens = must_lex("exit");
    ASSERT_EQ(tokens.size(), 3U);
    EXPECT_EQ(tokens[1].kind, TokenKind::Newline);
}

TEST(Lexer, FloatForms) {
    const auto tokens = must_lex("1.0 1e5 1.25E-3\n");
    EXPECT_EQ(tokens[0].kind, TokenKind::Float);
    EXPECT_EQ(tokens[1].kind, TokenKind::Float);
    EXPECT_EQ(tokens[2].kind, TokenKind::Float);
    EXPECT_EQ(tokens[2].text, "1.25e-3");
    // "7." has no fraction digits and is rejected rather than read as 7 followed by '.'.
    EXPECT_EQ(must_fail("7.\n"), "unexpected character in number");
}

TEST(Lexer, Errors) {
    EXPECT_EQ(must_fail(".\n"), "expected a directive name after '.'");
    EXPECT_EQ(must_fail("@\n"), "expected a predicate register after '@'");
    EXPECT_EQ(must_fail("%\n"), "expected a special register name after '%'");
    EXPECT_EQ(must_fail("0x\n"), "expected hexadecimal digits after 0x");
    EXPECT_EQ(must_fail("1e\n"), "expected digits in exponent");
    EXPECT_EQ(must_fail("12ab\n"), "unexpected character in number");
    EXPECT_EQ(must_fail("$\n"), "unexpected character '$'");
    const auto error = lex("exit\n  #").error();
    EXPECT_EQ(error.line, 2U);
    EXPECT_EQ(error.column, 3U);
}

} // namespace
