import re

import numpy as np

import warpsim


def test_version_is_semantic():
    assert re.fullmatch(r"\d+\.\d+\.\d+", warpsim.version())
    assert warpsim.__version__ == warpsim.version()


def test_numpy_is_available_for_the_harness():
    assert np.arange(4).sum() == 6
