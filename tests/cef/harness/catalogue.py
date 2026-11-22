# SPDX-License-Identifier: MIT
"""Strict, reviewable catalogue; malformed entries and stale selectors fail closed."""
import fnmatch
import re

from .paths import SUITE
from .contracts import load_json, require, validate_contract


def validate_catalogue(cases):
    require(isinstance(cases, list) and bool(cases), 'Catalogue must be a nonempty array')
    seen = set()
    for case in cases:
        require(isinstance(case, dict), 'Case must be an object')
        kind = case.get('kind')
        require(kind == 'cpp', f'Unknown case kind: {kind}')
        required = {'id', 'kind', 'tags', 'timeout', 'binary', 'evidence'}
        allowed = required | {'requires'}
        require(required <= case.keys() <= allowed, f'Unknown or missing case fields: {case}')
        identity = case['id']
        require(isinstance(identity, str) and re.fullmatch(r'[a-z][a-z0-9]*(?:[.-][a-z0-9]+)+', identity), f'Invalid case ID: {identity}')
        require(identity not in seen, f'Duplicate case ID: {identity}')
        seen.add(identity)
        require(type(case['timeout']) is int and 0 < case['timeout'] <= 1800, f'{identity}: invalid timeout')
        tags = case['tags']
        require(isinstance(tags, list) and bool(tags) and all(isinstance(t, str) and re.fullmatch(r'[a-z][a-z0-9-]*', t) for t in tags) and len(set(tags)) == len(tags), f'{identity}: invalid tags')
        dependencies = case.get('requires', [])
        require(isinstance(dependencies, list) and all(d in ('ffmpeg', 'ffprobe') for d in dependencies) and len(set(dependencies)) == len(dependencies), f'{identity}: invalid dependencies')
        require(isinstance(case['binary'], str) and re.fullmatch(r'[a-zA-Z0-9_-]+', case['binary']), f'{identity}: invalid binary')
        validate_contract(case['evidence'])
    return cases


def catalogue():
    entries = load_json((SUITE / 'manifests/cpp.json').read_bytes())
    require(isinstance(entries, list), 'cpp.json: expected array')
    require(all(isinstance(item, dict) and 'kind' not in item for item in entries), 'cpp.json: kind is assigned by the catalogue')
    return validate_catalogue([dict(item, kind='cpp') for item in entries])


def selected(cases, profile, patterns):
    profiles = {
        'smoke': lambda c: 'smoke' in c['tags'],
        'races': lambda c: 'race' in c['tags'],
        'full': lambda c: True,
    }
    for pattern in patterns:
        require(any(fnmatch.fnmatchcase(c['id'], pattern) for c in cases), f'Case selector matched zero scenarios: {pattern}')
    return [c for c in cases if (any(fnmatch.fnmatchcase(c['id'], p) for p in patterns)
                                 if patterns else profiles[profile](c))]
