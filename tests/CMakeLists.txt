include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
    GIT_SHALLOW TRUE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

# Registers one test executable that links the core library and the warning set.
function(warpsim_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE warpsim::core warpsim::warnings GTest::gtest_main)
    gtest_discover_tests(${name})
endfunction()

warpsim_add_test(version_test version_test.cpp)
warpsim_add_test(isa_instruction_test isa/instruction_test.cpp)
target_include_directories(isa_instruction_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
warpsim_add_test(isa_spec_table_test isa/spec_table_test.cpp)
target_compile_definitions(isa_spec_table_test
                           PRIVATE WARPSIM_SPEC_PATH="${PROJECT_SOURCE_DIR}/docs/wisa-spec.md")
warpsim_add_test(asm_lexer_test asm/lexer_test.cpp)
target_include_directories(asm_lexer_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
warpsim_add_test(asm_assembler_test asm/assembler_test.cpp)
target_compile_definitions(asm_assembler_test
                           PRIVATE WARPSIM_KERNELS_DIR="${PROJECT_SOURCE_DIR}/kernels")
warpsim_add_test(asm_reconvergence_test asm/reconvergence_test.cpp)
target_include_directories(asm_reconvergence_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
warpsim_add_test(asm_roundtrip_test asm/roundtrip_test.cpp)
target_include_directories(asm_roundtrip_test PRIVATE ${PROJECT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(asm_roundtrip_test
                           PRIVATE WARPSIM_KERNELS_DIR="${PROJECT_SOURCE_DIR}/kernels")
warpsim_add_test(core_register_file_test core/register_file_test.cpp)
warpsim_add_test(core_alu_test core/alu_test.cpp)
