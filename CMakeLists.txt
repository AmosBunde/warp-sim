cmake_minimum_required(VERSION 3.25)

project(
    warpsim
    VERSION 0.1.0
    DESCRIPTION "Functional GPU simulator with a PTX-inspired ISA"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(WARPSIM_BUILD_TESTS "Build the C++ test suite" ON)
option(WARPSIM_BUILD_PYTHON "Build the pybind11 extension" ON)
set(WARPSIM_SANITIZER
    "none"
    CACHE STRING "Sanitizer set to enable: none, address, undefined")

include(cmake/warnings.cmake)
include(cmake/sanitizers.cmake)

add_subdirectory(src)

if(WARPSIM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(WARPSIM_BUILD_PYTHON)
    add_subdirectory(python)
endif()
