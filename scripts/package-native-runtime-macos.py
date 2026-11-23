#!/usr/bin/env python3
"""Build an offline, relocatable macOS ARM64 native SDK or full runtime candidate."""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tarfile

import suitecut_version
from native_macos_signing import notarize, resolve_identity, sign, sign_app, signature

ROOT = Path(__file__).resolve().parent.parent


def run(*command):
    return subprocess.check_output([str(x) for x in command], text=True).strip()


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


CEF_PIN = json.loads((ROOT / 'native/browser/cef-build.json').read_text())
PIN_MARKER = '.suitecut-cef-pin.json'  # written by package-embed-kit-macos.py fetch-cef


def engine_cef(app, kind, sdk):
    """Check the engine app was built against `sdk`, the pinned CEF of `kind`."""
    sdk = sdk.resolve(strict=True)
    marker = sdk / PIN_MARKER
    marker = json.loads(marker.read_text()) if marker.exists() else {}
    pin = CEF_PIN['macosarm64Official' if kind == 'official' else 'macosarm64']
    if kind == 'official' and (marker.get('cef') != 'official' or marker.get('sha256') != pin['sha256']):
        raise ValueError(f'{sdk} is not the pinned official CEF SDK (package-embed-kit-macos.py fetch-cef)')
    if marker and marker.get('cef') != kind:
        raise ValueError(f'{sdk} is the {marker.get("cef")} CEF SDK, not {kind}')
    if CEF_PIN['cefVersion'] not in sdk.name and not marker:
        raise ValueError(f'{sdk} is not CEF {CEF_PIN["cefVersion"]}')
    cache = app.parent / 'CMakeCache.txt'
    if not cache.exists():
        raise ValueError(f'Pass the app inside its engine build folder: no {cache}')
    root = next((line.split('=', 1)[1] for line in cache.read_text().splitlines() if line.startswith('CEF_ROOT:')), '')
    if Path(root).resolve() != sdk:
        raise ValueError(f'The engine was built against {root}, not {sdk}')
    cef = {key: CEF_PIN[key] for key in ['cefVersion', 'chromiumVersion', 'channel']}
    cef.update({'build': kind, 'officialCefBuild': kind == 'official', 'url': pin['url'], 'sha256': pin['sha256'],
                'size': pin.get('size'), 'codecs': pin.get('codecs')})
    return cef


def clone(source, target):
    target.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(['/bin/cp', '-cR', str(source), str(target)], check=True)


def macho(path):
    if not path.is_file() or path.is_symlink():
        return False
    with path.open('rb') as stream:
        return stream.read(4) in (b'\xcf\xfa\xed\xfe', b'\xca\xfe\xba\xbe')


def dependencies(path):
    entries = [line.strip().split(' (compatibility')[0]
               for line in run('/usr/bin/otool', '-m', '-L', path).splitlines()[1:]]
    ids = run('/usr/bin/otool', '-m', '-D', path).splitlines()[1:]
    return [entry for entry in entries if entry not in ids]


def minimum_macos(path):
    """The minimum macOS a Mach-O file declares, as a version tuple (or None)."""
    commands = run('/usr/bin/otool', '-m', '-l', path)
    found = re.search(r'cmd LC_BUILD_VERSION\n(?:.*\n)*?\s+minos (\S+)', commands)
    found = found or re.search(r'cmd LC_VERSION_MIN_MACOSX\n(?:.*\n)*?\s+version (\S+)', commands)
    return tuple(int(x) for x in found.group(1).split('.')) if found else None


def system_library(name):
    """The macOS copy of a library, which lives in the dyld shared cache."""
    path = f'/usr/lib/{name}'
    try:
        ctypes.CDLL(path)
    except OSError as error:
        raise ValueError(f'--system-library {name}: macOS has no {path}') from error
    return path


