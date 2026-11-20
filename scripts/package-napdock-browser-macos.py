#!/usr/bin/env python3
"""Package SuiteCut.app for Napdock's browser: the CEF engine with the session
service beside it in Contents/MacOS, signed as one bundle. The service is
placed twice, as suitecut-session and as suitecut-shared-session, the name
Napdock's installed helper runs; they are the same program.

Napdock's browser helper looks for this bundle at
~/Library/Application Support/Napdock/Browser/SuiteCut.app (or
/Applications/SuiteCut.app) and runs it only when it carries SuiteCut's
Developer ID signature, so the tool must be self-contained: it may link only
system libraries.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess

from native_macos_signing import resolve_identity, sign, signature, verify

SYSTEM = ('/usr/lib/', '/System/Library/')


def clone(source, target):
    # APFS clones cost no space until either side changes.
    subprocess.run(['/bin/cp', '-cR', str(source), str(target)], check=True)


def linked(binary):
    out = subprocess.check_output(['/usr/bin/otool', '-L', str(binary)], text=True)
    return [line.strip().split(' (')[0] for line in out.splitlines()[1:] if line.strip()]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, required=True, help='Built suitecut-browser.app')
    parser.add_argument('--session-tool', type=Path, required=True,
                        help='Built suitecut-session (or suitecut-shared-session: the same program)')
    parser.add_argument('--output', type=Path, required=True, help='Where to write SuiteCut.app')
    parser.add_argument('--replace', action='store_true', help='Replace an existing bundle at --output')
    parser.add_argument('--signing-identity', help='Pinned certificate fingerprint; - explicitly selects ad-hoc')
    args = parser.parse_args()
    identity = resolve_identity(args.signing_identity)
    app = args.app.resolve(strict=True)
    tool = args.session_tool.resolve(strict=True)
    foreign = [path for path in linked(tool) if not path.startswith(SYSTEM)]
    if foreign:
        raise ValueError(f'The session tool links libraries outside the system: {foreign}; '
                         'build it with static OpenSSL (OPENSSL_SSL_LIBRARY/OPENSSL_CRYPTO_LIBRARY = the .a files)')
    output = args.output.resolve()
    if output.suffix != '.app':
        raise ValueError('--output must name a .app bundle')
    if output.exists():
        if not args.replace:
            raise ValueError(f'{output} exists; pass --replace to replace it')
        shutil.rmtree(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    staging = output.with_name(output.name + '.partial')
    if staging.exists():
        shutil.rmtree(staging)
    clone(app, staging)
    # The engine's nested helpers and framework keep their own signatures;
    # only the service and the outer seal change.
    for name in ('suitecut-session', 'suitecut-shared-session'):
        placed = staging / 'Contents/MacOS' / name
        clone(tool, placed)
        placed.chmod(0o755)
        sign(placed, identity)
    sign(staging, identity)
    verify(staging)
    staging.rename(output)
    print(json.dumps({'app': str(output), 'identity': identity, **signature(output)}))


if __name__ == '__main__':
    main()
