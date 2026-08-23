#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace warpsim::assembler {

/// An assembled kernel: encoded words plus the metadata the loader and the
/// disassembler need. Value type, freely copyable.
struct Program {
    std::string entry;
    std::vector<std::string> params;
    std::uint32_t shared_bytes = 0;
    std::vector<std::uint64_t> words;
    /// Label names by PC, kept so that the disassembler can print the source names.
    std::map<std::uint16_t, std::string> labels;
};

} // namespace warpsim::assembler
