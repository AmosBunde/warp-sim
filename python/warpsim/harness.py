"""Launch helper shared by every kernel test and by the report tool.

The harness is deliberately thin: it allocates, uploads, launches, and
downloads. Comparisons live in the tests, next to the tolerance they use.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any

import numpy as np

import warpsim


@dataclass(frozen=True)
class Output:
    """An output buffer argument: allocated before launch, downloaded after."""

    count: int
    dtype: str = "int32"
    fill: int | float | None = None


@dataclass(frozen=True)
class LaunchResult:
    outputs: list[np.ndarray]
    stats: dict[str, int]


def launch(
    program: warpsim.Program,
    grid: tuple[int, int],
    block: tuple[int, int],
    args: list[Any],
    *,
    global_bytes: int | None = None,
) -> LaunchResult:
    """Runs ``program`` with ``args`` in kernel parameter order.

    Each argument is an input ``np.ndarray`` (uploaded, its byte offset is
    passed), an :class:`Output` (allocated, offset passed, downloaded after
    the launch), or an ``int`` scalar (passed as is).
    """
    if global_bytes is None:
        needed = 0
        for arg in args:
            if isinstance(arg, np.ndarray):
                needed += arg.nbytes + 128
            elif isinstance(arg, Output):
                needed += arg.count * np.dtype(arg.dtype).itemsize + 128
        global_bytes = max(4096, needed * 2)
    device = warpsim.Device(global_bytes)
    arena = warpsim.Arena(device)
    params: list[int] = []
    outputs: list[tuple[int, Output]] = []
    for arg in args:
        if isinstance(arg, np.ndarray):
            params.append(arena.upload(arg))
        elif isinstance(arg, Output):
            if arg.fill is None:
                offset = arena.alloc(arg.count * np.dtype(arg.dtype).itemsize)
            else:
                offset = arena.upload(np.full(arg.count, arg.fill, dtype=arg.dtype))
            outputs.append((offset, arg))
            params.append(offset)
        elif isinstance(arg, (int, np.integer)):
            params.append(int(arg) & 0xFFFFFFFF)
        else:
            raise TypeError(f"unsupported argument {arg!r}")
    stats = device.launch(program, grid, block, params)
    downloaded = [arena.download(offset, out.count, out.dtype) for offset, out in outputs]
    return LaunchResult(downloaded, stats)


def grid_for(n: int, block: int) -> tuple[int, int]:
    """One-dimensional grid covering ``n`` elements with ``block`` lanes each."""
    return ((n + block - 1) // block, 1)
