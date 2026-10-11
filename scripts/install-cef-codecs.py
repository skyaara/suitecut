"""Install the checksum-pinned Linux x64 CEF SDK with H.264/AAC playback."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import stat
import tempfile
import urllib.request
import zipfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--destination', type=Path, default=root / '.suitecut/cef-codecs-sdk')
parser.add_argument('--archive', type=Path, help='Use an already downloaded archive; its checksum is still verified.')
args = parser.parse_args()
if platform.system() != 'Linux' or platform.machine() not in ('x86_64', 'AMD64'):
    parser.error('This verified distribution is Linux x64 only; macOS codec playback remains unverified.')
pin = json.loads((root / 'native/browser/cef-build-linux-codecs.json').read_text())
destination = args.destination.absolute()


def sha256(path):
    with path.open('rb') as file:
        return hashlib.file_digest(file, 'sha256').hexdigest()


if destination.exists():
    if sha256(destination / 'Release/libcef.so') != pin['runtimeLibrarySha256']:
        parser.error('Destination contains another runtime; choose a new destination. Existing SDKs are preserved.')
    print('Verified existing codec-enabled SDK:', destination)
    raise SystemExit(0)
destination.parent.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='suitecut-cef-install-', dir=destination.parent) as staging:
    staging = Path(staging)
    archive = args.archive.absolute() if args.archive else staging / 'cef.zip'
    if not args.archive:
        print('Downloading pinned codec-enabled CEF', pin['cefVersion'], flush=True)
        with urllib.request.urlopen(pin['url'], timeout=60) as source, archive.open('wb') as output:
            total = 0
            while chunk := source.read(1024 * 1024):
                total += len(chunk)
                if total > pin['size']:
                    raise ValueError('CEF archive exceeds its pinned size')
                output.write(chunk)
    if archive.stat().st_size != pin['size'] or sha256(archive) != pin['sha256']:
        raise ValueError('CEF archive size or SHA-256 mismatch; nothing installed')
    extracted = staging / 'extracted'
    extracted.mkdir()
    with zipfile.ZipFile(archive) as source:
        if sum(entry.file_size for entry in source.infolist()) > 2 * 1024 ** 3:
            raise ValueError('CEF archive exceeds the extraction limit')
        for entry in source.infolist():
            target = (extracted / entry.filename).resolve()
            if not target.is_relative_to(extracted.resolve()) or stat.S_ISLNK(entry.external_attr >> 16):
                raise ValueError('Unsafe archive path or symlink')
        source.extractall(extracted)
        for entry in source.infolist():
            mode = (entry.external_attr >> 16) & 0o777
            if mode and not entry.is_dir():
                (extracted / entry.filename).chmod(mode)
    sdk = extracted / ('cef_binary_' + pin['cefVersion'] + '_linux64_minimal')
    if sha256(sdk / 'Release/libcef.so') != pin['runtimeLibrarySha256']:
        raise ValueError('Extracted CEF library does not match its pin')
    if not (sdk / 'include/cef_version.h').is_file():
        raise ValueError('CEF SDK headers missing')
    os.rename(sdk, destination)
    (destination / 'suitecut-distribution.json').write_text(json.dumps(pin, indent=2) + '\n')
print('Installed verified codec-enabled CEF SDK:', destination)
