# Sanitizer flags are applied globally so that the fetched test framework and
# the simulator are instrumented consistently. Values: none, address, undefined.
if(WARPSIM_SANITIZER STREQUAL "address")
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address)
elseif(WARPSIM_SANITIZER STREQUAL "undefined")
    add_compile_options(-fsanitize=undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=undefined)
elseif(NOT WARPSIM_SANITIZER STREQUAL "none")
    message(FATAL_ERROR "Unknown WARPSIM_SANITIZER value: ${WARPSIM_SANITIZER}")
endif()
