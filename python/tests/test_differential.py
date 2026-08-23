"""Aggregate differential entry point over the four shipped kernels.

The per-kernel files hold the detailed 100-seed suites; this file adds a
second, disjoint seed range through the shared launchers so that the report
tool's code path is itself under test.
"""

from __future__ import annotations

import numpy as np
import pytest

from warpsim import golden, kernels, report

SEEDS = range(1000, 1025)


@pytest.mark.parametrize("seed", SEEDS)
@pytest.mark.parametrize("name", sorted(report.CHECKS))
def test_kernel_matches_golden(name, seed):
    rng = np.random.default_rng(seed)
    size = int(rng.integers(1, 200)) if name.startswith("matmul") is False else int(rng.integers(1, 70))
    run = report.CHECKS[name](size, seed)
    assert run.matches_golden, report.render([run])


def test_report_renders_every_kernel():
    runs = report.run_reports(list(report.CHECKS), size=None, seed=3)
    text = report.render(runs)
    for name in report.CHECKS:
        assert f"`{name}`" in text
    assert "MISMATCH" not in text
    assert report.main(["--kernel", "matmul_tiled", "--size", "32"]) == 0


def test_tiled_uses_fewer_segments_than_naive_on_equal_inputs():
    rng = np.random.default_rng(5)
    a = rng.standard_normal((48, 48), dtype=np.float32)
    b = rng.standard_normal((48, 48), dtype=np.float32)
    naive = kernels.run_matmul_naive(a, b)
    tiled = kernels.run_matmul_tiled(a, b)
    assert tiled.stats["global_segments"] < naive.stats["global_segments"]
    np.testing.assert_allclose(tiled.outputs[0], naive.outputs[0], rtol=1e-5, atol=golden.matmul_tolerance(48)[1])
