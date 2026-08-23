#include "warpsim/asm/assembler.hpp"

#include "warpsim/asm/program.hpp"
#include "warpsim/isa/instruction.hpp"
#include "warpsim/isa/opcode.hpp"
#include "warpsim/result.hpp"

#include <bit>
#include <charconv>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "lexer.hpp"

namespace warpsim::assembler {

namespace {

using isa::Instruction;
using isa::Opcode;
using isa::Shape;

/// A branch whose target label is resolved after the whole program is parsed.
struct PendingBranch {
    std::size_t pc;
    std::string label;
    std::uint32_t line;
    std::uint32_t column;
};

/// Opcodes whose immediate-capable operand is an f32 or b32 value and may
/// therefore be written as a float literal (specification section 7).
bool accepts_float_immediate(Opcode opcode) noexcept {
    switch (opcode) {
    case Opcode::FAdd:
    case Opcode::FSub:
    case Opcode::FMul:
    case Opcode::FMin:
    case Opcode::FMax:
    case Opcode::FNeg:
    case Opcode::CvtS32F32:
    case Opcode::SetpEqF32:
    case Opcode::SetpNeF32:
    case Opcode::SetpLtF32:
    case Opcode::SetpLeF32:
    case Opcode::SetpGtF32:
    case Opcode::SetpGeF32:
    case Opcode::Mov:
        return true;
    default:
        return false;
    }
}

/// std::from_chars takes a [first, last) pointer pair; this is the one place the
/// pointer arithmetic lives, and it is bounded by the view it is given.
template <typename T>
bool parse_number(std::string_view text, T& value, int base = 10) {
    const char* first = text.data();
    const char* last =
        text.data() + text.size(); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    const auto [ptr, ec] = [&] {
        if constexpr (std::is_floating_point_v<T>) {
            return std::from_chars(first, last, value);
        } else {
            return std::from_chars(first, last, value, base);
        }
    }();
    return ec == std::errc{} && ptr == last;
}

class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    Result<Program, AssemblyError> run() {
        while (!at(TokenKind::End)) {
            if (at(TokenKind::Newline)) {
                next();
                continue;
            }
            auto status = at(TokenKind::Directive) ? directive() : statement();
            if (!status.has_value()) {
                return fail(status.error());
            }
        }
        if (program_.entry.empty()) {
            return fail(AssemblyError{.line = 1, .column = 1, .message = "missing .entry"});
        }
        if (program_.words.size() > isa::no_reconvergence) {
            return fail(AssemblyError{
                .line = 1, .column = 1, .message = "program exceeds 65535 instructions"});
        }
        if (auto resolved = resolve_branches(); !resolved.has_value()) {
            return fail(resolved.error());
        }
        return program_;
    }

private:
    // Token access.
    [[nodiscard]] const Token& cur() const noexcept { return tokens_[index_]; }
    [[nodiscard]] bool at(TokenKind kind) const noexcept { return cur().kind == kind; }
    void next() noexcept {
        if (index_ + 1 < tokens_.size()) {
            ++index_;
        }
    }

    [[nodiscard]] AssemblyError error(std::string message) const {
        return AssemblyError{
            .line = cur().line, .column = cur().column, .message = std::move(message)};
    }

    Result<void, AssemblyError> expect(TokenKind kind, std::string_view what) {
        if (!at(kind)) {
            return fail(error("expected " + std::string(what)));
        }
        next();
        return {};
    }

    Result<void, AssemblyError> end_of_line() {
        if (!at(TokenKind::Newline) && !at(TokenKind::End)) {
            return fail(error("unexpected token '" + cur().text + "' at end of line"));
        }
        return {};
    }

