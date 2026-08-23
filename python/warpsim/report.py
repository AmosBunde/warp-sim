"""Kernel reports: one Markdown row per kernel from a real run.

Usage: ``python -m warpsim.report [--kernel NAME] [--size N]``. Every number
printed comes from the launch performed by this process; nothing is cached or
estimated. The columns are counts of the events defined in
``docs/wisa-spec.md`` section 2.5 and ``LaunchStats``; none is a time.
"""

from __future__ import annotations

import argparse
from collections.abc import Callable
from dataclasses import dataclass

import numpy as np

from warpsim import golden, kernels, stats
from warpsim.harness import LaunchResult

COLUMNS = [
    ("instructions_issued", "Instructions issued"),
    ("divergent_branches", "Divergent branches"),
    ("barriers_completed", "Barriers"),
    ("global_segments", "Global segments"),
    ("shared_wavefronts", "Shared wavefronts"),
    ("shared_conflicted_accesses", "Shared conflicted"),
]


@dataclass(frozen=True)
class KernelRun:
    name: str
    problem: str
    result: LaunchResult
    matches_golden: bool


def _check_vecadd(size: int, seed: int) -> KernelRun:
    rng = np.random.default_rng(seed)
    a = rng.standard_normal(size, dtype=np.float32)
    b = rng.standard_normal(size, dtype=np.float32)
    result = kernels.run_vecadd(a, b)
    ok = bool(np.array_equal(result.outputs[0], golden.vecadd(a, b)))
    return KernelRun("vecadd", f"n = {size}", result, ok)


def _check_reduce(size: int, seed: int) -> KernelRun:
    rng = np.random.default_rng(seed)
    x = rng.integers(-(2**31), 2**31 - 1, size=size, dtype=np.int64).astype(np.int32)
    result = kernels.run_reduce(x)
    ok = bool(np.array_equal(result.outputs[0], golden.reduce_blocks(x)))
    return KernelRun("reduce", f"n = {size}", result, ok)


def _check_matmul(run: Callable, name: str, size: int, seed: int) -> KernelRun:
    rng = np.random.default_rng(seed)
    a = rng.standard_normal((size, size), dtype=np.float32)
    b = rng.standard_normal((size, size), dtype=np.float32)
    result = run(a, b)
    rtol, atol = golden.matmul_tolerance(size)
    ok = bool(np.allclose(result.outputs[0].reshape(size, size), golden.matmul(a, b), rtol=rtol, atol=atol))
    return KernelRun(name, f"{size} x {size} x {size}", result, ok)


CHECKS: dict[str, Callable[[int, int], KernelRun]] = {
    "vecadd": _check_vecadd,
    "reduce": _check_reduce,
    "matmul_naive": lambda size, seed: _check_matmul(kernels.run_matmul_naive, "matmul_naive", size, seed),
    "matmul_tiled": lambda size, seed: _check_matmul(kernels.run_matmul_tiled, "matmul_tiled", size, seed),
}

DEFAULT_SIZES = {"vecadd": 4096, "reduce": 4096, "matmul_naive": 64, "matmul_tiled": 64}


def run_reports(names: list[str], size: int | None = None, seed: int = 0) -> list[KernelRun]:
    return [CHECKS[name](size or DEFAULT_SIZES[name], seed) for name in names]


def header() -> str:
    cols = " | ".join(label for _, label in COLUMNS)
    rule = "|".join("---:" for _ in COLUMNS)
    return f"| Kernel | Problem | Golden | {cols} | Lane utilization |\n|---|---|---|{rule}|---:|"


def row(run: KernelRun) -> str:
    values = " | ".join(str(run.result.stats[key]) for key, _ in COLUMNS)
    utilization = f"{stats.lane_utilization(run.result.stats):.3f}"
    return f"| `{run.name}` | {run.problem} | {'match' if run.matches_golden else 'MISMATCH'} | {values} | {utilization} |"


def render(runs: list[KernelRun]) -> str:
    return "\n".join([header(), *(row(r) for r in runs)])


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--kernel", choices=sorted(CHECKS), action="append", help="kernel to report (repeatable)")
    parser.add_argument("--size", type=int, help="problem size override")
    parser.add_argument("--seed", type=int, default=0)
    args = parser.parse_args(argv)
    names = args.kernel or list(CHECKS)
    runs = run_reports(names, args.size, args.seed)
    print(render(runs))
    return 0 if all(r.matches_golden for r in runs) else 1


if __name__ == "__main__":
    raise SystemExit(main())
