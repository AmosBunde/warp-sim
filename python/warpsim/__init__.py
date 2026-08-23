"""WarpSim: a functional GPU simulator with a PTX-inspired instruction set.

This package wraps the native extension ``warpsim._core`` and hosts the NumPy
golden models and the differential test harness.
"""

from warpsim._core import version

__all__ = ["version"]
__version__ = version()
