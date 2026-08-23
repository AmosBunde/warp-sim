"""Determinism: the same kernel, inputs, and seed produce identical outputs
and identical counters on two runs. Every counter is compared, including the
histogram and the cost breakdown."""

from __future__ import annotations

import numpy as np
import pytest

from warpsim import kernels

SEEDS = [0, 1, 2]


def _inputs(name, seed):
    rng = np.random.default_rng(seed)
    if name == "vecadd":
        n = int(rng.integers(1, 3000))
        return [rng.standard_normal(n, dtype=np.float32), rng.standard_normal(n, dtype=np.float32)]
    if name == "reduce":
        n = int(rng.integers(1, 3000))
        return [rng.integers(-(2**31), 2**31 - 1, size=n, dtype=np.int64).astype(np.int32)]
    size = int(rng.integers(1, 70))
    return [rng.standard_normal((size, size), dtype=np.float32), rng.standard_normal((size, size), dtype=np.float32)]


RUNNERS = {
    "vecadd": kernels.run_vecadd,
    "reduce": kernels.run_reduce,
    "matmul_naive": kernels.run_matmul_naive,
    "matmul_tiled": kernels.run_matmul_tiled,
}


@pytest.mark.parametrize("seed", SEEDS)
@pytest.mark.parametrize("name", sorted(RUNNERS))
def test_two_runs_are_identical(name, seed):
    args = _inputs(name, seed)
    first = RUNNERS[name](*[a.copy() for a in args])
    second = RUNNERS[name](*[a.copy() for a in args])
    for x, y in zip(first.outputs, second.outputs, strict=True):
        np.testing.assert_array_equal(x, y)
    assert first.stats == second.stats
    assert set(first.stats) >= {"instructions_issued", "active_lane_histogram", "cost", "global_segments"}


def test_bench_output_is_stable_and_ranks_tiled_first(capsys):
    from warpsim import bench

    assert bench.main(["--sizes", "32,48"]) == 0
    first = capsys.readouterr().out
    assert bench.main(["--sizes", "32,48"]) == 0
    second = capsys.readouterr().out
    assert first == second
    assert first.count("`matmul_tiled` ranks first") == 2
