#!/usr/bin/env python3
"""Build an offline, relocatable macOS ARM64 native SDK or full runtime candidate."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tarfile

from native_macos_signing import resolve_identity, sign, sign_app, signature

ROOT = Path(__file__).resolve().parent.parent


def run(*command):
    return subprocess.check_output([str(x) for x in command], text=True).strip()


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


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


def bundle_dependencies(stage, origins, identity):
    """Copy the complete non-system Mach-O dependency closure, then relocate it."""
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
    for binary in sorted(processed):
        if not any(p.suffix == '.app' and p.is_relative_to(stage) for p in binary.parents):
            sign(binary, identity)
    apps = [p for p in stage.rglob('*.app')
            if not any(parent.suffix == '.app' and parent.is_relative_to(stage) for parent in p.parents)]
    for app in apps:
        sign_app(app, identity)
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--kind', choices=['static', 'shared', 'runtime'], required=True)
    parser.add_argument('--app', type=Path)
    parser.add_argument('--ffmpeg', type=Path)
    parser.add_argument('--ffprobe', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--signing-identity', help='Pinned certificate fingerprint; - explicitly selects ad-hoc')
    args = parser.parse_args()
    if platform.system() != 'Darwin' or platform.machine() != 'arm64':
        raise ValueError('This recipe requires a native macOS ARM64 host')
    identity = resolve_identity(args.signing_identity)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    name = ('SuiteCutRuntime' if args.kind == 'runtime' else 'SuiteCutClient') + '-0.1.0-macos-arm64'
    if args.kind != 'runtime':
        name += '-' + args.kind
    stage = output / name
    clone(args.sdk.resolve(strict=True), stage)
    if args.kind == 'runtime':
        if not all([args.app, args.ffmpeg, args.ffprobe]):
            raise ValueError('Runtime requires --app, --ffmpeg and --ffprobe')
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
    provenance = bundle_dependencies(stage, origins, identity)
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
                'timestamped': identity != '-', 'notarized': False, 'published': False,
                'minimumMacOSTested': platform.mac_ver()[0],
                'cef': {key: value for key, value in json.loads((ROOT/'native/browser/cef-build.json').read_text()).items()
                        if key in ['cefVersion', 'chromiumVersion', 'channel']} if args.kind == 'runtime' else None,
                'packagerSha256': digest(Path(__file__).resolve()),
                'redistributionReady': False,
                'publicationHold': 'Review bundled dependency licenses and supply corresponding source for GPL-enabled media before public redistribution.',
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
