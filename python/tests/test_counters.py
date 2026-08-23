"""Counter semantics on the shipped kernels."""

from __future__ import annotations

import numpy as np

from warpsim import kernels, stats


def test_reduction_histogram_shows_halving_active_lanes():
    x = np.arange(256, dtype=np.int32)
    s = kernels.run_reduce(x).stats
    series = stats.halving_series(s)
    # The first warp of the block runs the tree steps with 16, 8, 4, 2, 1 lanes.
    for lanes in (32, 16, 8, 4, 2, 1):
        assert lanes in series, series
    assert s["active_lane_histogram"][1] > 0
    assert s["reconvergence_events"] > 0
    assert s["divergent_branches"] > 0
    assert s["barrier_instructions"] == 9 * 8  # 9 barriers x 8 warps
    assert s["barriers_completed"] == 9


def test_instruction_classes_sum_to_issues():
    rng = np.random.default_rng(0)
    a = rng.standard_normal((40, 24), dtype=np.float32)
    b = rng.standard_normal((24, 40), dtype=np.float32)
    for run in (kernels.run_matmul_naive, kernels.run_matmul_tiled):
        s = run(a, b).stats
        classes = s["alu_instructions"] + s["memory_instructions"] + s["control_instructions"] + s["barrier_instructions"]
        assert classes == s["instructions_issued"]
        mix = stats.instruction_mix(s)
        assert abs(sum(mix.values()) - 1.0) < 1e-9
        assert 0.0 < stats.lane_utilization(s) <= 1.0
        assert sum(s["active_lane_histogram"]) == s["instructions_issued"]
        assert s["lanes_launched"] == 256 * s["blocks_executed"]


def test_vecadd_utilization_reflects_ragged_tail():
    a = np.ones(33, dtype=np.float32)
    b = np.ones(33, dtype=np.float32)
    s = kernels.run_vecadd(a, b).stats
    # 128-lane block, n = 33. Warp 0: the bounds branch issues with 0
    # executing lanes (guard false everywhere). Warp 1: the branch issues
    # with 31 executing lanes, the taken lanes reach `done` and wait, the
    # 11-instruction store path runs with one lane, and the final exit
    # issues with all 32 after reconvergence. Warps 2 and 3 branch and exit
    # whole.
    assert s["warps_launched"] == 4
    assert s["active_lane_histogram"][0] == 1
    assert s["active_lane_histogram"][31] == 1
    assert s["active_lane_histogram"][1] == 11
    assert s["reconvergence_events"] == 2
    assert 0.5 < stats.lane_utilization(s) < 1.0
    assert stats.warps_per_block(s) == 4.0
    assert s["divergent_branches"] == 1