def bundle_dependencies(stage, origins, identity, system=()):
    """Copy the complete non-system Mach-O dependency closure, then relocate it.

    A dependency named in `system` is linked to macOS's own copy in /usr/lib
    instead of being bundled: Homebrew builds for the host's macOS, so a
    bundled copy can raise the archive's minimum macOS above its own code's.
    """
    system = {name: system_library(name) for name in system}
    shared_lib = stage / 'lib'
    shared_lib.mkdir(exist_ok=True)
    pending = [p for p in stage.rglob('*') if macho(p)]
    processed = set()
    input_hashes = {}
    provenance = []
    while pending:
        binary = pending.pop()
        if binary in processed:
            continue
        processed.add(binary)
        changes = []
        # macOS may launch a code-signing clone containing only the .app.
        # Keep its dependency closure inside the outer application bundle,
        # including the copies used by its nested CEF helper applications.
        # Loose dylibs belong directly in Frameworks: a custom code directory
        # is not a supported nested-code layout for signing/launch clones.
        app_roots = [p for p in binary.parents if p.suffix == '.app' and p.is_relative_to(stage)]
        app_root = app_roots[-1] if app_roots else None
        lib = app_root / 'Contents/Frameworks' if app_root else shared_lib
        lib.mkdir(parents=True, exist_ok=True)
        for dependency in dependencies(binary):
            if dependency.startswith(('/usr/lib/', '/System/Library/')):
                continue
            if Path(dependency).name in system:
                changes.extend(['-change', dependency, system[Path(dependency).name]])
                continue
            original = origins.get(binary, binary)
            if dependency.startswith('@rpath/'):
                name = dependency.removeprefix('@rpath/')
                candidates = [lib / name, original.parent / name, original.parent.parent / 'lib' / name]
                source = next((p.resolve() for p in candidates if p.is_file()), None)
                if source is None:
                    raise ValueError(f'Unresolved runtime dependency: {binary}: {dependency}')
            elif dependency.startswith('@loader_path/'):
                name = dependency.removeprefix('@loader_path/')
                source = (original.parent / name).resolve(strict=True)
            elif dependency.startswith('@'):
                raise ValueError(f'Unresolved runtime dependency: {binary}: {dependency}')
            else:
                source = Path(dependency).resolve(strict=True)
            if source.is_relative_to(stage) and (app_root is None or source.is_relative_to(app_root)):
                target = source
            else:
                target = lib / Path(dependency).name
                if not target.exists():
                    clone(source, target)
                    target.chmod(0o755)
                    provenance.append({'input': str(source), 'sha256': digest(source),
                                       'bundled': str(target.relative_to(stage))})
                    input_hashes[target.resolve()] = digest(source)
                    origins[target] = source
                    pending.append(target)
                elif digest(source) != input_hashes.get(target.resolve(), digest(target)):
                    raise ValueError(f'Conflicting library basename: {dependency}')
            replacement = '@loader_path/' + os.path.relpath(target, binary.parent)
            changes.extend(['-change', dependency, replacement])
        if binary.suffix == '.dylib':
            changes.extend(['-id', '@rpath/' + binary.name])
        if changes:
            binary.chmod(binary.stat().st_mode | 0o200)
            run('/usr/bin/install_name_tool', *changes, binary)
    # The hardened runtime on everything: notarization requires it.
    for binary in sorted(processed):
        if not any(p.suffix == '.app' and p.is_relative_to(stage) for p in binary.parents):
            sign(binary, identity, hardened=True)
    apps = [p for p in stage.rglob('*.app')
            if not any(parent.suffix == '.app' and parent.is_relative_to(stage) for parent in p.parents)]
    for app in apps:
        sign_app(app, identity, hardened=True)
    for binary in sorted(processed):
        for dependency in dependencies(binary):
            if dependency.startswith('/') and not dependency.startswith(('/usr/lib/', '/System/Library/')):
                raise ValueError(f'External runtime dependency remains: {binary}: {dependency}')
            if dependency.startswith('@loader_path/'):
                resolved = (binary.parent / dependency.removeprefix('@loader_path/')).resolve(strict=True)
                if not resolved.is_relative_to(stage):
                    raise ValueError(f'Runtime dependency escapes bundle: {binary}: {dependency}')
                app_roots = [p for p in binary.parents if p.suffix == '.app' and p.is_relative_to(stage)]
                if app_roots and not resolved.is_relative_to(app_roots[-1]):
                    raise ValueError(f'Application dependency escapes code-signing clone: {binary}: {dependency}')
    return provenance


