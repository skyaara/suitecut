"""Shared local macOS signing policy for engine builds and runtime packages."""
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parent.parent
CONFIG = ROOT / '.suitecut/macos-signing.json'
USER_CONFIG = Path.home() / 'Library/Application Support/SuiteCut/macos-signing.json'


def resolve_identity(explicit=None):
    identity = explicit or os.environ.get('SUITECUT_MACOS_SIGN_IDENTITY')
    policy = CONFIG if CONFIG.exists() else USER_CONFIG
    if not identity and policy.exists():
        config = json.loads(policy.read_text())
        if set(config) != {'identity'} or not isinstance(config['identity'], str):
            raise ValueError(f'Invalid signing policy: {policy}')
        identity = config['identity']
    identity = identity or '-'
    if identity == '-':
        return identity
    if not re.fullmatch(r'[0-9a-fA-F]{40}', identity):
        raise ValueError('Pin an exact 40-digit signing certificate fingerprint')
    identity = identity.upper()
    available = subprocess.check_output(
        ['/usr/bin/security', 'find-identity', '-v', '-p', 'codesigning'], text=True)
    if not re.search(r'\b' + identity + r'\b', available):
        raise ValueError(f'Configured signing identity {identity} is unavailable; refusing ad-hoc fallback')
    return identity


def sign(path, identity):
    # Certificate-backed builds retain stable identity requirements and use
    # Apple's trusted timestamp; ad-hoc development builds remain offline.
    # No keychain ACL edits, password exports, or per-build certificate creation.
    subprocess.run(['/usr/bin/codesign', '--force', '--sign', identity,
                    '--timestamp=none' if identity == '-' else '--timestamp', str(path)], check=True, timeout=120)


def verify(path):
    subprocess.run(['/usr/bin/codesign', '--verify', '--deep', '--strict', str(path)], check=True)


def signature(path):
    result = subprocess.run(['/usr/bin/codesign', '-d', '-r-', '--verbose=4', str(path)],
                            text=True, capture_output=True, check=True)
    lines = (result.stdout + result.stderr).splitlines()
    return {'identifier': next((x.split('=', 1)[1] for x in lines if x.startswith('Identifier=')), None),
            'teamIdentifier': next((x.split('=', 1)[1] for x in lines if x.startswith('TeamIdentifier=')), None),
            'designatedRequirement': next((x.removeprefix('designated => ') for x in lines if x.startswith('designated => ')), None)}


def sign_app(app, identity):
    app = Path(app).resolve(strict=True)
    binaries = []
    for path in app.rglob('*'):
        if path.is_file() and not path.is_symlink():
            with path.open('rb') as stream:
                if stream.read(4) in (b'\xcf\xfa\xed\xfe', b'\xca\xfe\xba\xbe'):
                    binaries.append(path)
    for path in sorted(binaries, key=lambda p: len(p.parts), reverse=True):
        sign(path, identity)
    # Seal nested bundles explicitly inside-out; do not rely on --deep signing.
    bundles = [p for p in app.rglob('*') if p.suffix in ('.app', '.framework') and not p.is_symlink()]
    for path in sorted(bundles, key=lambda p: len(p.parts), reverse=True):
        sign(path, identity)
    sign(app, identity)
    verify(app)
    return signature(app)
