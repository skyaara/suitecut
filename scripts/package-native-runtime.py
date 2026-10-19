#!/usr/bin/env python3
"""Assemble an explicit, offline Linux SDK + CEF + media runtime candidate."""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import platform
import shutil
import stat
import subprocess
import tarfile


ROOT = Path(__file__).resolve().parent.parent


def sha256(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def copy_tree(source, destination):
    """Copy without preserving developer-only modes or external symlinks."""
    source = source.resolve(strict=True)
    destination.mkdir(mode=0o755)
    for entry in sorted(source.iterdir()):
        target = destination / entry.name
        if entry.is_symlink():
            resolved = entry.resolve(strict=True)
            if not resolved.is_relative_to(source):
                raise ValueError(f'External symlink in bundle input: {entry}')
            target.symlink_to(os.path.relpath(resolved, entry.parent))
        elif entry.is_dir():
            copy_tree(entry, target)
        elif entry.is_file():
            copy_file(entry, target)
        else:
            raise ValueError(f'Unsupported bundle input: {entry}')


def copy_file(source, destination):
    with source.open('rb') as original, destination.open('xb') as output:
        shutil.copyfileobj(original, output, 1024 * 1024)
    destination.chmod(0o755 if source.stat().st_mode & 0o111 else 0o644)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', required=True, type=Path, help='Installed native SDK prefix')
    parser.add_argument('--runtime', required=True, type=Path, help='Existing CEF runtime directory')
    parser.add_argument('--engine', required=True, type=Path, help='SuiteCut engine to include')
    parser.add_argument('--cef-credits', required=True, type=Path)
    parser.add_argument('--media-archive', required=True, type=Path)
    parser.add_argument('--media-pin', type=Path, default=ROOT / 'native/client/tools/full-runtime-media.json')
    parser.add_argument('--patchelf', default='patchelf', help='Build-time patchelf executable')
    parser.add_argument('--output', required=True, type=Path, help='New artifact directory; never overwritten')
    args = parser.parse_args()
    if platform.system() != 'Linux' or platform.machine() != 'x86_64':
        raise ValueError('This full-runtime recipe currently qualifies Linux x86_64 only')
    pin = json.loads(args.media_pin.read_text())
    if sha256(args.media_archive) != pin['sha256']:
        raise ValueError('Media archive checksum does not match its pin')
    for relative in ['include/suitecut/client.h', 'lib/libsuitecut_client.a',
                     'bin/suitecut-native-control', 'bin/suitecut-native-record']:
        if not (args.sdk / relative).is_file():
            raise ValueError(f'Missing native SDK input: {relative}')
    required = ['libcef.so', 'resources.pak', 'icudtl.dat', 'locales',
                'chrome_100_percent.pak', 'chrome_200_percent.pak',
                'v8_context_snapshot.bin', 'CEF-LICENSE.txt', 'libyuv-LICENSE.txt']
    for relative in required:
        if not (args.runtime / relative).exists():
            raise ValueError(f'Missing CEF runtime input: {relative}')
    for file in [args.engine, args.cef_credits]:
        if not file.is_file():
            raise ValueError(f'Missing input: {file}')
    output = args.output.resolve()
    inputs = [args.sdk.resolve(), args.runtime.resolve()]
    if any(output.is_relative_to(p) or p.is_relative_to(output) for p in inputs):
        raise ValueError('Input and output directories must be disjoint')
    output.mkdir(parents=True, exist_ok=False)
    version = '0.1.0'
    name = f'SuiteCutRuntime-{version}-linux-x86_64'
    stage = output / name
    copy_tree(args.sdk, stage)
    cef = stage / 'libexec/suitecut/cef'
    cef.mkdir(parents=True, mode=0o755)
    optional = ['chrome-sandbox', 'libvk_swiftshader.so', 'libvulkan.so.1',
                'vk_swiftshader_icd.json', 'snapshot_blob.bin']
    for relative in required + optional:
        source = args.runtime / relative
        if not source.exists():
            continue
        if source.is_dir():
            copy_tree(source, cef / relative)
        else:
            copy_file(source, cef / relative)
    copy_file(args.engine, cef / 'suitecut-browser')
    # Strip only the new copy; preserve the developer binary and CEF library.
    subprocess.run(['strip', '--strip-unneeded', str(cef / 'suitecut-browser')], check=True)
    # Do not retain the developer's CEF path or search the caller's working directory.
    subprocess.run([args.patchelf, '--set-rpath', '$ORIGIN', str(cef / 'suitecut-browser')], check=True)
    copy_file(args.cef_credits, cef / 'CEF-CREDITS.html')
    notices = stage / 'share/doc/SuiteCutRuntime'
    notices.mkdir(parents=True, mode=0o755)
    for source in [ROOT / 'LICENSE', ROOT / 'THIRD_PARTY_NOTICES.md',
                   ROOT / 'docs/native-full-runtime.md']:
        copy_file(source, notices / source.name)
    media_docs = notices / 'ffmpeg'
    media_docs.mkdir(mode=0o755)
    with tarfile.open(args.media_archive, 'r:xz') as archive:
        for member in archive:
            path = PurePosixPath(member.name)
            if path.is_absolute() or '..' in path.parts or not path.parts:
                raise ValueError('Unsafe media archive entry')
            relative = PurePosixPath(*path.parts[1:])
            if str(relative) in ['bin/ffmpeg', 'bin/ffprobe']:
                target = stage / str(relative)
            elif str(relative) == 'LICENSE.txt' or (relative.parts and relative.parts[0] in ['doc', 'presets']):
                target = media_docs / str(relative)
            else:
                continue
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True, mode=0o755)
            elif member.isfile():
                target.parent.mkdir(parents=True, exist_ok=True, mode=0o755)
                with archive.extractfile(member) as original, target.open('xb') as stream:
                    shutil.copyfileobj(original, stream, 1024 * 1024)
                target.chmod(0o755 if str(relative).startswith('bin/') else 0o644)
            else:
                raise ValueError(f'Unsupported media archive entry: {member.name}')
    for name_ in ['ffmpeg', 'ffprobe']:
        if not (stage / 'bin' / name_).is_file():
            raise ValueError(f'Media archive does not contain {name_}')
    (notices / 'media-provenance.json').write_text(json.dumps(pin, indent=2) + '\n')
    # A relocatable launcher; later explicit flags may select an external browser.
    launcher = stage / 'bin/suitecut'
    launcher.write_text('''#!/bin/sh
set -eu
bin=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
exec "$bin/suitecut-native-control" --backend cef --browser "$bin/../libexec/suitecut/cef/suitecut-browser" "$@"
''')
    launcher.chmod(0o755)
    (stage / 'README.txt').write_text('''SuiteCut native full runtime (Linux x86_64)
Browser control: bin/suitecut --session demo open https://example.com
                bin/suitecut --session demo snapshot
                bin/suitecut --session demo close
Recording: bin/suitecut-native-record --url https://example.com --output /tmp/demo.webm --audio
No Node.js or media download is needed. Normal Linux desktop/system libraries are required.
Read share/doc/SuiteCutRuntime/native-full-runtime.md for scope and redistribution requirements.
''')
    files = []
    dependencies = []
    for path in sorted(stage.rglob('*')):
        if path.is_symlink():
            if not path.resolve(strict=True).is_relative_to(stage):
                raise ValueError('Packaged symlink escapes installation')
            files.append({'path':str(path.relative_to(stage)), 'symlink':os.readlink(path)})
        elif path.is_file():
            files.append({'path':str(path.relative_to(stage)), 'bytes':path.stat().st_size,
                          'sha256':sha256(path)})
            with path.open('rb') as stream:
                elf = stream.read(4) == b'\x7fELF'
            if elf:
                result = subprocess.run(['ldd',str(path)], text=True, capture_output=True)
                if 'not found' in result.stdout or 'not found' in result.stderr:
                    raise ValueError(f'Missing host dependency: {path}: {result.stdout}')
                dependencies.append({'path':str(path.relative_to(stage)), 'ldd':result.stdout.strip()})
    manifest = {'formatVersion':1, 'name':'SuiteCutRuntime', 'version':version,
                'platform':'linux', 'architecture':'x86_64', 'nodeRequired':False,
                'cef':json.loads((ROOT/'native/browser/cef-build.json').read_text()),
                'media':pin, 'files':files, 'systemDependencies':dependencies,
                'published':False, 'redistributionReady':False,
                'publicationHold':'Supply complete corresponding source for the GPL media build before public distribution.',
                'engineInputSha256':sha256(args.engine),
                'limits':['Linux x86_64 qualification only; host system libraries remain required.',
                          'Stock pinned CEF; custom CEF patches and proprietary decoding codecs are not included.',
                          'Native SDK recording helper targets Chromium; CEF recording uses its native command protocol.']}
    (stage / 'runtime-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    artifact = output / (name + '.tar.gz')
    # Normalize ownership, permissions and timestamps, retaining only executable bits.
    with artifact.open('xb') as stream, gzip.GzipFile(filename='',mode='wb',fileobj=stream,mtime=0,compresslevel=6) as compressed, tarfile.open(fileobj=compressed,mode='w') as tar:
        for path in [stage,*sorted(stage.rglob('*'))]:
            info=tar.gettarinfo(str(path), str(Path(name)/path.relative_to(stage)))
            info.uid=info.gid=0;info.uname=info.gname='';info.mtime=0
            info.mode=0o755 if info.isdir() or (info.mode & stat.S_IXUSR) else 0o644
            if info.isfile():
                with path.open('rb') as data:
                    tar.addfile(info,data)
            else:
                tar.addfile(info)
    digest=sha256(artifact)
    (output/'SHA256SUMS').write_text(f'{digest}  {artifact.name}\n')
    report={'archive':str(artifact),'sha256':digest,'bytes':artifact.stat().st_size,
            'unpackedBytes':sum(f.get('bytes',0) for f in files),'files':len(files),
            'published':False,'redistributionReady':False}
    (output/'artifact.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report),flush=True)


if __name__ == '__main__':
    main()
