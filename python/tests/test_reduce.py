"""Block reduction against the golden model across 100 seeds."""

from __future__ import annotations

import numpy as np
import pytest

import warpsim
from warpsim import golden
from warpsim.harness import Output, launch

SEEDS = range(100)
BLOCK = golden.REDUCE_BLOCK


def _run(x):
    n = len(x)
    blocks = (n + BLOCK - 1) // BLOCK
    program = warpsim.assemble_file(warpsim.kernels_dir() / "reduce.wisa")
    return launch(program, (blocks, 1), (BLOCK, 1), [x, Output(blocks, "int32"), n])


@pytest.mark.parametrize("seed", SEEDS)
def test_reduce_bit_exact(seed):
    rng = np.random.default_rng(seed)
    n = int(rng.integers(1, 3000))
    # Full int32 range so that wraparound occurs inside most blocks.
    x = rng.integers(-(2**31), 2**31 - 1, size=n, dtype=np.int64).astype(np.int32)
    result = _run(x)
    np.testing.assert_array_equal(result.outputs[0], golden.reduce_blocks(x))
    blocks = (n + BLOCK - 1) // BLOCK
    assert result.stats["barriers_completed"] == 9 * blocks  # 1 staging + 8 tree steps
    assert result.stats["divergent_branches"] > 0


def test_reduce_small_values_and_single_block():
    x = np.arange(1, 101, dtype=np.int32)
    result = _run(x)
    assert result.outputs[0].tolist() == [5050]
    assert result.stats["blocks_executed"] == 1
    # Shared traffic: 256 lanes store once (8 warps, degree 1), then per step
    # the active lanes below the stride do 2 loads and 1 store; the stride-1
    # access patterns are conflict free throughout.
    assert result.stats["shared_conflicted_accesses"] == 0
