#!/usr/bin/env python3
"""Package SuiteCut's macOS runtime as an embed kit: one folder an app copies
into its own bundle (Napdock: its Browser extension's helpers/).

  suitecut-browser.app                   the engine, with the CEF framework
                                         and the five helper apps (plain,
                                         GPU, Renderer, Plugin, Alerts) in
                                         Contents/Frameworks
  suitecut-session                       the session service (protocols 0 and 1)
  licenses/                              CEF, Chromium credits, libyuv, SuiteCut
                                         and the statically linked libraries
  kit.json                               versions, protocols and every file's
                                         size and sha256

The engine keeps its bundle: it cannot run bare, nor with the framework and
helpers beside its bundle. Chromium names its Mach port rendezvous service
after the main bundle's identifier (a bare engine's sandboxed children could
not look it up), picks the role helpers only for a bundled main executable,
and lets a sandboxed renderer read only inside the main bundle (it could not
load a framework beside it).

Every Mach-O links only system libraries, is built for macOS 14 or older, and
is signed with the hardened runtime and the entitlements in
native/browser/entitlements/. Nothing is notarized here.

Steps:

  # 1. The pinned CEF SDK (official by default; --cef codecs for the
  #    third-party build with H.264/AAC/HEVC, which carries no codec licence).
  scripts/package-embed-kit-macos.py fetch-cef --cef official --dest .suitecut/cef-official-sdk
  # 2. Static libraries for macOS 14 (scripts/build-embed-kit-deps-macos.py).
  # 3. The engine against that SDK and those libraries, and the client
  #    (docs/embedding/embed-kit-macos.md has the cmake lines).
  # 4. The kit:
  scripts/package-embed-kit-macos.py package --cef official --cef-sdk .suitecut/cef-official-sdk \\
      --engine-build .suitecut/kit-engine-build --session .suitecut/kit-client-build/suitecut-session \\
      --deps .suitecut/kit-deps --output .suitecut/embed-kit --replace

scripts/verify-embed-kit-macos.py checks the result.
"""
import argparse
import atexit
import hashlib
import json
import os
from pathlib import Path
import platform
import plistlib
import re
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request

from native_macos_signing import resolve_identity

ROOT = Path(__file__).resolve().parent.parent
CEF_PIN = json.loads((ROOT / 'native/browser/cef-build.json').read_text())
DEPS_PIN = json.loads((ROOT / 'native/browser/macos-kit-deps.json').read_text())
ENTITLEMENTS = ROOT / 'native/browser/entitlements'
FRAMEWORK = 'Chromium Embedded Framework.framework'
HELPERS = {  # helper app suffix -> (bundle identifier suffix, entitlements file)
    '': ('helper', None),
    ' (GPU)': ('helper.gpu', 'helper-gpu.plist'),
    ' (Renderer)': ('helper.renderer', 'helper-renderer.plist'),
    ' (Plugin)': ('helper.plugin', None),
    ' (Alerts)': ('helper.alerts', None),
}
ENGINE_ID = 'com.suitecut.browser'
ENGINE_APP = 'suitecut-browser.app'
SESSION_ID = 'com.suitecut.session'
MINIMUM = DEPS_PIN['deploymentTarget']
SYSTEM = ('/usr/lib/', '/System/Library/')
RELATIVE = ('@rpath/', '@loader_path/', '@executable_path/')
PIN_MARKER = '.suitecut-cef-pin.json'
# Locales the kit keeps. en.lproj is Chromium's en-US and its fallback for
# any other language.
LOCALES = {'en.lproj'}
NAME = re.compile(r'[A-Za-z0-9 ()._@+/-]+')
LICENSES = {'openssl': 'LICENSE.txt', 'brotli': 'LICENSE', 'libpsl': 'COPYING', 'curl': 'COPYING'}


_LINKS = None


def plain(binary):
    """otool reads `name(member)` as an archive member: give it a plain name."""
    global _LINKS
    binary = Path(binary)
    if '(' not in str(binary):
        return binary
    if _LINKS is None:
        _LINKS = Path(tempfile.mkdtemp(prefix='suitecut-otool-'))
        atexit.register(shutil.rmtree, _LINKS, True)
    link = _LINKS / hashlib.sha256(str(binary).encode()).hexdigest()[:16]
    if not link.exists():
        link.symlink_to(binary)
    return link


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def run(*command, **kwargs):
    return subprocess.run([str(x) for x in command], check=True, text=True, capture_output=True, **kwargs).stdout


