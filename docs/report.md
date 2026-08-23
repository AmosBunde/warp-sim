# WarpSim Report

This report connects every statistic the simulator prints to the mechanism that produces it. Every number below comes from a run of the built simulator and is reproduced by the command named next to it. Cost units are ordinal issue-slot units under the weights in `include/warpsim/instr/timing.hpp`; nothing here is a cycle count, and the project makes no claim of cycle accuracy.

## 1. The kernel table

Command: `make report` (equivalently `PYTHONPATH=python python -m warpsim.report`).

| Kernel | Problem | Golden | Instructions issued | Divergent branches | Barriers | Global segments | Shared wavefronts | Shared conflicted | Lane utilization | Cost units (ordinal) |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `vecadd` | n = 4096 | match | 2560 | 0 | 0 | 384 | 0 | 0 | 0.950 | 5632 |
| `reduce` | n = 4096 | match | 9712 | 80 | 144 | 144 | 720 | 0 | 0.909 | 11584 |
| `matmul_naive` | 64 x 64 x 64 | match | 78080 | 0 | 0 | 24832 | 0 | 0 | 0.892 | 276736 |
| `matmul_tiled` | 64 x 64 x 64 | match | 85888 | 0 | 128 | 2304 | 17408 | 0 | 0.961 | 121728 |

The Golden column is computed in the same process from the same launch: float32 vector add is compared exactly (one IEEE addition per element on both sides), the reduction is compared bit for bit as int32 with wraparound, and both matmuls are compared against the float64 product within the tolerance derived in `python/warpsim/golden.py` (`rtol = 1e-5`, `atol = 4 k 2^-24 max(1, sqrt(k))`). The randomized differential suite applies the same comparisons across 100 seeds per kernel on every pull request.

## 2. What each column counts

The definitions are in `docs/counters.md`; this section says what each one reveals about the four kernels.

**Instructions issued.** One warp-level issue of any instruction. The tiled matmul issues more instructions than the naive one on the same problem (85888 against 78080) because it runs the tile-staging code and two barriers per tile step in addition to the inner product. An instruction count alone would rank the naive kernel first, which is why the timing model weights memory segments.

**Divergent branches.** A guarded branch that split its warp. The matmuls show 0 because every lane of a warp takes the same loop decisions on an aligned 64-cube; the reduction shows 80 (16 blocks, and in each block the first warp splits at strides 16, 8, 4, 2, and 1). Divergence costs nothing by itself in the model: its cost appears in the issue count, because a split warp issues the two paths separately.

**Barriers.** Barrier completions per launch. The reduction needs 9 per block (one staging barrier and eight tree steps, 144 over 16 blocks); the tiled matmul needs 2 per tile step per block (4 steps times 16 blocks times 2, 128).

**Global segments.** Distinct 128-byte segments touched by the executing lanes of one global access, summed. This is the column that separates the matmuls: the naive kernel's inner loop reads one element of A and one of B per lane per k, and the B reads of a warp cover two rows 256 bytes apart, so the loop touches 24832 segments on the 64-cube; the tiled kernel stages each 16 by 16 tile with one coalesced load per lane per tile step and touches 2304. The exact derivations for three geometries are in `python/tests/test_matmul_naive.py` and `python/tests/test_matmul_tiled.py`.

**Shared wavefronts and shared conflicted accesses.** The bank-conflict degree summed over shared accesses, and the number of accesses with degree above 1. The tiled matmul performs 17408 shared accesses at degree 1 each (17408 wavefronts, 0 conflicted): its reads of the A tile broadcast within a row of the warp while the two rows of the warp land in different banks, and its reads of the B tile are 16 consecutive words broadcast to both half-warps. The reduction's tree is conflict free as well. The pattern kernels in `kernels/patterns/` show the other cases: a stride of 32 words costs 32 wavefronts per access, and a 32 by 32 transpose without padding costs 1056 wavefronts for its 32 column reads.

**Lane utilization.** Executing lanes divided by 32 times issues. The reduction's 0.909 is the visible cost of its halving tree: the active-lane histogram of a 4096-element reduction shows 8656 issues with 32 lanes, 112 with 16, 96 each with 8, 4, and 2, and 208 with 1 lane. The naive matmul's 0.892 comes from ragged lanes at the bounds checks and from the bounds-check `exit` paths; the tiled kernel keeps out-of-range lanes alive through the barriers and reaches 0.961.

**Cost units (ordinal).** `issue x 1 + global segments x 8 + shared wavefronts x 1`. The weights are stated in the header and exposed as `warpsim._core.COST_WEIGHTS`; the only property claimed of the total is the ranking it produces, and section 3 shows that ranking is produced by the segment column.

## 3. The ranking and its attribution

Command: `make bench` (equivalently `PYTHONPATH=python python -m warpsim.bench`).

| Cube | Naive total | Tiled total | Segments saved by tiled (x 8) | Extra issue and shared paid by tiled | Ratio |
|---:|---:|---:|---:|---:|---:|
| 32 | 35392 | 16032 | 22528 | 3168 | 7.11 |
| 48 | 117648 | 52272 | 76032 | 10656 | 7.14 |
| 64 | 276736 | 121728 | 180224 | 25216 | 7.15 |
| 96 | 926784 | 403488 | 608256 | 84960 | 7.16 |

On every size the tiled kernel ranks first, and on every size the cost it saves on global segments is about seven times the cost it pays in extra issue slots and shared wavefronts. The acceptance test `python/tests/test_timing_ordinal.py` asserts both facts, so a change to the model that ranked the kernels for a different reason would fail CI. The ranking holds for any global-segment weight above about 1.12 issue slots on the 64-cube; 8 is not a measured number, it is a round figure comfortably above that threshold, and the report states it as such.

## 4. What the simulator does not model

- No pipeline, no latency, no overlap between memory and arithmetic, no cache, no DRAM scheduling, no clock. The cost units are not convertible to time.
- One streaming multiprocessor, blocks executed one at a time, warps issued round robin one instruction per pass. `warps_per_block` is the resident warp count of that single multiprocessor; there is no occupancy model across blocks.
- Memory is a flat byte space with word access; there is no cache hierarchy, so coalescing is reported as segment counts rather than as traffic through a cache.

## 5. How correctness is established

- The NumPy golden models in `python/warpsim/golden.py` are authoritative; the one disagreement that occurred during development was a golden-model overflow in NumPy int32 arithmetic (pull request #37), resolved by computing in int64 and reducing to int32, not by widening a tolerance.
- The divergence-torture kernels (`kernels/torture/`) run 100 seeds each against their golden models in CI; the suite also proves its own teeth (a flipped guard breaks the comparison) and records that results are independent of the recorded reconvergence point while the instruction count is not (specification section 8.4).
- Determinism is tested by running every shipped kernel twice and comparing outputs and every counter, including the histogram and the cost breakdown.
- AddressSanitizer and UndefinedBehaviorSanitizer jobs run on every pull request and block merge; clang-format and clang-tidy are enforced; warnings are errors on GCC 13 and Clang 18 in debug and release.

## 6. Reproducing everything in this report

```sh
make quickstart   # build, C++ tests, Python tests including the differential suite, tiled report row
make report       # section 1
make bench        # section 3
PYTHONPATH=python python -m pytest python/tests/test_timing_ordinal.py   # the ordinal acceptance test
```
