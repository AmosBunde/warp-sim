#include "warpsim/isa/opcode.hpp"

#include <fstream>
#include <set>
#include <string>

#include <gtest/gtest.h>

namespace {

// Reads the backticked mnemonics from the first column of the instruction
// table in docs/wisa-spec.md section 6. The table rows look like
//   | `add` | 0x01 | RRR | ... |
std::set<std::string> mnemonics_in_specification() {
    std::ifstream spec(WARPSIM_SPEC_PATH);
    EXPECT_TRUE(spec.is_open()) << WARPSIM_SPEC_PATH;
    std::set<std::string> result;
    std::string line;
    bool in_table = false;
    while (std::getline(spec, line)) {
        if (line.starts_with("## 6.")) {
            in_table = true;
            continue;
        }
        if (line.starts_with("## 7.")) {
            break;
        }
        if (!in_table || !line.starts_with("| `")) {
            continue;
        }
        const auto end = line.find('`', 3);
        if (end != std::string::npos) {
            result.insert(line.substr(3, end - 3));
        }
    }
    return result;
}

TEST(SpecificationTable, MatchesOpcodeTableExactly) {
    const auto in_spec = mnemonics_in_specification();
    std::set<std::string> in_code;
    for (const auto& info : warpsim::isa::opcode_table) {
        in_code.emplace(info.mnemonic);
    }
    EXPECT_EQ(in_spec.size(), warpsim::isa::opcode_table.size());
    for (const auto& m : in_code) {
        EXPECT_TRUE(in_spec.contains(m)) << "in code but not in specification: " << m;
    }
    for (const auto& m : in_spec) {
        EXPECT_TRUE(in_code.contains(m)) << "in specification but not in code: " << m;
    }
}

} // namespace
