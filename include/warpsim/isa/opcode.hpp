#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace warpsim::isa {

/// Opcode numbers are those of docs/wisa-spec.md section 6. Zero is invalid.
enum class Opcode : std::uint8_t {
    Add = 0x01,
    Sub = 0x02,
    Mul = 0x03,
    Div = 0x04,
    Rem = 0x05,
    Min = 0x06,
    Max = 0x07,
    Neg = 0x08,
    And = 0x09,
    Or = 0x0A,
    Xor = 0x0B,
    Not = 0x0C,
    Shl = 0x0D,
    Shr = 0x0E,
    Sra = 0x0F,
    Mov = 0x10,
    FAdd = 0x11,
    FSub = 0x12,
    FMul = 0x13,
    FFma = 0x14,
    FMin = 0x15,
    FMax = 0x16,
    FNeg = 0x17,
    CvtF32S32 = 0x18,
    CvtS32F32 = 0x19,
    SetpEqS32 = 0x1A,
    SetpNeS32 = 0x1B,
    SetpLtS32 = 0x1C,
    SetpLeS32 = 0x1D,
    SetpGtS32 = 0x1E,
    SetpGeS32 = 0x1F,
    SetpEqF32 = 0x20,
    SetpNeF32 = 0x21,
    SetpLtF32 = 0x22,
    SetpLeF32 = 0x23,
    SetpGtF32 = 0x24,
    SetpGeF32 = 0x25,
    MovSreg = 0x26,
    Bra = 0x27,
    Exit = 0x28,
    BarSync = 0x29,
    LdGlobal = 0x2A,
    StGlobal = 0x2B,
    LdShared = 0x2C,
    StShared = 0x2D,
    LdParam = 0x2E,
};

/// Operand shapes of docs/wisa-spec.md section 3. The shape decides which
/// encoding fields an opcode uses; every other field must be zero.
enum class Shape : std::uint8_t {
    Rrr,  ///< rd, ra, rb|imm
    Rr,   ///< rd, ra|imm
    Acc,  ///< rd, ra, rb with rd as accumulator
    Prr,  ///< p, ra, rb|imm
    Sreg, ///< rd, %special (index in src0)
    Bra,  ///< label (target in imm[15:0], reconvergence in imm[31:16])
    None, ///< no operands
    Ld,   ///< rd, [ra+off]
    St,   ///< [ra+off], rb
    Ldp,  ///< rd, [param] (ordinal in imm)
};

struct OpcodeInfo {
    Opcode opcode;
    std::string_view mnemonic;
    Shape shape;
};

/// The complete opcode table in specification order.
inline constexpr std::array<OpcodeInfo, 46> opcode_table{{
    {Opcode::Add, "add", Shape::Rrr},
    {Opcode::Sub, "sub", Shape::Rrr},
    {Opcode::Mul, "mul", Shape::Rrr},
    {Opcode::Div, "div", Shape::Rrr},
    {Opcode::Rem, "rem", Shape::Rrr},
    {Opcode::Min, "min", Shape::Rrr},
    {Opcode::Max, "max", Shape::Rrr},
    {Opcode::Neg, "neg", Shape::Rr},
    {Opcode::And, "and", Shape::Rrr},
    {Opcode::Or, "or", Shape::Rrr},
    {Opcode::Xor, "xor", Shape::Rrr},
    {Opcode::Not, "not", Shape::Rr},
    {Opcode::Shl, "shl", Shape::Rrr},
    {Opcode::Shr, "shr", Shape::Rrr},
    {Opcode::Sra, "sra", Shape::Rrr},
    {Opcode::Mov, "mov", Shape::Rr},
    {Opcode::FAdd, "fadd", Shape::Rrr},
    {Opcode::FSub, "fsub", Shape::Rrr},
    {Opcode::FMul, "fmul", Shape::Rrr},
    {Opcode::FFma, "ffma", Shape::Acc},
    {Opcode::FMin, "fmin", Shape::Rrr},
    {Opcode::FMax, "fmax", Shape::Rrr},
    {Opcode::FNeg, "fneg", Shape::Rr},
    {Opcode::CvtF32S32, "cvt.f32.s32", Shape::Rr},
    {Opcode::CvtS32F32, "cvt.s32.f32", Shape::Rr},
    {Opcode::SetpEqS32, "setp.eq.s32", Shape::Prr},
    {Opcode::SetpNeS32, "setp.ne.s32", Shape::Prr},
    {Opcode::SetpLtS32, "setp.lt.s32", Shape::Prr},
    {Opcode::SetpLeS32, "setp.le.s32", Shape::Prr},
    {Opcode::SetpGtS32, "setp.gt.s32", Shape::Prr},
    {Opcode::SetpGeS32, "setp.ge.s32", Shape::Prr},
    {Opcode::SetpEqF32, "setp.eq.f32", Shape::Prr},
    {Opcode::SetpNeF32, "setp.ne.f32", Shape::Prr},
    {Opcode::SetpLtF32, "setp.lt.f32", Shape::Prr},
    {Opcode::SetpLeF32, "setp.le.f32", Shape::Prr},
    {Opcode::SetpGtF32, "setp.gt.f32", Shape::Prr},
    {Opcode::SetpGeF32, "setp.ge.f32", Shape::Prr},
    {Opcode::MovSreg, "mov.sreg", Shape::Sreg},
    {Opcode::Bra, "bra", Shape::Bra},
    {Opcode::Exit, "exit", Shape::None},
    {Opcode::BarSync, "bar.sync", Shape::None},
    {Opcode::LdGlobal, "ld.global", Shape::Ld},
    {Opcode::StGlobal, "st.global", Shape::St},
    {Opcode::LdShared, "ld.shared", Shape::Ld},
    {Opcode::StShared, "st.shared", Shape::St},
    {Opcode::LdParam, "ld.param", Shape::Ldp},
}};

