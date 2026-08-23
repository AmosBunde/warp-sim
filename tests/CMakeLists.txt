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