    // Directives.
    Result<void, AssemblyError> directive() {
        const std::string name = cur().text;
        next();
        if (name == ".entry") {
            if (!program_.entry.empty()) {
                return fail(error("duplicate .entry"));
            }
            if (!program_.words.empty()) {
                return fail(error(".entry must precede the first instruction"));
            }
            if (!at(TokenKind::Identifier)) {
                return fail(error("expected a kernel name after .entry"));
            }
            program_.entry = cur().text;
            next();
            return end_of_line();
        }
        if (name == ".param") {
            if (!at(TokenKind::Identifier)) {
                return fail(error("expected a parameter name after .param"));
            }
            for (const auto& existing : program_.params) {
                if (existing == cur().text) {
                    return fail(error("duplicate parameter '" + cur().text + "'"));
                }
            }
            program_.params.push_back(cur().text);
            next();
            return end_of_line();
        }
        if (name == ".shared") {
            auto value = integer_literal();
            if (!value.has_value()) {
                return fail(value.error());
            }
            program_.shared_bytes = value.value();
            return end_of_line();
        }
        return fail(error("unknown directive '" + name + "'"));
    }

    // Statements: [label:] [guard] mnemonic operands.
    Result<void, AssemblyError> statement() {
        if (at(TokenKind::Identifier) && tokens_[index_ + 1].kind == TokenKind::Colon) {
            const auto pc = static_cast<std::uint16_t>(program_.words.size());
            if (labels_.contains(cur().text)) {
                return fail(error("duplicate label '" + cur().text + "'"));
            }
            labels_.emplace(cur().text, pc);
            program_.labels.emplace(pc, cur().text);
            next();
            next();
            if (at(TokenKind::Newline)) {
                return {};
            }
        }
        Instruction instruction;
        if (at(TokenKind::Guard)) {
            auto pred = predicate_name(cur().text);
            if (!pred.has_value()) {
                return fail(error("invalid guard predicate '" + cur().text + "'"));
            }
            instruction.guard = {.present = true, .negate = cur().negate, .pred = *pred};
            next();
        }
        if (!at(TokenKind::Identifier)) {
            return fail(error("expected an instruction mnemonic"));
        }
        const auto info = isa::opcode_info(std::string_view{cur().text});
        if (!info.has_value()) {
            return fail(error("unknown mnemonic '" + cur().text + "'"));
        }
        instruction.opcode = info->opcode;
        next();
        if (auto operands = parse_operands(*info, instruction); !operands.has_value()) {
            return fail(operands.error());
        }
        if (auto eol = end_of_line(); !eol.has_value()) {
            return fail(eol.error());
        }
        const auto word = isa::encode(instruction);
        if (!word.has_value()) {
            return fail(error(std::string(isa::to_string(word.error()))));
        }
        program_.words.push_back(*word);
        return {};
    }

    Result<void, AssemblyError> parse_operands(const isa::OpcodeInfo& info, Instruction& out) {
        switch (info.shape) {
        case Shape::Rrr:
            return sequence({&Parser::dst_reg, &Parser::comma, &Parser::src0_reg, &Parser::comma,
                             &Parser::src1_reg_or_imm},
                            out);
        case Shape::Rr:
            return sequence({&Parser::dst_reg, &Parser::comma, &Parser::src0_reg_or_imm}, out);
        case Shape::Acc:
            return sequence({&Parser::dst_reg, &Parser::comma, &Parser::src0_reg, &Parser::comma,
                             &Parser::src1_reg},
                            out);
        case Shape::Prr:
            return sequence({&Parser::dst_pred, &Parser::comma, &Parser::src0_reg, &Parser::comma,
                             &Parser::src1_reg_or_imm},
                            out);
        case Shape::Sreg:
            return sequence({&Parser::dst_reg, &Parser::comma, &Parser::special}, out);
        case Shape::Bra:
            return branch_target(out);
        case Shape::None:
            return {};
        case Shape::Ld:
            return sequence({&Parser::dst_reg, &Parser::comma, &Parser::address}, out);
        case Shape::St:
            return sequence({&Parser::address, &Parser::comma, &Parser::src1_reg}, out);
        case Shape::Ldp:
            return sequence({&Parser::dst_reg, &Parser::comma, &Parser::param_ref}, out);
        }
        return fail(error("unsupported operand shape"));
    }

    using Step = Result<void, AssemblyError> (Parser::*)(Instruction&);

