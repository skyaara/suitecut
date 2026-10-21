#!/usr/bin/env python3
"""Check source ownership and public/private include boundaries without a build."""
# SPDX-License-Identifier: MIT
from pathlib import Path
import re

SDK = Path(__file__).resolve().parents[1]
SHARED = SDK.parent / 'shared'
MODULES = {
    'core': [], 'wire': [],
    'backends/chromium': ['wire', 'shared'],
    'backends/firefox': ['wire', 'shared'],
    'transport': ['backends/chromium', 'backends/firefox'],
    'browser': [], 'agent': [], 'c_api': [],
}
PUBLIC = {
    'core': {'json.hpp', 'credentials.hpp', 'image.hpp'},
    'wire': {'json.hpp', 'transport.hpp'},
    'backends/chromium': {'json.hpp', 'transport.hpp'},
    'backends/firefox': {'json.hpp', 'transport.hpp'},
    'transport': {'json.hpp', 'transport.hpp'},
    'browser': {'json.hpp', 'transport.hpp', 'browser.hpp', 'observation.hpp', 'visual.hpp'},
    'agent': {'json.hpp', 'transport.hpp', 'browser.hpp', 'agent.hpp', 'agent_driver.hpp',
              'simple_agent.hpp', 'agent_tools.hpp', 'credentials.hpp', 'image.hpp', 'observation.hpp'},
    'c_api': {'client.h', 'json.hpp', 'transport.hpp'},
}
INCLUDE = re.compile(r'^\s*#\s*include\s*([<"])([^">]+)[">]', re.MULTILINE)
PUBLIC_STANDARD = {'chrono', 'concepts', 'cstddef', 'cstdint', 'future', 'functional',
                   'initializer_list', 'map', 'memory', 'optional', 'span', 'stdexcept',
                   'string', 'string_view', 'type_traits', 'utility', 'variant', 'vector'}


def check():
    checked = 0
    for path in sorted((SDK / 'src').rglob('*')):
        if path.suffix not in {'.cpp', '.hpp'}:
            continue
        module = str(path.parent.relative_to(SDK / 'src'))
        if module not in MODULES:
            raise ValueError(f'Unowned SDK source: {path}')
        for delimiter, include in INCLUDE.findall(path.read_text()):
            if include.startswith('suitecut/'):
                if include.removeprefix('suitecut/') not in PUBLIC[module]:
                    raise ValueError(f'Invalid public dependency: {path}: {include}')
                target = SDK / 'include' / include
                if not target.is_file():
                    raise ValueError(f'Missing public dependency: {target}')
            elif delimiter == '"':
                roots = [path.parent] + [SHARED if x == 'shared' else SDK / 'src' / x
                                        for x in MODULES[module]]
                matches = [root / include for root in roots if (root / include).is_file()]
                if len(matches) != 1 or '/' in include:
                    raise ValueError(f'Invalid private dependency: {path}: {include}')
        checked += 1
    for path in sorted((SDK / 'include').rglob('*')):
        if path.is_file():
            for delimiter, include in INCLUDE.findall(path.read_text()):
                if delimiter == '<' and include in PUBLIC_STANDARD | {'stddef.h', 'stdint.h'}:
                    continue
                if not include.startswith('suitecut/') or not (SDK / 'include' / include).is_file():
                    raise ValueError(f'Public header depends on a private header: {path}: {include}')
    for path in sorted(SHARED.glob('*.h')):
        for delimiter, include in INCLUDE.findall(path.read_text()):
            if delimiter == '<' and include in PUBLIC_STANDARD:
                continue
            if '/' in include or not (SHARED / include).is_file():
                raise ValueError(f'Shared helper reaches outside its package: {path}: {include}')
    print(f'PASS: {checked} SDK source/header files; public and shared headers are isolated')


if __name__ == '__main__':
    check()
