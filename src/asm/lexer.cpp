#include "lexer.hpp"

#include "warpsim/asm/error.hpp"
#include "warpsim/result.hpp"

#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace warpsim::assembler {

namespace {

bool is_ident_start(char c) noexcept {
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool is_ident_char(char c) noexcept {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '.';
}

bool is_digit(char c) noexcept {
    return std::isdigit(static_cast<unsigned char>(c)) != 0;
}

bool is_hex_digit(char c) noexcept {
    return std::isxdigit(static_cast<unsigned char>(c)) != 0;
}

class Lexer {
public:
    explicit Lexer(std::string_view source) : source_(source) {}

    Result<std::vector<Token>, AssemblyError> run() {
        std::vector<Token> tokens;
        while (pos_ < source_.size()) {
            const char c = source_[pos_];
            if (c == '\n') {
                tokens.push_back(make(TokenKind::Newline, "\n"));
                advance();
                ++line_;
                column_ = 1;
                continue;
            }
            if (c == ' ' || c == '\t' || c == '\r') {
                advance();
                continue;
            }
            if (c == '/' && peek(1) == '/') {
                while (pos_ < source_.size() && source_[pos_] != '\n') {
                    advance();
                }
                continue;
            }
            auto token = next_token();
            if (!token.has_value()) {
                return fail(token.error());
            }
            tokens.push_back(std::move(token.value()));
        }
        // Every statement ends with a newline token, including the last one in a
        // source that lacks a trailing newline; a source that has one gets no extra.
        if (tokens.empty() || tokens.back().kind != TokenKind::Newline) {
            tokens.push_back(make(TokenKind::Newline, "\n"));
        }
        tokens.push_back(make(TokenKind::End, ""));
        return tokens;
    }

private:
    [[nodiscard]] char peek(std::size_t ahead) const noexcept {
        return pos_ + ahead < source_.size() ? source_[pos_ + ahead] : '\0';
    }

    void advance() noexcept {
        ++pos_;
        ++column_;
    }

    [[nodiscard]] Token make(TokenKind kind, std::string text) const {
        return Token{.kind = kind, .text = std::move(text), .line = line_, .column = column_};
    }

    [[nodiscard]] AssemblyError error(std::string message) const {
        return AssemblyError{.line = line_, .column = column_, .message = std::move(message)};
    }

    std::string take_while(bool (*pred)(char) noexcept) {
        const std::size_t start = pos_;
        while (pos_ < source_.size() && pred(source_[pos_])) {
            advance();
        }
        return std::string(source_.substr(start, pos_ - start));
    }

    Result<Token, AssemblyError> next_token() {
        const char c = source_[pos_];
        const Token at = make(TokenKind::End, "");
        auto punct = [&](TokenKind kind) {
            Token t = at;
            t.kind = kind;
            t.text = std::string(1, c);
            advance();
            return t;
        };
        switch (c) {
        case ',':
            return punct(TokenKind::Comma);
        case '[':
            return punct(TokenKind::LBracket);
        case ']':
            return punct(TokenKind::RBracket);
        case '+':
            return punct(TokenKind::Plus);
        case '-':
            return punct(TokenKind::Minus);
        case ':':
            return punct(TokenKind::Colon);
        default:
            break;
        }
        if (c == '.') {
            Token t = at;
            t.kind = TokenKind::Directive;
            advance();
            t.text = "." + take_while(is_ident_char);
            if (t.text.size() == 1) {
                return fail(error("expected a directive name after '.'"));
            }
            return t;
        }
        if (c == '@') {
            Token t = at;
            t.kind = TokenKind::Guard;
            advance();
            if (peek(0) == '!') {
                t.negate = true;
                advance();
            }
            t.text = take_while(is_ident_char);
            if (t.text.empty()) {
                return fail(error("expected a predicate register after '@'"));
            }
            return t;
        }
        if (c == '%') {
            Token t = at;
            t.kind = TokenKind::Special;
            advance();
            t.text = "%" + take_while(is_ident_char);
            if (t.text.size() == 1) {
                return fail(error("expected a special register name after '%'"));
            }
            return t;
        }
        if (is_ident_start(c)) {
            Token t = at;
            t.kind = TokenKind::Identifier;
            t.text = take_while(is_ident_char);
            return t;
        }
        if (is_digit(c)) {
            return number(at);
        }
        return fail(error("unexpected character '" + std::string(1, c) + "'"));
    }

    Result<Token, AssemblyError> number(Token t) {
        t.kind = TokenKind::Integer;
        if (source_[pos_] == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
            advance();
            advance();
            const std::string digits = take_while(is_hex_digit);
            if (digits.empty()) {
                return fail(error("expected hexadecimal digits after 0x"));
            }
            t.text = "0x" + digits;
            return t;
        }
        std::string text = take_while(is_digit);
        bool is_float = false;
        if (peek(0) == '.' && is_digit(peek(1))) {
            is_float = true;
            advance();
            text += "." + take_while(is_digit);
        }
        if (peek(0) == 'e' || peek(0) == 'E') {
            is_float = true;
            text += 'e';
            advance();
            if (peek(0) == '+' || peek(0) == '-') {
                text += source_[pos_];
                advance();
            }
            const std::string exponent = take_while(is_digit);
            if (exponent.empty()) {
                return fail(error("expected digits in exponent"));
            }
            text += exponent;
        }
        if (is_ident_char(peek(0))) {
            return fail(error("unexpected character in number"));
        }
        t.kind = is_float ? TokenKind::Float : TokenKind::Integer;
        t.text = std::move(text);
        return t;
    }

    std::string_view source_;
    std::size_t pos_ = 0;
    std::uint32_t line_ = 1;
    std::uint32_t column_ = 1;
};

} // namespace

Result<std::vector<Token>, AssemblyError> lex(std::string_view source) {
    return Lexer(source).run();
}

} // namespace warpsim::assembler
