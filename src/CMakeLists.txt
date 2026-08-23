add_library(warpsim_core STATIC version.cpp isa/instruction.cpp asm/lexer.cpp asm/assembler.cpp asm/reconvergence.cpp asm/disassembler.cpp core/alu.cpp core/warp.cpp core/memory.cpp core/device.cpp core/memory_analysis.cpp instr/timing.cpp)
target_include_directories(warpsim_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_library(warpsim::core ALIAS warpsim_core)

target_include_directories(warpsim_core PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_compile_definitions(warpsim_core PRIVATE WARPSIM_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(warpsim_core PRIVATE warpsim::warnings)

# The static library is linked into the pybind11 shared module, so every object
# must be position independent regardless of the compiler's default PIE setting.
set_target_properties(warpsim_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
