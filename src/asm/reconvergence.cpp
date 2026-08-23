#include "reconvergence.hpp"

#include "warpsim/isa/instruction.hpp"
#include "warpsim/isa/opcode.hpp"
#include "warpsim/result.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace warpsim::assembler {

namespace {

using isa::Instruction;
using isa::Opcode;

/// A fixed-size set of block indices, packed 64 per word.
class BlockSet {
public:
    explicit BlockSet(std::size_t size, bool all)
        : words_((size + 63) / 64, all ? ~std::uint64_t{0} : 0), size_(size) {
        if (all && size % 64 != 0) {
            words_.back() &= (std::uint64_t{1} << (size % 64)) - 1;
        }
    }

    [[nodiscard]] bool contains(std::size_t i) const noexcept {
        return ((words_[i / 64] >> (i % 64)) & 1U) != 0;
    }
    void insert(std::size_t i) noexcept { words_[i / 64] |= std::uint64_t{1} << (i % 64); }
    void erase(std::size_t i) noexcept { words_[i / 64] &= ~(std::uint64_t{1} << (i % 64)); }
    void intersect(const BlockSet& other) noexcept {
        for (std::size_t w = 0; w < words_.size(); ++w) {
            words_[w] &= other.words_[w];
        }
    }
    [[nodiscard]] bool operator==(const BlockSet& other) const noexcept = default;
    [[nodiscard]] std::size_t count() const noexcept {
        std::size_t n = 0;
        for (const auto w : words_) {
            n += static_cast<std::size_t>(std::popcount(w));
        }
        return n;
    }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }

private:
    std::vector<std::uint64_t> words_;
    std::size_t size_;
};

bool is_terminator(const Instruction& i) noexcept {
    return i.opcode == Opcode::Bra || i.opcode == Opcode::Exit;
}

struct Cfg {
    std::vector<std::size_t> leaders;            ///< first PC of each block, ascending
    std::vector<std::size_t> block_of;           ///< block index per PC
    std::vector<std::vector<std::size_t>> succs; ///< successor blocks; `exit_node` is virtual
    std::size_t exit_node = 0;
};

Result<Cfg, ReconvergenceError> build_cfg(std::span<const Instruction> program) {
    const std::size_t n = program.size();
    std::vector<bool> is_leader(n + 1, false);
    is_leader[0] = true;
    for (std::size_t pc = 0; pc < n; ++pc) {
        const auto& i = program[pc];
        if (i.opcode == Opcode::Bra) {
            const std::size_t target = i.branch_target();
            if (target >= n) {
                return fail(ReconvergenceError{.pc = pc, .message = "branch target out of range"});
            }
            is_leader[target] = true;
        }
        if (is_terminator(i) && pc + 1 < n) {
            is_leader[pc + 1] = true;
        }
    }

    Cfg cfg;
    cfg.block_of.assign(n, 0);
    for (std::size_t pc = 0; pc < n; ++pc) {
        if (is_leader[pc]) {
            cfg.leaders.push_back(pc);
        }
        cfg.block_of[pc] = cfg.leaders.size() - 1;
    }
    const std::size_t blocks = cfg.leaders.size();
    cfg.exit_node = blocks;
    cfg.succs.assign(blocks + 1, {});

    for (std::size_t b = 0; b < blocks; ++b) {
        const std::size_t last = (b + 1 < blocks ? cfg.leaders[b + 1] : n) - 1;
        const auto& i = program[last];
        const auto fallthrough = [&]() -> std::size_t {
            return last + 1 < n ? cfg.block_of[last + 1] : cfg.exit_node;
        };
        auto& out = cfg.succs[b];
        if (i.opcode == Opcode::Bra) {
            out.push_back(cfg.block_of[i.branch_target()]);
            if (i.guard.present) {
                out.push_back(fallthrough());
            }
        } else if (i.opcode == Opcode::Exit) {
            out.push_back(cfg.exit_node);
            if (i.guard.present) {
                out.push_back(fallthrough());
            }
        } else {
            out.push_back(fallthrough());
        }
    }
    return cfg;
}

/// Post-dominator sets by iterative dataflow on the reversed graph:
/// pdom(exit) = {exit}; pdom(b) = {b} union intersection over successors.
std::vector<BlockSet> post_dominators(const Cfg& cfg) {
    const std::size_t nodes = cfg.exit_node + 1;
    std::vector<BlockSet> pdom(nodes, BlockSet(nodes, true));
    pdom[cfg.exit_node] = BlockSet(nodes, false);
    pdom[cfg.exit_node].insert(cfg.exit_node);

    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t b = 0; b < cfg.exit_node; ++b) {
            BlockSet next(nodes, true);
            for (const std::size_t s : cfg.succs[b]) {
                next.intersect(pdom[s]);
            }
            next.insert(b);
            if (next != pdom[b]) {
                pdom[b] = next;
                changed = true;
            }
        }
    }
    return pdom;
}

/// The immediate post-dominator is the strict post-dominator whose own
/// post-dominator set equals the strict set (the sets of a chain are nested).
std::size_t immediate_post_dominator(const std::vector<BlockSet>& pdom, std::size_t b) {
    BlockSet strict = pdom[b];
    strict.erase(b);
    const std::size_t wanted = strict.count();
    for (std::size_t d = 0; d < strict.size(); ++d) {
        if (strict.contains(d) && pdom[d].count() == wanted) {
            return d;
        }
    }
    // No strict post-dominator reaches the exit: the block is in a loop that
    // never terminates. Report the virtual exit so the branch gets 0xFFFF.
    return strict.size() - 1;
}

} // namespace

Result<void, ReconvergenceError> annotate_reconvergence(std::span<Instruction> program) {
    auto cfg = build_cfg(program);
    if (!cfg.has_value()) {
        return fail(cfg.error());
    }
    const auto pdom = post_dominators(*cfg);

    for (std::size_t pc = 0; pc < program.size(); ++pc) {
        auto& i = program[pc];
        if (i.opcode != Opcode::Bra) {
            continue;
        }
        std::uint16_t reconvergence = isa::no_reconvergence;
        if (i.guard.present) {
            const std::size_t ipdom = immediate_post_dominator(pdom, cfg->block_of[pc]);
            if (ipdom != cfg->exit_node) {
                reconvergence = static_cast<std::uint16_t>(cfg->leaders[ipdom]);
            }
        }
        i.imm = Instruction::make_branch_imm(i.branch_target(), reconvergence);
    }
    return {};
}

} // namespace warpsim::assembler
