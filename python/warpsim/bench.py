"""Naive against tiled matmul with the attribution visible.

Usage: ``python -m warpsim.bench [--sizes 32,48,64,96]``. Prints, for each
cube size, the ordinal cost breakdown of both kernels, the ranking, and the
mechanism that produces it. Every number is from the launches performed by
this process. Cost units are ordinal issue-slot units, never cycles.
"""

from __future__ import annotations

import argparse

import numpy as np

from warpsim import golden, kernels

DEFAULT_SIZES = [32, 48, 64, 96]


def run_pair(size: int, seed: int = 0):
    rng = np.random.default_rng(seed)
    a = rng.standard_normal((size, size), dtype=np.float32)
    b = rng.standard_normal((size, size), dtype=np.float32)
    naive = kernels.run_matmul_naive(a, b)
    tiled = kernels.run_matmul_tiled(a, b)
    rtol, atol = golden.matmul_tolerance(size)
    reference = golden.matmul(a, b)
    ok = bool(np.allclose(naive.outputs[0].reshape(size, size), reference, rtol=rtol, atol=atol)) and bool(
        np.allclose(tiled.outputs[0].reshape(size, size), reference, rtol=rtol, atol=atol)
    )
    return naive.stats, tiled.stats, ok


def render(size: int, naive: dict, tiled: dict, ok: bool) -> str:
    cn, ct = naive["cost"], tiled["cost"]
    lines = [
        f"### {size} x {size} x {size} (golden: {'both match' if ok else 'MISMATCH'})",
        "",
        "| Kernel | Issue slots | Global segments x 8 | Shared wavefronts | Total (ordinal) | Global segments | Shared conflicted | Barriers |",
        "|---|---:|---:|---:|---:|---:|---:|---:|",
        f"| `matmul_naive` | {cn['issue']} | {cn['global']} | {cn['shared']} | {cn['total']} | {naive['global_segments']} | {naive['shared_conflicted_accesses']} | {naive['barriers_completed']} |",
        f"| `matmul_tiled` | {ct['issue']} | {ct['global']} | {ct['shared']} | {ct['total']} | {tiled['global_segments']} | {tiled['shared_conflicted_accesses']} | {tiled['barriers_completed']} |",
        "",
    ]
    faster = "tiled" if ct["total"] < cn["total"] else "naive"
    saved = cn["global"] - ct["global"]
    extra = (ct["issue"] - cn["issue"]) + (ct["shared"] - cn["shared"])
    lines.append(
        f"Ranking: `matmul_{faster}` ranks first. Attribution: tiled saves {saved} cost units on global "
        f"segments ({naive['global_segments']} against {tiled['global_segments']}) and pays {extra} extra in "
        f"issue slots and shared wavefronts; the segment saving is {saved / extra:.2f}x the extra cost."
    )
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--sizes", default=",".join(str(s) for s in DEFAULT_SIZES))
    parser.add_argument("--seed", type=int, default=0)
    args = parser.parse_args(argv)
    sizes = [int(s) for s in args.sizes.split(",")]
    print("# make bench: naive against tiled matmul (ordinal cost units, not cycles)\n")
    all_ok = True
    for size in sizes:
        naive, tiled, ok = run_pair(size, args.seed)
        all_ok = all_ok and ok and tiled["cost"]["total"] < naive["cost"]["total"]
        print(render(size, naive, tiled, ok))
        print()
    return 0 if all_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