/// Looks up an opcode by number. Returns nullopt for any number not in the table.
[[nodiscard]] constexpr std::optional<OpcodeInfo> opcode_info(std::uint8_t number) noexcept {
    for (const auto& entry : opcode_table) {
        if (static_cast<std::uint8_t>(entry.opcode) == number) {
            return entry;
        }
    }
    return std::nullopt;
}

[[nodiscard]] constexpr OpcodeInfo opcode_info(Opcode opcode) noexcept {
    // Every enumerator is in the table by construction; the fallback is unreachable.
    const auto found = opcode_info(static_cast<std::uint8_t>(opcode));
    return found.has_value() ? *found : opcode_table.front();
}

/// Looks up an opcode by mnemonic, for the assembler.
[[nodiscard]] constexpr std::optional<OpcodeInfo> opcode_info(std::string_view mnemonic) noexcept {
    for (const auto& entry : opcode_table) {
        if (entry.mnemonic == mnemonic) {
            return entry;
        }
    }
    return std::nullopt;
}

/// Special register indices of docs/wisa-spec.md section 2.4.
enum class SpecialRegister : std::uint8_t {
    TidX = 0,
    TidY = 1,
    NtidX = 2,
    NtidY = 3,
    CtaidX = 4,
    CtaidY = 5,
    NctaidX = 6,
    NctaidY = 7,
    LaneId = 8,
    WarpId = 9,
};

struct SpecialRegisterInfo {
    SpecialRegister reg;
    std::string_view name;
};

inline constexpr std::array<SpecialRegisterInfo, 10> special_register_table{{
    {SpecialRegister::TidX, "%tid.x"},
    {SpecialRegister::TidY, "%tid.y"},
    {SpecialRegister::NtidX, "%ntid.x"},
    {SpecialRegister::NtidY, "%ntid.y"},
    {SpecialRegister::CtaidX, "%ctaid.x"},
    {SpecialRegister::CtaidY, "%ctaid.y"},
    {SpecialRegister::NctaidX, "%nctaid.x"},
    {SpecialRegister::NctaidY, "%nctaid.y"},
    {SpecialRegister::LaneId, "%laneid"},
    {SpecialRegister::WarpId, "%warpid"},
}};

[[nodiscard]] constexpr std::optional<SpecialRegisterInfo>
special_register_info(std::string_view name) noexcept {
    for (const auto& entry : special_register_table) {
        if (entry.name == name) {
            return entry;
        }
    }
    return std::nullopt;
}

[[nodiscard]] constexpr std::optional<SpecialRegisterInfo>
special_register_info(std::uint8_t index) noexcept {
    for (const auto& entry : special_register_table) {
        if (static_cast<std::uint8_t>(entry.reg) == index) {
            return entry;
        }
    }
    return std::nullopt;
}

} // namespace warpsim::isa
