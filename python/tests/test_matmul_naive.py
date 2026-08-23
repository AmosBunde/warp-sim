"""Naive matmul against the float64 golden model across 100 seeds."""

from __future__ import annotations

import numpy as np
import pytest

import warpsim
from warpsim import golden
from warpsim.harness import Output, launch

SEEDS = range(100)
TILE = 16


def run_naive(a, b):
    m, k = a.shape
    _, n = b.shape
    program = warpsim.assemble_file(warpsim.kernels_dir() / "matmul_naive.wisa")
    grid = ((n + TILE - 1) // TILE, (m + TILE - 1) // TILE)
    return launch(program, grid, (TILE, TILE), [a, b, Output(m * n, "float32"), m, n, k])


@pytest.mark.parametrize("seed", SEEDS)
def test_matmul_naive(seed):
    rng = np.random.default_rng(seed)
    m, n, k = (int(v) for v in rng.integers(1, 97, size=3))
    a = rng.standard_normal((m, k), dtype=np.float32)
    b = rng.standard_normal((k, n), dtype=np.float32)
    result = run_naive(a, b)
    c = result.outputs[0].reshape(m, n)
    rtol, atol = golden.matmul_tolerance(k)
    np.testing.assert_allclose(c, golden.matmul(a, b), rtol=rtol, atol=atol)


def test_segment_counts_follow_the_access_pattern():
    # A 16 x 16 block is 8 warps; each warp covers 2 rows x 16 columns.
    # M = N = 32, K = 1: per warp, the A load reads 2 adjacent words (1
    # segment), the B load reads 16 consecutive words of one row (1 segment),
    # and the C store covers 2 rows 128 bytes apart (2 segments): 4 per warp,
    # 8 warps x 4 blocks = 128.
    a = np.ones((32, 1), dtype=np.float32)
    b = np.ones((1, 32), dtype=np.float32)
    result = run_naive(a, b)
    assert result.stats["global_segments"] == 128

    # N = 64: same per-warp counts (C rows are 256 bytes apart, still 2
    # segments), 8 blocks: 256.
    a = np.ones((32, 1), dtype=np.float32)
    b = np.ones((1, 64), dtype=np.float32)
    result = run_naive(a, b)
    assert result.stats["global_segments"] == 256

    # K = 8, M = N = 32: per k the A load reads 2 words 32 bytes apart (1
    # segment) and the B load reads 16 consecutive words of row k (1 segment):
    # 16 over the loop, plus the 2-segment store = 18 per warp; 32 warps: 576.
    a = np.ones((32, 8), dtype=np.float32)
    b = np.ones((8, 32), dtype=np.float32)
    result = run_naive(a, b)
    assert result.stats["global_segments"] == 576
