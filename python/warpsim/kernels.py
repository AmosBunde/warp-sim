"""Launchers for the shipped kernels, shared by the tests and the report tool."""

from __future__ import annotations

import numpy as np

import warpsim
from warpsim.harness import LaunchResult, Output, grid_for, launch

TILE = 16
REDUCE_BLOCK = 256
VECADD_BLOCK = 128


def _program(name: str) -> warpsim.Program:
    return warpsim.assemble_file(warpsim.kernels_dir() / f"{name}.wisa")


def run_vecadd(a: np.ndarray, b: np.ndarray) -> LaunchResult:
    n = len(a)
    return launch(_program("vecadd"), grid_for(n, VECADD_BLOCK), (VECADD_BLOCK, 1),
                  [a, b, Output(n, "float32"), n])


def run_vecadd_s32(a: np.ndarray, b: np.ndarray) -> LaunchResult:
    n = len(a)
    return launch(_program("vecadd_s32"), grid_for(n, VECADD_BLOCK), (VECADD_BLOCK, 1),
                  [a, b, Output(n, "int32"), n])


def run_reduce(x: np.ndarray) -> LaunchResult:
    n = len(x)
    blocks = (n + REDUCE_BLOCK - 1) // REDUCE_BLOCK
    return launch(_program("reduce"), (blocks, 1), (REDUCE_BLOCK, 1), [x, Output(blocks, "int32"), n])


def _matmul(name: str, a: np.ndarray, b: np.ndarray) -> LaunchResult:
    m, k = a.shape
    _, n = b.shape
    grid = ((n + TILE - 1) // TILE, (m + TILE - 1) // TILE)
    return launch(_program(name), grid, (TILE, TILE), [a, b, Output(m * n, "float32"), m, n, k])


def run_matmul_naive(a: np.ndarray, b: np.ndarray) -> LaunchResult:
    return _matmul("matmul_naive", a, b)


def run_matmul_tiled(a: np.ndarray, b: np.ndarray) -> LaunchResult:
    return _matmul("matmul_tiled", a, b)