def bundle_media_source(work, stage, notices):
    """Copy the LGPL FFmpeg's source tarball, build record and licenses."""
    record = json.loads((work / 'prefix/runtime-ffmpeg.json').read_text())
    for tool, expected in record['tools'].items():
        if digest(stage / 'bin' / tool) != expected:
            raise ValueError(f'bin/{tool} is not the FFmpeg that {work} built')
    source = stage / 'share/src'
    source.mkdir(parents=True, exist_ok=True)
    media = notices / 'media'
    media.mkdir(parents=True, exist_ok=True)
    clone(work / 'prefix/runtime-ffmpeg.json', media / 'runtime-ffmpeg.json')
    licenses = {'ffmpeg': ['COPYING.LGPLv3', 'COPYING.LGPLv2.1', 'LICENSE.md'], 'libvpx': ['LICENSE', 'PATENTS'],
                'opus': ['COPYING']}
    for name, wanted in licenses.items():
        url = record['packages'][name]['url']
        archive = work / 'archives' / (name + '-' + url.rsplit('/', 1)[1].removeprefix(name + '-'))
        if digest(archive) != record['packages'][name]['sha256']:
            raise ValueError(f'{archive} differs from its pin')
        if name == 'ffmpeg':
            clone(archive, source / archive.name)
        with tarfile.open(archive) as tar:
            for member in tar.getmembers():
                parts = Path(member.name).parts
                if len(parts) == 2 and parts[1] in wanted and member.isfile():
                    (media / name).mkdir(exist_ok=True)
                    (media / name / parts[1]).write_bytes(tar.extractfile(member).read())
    return {'license': record['license'], 'version': record['version'], 'configuration': record['configuration'],
            'packages': {name: value['version'] for name, value in record['packages'].items()},
            'source': str((source / Path(record['packages']['ffmpeg']['url']).name).relative_to(stage))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--kind', choices=['static', 'shared', 'runtime'], required=True)
    parser.add_argument('--app', type=Path)
    parser.add_argument('--cef', choices=['official', 'codecs'], default='official',
                        help='The CEF the engine was built against: official (the default; publishable) or codecs')
    parser.add_argument('--cef-sdk', type=Path, help='That CEF SDK; the runtime checks the engine build used it')
    parser.add_argument('--ffmpeg', type=Path)
    parser.add_argument('--ffprobe', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--signing-identity', help='Pinned certificate fingerprint; - explicitly selects ad-hoc')
    parser.add_argument('--system-library', action='append', default=[], metavar='NAME',
                        help='Link this dependency (e.g. liblzma.5.dylib) to macOS\'s /usr/lib copy instead of bundling it')
    parser.add_argument('--media-source', type=Path, metavar='WORK',
                        help='Work folder of scripts/build-runtime-ffmpeg-macos.py: bundles its FFmpeg source and licenses')
    parser.add_argument('--notarize', metavar='PROFILE',
                        help='Notarize with this notarytool keychain profile (xcrun notarytool store-credentials)')
    parser.add_argument('--require-macos', metavar='VERSION',
                        help='Fail if any packaged Mach-O file needs a newer macOS than this')
    args = parser.parse_args()
    if platform.system() != 'Darwin' or platform.machine() != 'arm64':
        raise ValueError('This recipe requires a native macOS ARM64 host')
    identity = resolve_identity(args.signing_identity)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    name = ('SuiteCutRuntime' if args.kind == 'runtime' else 'SuiteCutClient') + f'-{suitecut_version.version()}-macos-arm64'
    if args.kind != 'runtime':
        name += '-' + args.kind
    stage = output / name
    clone(args.sdk.resolve(strict=True), stage)
    if args.kind == 'runtime':
        if not all([args.app, args.cef_sdk, args.ffmpeg, args.ffprobe]):
            raise ValueError('Runtime requires --app, --cef-sdk, --ffmpeg and --ffprobe')
        cef = engine_cef(args.app.resolve(strict=True), args.cef, args.cef_sdk)
        app = stage / 'libexec/suitecut/suitecut-browser.app'
        clone(args.app.resolve(strict=True), app)
        for tool, source in [('ffmpeg', args.ffmpeg), ('ffprobe', args.ffprobe)]:
            clone(source.resolve(strict=True), stage / 'bin' / tool)
        launcher = stage / 'bin/suitecut'
        launcher.write_text('''#!/bin/sh
set -eu
bin=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
exec "$bin/suitecut-native-control" --backend cef --browser "$bin/../libexec/suitecut/suitecut-browser.app/Contents/MacOS/suitecut-browser" "$@"
''')
        launcher.chmod(0o755)
    notices = stage / 'share/doc/SuiteCutRuntime'
    notices.mkdir(parents=True, exist_ok=True)
    for source in [ROOT / 'LICENSE', ROOT / 'THIRD_PARTY_NOTICES.md', ROOT / 'docs/native-full-runtime.md',
                   ROOT / 'docs/native-full-runtime-macos.md']:
        clone(source, notices / source.name)
    origins = {p: args.sdk.resolve() / p.relative_to(stage) for p in stage.rglob('*') if macho(p)}
    if args.kind == 'runtime':
        for path in app.rglob('*'):
            if macho(path):
                origins[path] = args.app.resolve() / path.relative_to(app)
        origins[stage/'bin/ffmpeg'] = args.ffmpeg.resolve()
        origins[stage/'bin/ffprobe'] = args.ffprobe.resolve()
    provenance = bundle_dependencies(stage, origins, identity, args.system_library)
    minimums = {str(p.relative_to(stage)): minimum_macos(p) for p in stage.rglob('*') if macho(p)}
    minimum = max((v for v in minimums.values() if v), default=None)
    if args.require_macos and minimum and minimum > tuple(int(x) for x in args.require_macos.split('.')):
        newer = sorted(k for k, v in minimums.items() if v and v > tuple(int(x) for x in args.require_macos.split('.')))
        raise ValueError(f'Needs macOS {".".join(map(str, minimum))}, above --require-macos {args.require_macos}: {newer}')
    # Retain formula license files for bundled Homebrew dependencies and media.
    sources = [Path(p['input']) for p in provenance]
    if args.kind == 'runtime':
        sources += [args.ffmpeg.resolve(), args.ffprobe.resolve()]
    for source in sources:
        parts = source.parts
        if 'Cellar' not in parts:
            continue
        index = parts.index('Cellar')
        formula = Path(*parts[:index + 3])
        folder = notices / 'dependencies' / parts[index + 1]
        for file in formula.iterdir():
            if file.is_file() and ('license' in file.name.lower() or 'copying' in file.name.lower()):
                folder.mkdir(parents=True, exist_ok=True)
                if not (folder / file.name).exists():
                    clone(file, folder / file.name)
    media = None
    holds = []
    if args.kind == 'runtime':
        # A GPL FFmpeg could ship only with its complete corresponding source
        # and GPL terms for the whole runtime; the LGPL build ships with its
        # source tarball and licenses inside the archive.
        # The codec build of CEF plays H.264, AAC and HEVC with no patent licence.
        if not cef['officialCefBuild']:
            holds.append('The engine is the third-party codec build of CEF.')
        configuration = run(stage / 'bin/ffmpeg', '-hide_banner', '-buildconf')
        if '--enable-gpl' in configuration or '--enable-nonfree' in configuration:
            holds.append('The bundled FFmpeg is a GPL or nonfree build.')
        if args.media_source:
            media = bundle_media_source(args.media_source.resolve(strict=True), stage, notices)
        else:
            holds.append('FFmpeg\'s corresponding source is not bundled (--media-source).')
    # Notarize the finished, signed tree before listing its files: stapling
    # adds each app's ticket.
    notarization = notarize(stage, args.notarize, output) if args.notarize else None
    files = []
    for path in sorted(stage.rglob('*')):
        if path.is_symlink():
            if not path.resolve(strict=True).is_relative_to(stage):
                raise ValueError(f'Symlink escapes runtime: {path}')
            files.append({'path': str(path.relative_to(stage)), 'symlink': os.readlink(path)})
        elif path.is_file():
            files.append({'path': str(path.relative_to(stage)), 'bytes': path.stat().st_size, 'sha256': digest(path)})
    manifest = {'formatVersion': 1, 'name': name, 'platform': 'macos', 'architecture': 'arm64',
                'kind': args.kind, 'sourceCommit': run('git', '-C', ROOT, 'rev-parse', 'HEAD'),
                'sourceDirty': bool(run('git', '-C', ROOT, 'status', '--porcelain')),
                'nodeRequired': False, 'systemMediaRequired': False if args.kind == 'runtime' else None,
                'signing': 'ad-hoc' if identity == '-' else 'certificate',
                'signingIdentity': identity, 'signature': signature(app) if args.kind == 'runtime' else None,
                'timestamped': identity != '-', 'hardenedRuntime': True,
                'notarized': bool(notarization), 'notarization': notarization, 'published': False,
                'minimumMacOSTested': platform.mac_ver()[0],
                'minimumMacOS': '.'.join(map(str, minimum)) if minimum else None,
                'systemLibraries': sorted(args.system_library),
                'cef': cef if args.kind == 'runtime' else None,
                'packagerSha256': digest(Path(__file__).resolve()),
                'media': media,
                'redistributionReady': not holds,
                'publicationHold': ' '.join(holds) or None,
                'host': platform.mac_ver()[0], 'dependencies': provenance, 'files': files}
    (stage / 'runtime-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    archive = output / (name + '.tar.gz')
    with tarfile.open(archive, 'w:gz', compresslevel=6) as tar:
        tar.add(stage, arcname=name)
    checksum = digest(archive)
    (output / 'SHA256SUMS').write_text(f'{checksum}  {archive.name}\n')
    result = {'archive': str(archive), 'installation': str(stage), 'sha256': checksum,
              'bytes': archive.stat().st_size, 'files': len(files)}
    (output / 'artifact.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result), flush=True)


if __name__ == '__main__':
    main()
