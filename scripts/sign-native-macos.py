#!/usr/bin/env python3
"""Sign a local native engine with the same pinned identity used by packaging."""
import argparse
import json
from pathlib import Path

from native_macos_signing import resolve_identity, sign_app

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--app', required=True, type=Path)
parser.add_argument('--signing-identity', help='Exact certificate fingerprint; - explicitly selects ad-hoc')
args = parser.parse_args()
identity = resolve_identity(args.signing_identity)
print(json.dumps({'identity': identity, **sign_app(args.app, identity)}))
