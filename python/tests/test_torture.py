"""Divergence-torture kernels against NumPy golden models on randomized inputs.

Every kernel here is integer only and must match bit for bit. Sizes are chosen
so that the final warp of the final block is ragged.
"""

from __future__ import annotations

import numpy as np
import pytest

import warpsim
from warpsim import golden

SEEDS = range(100)
SENTINEL = -12345


def _launch(kernel: str, x: np.ndarray, *, sentinel: int | None = None, words=None):
    n = len(x)
    device = warpsim.Device(max(64 * 1024, n * 16))
    arena = warpsim.Arena(device)
    off_x = arena.upload(x.astype(np.int32))
    init = np.full(n, sentinel if sentinel is not None else 0, dtype=np.int32)
    off_out = arena.upload(init)
    program = warpsim.assemble_file(warpsim.kernels_dir() / "torture" / f"{kernel}.wisa")
    if words is not None:
        program = words(program)
    block = 96
    grid = (n + block - 1) // block
    stats = device.launch(program, (grid, 1), (block, 1), [off_x, off_out, n])
    return arena.download(off_out, n, "int32"), stats


def _random_size(rng: np.random.Generator) -> int:
    # Not a multiple of 32 so that the last warp is partial.
    return int(rng.integers(1, 700)) * 2 + 1


@pytest.mark.parametrize("seed", SEEDS)
def test_nested(seed):
    rng = np.random.default_rng(seed)
    x = rng.integers(-200, 400, size=_random_size(rng), dtype=np.int32)
    out, stats = _launch("nested", x)
    np.testing.assert_array_equal(out, golden.torture_nested(x))
    assert stats["divergent_branches"] > 0


@pytest.mark.parametrize("seed", SEEDS)
def test_loops(seed):
    rng = np.random.default_rng(seed)
    x = rng.integers(0, 64, size=_random_size(rng), dtype=np.int32)
    out, stats = _launch("loops", x)
    np.testing.assert_array_equal(out, golden.torture_loops(x))
    assert stats["divergent_branches"] > 0


@pytest.mark.parametrize("seed", SEEDS)
def test_early_exit(seed):
    rng = np.random.default_rng(seed)
    x = rng.integers(0, 1000, size=_random_size(rng), dtype=np.int32)
    out, stats = _launch("early_exit", x, sentinel=SENTINEL)
    np.testing.assert_array_equal(out, golden.torture_early_exit(x, SENTINEL))
    assert stats["divergent_branches"] > 0


@pytest.mark.parametrize("seed", SEEDS)
def test_combined(seed):
    rng = np.random.default_rng(seed)
    x = rng.integers(-(2**31), 2**31 - 1, size=_random_size(rng), dtype=np.int64).astype(np.int32)
    out, stats = _launch("combined", x)
    np.testing.assert_array_equal(out, golden.torture_combined(x))
    assert stats["divergent_branches"] > 0


def _patch_words(program, patch):
    words = list(program.words)
    patched = 0
    for pc, w in enumerate(words):
        opcode = w >> 56
        guarded = (w >> 55) & 1
        if opcode == 0x27 and guarded and pc > 7:
            words[pc] = patch(w)
            patched += 1
    assert patched > 0
    return warpsim.Program(program.entry, program.params, program.shared_bytes, words)


def test_suite_has_teeth_against_wrong_control_flow():
    """Flipping the guard polarity of every divergent branch must break the
    golden comparison; otherwise the comparison would not be testing control
    flow at all."""
    rng = np.random.default_rng(0)
    x = rng.integers(-200, 400, size=501, dtype=np.int32)
    out, _ = _launch("nested", x)
    np.testing.assert_array_equal(out, golden.torture_nested(x))
    with pytest.raises(AssertionError):
        out_bad, _ = _launch("nested", x, words=lambda p: _patch_words(p, lambda w: w ^ (1 << 54)))
        np.testing.assert_array_equal(out_bad, golden.torture_nested(x))


def test_reconvergence_points_affect_issue_count_not_results():
    """Specification 8.3 rule 4 makes results independent of the recorded
    reconvergence point: a path that never reaches it runs to exit and the
    deferred path resumes. What a wrong point changes is when lanes rejoin,
    which is visible as the common tail being issued once per path instead
    of once. Both facts are asserted here."""
    rng = np.random.default_rng(1)
    x = rng.integers(-200, 400, size=501, dtype=np.int32)
    out, stats = _launch("nested", x)
    none = 0xFFFF << 16
    out_none, stats_none = _launch(
        "nested", x, words=lambda p: _patch_words(p, lambda w: (w & ~0xFFFF0000) | none)
    )
    np.testing.assert_array_equal(out, golden.torture_nested(x))
    np.testing.assert_array_equal(out_none, golden.torture_nested(x))
    assert stats_none["instructions_issued"] > stats["instructions_issued"]