    Result<void, AssemblyError> sequence(std::initializer_list<Step> steps, Instruction& out) {
        for (const Step step : steps) {
            if (auto r = (this->*step)(out); !r.has_value()) {
                return fail(r.error());
            }
        }
        return {};
    }

    Result<void, AssemblyError> comma(Instruction& /*out*/) {
        return expect(TokenKind::Comma, "','");
    }

    Result<std::uint8_t, AssemblyError> general_register() {
        if (at(TokenKind::Identifier)) {
            if (auto index = register_index(cur().text, 'r', isa::general_register_count)) {
                next();
                return *index;
            }
        }
        return fail(error("expected a general register r0..r63"));
    }

    Result<void, AssemblyError> dst_reg(Instruction& out) {
        auto r = general_register();
        if (!r.has_value()) {
            return fail(r.error());
        }
        out.dst = *r;
        return {};
    }

    Result<void, AssemblyError> src0_reg(Instruction& out) {
        auto r = general_register();
        if (!r.has_value()) {
            return fail(r.error());
        }
        out.src0 = *r;
        return {};
    }

    Result<void, AssemblyError> src1_reg(Instruction& out) {
        auto r = general_register();
        if (!r.has_value()) {
            return fail(r.error());
        }
        out.src1 = *r;
        return {};
    }

    Result<void, AssemblyError> src1_reg_or_imm(Instruction& out) {
        if (at(TokenKind::Identifier)) {
            return src1_reg(out);
        }
        return immediate(out);
    }

    Result<void, AssemblyError> src0_reg_or_imm(Instruction& out) {
        if (at(TokenKind::Identifier)) {
            return src0_reg(out);
        }
        return immediate(out);
    }

    Result<void, AssemblyError> dst_pred(Instruction& out) {
        if (at(TokenKind::Identifier)) {
            if (auto index = predicate_name(cur().text)) {
                out.dst = *index;
                next();
                return {};
            }
        }
        return fail(error("expected a predicate register p0..p7"));
    }

    Result<void, AssemblyError> special(Instruction& out) {
        if (at(TokenKind::Special)) {
            if (auto info = isa::special_register_info(std::string_view{cur().text})) {
                out.src0 = static_cast<std::uint8_t>(info->reg);
                next();
                return {};
            }
            return fail(error("unknown special register '" + cur().text + "'"));
        }
        return fail(error("expected a special register"));
    }

    Result<void, AssemblyError> branch_target(Instruction& out) {
        if (!at(TokenKind::Identifier)) {
            return fail(error("expected a label"));
        }
        pending_.push_back(PendingBranch{.pc = program_.words.size(),
                                         .label = cur().text,
                                         .line = cur().line,
                                         .column = cur().column});
        // Target is patched in resolve_branches; the reconvergence half is
        // filled by the analysis pass. Both halves start as "none".
        out.imm = Instruction::make_branch_imm(0, isa::no_reconvergence);
        next();
        return {};
    }

    Result<void, AssemblyError> address(Instruction& out) {
        if (auto r = expect(TokenKind::LBracket, "'['"); !r.has_value()) {
            return fail(r.error());
        }
        auto base = general_register();
        if (!base.has_value()) {
            return fail(base.error());
        }
        out.src0 = *base;
        out.imm_flag = true;
        out.imm = 0;
        if (at(TokenKind::Plus) || at(TokenKind::Minus)) {
            const bool negative = at(TokenKind::Minus);
            next();
            auto offset = integer_literal();
            if (!offset.has_value()) {
                return fail(offset.error());
            }
            out.imm = negative ? static_cast<std::uint32_t>(0U - *offset) : *offset;
        }
        return expect(TokenKind::RBracket, "']'");
    }

    Result<void, AssemblyError> param_ref(Instruction& out) {
        if (auto r = expect(TokenKind::LBracket, "'['"); !r.has_value()) {
            return fail(r.error());
        }
        if (!at(TokenKind::Identifier)) {
            return fail(error("expected a parameter name"));
        }
        for (std::size_t i = 0; i < program_.params.size(); ++i) {
            if (program_.params[i] == cur().text) {
                out.imm_flag = true;
                out.imm = static_cast<std::uint32_t>(i);
                next();
                return expect(TokenKind::RBracket, "']'");
            }
        }
        return fail(error("undeclared parameter '" + cur().text + "'"));
    }

