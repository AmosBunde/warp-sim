# WarpSim Breakdown

This document is the issue-level plan. Every issue below is opened on GitHub with a Problem or Hypothesis section, a Design section, and Acceptance criteria, assigned a label and a milestone, developed on a linked branch named `<type>/<n>-<slug>`, and merged through a pull request that closes it. Milestones are sequential: a milestone does not start until the previous one is merged to `main` with CI fully green, sanitizers included.

Plan numbers (P1 and so on) are followed by the GitHub issue number in parentheses. Two issues were added during execution to keep pull requests within the size guideline (#20 assembler lexer, #19 reconvergence analysis) and one was added for the Python bindings (#30); the barrier planned as P18 was delivered with the scheduler in #29. Final state: 30 issues, each closed by a squash-merged pull request from a linked development branch.

## Labels

| Label | Meaning |
|-------|---------|
| `area:scaffold` | Build system, CI, tooling |
| `area:isa` | Instruction set, encoding, assembler, disassembler |
| `area:core` | Register file, ALU, divergence, scheduler |
| `area:memory` | Global and shared memory, coalescing, bank conflicts, barriers |
| `area:kernels` | Shipped WISA kernels and golden models |
| `area:instrumentation` | Counters, timing model, reports |
| `area:docs` | Specification, diagrams, writeups |
| `type:feature` | New capability |
| `type:test` | Test-only change |
| `type:docs` | Documentation-only change |
| `type:chore` | Tooling and maintenance |

Branch prefixes follow the type label: `feat/`, `test/`, `docs/`, `chore/`.

## Milestones

| Milestone | Goal | Exit criterion |
|-----------|------|----------------|
| M0 Scaffold | A building, testing, linting, sanitizing, Python-importable skeleton | `make quickstart` runs a smoke test; every CI job is green |
| M1 ISA and assembler | WISA specified, encoded, assembled, disassembled | Round-trip property test covers every instruction in the specification |
| M2 Execution core | Kernels execute with correct divergence | Divergence-torture kernels pass against the golden model |
| M3 Memory system | Global and shared memory with analysis and barriers | Memory-pattern kernels produce the expected coalescing and conflict counts |
| M4 Kernels and harness | Four kernels, golden models, randomized differential suite | All four kernels match in CI across randomized inputs |
| M5 Instrumentation and timing | Counters, occupancy, coarse timing, final writeup | Ordinal acceptance test passes; README results table filled from real runs |

## M0 Scaffold (`area:scaffold`)

### P1 (#1) Project contract documents (`type:docs`)
- **Problem.** The repository has no statement of scope, architecture, or plan, so nothing can be reviewed against a contract.
- **Design.** Commit `README.md` (scope statement, architecture summary, component table, correctness claims) and this `BREAKDOWN.md`. Add `LICENSE` (MIT) and a `.gitignore`.
- **Acceptance.** Both documents are present on `main`; prose contains no contractions and no em dashes; the README Mermaid diagram renders on GitHub.

### P2 (#2) CMake presets, core library skeleton, GoogleTest (`type:chore`)
- **Problem.** There is no build.
- **Design.** `CMakeLists.txt` with `CMakePresets.json` defining `debug`, `release`, `asan`, `ubsan`. A `warpsim_core` static library with a version function. GoogleTest via `FetchContent`, one smoke test. Warnings are errors (`-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror`).
- **Acceptance.** `cmake --preset debug && cmake --build --preset debug && ctest --preset debug` passes locally with GCC and Clang.

### P3 (#3) clang-format and clang-tidy configuration and CI lint job (`type:chore`)
- **Problem.** Style and static analysis are not enforced.
- **Design.** `.clang-format` (LLVM base, 4-space indent, 100 columns), `.clang-tidy` with `bugprone-*`, `modernize-*`, `performance-*`, `readability-*`, `cppcoreguidelines-*` with a documented exclusion list. A `lint` CI job fails on any diff or warning.
- **Acceptance.** `make lint` passes on `main`; introducing a formatting violation in a test branch fails CI.

### P4 (#4) Build and test CI matrix with ASan and UBSan gates (`type:chore`)
- **Problem.** Nothing runs on pull requests; memory errors would go unnoticed.
- **Design.** `.github/workflows/ci.yml` with jobs: build-test (GCC and Clang, debug and release), `asan`, `ubsan`, `lint`. Branch protection requires all jobs.
- **Acceptance.** A pull request shows all jobs; a deliberate use-after-free in a throwaway branch fails the `asan` job.

### P5 (#5) pybind11 module, Makefile, quickstart, Dockerfile (`type:feature`)
- **Problem.** The differential harness needs the simulator importable from Python, and a newcomer needs one command.
- **Design.** pybind11 via `FetchContent`, module `warpsim._core` exposing `version()`. `Makefile` with `quickstart`, `test`, `bench`, `lint`, `sanitize`. `Dockerfile` with the toolchain. A pytest smoke test imports the module.
- **Acceptance.** `make quickstart` on a clean checkout builds, runs C++ and Python tests, and prints a placeholder-free status line; `docker build` succeeds.

## M1 ISA and assembler (`area:isa`)

### P6 (#12) WISA specification v0.1 (`type:docs`)
- **Problem.** There is no authoritative instruction list, so the assembler cannot be checked.
- **Design.** `docs/wisa-spec.md`: execution model, registers, special registers, predication, the 64-bit encoding layout, every instruction with syntax, semantics, and encoding, the reconvergence rule for `bra`, and the assembler directives (`.entry`, `.param`, `.shared`, labels).
- **Acceptance.** Every instruction that later exists in `opcode.hpp` appears in the specification, checked by a test that parses the specification table.

### P7 (#13) Instruction encoding and decoding (`type:feature`)
- **Problem.** Instructions need a fixed binary form that the core executes and the tools round-trip.
- **Design.** `Opcode` enum, `Instruction` struct with `encode()` and `decode()`, bit layout exactly as specified. Unit tests on field boundaries and on every opcode.
- **Acceptance.** `decode(encode(i)) == i` for every opcode and random field values; invalid opcodes decode to an error, never undefined behavior.

### P8a (#20) Assembler lexer (`type:feature`)
- **Problem.** The lexer is a self-contained module and is split from the parser to keep pull requests reviewable.
- **Design.** `lex(std::string_view)` producing tokens with one-based positions; comments dropped; every error message tested.
- **Acceptance.** Unit tests cover every token kind, positions, and every error.

### P8 (#14) Assembler (`type:feature`)
- **Problem.** Kernels are written as text.
- **Design.** Lexer, parser, symbol table, two-pass label resolution, predicate guards, directives, and a control-flow graph from which the immediate post-dominator of every conditional branch is computed and recorded as the reconvergence point. Errors carry line and column.
- **Acceptance.** Each shipped kernel and each torture kernel assembles; error tests cover undefined labels, bad operands, and unknown mnemonics.

### P8b (#19) Reconvergence analysis (`type:feature`)
- **Problem.** The post-dominator pass is a separate module from the assembler front end and is split out to keep pull requests reviewable.
- **Design.** `annotate_reconvergence(std::span<Instruction>)`: basic blocks, virtual exit node, post-dominators by iterative dataflow, immediate post-dominator written into every guarded `bra`.
- **Acceptance.** Unit tests reproduce the reconvergence PCs for if, if-else, nested depth 3, loops, early exit, `exit` in divergent code, and the specification worked example.

### P9 (#15) Disassembler and round-trip property tests (`type:test`)
- **Problem.** Without a disassembler, encodings are opaque and the specification cannot be checked against the assembler.
- **Design.** Canonical text form. Property test: for every opcode and random operands, assemble, disassemble, reassemble, and compare encodings.
- **Acceptance.** The property test enumerates the opcode table so that adding an opcode without a round-trip case fails the build.

### P10 (#16) Architecture diagram and README summary (`type:docs`)
- **Problem.** The component boundaries need a canonical visual.
- **Design.** `docs/architecture.html` drawn with the archify skill: dark theme, JetBrains Mono, semantic colors, a boundary box around the differential harness, legend outside all boundaries. README Mermaid kept in sync.
- **Acceptance.** The HTML renders standalone; the README diagram lists the same components.

## M2 Execution core (`area:core`)

### P11 (#26) Register file and thread and warp state (`type:feature`)
- **Problem.** Lanes need storage.
- **Design.** `RegisterFile` with 64 general and 8 predicate registers per lane, 32 lanes per warp, `std::array` storage, bounds-checked accessors. `WarpState` with PC, active mask, and divergence stack storage.
- **Acceptance.** Unit tests on read and write, lane isolation, and mask typing.

### P12 (#27) ALU, predication, special registers (`type:feature`)
- **Problem.** Instructions need semantics.
- **Design.** `execute(Instruction, WarpState, ...)` for arithmetic, logic, float, conversion, compare, and move instructions; predicate guards applied per lane; special registers read from launch context. Integer overflow is defined as wraparound; division by zero produces zero and sets no trap, as documented in the specification.
- **Acceptance.** Per-instruction unit tests including edge values; UBSan clean.

### P13 (#28) Active-mask divergence stack with reconvergence (`type:feature`)
- **Problem.** Divergent branches must execute both paths and rejoin correctly.
- **Design.** On a divergent `bra`, push the reconvergence PC and the fall-through mask, continue with the taken mask; at the reconvergence PC, pop and run the deferred path; when both paths have arrived, merge. Documented invariants: stack entries are ordered by nesting, a lane is active in at most one pending entry, and `exit` removes a lane from every pending mask.
- **Acceptance.** Unit tests for if, if-else, nested depth 3, loops with divergent exits, and `exit` inside divergent code.

### P14 (#29) Warp scheduler, launch, barrier, and minimal memory (`type:feature`)
- **Problem.** A grid must be executed.
- **Design.** `Device::launch(program, grid, block, params)`: blocks executed in order, warps of a block under round-robin selection, each warp issuing one instruction per scheduling step. Deterministic ordering.
- **Acceptance.** A multi-block kernel writes every element; two runs produce identical results.

### P14b (#30) Python bindings for assemble, Device, and launch (`type:feature`)
- **Problem.** Torture kernels and all later kernels are checked from Python.
- **Design.** `assemble`, `Program`, `Device` with `write`, `read`, `launch`; faults raise `SimFault`.
- **Acceptance.** pytest runs vecadd through the bindings and matches NumPy.

### P15 (#31) Divergence-torture kernels (`type:test`)
- **Problem.** Divergence correctness must be proven on data-dependent control flow.
- **Design.** Kernels under `kernels/torture/` with nesting to depth 3, data-dependent branches, loops with divergent trip counts, and early `exit`. NumPy golden models. Randomized inputs in CI.
- **Acceptance.** Every torture kernel matches the golden model bit for bit across 100 seeds.

## M3 Memory system (`area:memory`)

### P16 (#38) Global memory with coalescing analyzer (`type:feature`)
- **Problem.** Kernels need global loads and stores, and the access pattern must be observable.
- **Design.** Byte-addressed `GlobalMemory` with bounds checking. Per warp memory instruction the analyzer counts distinct 128-byte segments touched by the active lanes. Counters for segments and transactions.
- **Acceptance.** Unit tests: contiguous access touches 1 segment per 32 words, stride 2 touches 2, stride 32 words touches 32; out-of-bounds access raises a simulator fault with the lane and address.

### P17 (#39) Shared memory with bank-conflict counter (`type:feature`)
- **Problem.** Tiled kernels depend on shared memory and its conflict behavior.
- **Design.** Per block `SharedMemory`, 32 banks of 4 bytes. For each warp access, conflict degree is the maximum over banks of the number of distinct addresses mapped to that bank; identical addresses broadcast. Counters for accesses and conflicting accesses.
- **Acceptance.** Unit tests: stride 1 degree 1, stride 2 degree 2, stride 32 degree 32, broadcast degree 1.

### P18 Block barrier (delivered in #29 with the scheduler, no separate issue)
- **Problem.** Shared-memory kernels need `bar.sync`.
- **Design.** A warp arriving at `bar.sync` parks until all warps of the block arrive; the scheduler skips parked warps. A barrier inside divergent code is a specification-defined fault.
- **Acceptance.** Producer and consumer test across warps; deadlock is detected and reported when a warp exits before a barrier it would have been counted in.

### P19 (#40) Memory-pattern kernels (`type:test`)
- **Problem.** The analyzers need end-to-end confirmation.
- **Design.** Kernels under `kernels/patterns/` for coalesced, strided, and transposed access, and for conflict-free and conflicting shared access, with expected counter values.
- **Acceptance.** Counter expectations hold exactly in CI.

## M4 Kernels and differential harness (`area:kernels`)

### P20 (#44) Harness infrastructure and vector add (`type:feature`)
- **Problem.** The Python side needs launch, memory upload and download, counters, and a seeded test pattern.
- **Design.** pybind11 bindings for `Device`, `assemble`, memory buffers as NumPy arrays, counters as a dictionary. `kernels/vecadd.wisa`, golden model, pytest with seeded randomized sizes and values.
- **Acceptance.** Vector add matches bit for bit for integer and within tolerance for float across 100 seeds.

### P21 (#45) Block reduction kernel (`type:feature`)
- **Problem.** A divergent, barrier-using kernel with shared memory.
- **Design.** Tree reduction per block with halving active lanes; per-block partial sums compared to the golden model.
- **Acceptance.** Matches across 100 seeds including sizes that are not powers of two.

### P22 (#46) Naive matmul kernel (`type:feature`)
- **Problem.** The uncoalesced baseline.
- **Design.** One lane per output element, inner loop over k, one operand strided.
- **Acceptance.** Matches within tolerance across 100 seeds; coalescing counters record the strided pattern.

### P23 (#47) Tiled matmul kernel (`type:feature`)
- **Problem.** The optimized comparison point.
- **Design.** Shared-memory tiles, two barriers per tile step, coalesced loads.
- **Acceptance.** Matches within tolerance across 100 seeds; bank-conflict counter is zero for the chosen layout.

### P24 (#48) Randomized differential suite in CI and first kernel reports (`type:test`)
- **Problem.** Correctness must be checked on every pull request and reported.
- **Design.** `python/tests/test_differential.py` runs all kernels across seeds; CI runs it; `python/warpsim/report.py` prints a per-kernel table of counters.
- **Acceptance.** CI job green; the pull request body leads with the report table from a real run.

## M5 Instrumentation, timing, report (`area:instrumentation`)

### P25 (#54) Counters, divergence statistics, occupancy (`type:feature`)
- **Problem.** The mechanisms need consistent, named observables.
- **Design.** `Counters` struct: instructions issued, per-class counts, divergent branches, reconvergence events, average active lanes, global segments, shared conflicts, barriers; occupancy as warps resident per block and lanes active per issue.
- **Acceptance.** Every counter is documented; the reduction kernel reports a halving active-lane series.

### P26 (#55) Coarse timing model with ordinal acceptance test (`type:feature`)
- **Problem.** Counters need to compose into a comparison that is ordinal only.
- **Design.** Cost units: 1 per issued instruction, plus per global segment, plus per shared conflict degree, plus serialized divergent paths. Documented as ordinal. `python/tests/test_timing_ordinal.py` asserts tiled ranks above naive and that the attribution (segments, conflicts, divergence) explains the gap.
- **Acceptance.** Ordinal test passes; no text anywhere implies cycle accuracy.

### P27 (#56) Determinism test and `make bench` (`type:test`)
- **Problem.** Reproducibility must be demonstrated, not asserted.
- **Design.** `python/tests/test_determinism.py` runs each kernel twice and compares outputs and counters; `make bench` runs naive against tiled and prints the attribution.
- **Acceptance.** Test in CI; `make bench` output is stable across runs.

### P28 (#57) Final writeup and README results (`type:docs`)
- **Problem.** The project needs a narrative that connects every statistic to a mechanism.
- **Design.** `docs/report.md` with the kernel table, the ordinal ranking, and an explanation of each counter; README results table filled from `make bench`.
- **Acceptance.** Every number is reproducible by stated commands; no placeholder, contraction, or em dash remains anywhere in the repository.

## Pull request rules

- One issue, one branch, one pull request. A pull request over roughly 400 reviewable lines is split by module boundary.
- Body leads with `Closes #<n>`, states the differential-suite and sanitizer status explicitly, and for M4 and M5 leads with the kernel report table from a real run.
- Commit comments via `gh api` explain encoding layout choices, mask-stack invariants, and bank-conflict counting rules.
- A substantive self-review comment, grounded in rerun tests, precedes merge. Squash merge with the pull request title as subject. A closing comment on the issue records post-merge evidence.
