"""NumPy golden models. These are authoritative: a disagreement between the
simulator and a function here is a simulator bug until a written analysis in
the pull request proves otherwise.

Integer models use int32 wraparound semantics throughout, matching the
specification: arithmetic is performed in NumPy int32 with overflow allowed.
"""

from __future__ import annotations

import numpy as np

_I32 = np.int32


def _wrap(values: np.ndarray) -> np.ndarray:
    """Reduces int64 values to int32 two's complement, as the simulator does."""
    return (values.astype(np.int64) & 0xFFFFFFFF).astype(np.uint32).view(_I32)


def vecadd(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    """Golden model of kernels/vecadd.wisa: one IEEE binary32 addition per element."""
    return a.astype(np.float32) + b.astype(np.float32)


def vecadd_s32(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    """Golden model of kernels/vecadd_s32.wisa: int32 addition with wraparound."""
    return _wrap(a.astype(np.int64) + b.astype(np.int64))


def torture_nested(x: np.ndarray) -> np.ndarray:
    """Golden model of kernels/torture/nested.wisa."""
    x = x.astype(_I32)
    out = np.zeros(len(x), dtype=np.int64)
    for i, v in enumerate(x.tolist()):
        acc = 0
        if v & 1:
            acc += 1
            if v < 100:
                acc -= 7
            else:
                acc += 20
                if v & 4:
                    acc += 300
            acc *= 3
        else:
            acc += 2
            if v > 50:
                acc = v - acc
            else:
                acc = acc * v
        out[i] = acc & 0xFFFFFFFF
    return _wrap(out)


def torture_loops(x: np.ndarray) -> np.ndarray:
    """Golden model of kernels/torture/loops.wisa (vectorized)."""
    x = x.astype(np.int64)
    out = np.zeros(len(x), dtype=np.int64)
    for i, trips in enumerate(x.tolist()):
        k = np.arange(max(trips, 0), dtype=np.int64)
        out[i] = int((k * k + i).sum())
    return _wrap(out)


def torture_early_exit(x: np.ndarray, sentinel: int) -> np.ndarray:
    """Golden model of kernels/torture/early_exit.wisa.

    Lanes with x mod 4 == 3 retire without writing, so their output stays at
    `sentinel`.
    """
    x = x.astype(np.int64)
    out = np.full(len(x), sentinel, dtype=np.int64)
    for i, v in enumerate(x.tolist()):
        if v & 3 == 3:
            continue
        acc = 0
        k = 0
        while k < 64:
            acc += k
            if k > 0 and (k * 7 + v) % 13 == 0:
                break
            k += 1
        out[i] = acc
    return _wrap(out)


def torture_combined(x: np.ndarray) -> np.ndarray:
    """Golden model of kernels/torture/combined.wisa."""
    x = x.astype(np.int64)
    out = np.zeros(len(x), dtype=np.int64)
    mask32 = 0xFFFFFFFF

    def s32(v: int) -> int:
        v &= mask32
        return v - (1 << 32) if v & 0x80000000 else v

    for i, v0 in enumerate(x.tolist()):
        state = s32(v0)
        acc = 0
        k = 0
        while k < 32:
            if state & 1:
                state = s32(state * 3 + 1)
                acc = s32(acc + k)
                if state & 2:
                    acc = s32(acc ^ state)
                    if state & 4:
                        acc = s32(acc - 5)
            else:
                state = state >> 1  # arithmetic shift on Python ints
                acc = s32(acc + state)
                if state == 0:
                    break
            k += 1
        out[i] = acc
    return _wrap(out)
