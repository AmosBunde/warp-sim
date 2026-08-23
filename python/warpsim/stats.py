"""Derived statistics over a launch stats dictionary.

Every value here is a ratio or count of the events in ``docs/counters.md``;
none is a time.
"""

from __future__ import annotations

WARP_SIZE = 32


def average_active_lanes(stats: dict) -> float:
    """Mean number of lanes executing per issued instruction."""
    issued = stats["instructions_issued"]
    return stats["active_lane_sum"] / issued if issued else 0.0


def lane_utilization(stats: dict) -> float:
    """Fraction of issue slots carrying an executing lane: active_lane_sum / (32 x issues)."""
    issued = stats["instructions_issued"]
    return stats["active_lane_sum"] / (WARP_SIZE * issued) if issued else 0.0


def warps_per_block(stats: dict) -> float:
    blocks = stats["blocks_executed"]
    return stats["warps_launched"] / blocks if blocks else 0.0


def instruction_mix(stats: dict) -> dict[str, float]:
    """Fraction of issues per class."""
    issued = stats["instructions_issued"] or 1
    return {
        "alu": stats["alu_instructions"] / issued,
        "memory": stats["memory_instructions"] / issued,
        "control": stats["control_instructions"] / issued,
        "barrier": stats["barrier_instructions"] / issued,
    }


def halving_series(stats: dict) -> list[int]:
    """Lane counts that appear in the histogram, descending, for divergence plots."""
    return [lanes for lanes in range(WARP_SIZE, 0, -1) if stats["active_lane_histogram"][lanes] > 0]
