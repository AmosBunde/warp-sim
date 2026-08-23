"""Tiled matmul against the float64 golden model across 100 seeds, judged on
the same tolerance as the naive kernel."""

from __future__ import annotations

import numpy as np
import pytest

import warpsim
from warpsim import golden
from warpsim.harness import Output, launch

SEEDS = range(100)
TILE = 16


def run_tiled(a, b):
    m, k = a.shape
    _, n = b.shape
    program = warpsim.assemble_file(warpsim.kernels_dir() / "matmul_tiled.wisa")
    grid = ((n + TILE - 1) // TILE, (m + TILE - 1) // TILE)
    return launch(program, grid, (TILE, TILE), [a, b, Output(m * n, "float32"), m, n, k])


@pytest.mark.parametrize("seed", SEEDS)
def test_matmul_tiled(seed):
    rng = np.random.default_rng(seed)
    m, n, k = (int(v) for v in rng.integers(1, 97, size=3))
    a = rng.standard_normal((m, k), dtype=np.float32)
    b = rng.standard_normal((k, n), dtype=np.float32)
    result = run_tiled(a, b)
    c = result.outputs[0].reshape(m, n)
    rtol, atol = golden.matmul_tolerance(k)
    np.testing.assert_allclose(c, golden.matmul(a, b), rtol=rtol, atol=atol)
    assert result.stats["shared_conflicted_accesses"] == 0


def test_tiled_touches_fewer_segments_than_naive_and_has_no_conflicts():
    from test_matmul_naive import run_naive

    rng = np.random.default_rng(0)
    a = rng.standard_normal((64, 64), dtype=np.float32)
    b = rng.standard_normal((64, 64), dtype=np.float32)
    tiled = run_tiled(a, b).stats
    naive = run_naive(a, b).stats
    # 16 blocks x 8 warps x 4 tile steps: per step each warp loads 2 rows of
    # A (2 segments) and 2 rows of B (2 segments); plus the 2-segment store.
    assert tiled["global_segments"] == 16 * 8 * (4 * 4 + 2)
    assert tiled["global_segments"] < naive["global_segments"]
    assert tiled["shared_conflicted_accesses"] == 0
    # Two barriers per tile step per block.
    assert tiled["barriers_completed"] == 16 * 4 * 2
