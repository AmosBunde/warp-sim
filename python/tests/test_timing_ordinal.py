"""Ordinal acceptance test of the coarse timing model.

The model composes counts into cost units with stated weights. The only
claim made of it is the ranking, and the ranking must be attributable to an
observable mechanism: here, the global segment count. No number in this file
is a cycle count.
"""

from __future__ import annotations

import numpy as np
import pytest

from warpsim import kernels
from warpsim._core import COST_WEIGHTS

SIZES = [32, 48, 64, 96]


def _pair(size, seed=0):
    rng = np.random.default_rng(seed)
    a = rng.standard_normal((size, size), dtype=np.float32)
    b = rng.standard_normal((size, size), dtype=np.float32)
    return kernels.run_matmul_naive(a, b).stats, kernels.run_matmul_tiled(a, b).stats


@pytest.mark.parametrize("size", SIZES)
def test_tiled_ranks_above_naive_and_the_gap_is_the_segments(size):
    naive, tiled = _pair(size)
    cn, ct = naive["cost"], tiled["cost"]
    # Ranking.
    assert ct["total"] < cn["total"]
    # Attribution: the tiled kernel pays more issue slots and shared
    # wavefronts; the naive kernel pays far more global segments, and that
    # difference exceeds everything the tiled kernel pays extra.
    extra_paid_by_tiled = (ct["issue"] - cn["issue"]) + (ct["shared"] - cn["shared"])
    saved_by_tiled_on_global = cn["global"] - ct["global"]
    assert saved_by_tiled_on_global > extra_paid_by_tiled > 0
    assert tiled["global_segments"] < naive["global_segments"] / 4
    assert tiled["shared_conflicted_accesses"] == 0


def test_cost_is_composed_from_counts_with_the_stated_weights():
    naive, _ = _pair(32)
    w = COST_WEIGHTS
    assert naive["cost"]["issue"] == naive["instructions_issued"] * w["issue_slot"]
    assert naive["cost"]["global"] == naive["global_segments"] * w["global_segment"]
    assert naive["cost"]["shared"] == naive["shared_wavefronts"] * w["shared_wavefront"]
    assert naive["cost"]["total"] == sum(naive["cost"][k] for k in ("issue", "global", "shared"))
    assert w == {"issue_slot": 1, "global_segment": 8, "shared_wavefront": 1}
