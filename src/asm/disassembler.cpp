#include "warpsim/asm/disassembler.hpp"

#include "warpsim/asm/program.hpp"
#include "warpsim/isa/instruction.hpp"
#include "warpsim/isa/opcode.hpp"
#include "warpsim/result.hpp"

#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

namespace warpsim::assembler {

namespace {

using isa::Instruction;
using isa::Opcode;
using isa::Shape;

std::string reg(std::uint8_t index) {
    return "r" + std::to_string(index);
}
std::string pred(std::uint8_t index) {
    return "p" + std::to_string(index);
}

std::string signed_decimal(std::uint32_t bits) {
    return std::to_string(static_cast<std::int32_t>(bits));
}

/// std::to_chars takes a [first, last) pointer pair; this is the one place in
/// the disassembler where that arithmetic lives, bounded by the array.
template <typename T, std::size_t N>
std::string to_chars_string(std::array<char, N>& buffer, T value, int base) {
    char* first = buffer.data();
    char* last = buffer.data() + N; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    const auto [end, ec] = [&] {
        if constexpr (std::is_floating_point_v<T>) {
            return std::to_chars(first, last, value);
        } else {
            return std::to_chars(first, last, value, base);
        }
    }();
    return std::string(first, end);
}

std::string hex(std::uint32_t bits) {
    std::array<char, 10> buffer{};
    return "0x" + to_chars_string(buffer, bits, 16);
}

/// Opcodes whose immediate is printed as a float literal when finite. Mirrors
/// the assembler's accepts_float_immediate, except `mov`, whose immediate is
/// a raw bit pattern and prints as hexadecimal so that integers stay readable.
bool prints_float_immediate(Opcode opcode) noexcept {
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
        return true;
    default:
        return false;
    }
}

/// Shortest literal that reads back to exactly the same binary32 bits.
std::string immediate(const Instruction& i) {
    if (prints_float_immediate(i.opcode)) {
        const auto value = std::bit_cast<float>(i.imm);
        if (std::isfinite(value)) {
            std::array<char, 32> buffer{};
            std::string text = to_chars_string(buffer, value, 10);
            if (text.find('.') == std::string::npos && text.find('e') == std::string::npos) {
                text += ".0";
            }
            return text;
        }
        return hex(i.imm);
    }
    if (i.opcode == Opcode::Mov) {
        return hex(i.imm);
    }
    return signed_decimal(i.imm);
}

std::string address(const Instruction& i) {
    const auto offset = static_cast<std::int32_t>(i.imm);
    if (offset == 0) {
        return "[" + reg(i.src0) + "]";
    }
    if (offset < 0) {
        return "[" + reg(i.src0) + "-" + std::to_string(0U - i.imm) + "]";
    }
    return "[" + reg(i.src0) + "+" + std::to_string(i.imm) + "]";
}

std::string label_for(std::uint16_t pc, const std::map<std::uint16_t, std::string>& labels) {
    const auto found = labels.find(pc);
    return found != labels.end() ? found->second : "L" + std::to_string(pc);
}

} // namespace

Result<std::string, DisassemblyError>
format_instruction(const Instruction& i, const std::map<std::uint16_t, std::string>& labels,
                   const std::vector<std::string>& params) {
    const auto info = isa::opcode_info(i.opcode);
    std::string out;
    if (i.guard.present) {
        out += i.guard.negate ? "@!" : "@";
        out += pred(i.guard.pred) + " ";
    }
    out += info.mnemonic;
    const std::string last = i.imm_flag ? immediate(i) : reg(i.src1);
    switch (info.shape) {
    case Shape::Rrr:
        out += " " + reg(i.dst) + ", " + reg(i.src0) + ", " + last;
        break;
    case Shape::Prr:
        out += " " + pred(i.dst) + ", " + reg(i.src0) + ", " + last;
        break;
    case Shape::Rr:
        out += " " + reg(i.dst) + ", " + (i.imm_flag ? immediate(i) : reg(i.src0));
        break;
    case Shape::Acc:
        out += " " + reg(i.dst) + ", " + reg(i.src0) + ", " + reg(i.src1);
        break;
    case Shape::Sreg: {
        const auto special = isa::special_register_info(i.src0);
        if (!special.has_value()) {
            return fail(DisassemblyError{.pc = 0, .message = "special register out of range"});
        }
        out += " " + reg(i.dst) + ", " + std::string(special->name);
        break;
    }
    case Shape::Bra:
        out += " " + label_for(i.branch_target(), labels);
        if (i.reconvergence_pc() != isa::no_reconvergence) {
            out += "    // reconverge " + label_for(i.reconvergence_pc(), labels);
        }
        break;
    case Shape::None:
        break;
    case Shape::Ld:
        out += " " + reg(i.dst) + ", " + address(i);
        break;
    case Shape::St:
        out += " " + address(i) + ", " + reg(i.src1);
        break;
    case Shape::Ldp:
        if (i.imm >= params.size()) {
            return fail(DisassemblyError{.pc = 0, .message = "parameter ordinal out of range"});
        }
        out += " " + reg(i.dst) + ", [" + params[i.imm] + "]";
        break;
    }
    return out;
}

Result<std::string, DisassemblyError> disassemble(const Program& program) {
    std::string out = ".entry " + program.entry + "\n";
    for (const auto& param : program.params) {
        out += ".param " + param + "\n";
    }
    out += ".shared " + std::to_string(program.shared_bytes) + "\n\n";

    // Every branch target needs a label line; synthesize names for targets the
    // source did not name so that the output is always reassemblable.
    std::map<std::uint16_t, std::string> labels = program.labels;
    for (std::size_t pc = 0; pc < program.words.size(); ++pc) {
        const auto decoded = isa::decode(program.words[pc]);
        if (!decoded.has_value()) {
            return fail(DisassemblyError{.pc = pc,
                                         .message = std::string(isa::to_string(decoded.error()))});
        }
        if (decoded->opcode == Opcode::Bra) {
            labels.try_emplace(decoded->branch_target(),
                               "L" + std::to_string(decoded->branch_target()));
        }
    }

    for (std::size_t pc = 0; pc < program.words.size(); ++pc) {
        if (const auto label = labels.find(static_cast<std::uint16_t>(pc)); label != labels.end()) {
            out += label->second + ":\n";
        }
        const auto decoded = isa::decode(program.words[pc]).value();
        auto text = format_instruction(decoded, labels, program.params);
        if (!text.has_value()) {
            return fail(DisassemblyError{.pc = pc, .message = text.error().message});
        }
        out += "    " + *text + "\n";
    }
    return out;
}

} // namespace warpsim::assembler
