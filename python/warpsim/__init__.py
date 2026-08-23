"""WarpSim: a functional GPU simulator with a PTX-inspired instruction set.

This package wraps the native extension ``warpsim._core`` and hosts the NumPy
golden models and the differential test harness.
"""

from __future__ import annotations

from pathlib import Path

import numpy as np

from warpsim._core import AssemblyError, Device, Program, SimFault, assemble, version

__all__ = [
    "Arena",
    "AssemblyError",
    "Device",
    "Program",
    "SimFault",
    "assemble",
    "assemble_file",
    "kernels_dir",
    "version",
]
__version__ = version()

_PACKAGE_ROOT = Path(__file__).resolve().parent.parent.parent


def kernels_dir() -> Path:
    """Directory holding the shipped ``.wisa`` kernels."""
    return _PACKAGE_ROOT / "kernels"


def assemble_file(path: str | Path) -> Program:
    """Assembles a ``.wisa`` file."""
    return assemble(Path(path).read_text(encoding="utf-8"))


class Arena:
    """A bump allocator over a device's global memory.

    Buffers are placed at 128-byte aligned offsets so that the coalescing
    analyzer sees segment-aligned bases unless a test deliberately offsets
    them.
    """

    def __init__(self, device: Device, alignment: int = 128) -> None:
        self.device = device
        self.alignment = alignment
        self._next = 0

    def alloc(self, nbytes: int) -> int:
        offset = self._next
        self._next = (offset + nbytes + self.alignment - 1) // self.alignment * self.alignment
        if self._next > self.device.global_bytes:
            raise MemoryError(
                f"arena exhausted: need {self._next} bytes, device has {self.device.global_bytes}"
            )
        return offset

    def upload(self, array: np.ndarray) -> int:
        """Allocates and copies ``array``; returns the byte offset."""
        array = np.ascontiguousarray(array)
        offset = self.alloc(array.nbytes)
        self.device.write(offset, array)
        return offset

    def download(self, offset: int, count: int, dtype: str = "uint32") -> np.ndarray:
        return self.device.read(offset, count, dtype)
