#!/usr/bin/env python3
"""Validate the private CEF module boundaries without loading CEF."""
# SPDX-License-Identifier: MIT
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def check():
    cmake = (ROOT / 'cmake/RuntimeModules.cmake').read_text()
    modules = dict(re.findall(r'^\s*suitecut_browser_module\((\w+)\s+([^)]*)\)', cmake, re.MULTILINE))
    expected = {'protocol', 'runtime', 'page', 'process', 'pch'}
    if set(modules) != expected:
        raise ValueError(f'Unexpected module inventory: {set(modules)}')
    declared = [name for sources in modules.values() for name in sources.split()]
    actual = {str(p.relative_to(ROOT)) for folder in ('protocol', 'runtime', 'page', 'process')
              for p in (ROOT / folder).rglob('*.cc')}
    if len(declared) != len(set(declared)) or set(declared) != actual:
        raise ValueError('Every implementation must belong to exactly one module')
    if (ROOT / 'browser.cc').exists() or list(ROOT.glob('*.inc')):
        raise ValueError('Flat browser monolith/controller fragments reintroduced')
    include = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.MULTILINE)
    count = 0
    for path in ROOT.rglob('*'):
        if not path.is_file() or path.suffix not in {'.cc', '.h', '.mm', '.inc'}:
            continue
        text = path.read_text()
        names = include.findall(text) + re.findall(
            r'^\s*#\s*define\s+SUITECUT_\w*INCLUDE\s+"([^"\n]+\.inc)"', text, re.MULTILINE)
        for name in names:
            if name.endswith('.cc') and any(path.is_relative_to(ROOT / folder)
                                           for folder in ('protocol', 'runtime', 'page', 'process')):
                raise ValueError(f'Textual implementation include: {path}: {name}')
            if name.endswith('.inc'):
                matches = {(path.parent / name).resolve(), (ROOT / name).resolve()}
                if not any(p.is_file() and p.is_relative_to(ROOT / 'controllers') for p in matches):
                    raise ValueError(f'Missing or unowned controller include: {path}: {name}')
            if path.parent == ROOT / 'protocol' and name.startswith(('page/', 'runtime/', 'process/')):
                raise ValueError(f'Protocol depends on a higher layer: {path}: {name}')
        if path.suffix == '.inc':
            if not path.is_relative_to(ROOT / 'controllers'):
                raise ValueError(f'Controller outside its package: {path}')
            count += 1
    print(f'PASS: {len(actual)} compiled sources, four runtime modules, {count} domain controller fragments')


if __name__ == '__main__':
    check()
