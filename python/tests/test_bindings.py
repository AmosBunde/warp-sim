import numpy as np
import pytest

import warpsim


def test_assemble_and_disassemble_round_trip():
    program = warpsim.assemble_file(warpsim.kernels_dir() / "vecadd.wisa")
    assert program.entry == "vecadd"
    assert program.params == ["a", "b", "c", "n"]
    assert len(program) == 20
    again = warpsim.assemble(program.disassemble())
    assert again.words == program.words


def test_assembly_error_has_position():
    with pytest.raises(warpsim.AssemblyError) as info:
        warpsim.assemble(".entry k\n add r1, r2\n")
    assert info.value.line == 2
    assert "expected ','" in str(info.value)


def test_vecadd_matches_numpy():
    rng = np.random.default_rng(7)
    n = 1000
    a = rng.standard_normal(n, dtype=np.float32)
    b = rng.standard_normal(n, dtype=np.float32)
    device = warpsim.Device(64 * 1024)
    arena = warpsim.Arena(device)
    off_a = arena.upload(a)
    off_b = arena.upload(b)
    off_c = arena.alloc(n * 4)
    program = warpsim.assemble_file(warpsim.kernels_dir() / "vecadd.wisa")
    block = 128
    grid = (n + block - 1) // block
    stats = device.launch(program, (grid, 1), (block, 1), [off_a, off_b, off_c, n])
    c = arena.download(off_c, n, "float32")
    np.testing.assert_array_equal(c, a + b)
    assert stats["blocks_executed"] == grid
    assert stats["warps_launched"] == grid * 4
    assert stats["instructions_issued"] > 0


def test_fault_surfaces_as_exception():
    device = warpsim.Device(64)
    program = warpsim.assemble(".entry k\n mov r1, 64\n ld.global r2, [r1+4]\n")
    with pytest.raises(warpsim.SimFault) as info:
        device.launch(program, (1, 1), (1, 1), [])
    assert info.value.pc == 1
    assert info.value.address == 68
    assert info.value.message == "access out of bounds"
    assert "pc 1" in str(info.value)


def test_write_and_read_bounds():
    device = warpsim.Device(16)
    with pytest.raises(IndexError):
        device.write(8, np.zeros(4, dtype=np.uint32))
    with pytest.raises(IndexError):
        device.read(8, 4, "uint32")
    device.write(0, np.array([1, 2, 3, 4], dtype=np.int32))
    np.testing.assert_array_equal(device.read(4, 2, "int32"), np.array([2, 3], dtype=np.int32))
