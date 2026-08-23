add_library(warpsim_core STATIC version.cpp isa/instruction.cpp)
add_library(warpsim::core ALIAS warpsim_core)

target_include_directories(warpsim_core PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_compile_definitions(warpsim_core PRIVATE WARPSIM_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(warpsim_core PRIVATE warpsim::warnings)

# The static library is linked into the pybind11 shared module, so every object
# must be position independent regardless of the compiler's default PIE setting.
set_target_properties(warpsim_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
