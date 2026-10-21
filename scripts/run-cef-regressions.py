#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compatibility entry point; implementation lives in tests/cef/harness."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tests/cef"))
from harness.runner import main

if __name__ == "__main__":
    raise SystemExit(main())
