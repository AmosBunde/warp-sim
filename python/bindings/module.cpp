#include "warpsim/asm/assembler.hpp"
#include "warpsim/asm/disassembler.hpp"
#include "warpsim/asm/program.hpp"
#include "warpsim/core/device.hpp"
#include "warpsim/core/fault.hpp"
#include "warpsim/core/types.hpp"
#include "warpsim/version.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

namespace {

using warpsim::assembler::Program;
using warpsim::core::Device;
using warpsim::core::Dim2;
using warpsim::core::Fault;
using warpsim::core::LaunchStats;

Dim2 to_dim(const std::pair<std::uint32_t, std::uint32_t>& xy) {
    return Dim2{.x = xy.first, .y = xy.second};
}

/// Raised as warpsim.SimFault; carries the fault fields as attributes.
class SimFaultError : public std::runtime_error {
public:
    explicit SimFaultError(Fault fault)
        : std::runtime_error(fault.describe()), fault_(std::move(fault)) {}
    [[nodiscard]] const Fault& fault() const noexcept { return fault_; }

private:
    Fault fault_;
};

/// Raised as warpsim.AssemblyError with line and column attributes.
class AssemblyErrorPy : public std::runtime_error {
public:
    explicit AssemblyErrorPy(warpsim::assembler::AssemblyError error)
        : std::runtime_error(std::to_string(error.line) + ":" + std::to_string(error.column) +
                             ": " + error.message),
          error_(std::move(error)) {}
    [[nodiscard]] const warpsim::assembler::AssemblyError& error() const noexcept { return error_; }

private:
    warpsim::assembler::AssemblyError error_;
};

py::dict stats_to_dict(const LaunchStats& s) {
    py::dict d;
    d["instructions_issued"] = s.instructions_issued;
    d["divergent_branches"] = s.divergent_branches;
    d["barriers_completed"] = s.barriers_completed;
    d["blocks_executed"] = s.blocks_executed;
    d["warps_launched"] = s.warps_launched;
    return d;
}

} // namespace

PYBIND11_MODULE(_core, m) {
    m.doc() = "WarpSim functional GPU simulator, native extension";
    m.def(
        "version", [] { return std::string{warpsim::version()}; },
        "Semantic version of the native simulator library");

    static py::exception<SimFaultError> sim_fault(m, "SimFault");
    static py::exception<AssemblyErrorPy> assembly_error(m, "AssemblyError");
    // pybind11 requires a by-value std::exception_ptr for translators.
    py::register_exception_translator(
        [](std::exception_ptr p) { // NOLINT(performance-unnecessary-value-param)
            try {
                if (p) {
                    std::rethrow_exception(p);
                }
            } catch (const SimFaultError& e) {
                py::object exc = py::reinterpret_borrow<py::object>(sim_fault.ptr())(e.what());
                exc.attr("message") = e.fault().message;
                exc.attr("block") = e.fault().block;
                exc.attr("warp") = e.fault().warp;
                exc.attr("lane") = e.fault().lane;
                exc.attr("pc") = e.fault().pc;
                exc.attr("address") = e.fault().address;
                PyErr_SetObject(sim_fault.ptr(), exc.ptr());
            } catch (const AssemblyErrorPy& e) {
                py::object exc = py::reinterpret_borrow<py::object>(assembly_error.ptr())(e.what());
                exc.attr("line") = e.error().line;
                exc.attr("column") = e.error().column;
                exc.attr("message") = e.error().message;
                PyErr_SetObject(assembly_error.ptr(), exc.ptr());
            }
        });

    py::class_<Program>(m, "Program", "An assembled WISA kernel")
        .def_readonly("entry", &Program::entry)
        .def_readonly("params", &Program::params)
        .def_readonly("shared_bytes", &Program::shared_bytes)
        .def_readonly("words", &Program::words)
        .def_property_readonly("labels", [](const Program& p) { return p.labels; })
        .def(
            "disassemble",
            [](const Program& p) {
                const auto text = warpsim::assembler::disassemble(p);
                if (!text.has_value()) {
                    throw std::runtime_error(text.error().message);
                }
                return *text;
            },
            "Canonical text that reassembles to the same words")
        .def("__len__", [](const Program& p) { return p.words.size(); });

    m.def(
        "assemble",
        [](const std::string& source) {
            auto program = warpsim::assembler::assemble(source);
            if (!program.has_value()) {
                throw AssemblyErrorPy(program.error());
            }
            return *program;
        },
        py::arg("source"), "Assembles WISA source text into a Program");

    py::class_<Device>(m, "Device", "A simulated device with one flat global memory")
        .def(py::init<std::size_t>(), py::arg("global_bytes"))
        .def_property_readonly("global_bytes", [](const Device& d) { return d.global().size(); })
        .def(
            "write",
            [](Device& d, std::uint32_t offset, const py::array& data) {
                const py::buffer_info info = py::array::ensure(data).request();
                const auto nbytes =
                    static_cast<std::size_t>(info.size) * static_cast<std::size_t>(info.itemsize);
                if (static_cast<std::size_t>(offset) + nbytes > d.global().size()) {
                    throw std::out_of_range("write past the end of global memory");
                }
                const py::array contiguous = py::array::ensure(data, py::array::c_style);
                const py::buffer_info source = contiguous.request();
                std::memcpy(&d.global().bytes()[offset], source.ptr, nbytes);
            },
            py::arg("offset"), py::arg("data"),
            "Copies a NumPy array into global memory at a byte offset")
        .def(
            "read",
            [](const Device& d, std::uint32_t offset, std::size_t count, const std::string& dtype) {
                const py::dtype dt(dtype);
                const auto nbytes = count * static_cast<std::size_t>(dt.itemsize());
                if (static_cast<std::size_t>(offset) + nbytes > d.global().size()) {
                    throw std::out_of_range("read past the end of global memory");
                }
                py::array out(dt, static_cast<py::ssize_t>(count));
                const py::buffer_info target = out.request(true);
                std::memcpy(target.ptr, &d.global().bytes()[offset], nbytes);
                return out;
            },
            py::arg("offset"), py::arg("count"), py::arg("dtype") = "uint32",
            "Copies `count` elements of `dtype` out of global memory at a byte offset")
        .def(
            "launch",
            [](Device& d, const Program& program, std::pair<std::uint32_t, std::uint32_t> grid,
               std::pair<std::uint32_t, std::uint32_t> block,
               const std::vector<std::uint32_t>& params) {
                const auto stats = d.launch(program, to_dim(grid), to_dim(block), params);
                if (!stats.has_value()) {
                    throw SimFaultError(stats.error());
                }
                return stats_to_dict(*stats);
            },
            py::arg("program"), py::arg("grid"), py::arg("block"), py::arg("params"),
            "Runs the program; returns launch statistics or raises SimFault");
}
