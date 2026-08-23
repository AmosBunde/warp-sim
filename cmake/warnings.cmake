# Interface target carrying the warning set. Every WarpSim target links it so
# that warnings are errors uniformly, and third-party code fetched through
# FetchContent never sees these flags.
add_library(warpsim_warnings INTERFACE)
add_library(warpsim::warnings ALIAS warpsim_warnings)

target_compile_options(
    warpsim_warnings
    INTERFACE -Wall
              -Wextra
              -Wpedantic
              -Wconversion
              -Wsign-conversion
              -Wshadow
              -Wnon-virtual-dtor
              -Wold-style-cast
              -Wcast-align
              -Wunused
              -Woverloaded-virtual
              -Wnull-dereference
              -Wdouble-promotion
              -Wformat=2
              -Wimplicit-fallthrough
              -Werror)