    /// Parses an immediate operand: [-]integer or [-]float, with the float form
    /// accepted only where the specification permits it.
    Result<void, AssemblyError> immediate(Instruction& out) {
        bool negative = false;
        if (at(TokenKind::Minus)) {
            negative = true;
            next();
        }
        if (at(TokenKind::Float)) {
            if (!accepts_float_immediate(out.opcode)) {
                return fail(error("float immediate is not valid for this instruction"));
            }
            float value = 0.0F;
            if (!parse_number(cur().text, value)) {
                return fail(error("invalid float literal '" + cur().text + "'"));
            }
            out.imm_flag = true;
            out.imm = std::bit_cast<std::uint32_t>(negative ? -value : value);
            next();
            return {};
        }
        if (!at(TokenKind::Integer)) {
            return fail(error("expected a register or an immediate"));
        }
        auto magnitude = integer_literal();
        if (!magnitude.has_value()) {
            return fail(magnitude.error());
        }
        if (negative) {
            constexpr std::uint32_t int32_min_magnitude = 0x80000000U;
            if (*magnitude > int32_min_magnitude) {
                return fail(error("immediate does not fit in 32 bits"));
            }
            out.imm = 0U - *magnitude;
        } else {
            out.imm = *magnitude;
        }
        out.imm_flag = true;
        return {};
    }

    /// An unsigned integer literal that fits in 32 bits.
    Result<std::uint32_t, AssemblyError> integer_literal() {
        if (!at(TokenKind::Integer)) {
            return fail(error("expected an integer"));
        }
        const std::string& text = cur().text;
        std::uint64_t value = 0;
        const bool hex = text.starts_with("0x");
        const std::string_view digits = hex ? std::string_view{text}.substr(2) : text;
        if (!parse_number(digits, value, hex ? 16 : 10) ||
            value > std::numeric_limits<std::uint32_t>::max()) {
            return fail(error("integer does not fit in 32 bits: '" + text + "'"));
        }
        next();
        return static_cast<std::uint32_t>(value);
    }

    static std::optional<std::uint8_t> register_index(std::string_view text, char prefix,
                                                      std::uint8_t count) {
        if (text.size() < 2 || text[0] != prefix) {
            return std::nullopt;
        }
        unsigned value = 0;
        if (!parse_number(text.substr(1), value) || value >= count) {
            return std::nullopt;
        }
        // Reject leading zeros such as r01 so that every register has one spelling.
        if (text.size() > 2 && text[1] == '0') {
            return std::nullopt;
        }
        return static_cast<std::uint8_t>(value);
    }

    static std::optional<std::uint8_t> predicate_name(std::string_view text) {
        return register_index(text, 'p', isa::predicate_register_count);
    }

    Result<void, AssemblyError> resolve_branches() {
        for (const auto& pending : pending_) {
            const auto found = labels_.find(pending.label);
            if (found == labels_.end()) {
                return fail(AssemblyError{.line = pending.line,
                                          .column = pending.column,
                                          .message = "undefined label '" + pending.label + "'"});
            }
            auto decoded = isa::decode(program_.words[pending.pc]);
            Instruction instruction = decoded.value();
            instruction.imm = Instruction::make_branch_imm(found->second, isa::no_reconvergence);
            program_.words[pending.pc] = isa::encode(instruction).value();
        }
        return {};
    }

    std::vector<Token> tokens_;
    std::size_t index_ = 0;
    Program program_;
    std::map<std::string, std::uint16_t> labels_;
    std::vector<PendingBranch> pending_;
};

} // namespace

Result<Program, AssemblyError> assemble(std::string_view source) {
    auto tokens = lex(source);
    if (!tokens.has_value()) {
        return fail(tokens.error());
    }
    return Parser(std::move(tokens.value())).run();
}

} // namespace warpsim::assembler