def clone(source, target):
    target.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(['/bin/cp', '-cR', str(source), str(target)], check=True)


def cef_pin(kind):
    return CEF_PIN['macosarm64Official' if kind == 'official' else 'macosarm64']


def macho(path):
    if not path.is_file() or path.is_symlink():
        return False
    with path.open('rb') as stream:
        return stream.read(4) in (b'\xcf\xfa\xed\xfe', b'\xca\xfe\xba\xbe')


def size(path):
    path = Path(path)
    return sum(p.stat().st_size for p in path.rglob('*') if p.is_file() and not p.is_symlink())


def fetch_cef(kind, dest):
    pin = cef_pin(kind)
    dest = dest.resolve()
    if dest.exists():
        raise ValueError(f'{dest} exists; remove it first')
    work = dest.with_name(dest.name + '.download')
    if work.exists():
        shutil.rmtree(work)
    (work / 'x').mkdir(parents=True)
    archive = work / 'cef.tar.bz2'
    with urllib.request.urlopen(pin['url'], timeout=300) as response, archive.open('wb') as out:
        shutil.copyfileobj(response, out)
    if archive.stat().st_size != pin['size'] or digest(archive) != pin['sha256']:
        raise ValueError('The CEF archive differs from the pin in cef-build.json')
    with tarfile.open(archive) as tar:
        tar.extractall(work / 'x', filter='data')
    (top,) = list((work / 'x').iterdir())
    top.rename(dest)
    shutil.rmtree(work)
    (dest / PIN_MARKER).write_text(json.dumps({'cef': kind, 'cefVersion': CEF_PIN['cefVersion'], **pin}, indent=2) + '\n')
    print(json.dumps({'sdk': str(dest), 'cef': kind, 'sha256': pin['sha256']}))


def cache(build):
    values = {}
    for line in (build / 'CMakeCache.txt').read_text().splitlines():
        match = re.match(r'^([A-Za-z0-9_]+):[A-Z]+=(.*)$', line)
        if match:
            values[match.group(1)] = match.group(2)
    return values


def dependencies(binary):
    lines = run('/usr/bin/otool', '-L', plain(binary)).splitlines()[1:]
    entries = [line.strip().split(' (compatibility')[0] for line in lines if line.strip()]
    own = run('/usr/bin/otool', '-D', plain(binary)).splitlines()[1:]
    return [entry for entry in entries if entry not in own]


def minimum_os(binary):
    out = run('/usr/bin/vtool', '-show-build', plain(binary))
    found = re.findall(r'minos (\S+)', out)
    return found[0] if found else None


def version_tuple(text):
    return tuple(int(x) for x in text.split('.'))


def sign(path, identity, identifier=None, entitlements=None):
    command = ['/usr/bin/codesign', '--force', '--sign', identity, '--options', 'runtime',
               '--timestamp=none' if identity == '-' else '--timestamp']
    if identifier:
        command += ['--identifier', identifier]
    if entitlements:
        command += ['--entitlements', str(ENTITLEMENTS / entitlements)]
    subprocess.run(command + [str(path)], check=True, timeout=300)


def license_from(archive, member_name):
    with tarfile.open(archive) as tar:
        for member in tar.getmembers():
            parts = Path(member.name).parts
            if len(parts) == 2 and parts[1] == member_name and member.isfile():
                return tar.extractfile(member).read()
    raise ValueError(f'{archive} has no {member_name}')


