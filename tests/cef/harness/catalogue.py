# SPDX-License-Identifier: MIT
"""Strict, reviewable catalogue; malformed entries and stale selectors fail closed."""
import fnmatch
from pathlib import Path
import re
import subprocess

from .paths import ROOT, SUITE
from .contracts import load_json, require, validate_contract


def validate_catalogue(cases):
    require(isinstance(cases, list) and bool(cases), 'Catalogue must be a nonempty array')
    seen = set()
    for case in cases:
        require(isinstance(case, dict), 'Case must be an object')
        kind = case.get('kind')
        require(kind in ('journey', 'proof', 'cpp'), f'Unknown case kind: {kind}')
        required = {'id', 'kind', 'tags', 'timeout'}
        allowed = required | {'requires'}
        if kind == 'proof':
            required |= {'script', 'evidence'}
            allowed |= {'script', 'evidence', 'args', 'env'}
        elif kind == 'cpp':
            required |= {'binary', 'evidence'}
            allowed |= {'binary', 'evidence'}
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
        if kind == 'proof':
            script = case['script']
            require(isinstance(script, str) and script.startswith('scripts/verify-native-') and script.endswith('.mjs') and (ROOT / script).resolve().is_relative_to(ROOT / 'scripts') and (ROOT / script).is_file(), f'{identity}: invalid proof script')
            require(isinstance(case.get('args', []), list) and all(isinstance(a, str) for a in case.get('args', [])), f'{identity}: invalid arguments')
            env = case.get('env', {})
            reserved = {'SUITECUT_NATIVE_EXECUTABLE', 'SUITECUT_NATIVE_SDK_DIRECTORY', 'SUITECUT_FFMPEG', 'SUITECUT_FFPROBE'}
            require(isinstance(env, dict) and all(isinstance(k, str) and k.startswith('SUITECUT_') and isinstance(v, str) for k, v in env.items()) and not reserved.intersection(env), f'{identity}: invalid environment')
        if kind == 'cpp':
            require(isinstance(case['binary'], str) and re.fullmatch(r'[a-zA-Z0-9_-]+', case['binary']), f'{identity}: invalid binary')
        if kind != 'journey':
            validate_contract(case['evidence'])
    return cases


def catalogue(node):
    raw = subprocess.check_output([node, str(SUITE / 'harness/worker.mjs'), '--list'], timeout=20)
    cases = [dict(item, kind='journey') for item in load_json(raw)]
    for filename in ('proofs.json', 'network-proofs.json'):
        entries = load_json((SUITE / 'manifests' / filename).read_bytes())
        require(isinstance(entries, list), f'{filename}: expected array')
        for item in entries:
            require(isinstance(item, dict) and isinstance(item.get('script'), str), f'{filename}: invalid entry')
            require(not {'kind', 'id'}.intersection(item), f'{filename}: ID/kind are derived from the proof script')
            cases.append(dict(item, kind='proof', id='proof.' + Path(item['script']).stem.removeprefix('verify-native-')))
    entries = load_json((SUITE / 'manifests/cpp.json').read_bytes())
    require(isinstance(entries, list), 'cpp.json: expected array')
    require(all(isinstance(item, dict) and 'kind' not in item for item in entries), 'cpp.json: kind is assigned by the catalogue')
    cases += [dict(item, kind='cpp') for item in entries]
    return validate_catalogue(cases)


def selected(cases, profile, patterns):
    profiles = {
        'smoke': lambda c: 'smoke' in c['tags'],
        'journeys': lambda c: c['kind'] == 'journey',
        'races': lambda c: 'race' in c['tags'],
        'proofs': lambda c: c['kind'] == 'proof',
        'full': lambda c: True,
    }
    for pattern in patterns:
        require(any(fnmatch.fnmatchcase(c['id'], pattern) for c in cases), f'Case selector matched zero scenarios: {pattern}')
    return [c for c in cases if (any(fnmatch.fnmatchcase(c['id'], p) for p in patterns)
                                 if patterns else profiles[profile](c))]
