include(FetchContent)

FetchContent_Declare(
    pybind11
    GIT_REPOSITORY https://github.com/pybind/pybind11.git
    GIT_TAG v2.13.6
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(pybind11)

# NO_EXTRAS: LTO and strip are unnecessary for a simulator extension and LTO emits
# toolchain warnings that are not ours to silence.
pybind11_add_module(_core MODULE NO_EXTRAS bindings/module.cpp)
target_link_libraries(_core PRIVATE warpsim::core warpsim::warnings)
# Third-party headers are system headers: our warning set applies to our code,
# not to pybind11 or CPython. GCC ignores -I for a directory also given with
# -isystem, so this takes precedence over the include path pybind11 adds.
target_include_directories(_core SYSTEM PRIVATE ${pybind11_SOURCE_DIR}/include ${Python_INCLUDE_DIRS})

# The extension is written next to the pure-Python package so that
# `PYTHONPATH=python python -c "import warpsim"` works without an install step.
set_target_properties(_core PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/python/warpsim)
