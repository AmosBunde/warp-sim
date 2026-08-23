# Counters

Every number WarpSim reports is a count of a defined event or a ratio of such counts. None is a time. The coarse timing model (`docs/wisa-spec.md` scope, `docs/report.md`) composes these counts into ordinal cost units with stated weights; it does not convert them into cycles.

## Launch counters (`LaunchStats`, the Python stats dictionary)

| Counter | Event counted |
|---------|---------------|
| `instructions_issued` | A warp issued one instruction of any class. Divergent paths issue separately, so a common tail after a branch that did not reconverge is counted once per path. |
| `alu_instructions` | Issues of arithmetic, logic, compare, move, conversion, and special register instructions. |
| `memory_instructions` | Issues of `ld.global`, `st.global`, `ld.shared`, `st.shared`, `ld.param`. |
| `control_instructions` | Issues of `bra` and `exit`. |
| `barrier_instructions` | Issues of `bar.sync`. |
| `divergent_branches` | A guarded `bra` executed with some lanes taking and some falling through (specification 8.3 rule 2). |
| `reconvergence_events` | A path arrived at its recorded reconvergence point and the stack was popped (rule 3), counted for both the first and the second arrival. |
| `barriers_completed` | Every unfinished warp of a block was parked at `bar.sync` and all were released. |
| `active_lane_sum` | Lanes that executed the instruction (active mask and guard), summed over issues. |
| `active_lane_histogram[k]` | Issues on which exactly `k` lanes executed, for `k` in 0 through 32. |
| `blocks_executed` | Blocks run to completion. |
| `warps_launched` | Warps created, summed over blocks (a block of 35 lanes launches 2). |
| `lanes_launched` | Block size times blocks. |

## Memory counters (`MemoryStats`, flattened into the same dictionary)

| Counter | Event counted |
|---------|---------------|
| `global_loads`, `global_stores` | Warp-level `ld.global` and `st.global` issues with at least one executing lane. |
| `global_lane_accesses` | Executing lanes summed over those issues. |
| `global_segments` | Distinct 128-byte segments (`address / 128`) touched by the executing lanes of one global access, summed over accesses. A coalesced 32-word access adds 1; a 32-word stride adds 32. |
| `shared_accesses` | Warp-level `ld.shared` and `st.shared` issues with at least one executing lane. |
| `shared_lane_accesses` | Executing lanes summed over those issues. |
| `shared_wavefronts` | Bank-conflict degree summed over shared accesses. The degree is the maximum over the 32 four-byte banks of the number of distinct addresses directed at the bank; identical addresses broadcast. |
| `shared_conflicted_accesses` | Shared accesses whose degree exceeds 1. |

## Derived statistics (`warpsim.stats`)

| Statistic | Definition |
|-----------|------------|
| `average_active_lanes` | `active_lane_sum / instructions_issued` |
| `lane_utilization` | `active_lane_sum / (32 x instructions_issued)`: the fraction of issue slots that carried an executing lane. Divergence and ragged warps lower it. |
| `warps_per_block` | `warps_launched / blocks_executed`: the resident warp count of the single modeled multiprocessor while a block runs. |
| `instruction_mix` | Fraction of issues per class. |
| `halving_series` | Lane counts present in the histogram, descending; a tree reduction shows 32, 16, 8, 4, 2, 1. |
