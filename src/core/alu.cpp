#include "warpsim/core/alu.hpp"

#include "warpsim/core/lane_context.hpp"
#include "warpsim/core/register_file.hpp"
#include "warpsim/core/types.hpp"
#include "warpsim/isa/instruction.hpp"
#include "warpsim/isa/opcode.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

namespace warpsim::core {

namespace {

using isa::Instruction;
using isa::Opcode;

constexpr std::uint32_t shift_mask = 31;

[[nodiscard]] std::int32_t as_s32(std::uint32_t bits) noexcept {
    return std::bit_cast<std::int32_t>(bits);
}
[[nodiscard]] std::uint32_t from_s32(std::int32_t value) noexcept {
    return std::bit_cast<std::uint32_t>(value);
}
[[nodiscard]] float as_f32(std::uint32_t bits) noexcept {
    return std::bit_cast<float>(bits);
}
[[nodiscard]] std::uint32_t from_f32(float value) noexcept {
    return std::bit_cast<std::uint32_t>(value);
}

// Wrapping arithmetic is performed on unsigned values, which is defined
// behavior, and reinterpreted; signed overflow never occurs.
[[nodiscard]] std::uint32_t wrap_add(std::uint32_t a, std::uint32_t b) noexcept {
    return a + b;
}
[[nodiscard]] std::uint32_t wrap_sub(std::uint32_t a, std::uint32_t b) noexcept {
    return a - b;
}
[[nodiscard]] std::uint32_t wrap_mul(std::uint32_t a, std::uint32_t b) noexcept {
    return a * b;
}

[[nodiscard]] std::uint32_t s32_div(std::uint32_t a, std::uint32_t b) noexcept {
    const std::int32_t x = as_s32(a);
    const std::int32_t y = as_s32(b);
    if (y == 0) {
        return 0;
    }
    if (x == std::numeric_limits<std::int32_t>::min() && y == -1) {
        return a; // INT_MIN / -1 = INT_MIN (specification section 4)
    }
    return from_s32(x / y);
}

[[nodiscard]] std::uint32_t s32_rem(std::uint32_t a, std::uint32_t b) noexcept {
    const std::int32_t x = as_s32(a);
    const std::int32_t y = as_s32(b);
    if (y == 0 || (x == std::numeric_limits<std::int32_t>::min() && y == -1)) {
        return 0;
    }
    return from_s32(x % y);
}

[[nodiscard]] std::uint32_t sra(std::uint32_t a, std::uint32_t count) noexcept {
    // Arithmetic shift of a signed value is implementation defined before
    // C++20 and defined as sign propagating from C++20 on; spelled out anyway.
    const std::uint32_t n = count & shift_mask;
    const std::uint32_t logical = a >> n;
    if (as_s32(a) < 0 && n > 0) {
        return logical | (~std::uint32_t{0} << (32 - n));
    }
    return logical;
}

[[nodiscard]] std::uint32_t f32_to_s32(std::uint32_t bits) noexcept {
    const float value = as_f32(bits);
    if (std::isnan(value)) {
        return 0;
    }
    // Truncate toward zero and saturate. 2^31 is exactly representable in
    // binary32, so the comparison is exact on both ends.
    constexpr float upper = 2147483648.0F;
    constexpr float lower = -2147483648.0F;
    if (value >= upper) {
        return from_s32(std::numeric_limits<std::int32_t>::max());
    }
    if (value <= lower) {
        return from_s32(std::numeric_limits<std::int32_t>::min());
    }
    return from_s32(static_cast<std::int32_t>(value));
}

[[nodiscard]] bool compare_s32(Opcode op, std::uint32_t a, std::uint32_t b) noexcept {
    const std::int32_t x = as_s32(a);
    const std::int32_t y = as_s32(b);
    switch (op) {
    case Opcode::SetpEqS32:
        return x == y;
    case Opcode::SetpNeS32:
        return x != y;
    case Opcode::SetpLtS32:
        return x < y;
    case Opcode::SetpLeS32:
        return x <= y;
    case Opcode::SetpGtS32:
        return x > y;
    case Opcode::SetpGeS32:
        return x >= y;
    default:
        return false;
    }
}

// Ordered comparisons are false on NaN; ne is unordered and true on NaN.
[[nodiscard]] bool compare_f32(Opcode op, std::uint32_t a, std::uint32_t b) noexcept {
    const float x = as_f32(a);
    const float y = as_f32(b);
    switch (op) {
    case Opcode::SetpEqF32:
        return x == y;
    case Opcode::SetpNeF32:
        return !(x == y);
    case Opcode::SetpLtF32:
        return x < y;
    case Opcode::SetpLeF32:
        return x <= y;
    case Opcode::SetpGtF32:
        return x > y;
    case Opcode::SetpGeF32:
        return x >= y;
    default:
        return false;
    }
}

struct Operands {
    std::uint32_t a = 0; ///< src0
    std::uint32_t b = 0; ///< src1 or immediate
};

[[nodiscard]] Operands fetch(const Instruction& i, const RegisterFile& rf, unsigned lane) noexcept {
    const auto shape = isa::opcode_info(i.opcode).shape;
    if (shape == isa::Shape::Rr) {
        // The single source is src0, or the immediate.
        return {.a = i.imm_flag ? i.imm : rf.read(lane, i.src0), .b = 0};
    }
    return {.a = rf.read(lane, i.src0), .b = i.imm_flag ? i.imm : rf.read(lane, i.src1)};
}

[[nodiscard]] std::uint32_t evaluate(const Instruction& i, Operands o, std::uint32_t dst_old,
                                     unsigned lane, const LaneContext& ctx) noexcept {
    switch (i.opcode) {
    case Opcode::Add:
        return wrap_add(o.a, o.b);
    case Opcode::Sub:
        return wrap_sub(o.a, o.b);
    case Opcode::Mul:
        return wrap_mul(o.a, o.b);
    case Opcode::Div:
        return s32_div(o.a, o.b);
    case Opcode::Rem:
        return s32_rem(o.a, o.b);
    case Opcode::Min:
        return from_s32(std::min(as_s32(o.a), as_s32(o.b)));
    case Opcode::Max:
        return from_s32(std::max(as_s32(o.a), as_s32(o.b)));
    case Opcode::Neg:
        return wrap_sub(0, o.a);
    case Opcode::And:
        return o.a & o.b;
    case Opcode::Or:
        return o.a | o.b;
    case Opcode::Xor:
        return o.a ^ o.b;
    case Opcode::Not:
        return ~o.a;
    case Opcode::Shl:
        return o.a << (o.b & shift_mask);
    case Opcode::Shr:
        return o.a >> (o.b & shift_mask);
    case Opcode::Sra:
        return sra(o.a, o.b);
    case Opcode::Mov:
        return o.a;
    case Opcode::FAdd:
        return from_f32(as_f32(o.a) + as_f32(o.b));
    case Opcode::FSub:
        return from_f32(as_f32(o.a) - as_f32(o.b));
    case Opcode::FMul:
        return from_f32(as_f32(o.a) * as_f32(o.b));
    case Opcode::FFma:
        return from_f32(std::fma(as_f32(o.a), as_f32(o.b), as_f32(dst_old)));
    case Opcode::FMin:
        return from_f32(std::fmin(as_f32(o.a), as_f32(o.b)));
    case Opcode::FMax:
        return from_f32(std::fmax(as_f32(o.a), as_f32(o.b)));
    case Opcode::FNeg:
        return o.a ^ 0x80000000U;
    case Opcode::CvtF32S32:
        return from_f32(static_cast<float>(as_s32(o.a)));
    case Opcode::CvtS32F32:
        return f32_to_s32(o.a);
    case Opcode::MovSreg:
        return ctx.special(static_cast<isa::SpecialRegister>(i.src0), lane);
    default:
        return 0;
    }
}

[[nodiscard]] bool is_setp(Opcode op) noexcept {
    const auto n = static_cast<std::uint8_t>(op);
    return n >= static_cast<std::uint8_t>(Opcode::SetpEqS32) &&
           n <= static_cast<std::uint8_t>(Opcode::SetpGeF32);
}

[[nodiscard]] bool is_setp_f32(Opcode op) noexcept {
    return static_cast<std::uint8_t>(op) >= static_cast<std::uint8_t>(Opcode::SetpEqF32);
}

} // namespace

bool is_alu_opcode(Opcode opcode) noexcept {
    switch (opcode) {
    case Opcode::Bra:
    case Opcode::Exit:
    case Opcode::BarSync:
    case Opcode::LdGlobal:
    case Opcode::StGlobal:
    case Opcode::LdShared:
    case Opcode::StShared:
    case Opcode::LdParam:
        return false;
    default:
        return true;
    }
}

void execute_alu(const Instruction& i, LaneMask exec, RegisterFile& rf,
                 const LaneContext& ctx) noexcept {
    for (unsigned lane = 0; lane < warp_size; ++lane) {
        if (!has_lane(exec, lane)) {
            continue;
        }
        const Operands o = fetch(i, rf, lane);
        if (is_setp(i.opcode)) {
            const bool result = is_setp_f32(i.opcode) ? compare_f32(i.opcode, o.a, o.b)
                                                      : compare_s32(i.opcode, o.a, o.b);
            rf.write_pred(lane, i.dst, result);
            continue;
        }
        rf.write(lane, i.dst, evaluate(i, o, rf.read(lane, i.dst), lane, ctx));
    }
}

} // namespace warpsim::core
