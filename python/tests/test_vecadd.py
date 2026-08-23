"""Vector add against the golden model across 100 seeds."""

from __future__ import annotations

import numpy as np
import pytest

import warpsim
from warpsim import golden
from warpsim.harness import Output, grid_for, launch

SEEDS = range(100)
BLOCK = 128


@pytest.mark.parametrize("seed", SEEDS)
def test_vecadd_f32_exact(seed):
    rng = np.random.default_rng(seed)
    n = int(rng.integers(1, 5000))
    a = rng.standard_normal(n, dtype=np.float32) * 1000
    b = rng.standard_normal(n, dtype=np.float32)
    program = warpsim.assemble_file(warpsim.kernels_dir() / "vecadd.wisa")
    result = launch(program, grid_for(n, BLOCK), (BLOCK, 1), [a, b, Output(n, "float32"), n])
    # One IEEE addition per element in both implementations: exact equality.
    np.testing.assert_array_equal(result.outputs[0], golden.vecadd(a, b))
    assert result.stats["blocks_executed"] == grid_for(n, BLOCK)[0]


@pytest.mark.parametrize("seed", SEEDS)
def test_vecadd_s32_bit_exact(seed):
    rng = np.random.default_rng(seed)
    n = int(rng.integers(1, 5000))
    a = rng.integers(-(2**31), 2**31 - 1, size=n, dtype=np.int64).astype(np.int32)
    b = rng.integers(-(2**31), 2**31 - 1, size=n, dtype=np.int64).astype(np.int32)
    program = warpsim.assemble_file(warpsim.kernels_dir() / "vecadd_s32.wisa")
    result = launch(program, grid_for(n, BLOCK), (BLOCK, 1), [a, b, Output(n, "int32"), n])
    np.testing.assert_array_equal(result.outputs[0], golden.vecadd_s32(a, b))


def test_vecadd_does_not_touch_past_n():
    n = 100
    a = np.ones(n, dtype=np.float32)
    b = np.ones(n, dtype=np.float32)
    program = warpsim.assemble_file(warpsim.kernels_dir() / "vecadd.wisa")
    result = launch(program, grid_for(n, BLOCK), (BLOCK, 1), [a, b, Output(n + 28, "float32", fill=-1.0), n])
    out = result.outputs[0]
    np.testing.assert_array_equal(out[:n], np.full(n, 2.0, dtype=np.float32))
    np.testing.assert_array_equal(out[n:], np.full(28, -1.0, dtype=np.float32))
