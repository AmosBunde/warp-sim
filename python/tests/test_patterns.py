"""Memory-pattern kernels with counter values derived by hand in the comments.

Every kernel uses one block so that the expectations are exact per warp.
"""

from __future__ import annotations

import numpy as np

import warpsim


def _run(name, block, params_builder, global_bytes=64 * 1024):
    device = warpsim.Device(global_bytes)
    arena = warpsim.Arena(device)
    params, check = params_builder(arena)
    program = warpsim.assemble_file(warpsim.kernels_dir() / "patterns" / f"{name}.wisa")
    stats = device.launch(program, (1, 1), block, params)
    check(arena)
    return stats


def test_coalesced():
    # One warp: 1 load on an aligned 128-byte window (1 segment) and 1 store (1 segment).
    def build(arena):
        data = np.arange(32, dtype=np.int32)
        off_in = arena.upload(data)
        off_out = arena.alloc(128)
        return [off_in, off_out], lambda a: np.testing.assert_array_equal(a.download(off_out, 32, "int32"), data)

    s = _run("coalesced", (32, 1), build)
    assert s["global_loads"] == 1 and s["global_stores"] == 1
    assert s["global_segments"] == 2
    assert s["shared_accesses"] == 0


def test_strided():
    # One warp: the load touches word 32 i for i in 0..31, one segment each (32);
    # the store is coalesced (1).
    def build(arena):
        data = np.arange(32 * 32, dtype=np.int32)
        off_in = arena.upload(data)
        off_out = arena.alloc(128)
        expected = data[::32]
        return [off_in, off_out], lambda a: np.testing.assert_array_equal(a.download(off_out, 32, "int32"), expected)

    s = _run("strided", (32, 1), build)
    assert s["global_segments"] == 32 + 1
    assert s["global_lane_accesses"] == 64


def test_offset():
    # Load window starts at byte 64 of an aligned buffer: bytes 64..191 span
    # two segments (2); the store is aligned (1).
    def build(arena):
        data = np.arange(64, dtype=np.int32)
        off_in = arena.upload(data)
        off_out = arena.alloc(128)
        expected = data[16:48]
        return [off_in, off_out], lambda a: np.testing.assert_array_equal(a.download(off_out, 32, "int32"), expected)

    s = _run("offset", (32, 1), build)
    assert s["global_segments"] == 2 + 1


def test_broadcast():
    # One lane stores shared word 0 (degree 1), then all 32 lanes load word 0:
    # identical addresses broadcast, degree 1. Two accesses, two wavefronts.
    def build(arena):
        off_out = arena.alloc(128)
        return [off_out], lambda a: np.testing.assert_array_equal(a.download(off_out, 32, "int32"), np.full(32, 42, dtype=np.int32))

    s = _run("broadcast", (32, 1), build)
    assert s["shared_accesses"] == 2
    assert s["shared_lane_accesses"] == 1 + 32
    assert s["shared_wavefronts"] == 2
    assert s["shared_conflicted_accesses"] == 0
    assert s["barriers_completed"] == 1


def test_bank_conflict():
    # Store and load at word 32 i: all 32 lanes hit bank 0 with distinct
    # addresses, degree 32 each. Two accesses, 64 wavefronts, 2 conflicted.
    def build(arena):
        off_out = arena.alloc(128)
        return [off_out], lambda a: np.testing.assert_array_equal(a.download(off_out, 32, "int32"), np.arange(32, dtype=np.int32))

    s = _run("bank_conflict", (32, 1), build)
    assert s["shared_accesses"] == 2
    assert s["shared_wavefronts"] == 64
    assert s["shared_conflicted_accesses"] == 2


def test_transpose():
    # 32 warps (one per row). Global: each warp loads one row (1 segment) and
    # stores one row (1 segment): 64 segments. Shared: each warp stores its row
    # (degree 1) and then reads a column with stride 32 words (degree 32):
    # 32 * (1 + 32) = 1056 wavefronts, 32 conflicted accesses.
    def build(arena):
        tile = np.arange(32 * 32, dtype=np.int32).reshape(32, 32)
        off_in = arena.upload(tile)
        off_out = arena.alloc(4096)
        return [off_in, off_out], lambda a: np.testing.assert_array_equal(a.download(off_out, 1024, "int32").reshape(32, 32), tile.T)

    s = _run("transpose", (32, 32), build)
    assert s["warps_launched"] == 32
    assert s["global_loads"] == 32 and s["global_stores"] == 32
    assert s["global_segments"] == 64
    assert s["shared_accesses"] == 64
    assert s["shared_wavefronts"] == 32 * (1 + 32)
    assert s["shared_conflicted_accesses"] == 32
    assert s["barriers_completed"] == 1
