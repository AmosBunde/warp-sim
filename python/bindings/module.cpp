#include "warpsim/version.hpp"

#include <string>

#include <pybind11/pybind11.h>

PYBIND11_MODULE(_core, m) {
    m.doc() = "WarpSim functional GPU simulator, native extension";
    m.def(
        "version", [] { return std::string{warpsim::version()}; },
        "Semantic version of the native simulator library");
}
