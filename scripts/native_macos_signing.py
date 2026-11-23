"""Shared local macOS signing policy for engine builds and runtime packages."""
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

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


ENTITLEMENTS = ROOT / 'native/browser/entitlements'
# CEF's GPU and renderer helpers JIT, which the hardened runtime allows only
# with these entitlements. Keys are the end of the helper app's name.
HELPER_ENTITLEMENTS = {' (GPU)': 'helper-gpu.plist', ' (Renderer)': 'helper-renderer.plist'}


def sign(path, identity, *, hardened=False, identifier=None, entitlements=None):
    # Certificate-backed builds retain stable identity requirements and use
    # Apple's trusted timestamp; ad-hoc development builds remain offline.
    # No keychain ACL edits, password exports, or per-build certificate creation.
    # Release archives sign with the hardened runtime, which notarization needs.
    command = ['/usr/bin/codesign', '--force', '--sign', identity,
               '--timestamp=none' if identity == '-' else '--timestamp']
    if hardened:
        command += ['--options', 'runtime']
    if identifier:
        command += ['--identifier', identifier]
    if entitlements:
        command += ['--entitlements', str(ENTITLEMENTS / entitlements)]
    # Apple's timestamp service drops requests now and then; retry only that.
    for attempt in range(6):
        result = subprocess.run(command + [str(path)], capture_output=True, text=True, timeout=300)
        if result.returncode == 0 or 'timestamp service is not available' not in result.stderr or attempt == 5:
            break
        time.sleep(2 ** attempt)
    sys.stderr.write(result.stderr)
    result.check_returncode()


def helper_entitlements(bundle):
    return next((plist for suffix, plist in HELPER_ENTITLEMENTS.items() if Path(bundle).stem.endswith(suffix)), None)


def verify(path):
    subprocess.run(['/usr/bin/codesign', '--verify', '--deep', '--strict', str(path)], check=True)


def signature(path):
    result = subprocess.run(['/usr/bin/codesign', '-d', '-r-', '--verbose=4', str(path)],
                            text=True, capture_output=True, check=True)
    lines = (result.stdout + result.stderr).splitlines()
    return {'identifier': next((x.split('=', 1)[1] for x in lines if x.startswith('Identifier=')), None),
            'teamIdentifier': next((x.split('=', 1)[1] for x in lines if x.startswith('TeamIdentifier=')), None),
            'designatedRequirement': next((x.removeprefix('designated => ') for x in lines if x.startswith('designated => ')), None)}


def sign_app(app, identity, hardened=False):
    app = Path(app).resolve(strict=True)
    binaries = []
    for path in app.rglob('*'):
        if path.is_file() and not path.is_symlink():
            with path.open('rb') as stream:
                if stream.read(4) in (b'\xcf\xfa\xed\xfe', b'\xca\xfe\xba\xbe'):
                    binaries.append(path)
    for path in sorted(binaries, key=lambda p: len(p.parts), reverse=True):
        sign(path, identity, hardened=hardened)
    # Seal nested bundles explicitly inside-out; do not rely on --deep signing.
    bundles = [p for p in app.rglob('*') if p.suffix in ('.app', '.framework') and not p.is_symlink()]
    for path in sorted(bundles, key=lambda p: len(p.parts), reverse=True):
        sign(path, identity, hardened=hardened, entitlements=helper_entitlements(path) if hardened else None)
    sign(app, identity, hardened=hardened)
    verify(app)
    return signature(app)


def notarize(folder, profile, scratch):
    """Submit a signed folder to Apple's notary service, then staple its apps.

    `profile` names a notarytool keychain profile, created once with
    `xcrun notarytool store-credentials`; no password passes through here.
    Bare executables and libraries cannot hold a ticket: Gatekeeper finds
    theirs online by code hash. Each outermost .app gets its ticket stapled.
    """
    folder = Path(folder).resolve(strict=True)
    upload = Path(scratch) / (folder.name + '.zip')
    subprocess.run(['/usr/bin/ditto', '-c', '-k', '--keepParent', str(folder), str(upload)], check=True)
    try:
        submitted = subprocess.run(['/usr/bin/xcrun', 'notarytool', 'submit', str(upload), '--keychain-profile', profile,
                                    '--wait', '--timeout', '2h', '--output-format', 'json'],
                                   capture_output=True, text=True, timeout=7500)
    finally:
        upload.unlink(missing_ok=True)
    try:
        result = json.loads(submitted.stdout)
    except ValueError:
        raise ValueError(f'notarytool failed: {(submitted.stderr or submitted.stdout).strip()}') from None
    if result.get('status') != 'Accepted':
        log = subprocess.run(['/usr/bin/xcrun', 'notarytool', 'log', result.get('id', ''), '--keychain-profile', profile],
                             capture_output=True, text=True).stdout
        raise ValueError(f'Notarization {result.get("status")} ({result.get("id")}): {log[-4000:]}')
    apps = [p for p in folder.rglob('*.app') if not any(q.suffix == '.app' for q in p.relative_to(folder).parents)]
    for app in sorted(apps):
        subprocess.run(['/usr/bin/xcrun', 'stapler', 'staple', str(app)], check=True, capture_output=True)
        subprocess.run(['/usr/bin/xcrun', 'stapler', 'validate', str(app)], check=True, capture_output=True)
    return {'submissionId': result['id'], 'status': result['status'],
            'stapled': [str(app.relative_to(folder)) for app in sorted(apps)]}
