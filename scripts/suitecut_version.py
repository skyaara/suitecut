# SPDX-License-Identifier: MIT
"""suitecut's release version, read from the one place it is set: the
client's project() line in native/client/CMakeLists.txt."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parent.parent


def version():
    text = (ROOT / 'native/client/CMakeLists.txt').read_text()
    found = re.search(r'^project\(SuiteCutClient VERSION (\d+\.\d+\.\d+)\b', text, re.M)
    if not found:
        raise ValueError('Cannot read suitecut\'s version from native/client/CMakeLists.txt')
    return found.group(1)
