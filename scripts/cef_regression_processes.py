# SPDX-License-Identifier: MIT
"""Compatibility import for the CEF harness process ownership utilities."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tests/cef"))
from harness.processes import Descendants, snapshot