def package(args):
    if platform.system() != 'Darwin' or platform.machine() != 'arm64':
        raise ValueError('The kit is built on macOS arm64')
    identity = resolve_identity(args.signing_identity)
    sdk = args.cef_sdk.resolve(strict=True)
    marker = json.loads((sdk / PIN_MARKER).read_text())
    pin = cef_pin(args.cef)
    if marker.get('cef') != args.cef or marker.get('sha256') != pin['sha256']:
        raise ValueError(f'{sdk} is not the pinned {args.cef} CEF SDK (fetch it with fetch-cef)')
    build = args.engine_build.resolve(strict=True)
    configured = cache(build)
    if Path(configured.get('CEF_ROOT', '')).resolve() != sdk:
        raise ValueError(f'The engine was built against {configured.get("CEF_ROOT")}, not {sdk}')
    app = build / 'suitecut-browser.app'
    session = args.session.resolve(strict=True)
    deps = args.deps.resolve(strict=True)

    output = args.output.resolve()
    if output.exists() and not args.replace:
        raise ValueError(f'{output} exists; pass --replace')
    stage = output.with_name(output.name + '.partial')
    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir(parents=True)

    engine_app = stage / ENGINE_APP
    clone(app, engine_app)
    shutil.rmtree(engine_app / 'Contents/_CodeSignature', ignore_errors=True)
    frameworks = engine_app / 'Contents/Frameworks'
    # The framework straight from the pinned SDK, not the build's copy.
    shutil.rmtree(frameworks / FRAMEWORK)
    clone(sdk / 'Release' / FRAMEWORK, frameworks / FRAMEWORK)
    info = plistlib.loads((engine_app / 'Contents/Info.plist').read_bytes())
    if info.get('CFBundleIdentifier') != ENGINE_ID or info.get('CFBundleExecutable') != 'suitecut-browser':
        raise ValueError('The engine bundle has an unexpected Info.plist')
    for suffix, (id_suffix, _) in HELPERS.items():
        name = f'suitecut-browser Helper{suffix}.app'
        # Each helper its own identifier, as Chromium's are.
        plist = frameworks / name / 'Contents/Info.plist'
        helper = plistlib.loads(plist.read_bytes())
        helper['CFBundleIdentifier'] = f'{ENGINE_ID}.{id_suffix}'
        plist.write_bytes(plistlib.dumps(helper))
        shutil.rmtree(frameworks / name / 'Contents/_CodeSignature', ignore_errors=True)
    clone(session, stage / 'suitecut-session')
    full_size = size(stage)
    resources = frameworks / FRAMEWORK / 'Resources'
    dropped = sorted(p.name for p in resources.glob('*.lproj') if p.name not in LOCALES)
    for name in dropped:
        shutil.rmtree(resources / name)

    licenses = stage / 'licenses'
    licenses.mkdir()
    shutil.copy2(sdk / 'LICENSE.txt', licenses / 'CEF-LICENSE.txt')
    shutil.copy2(sdk / 'CREDITS.html', licenses / 'CEF-CREDITS.html')
    shutil.copy2(Path(configured['LIBYUV_ROOT']) / 'LICENSE', licenses / 'libyuv-LICENSE.txt')
    for name, member in LICENSES.items():
        archive = deps / 'archives' / (name + '-' + DEPS_PIN['packages'][name]['url'].rsplit('/', 1)[1])
        if digest(archive) != DEPS_PIN['packages'][name]['sha256']:
            raise ValueError(f'{archive} differs from its pin')
        (licenses / f'{name}-{member}').write_bytes(license_from(archive, member))
    shutil.copy2(ROOT / 'LICENSE', licenses / 'SuiteCut-LICENSE.txt')

    # Only system libraries; our own programs built for MINIMUM.
    machos = sorted(p for p in stage.rglob('*') if macho(p))
    problems = []
    for binary in machos:
        for dependency in dependencies(binary):
            if not dependency.startswith(SYSTEM + RELATIVE):
                problems.append(f'{binary.relative_to(stage)} links {dependency}')
        found = minimum_os(binary)
        if not found or version_tuple(found) > version_tuple(MINIMUM):
            problems.append(f'{binary.relative_to(stage)} needs macOS {found}')
    helper_names = [f'suitecut-browser Helper{s}' for s in HELPERS]
    ours = [engine_app / 'Contents/MacOS/suitecut-browser', stage / 'suitecut-session'] + [
        frameworks / f'{h}.app/Contents/MacOS/{h}' for h in helper_names]
    for binary in ours:
        if minimum_os(binary) != MINIMUM:
            problems.append(f'{binary.relative_to(stage)} is built for macOS {minimum_os(binary)}, not {MINIMUM}')
    if problems:
        raise ValueError('The kit is not self-contained:\n  ' + '\n  '.join(problems))

    # Inside out: the framework's libraries, the framework, each helper, the
    # engine's bundle, then the session.
    inside = f'{ENGINE_APP}/Contents/Frameworks'
    for library in sorted((frameworks / FRAMEWORK / 'Libraries').glob('*.dylib')):
        sign(library, identity)
    sign(frameworks / FRAMEWORK, identity)
    signed = {f'{inside}/{FRAMEWORK}': None}
    for suffix, (id_suffix, entitlements) in HELPERS.items():
        name = f'suitecut-browser Helper{suffix}.app'
        sign(frameworks / name, identity, entitlements=entitlements)
        signed[f'{inside}/{name}'] = entitlements
    sign(engine_app, identity)
    sign(stage / 'suitecut-session', identity, identifier=SESSION_ID)
    signed[ENGINE_APP] = None
    signed['suitecut-session'] = None
    for name in signed:
        subprocess.run(['/usr/bin/codesign', '--verify', '--deep', '--strict', str(stage / name)], check=True)

    described = json.loads(run(stage / 'suitecut-session', '--version', '--json'))
    # What an installer reading kit.json accepts (Napdock's): regular files
    # only, no symlinks or hidden names, and plain names.
    files = []
    for path in sorted(stage.rglob('*')):
        relative = str(path.relative_to(stage))
        if path.is_symlink():
            raise ValueError(f'The kit holds a symlink: {relative}')
        if any(part.startswith('.') for part in path.relative_to(stage).parts):
            raise ValueError(f'The kit holds a hidden name: {relative}')
        if not NAME.fullmatch(relative):
            raise ValueError(f'A name in the kit has characters outside {NAME.pattern}: {relative}')
        if path.is_file():
            files.append({'path': relative, 'size': path.stat().st_size, 'sha256': digest(path)})
    team = re.search(r'TeamIdentifier=(\S+)', subprocess.run(
        ['/usr/bin/codesign', '-dv', str(engine_app)], text=True, capture_output=True).stderr)
    manifest = {
        'formatVersion': 1,
        'name': 'suitecut-embed-kit',
        'platform': 'macos', 'architecture': 'arm64',
        'suitecutVersion': json.loads((ROOT / 'package.json').read_text())['version'],
        'sessionVersion': described.get('version'),
        'protocols': described.get('protocols', {}),
        'minimumMacOS': MINIMUM,
        'cef': {'version': CEF_PIN['cefVersion'], 'chromiumVersion': CEF_PIN['chromiumVersion'],
                'build': args.cef, 'officialCefBuild': args.cef == 'official',
                'url': pin['url'], 'sha256': pin['sha256'], 'codecs': pin.get('codecs')},
        'staticLibraries': {name: value['version'] for name, value in DEPS_PIN['packages'].items()},
        'locales': sorted(LOCALES),
        'layout': {'engine': f'{ENGINE_APP}/Contents/MacOS/suitecut-browser', 'engineBundle': ENGINE_APP,
                   'session': 'suitecut-session', 'framework': f'{inside}/{FRAMEWORK}',
                   'helpers': [f'{inside}/{h}.app' for h in helper_names]},
        'signing': {'identity': identity, 'teamIdentifier': team.group(1) if team else None,
                    'hardenedRuntime': True, 'timestamped': identity != '-', 'notarized': False,
                    'entitlements': {name: (str((ENTITLEMENTS / e).relative_to(ROOT)) if e else None)
                                     for name, e in signed.items()}},
        'sourceCommit': run('git', '-C', ROOT, 'rev-parse', 'HEAD').strip(),
        'sourceDirty': bool(run('git', '-C', ROOT, 'status', '--porcelain', '--', 'native', 'scripts').strip()),
        'files': files,
    }
    (stage / 'kit.json').write_text(json.dumps(manifest, indent=2) + '\n')
    if output.exists():
        shutil.rmtree(output)
    stage.rename(output)
    print(json.dumps({'kit': str(output), 'bytes': size(output), 'bytesBeforeThinning': full_size,
                      'localesDropped': len(dropped), 'files': len(files)}))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    steps = parser.add_subparsers(dest='step', required=True)
    fetch = steps.add_parser('fetch-cef', help='Download, check and unpack the pinned CEF SDK')
    fetch.add_argument('--cef', choices=['official', 'codecs'], default='official')
    fetch.add_argument('--dest', type=Path, required=True)
    pack = steps.add_parser('package', help='Assemble, check and sign the kit')
    pack.add_argument('--cef', choices=['official', 'codecs'], default='official',
                      help='official (the default: no proprietary codecs) or codecs (OpenArch H.264/AAC/HEVC build)')
    pack.add_argument('--cef-sdk', type=Path, required=True)
    pack.add_argument('--engine-build', type=Path, required=True, help='The engine build folder (its CMakeCache.txt)')
    pack.add_argument('--session', type=Path, required=True, help='suitecut-session built for macOS 14')
    pack.add_argument('--deps', type=Path, required=True, help='The work folder of build-embed-kit-deps-macos.py')
    pack.add_argument('--output', type=Path, required=True)
    pack.add_argument('--replace', action='store_true')
    pack.add_argument('--signing-identity', help='Certificate fingerprint; - for ad-hoc')
    args = parser.parse_args()
    if args.step == 'fetch-cef':
        fetch_cef(args.cef, args.dest)
    else:
        package(args)


if __name__ == '__main__':
    main()
