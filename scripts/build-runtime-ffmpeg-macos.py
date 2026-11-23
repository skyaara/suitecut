#!/usr/bin/env python3
"""Build the LGPL FFmpeg the macOS runtime bundles, for macOS 14.

The runtime records and streams through `ffmpeg` and checks its output with
`ffprobe`. Homebrew's FFmpeg is a GPL build (x264, x265) for the host's
macOS, so this builds one from the sources pinned in
native/browser/macos-ffmpeg.json with only what suitecut uses:

- VP9 through libvpx and Opus through libopus (both BSD);
- H.264 through VideoToolbox, AAC through FFmpeg's own encoder;
- RTMPS and HTTPS through the embed kit's static OpenSSL 3 (Apache-2.0,
  which needs --enable-version3, so the result is LGPL-3.0-or-later).

Nothing is autodetected, so the binaries link only macOS's own libraries and
frameworks. `--enable-gpl` and `--enable-nonfree` are never passed.

  scripts/build-runtime-ffmpeg-macos.py fetch --work .suitecut/runtime-ffmpeg
  scripts/build-runtime-ffmpeg-macos.py build --work .suitecut/runtime-ffmpeg \\
      --openssl .suitecut/kit-deps/prefix

`build` installs into WORK/prefix and writes WORK/prefix/runtime-ffmpeg.json
with the configuration and hashes. Ship WORK/archives/ffmpeg-*.tar.xz with
the runtime: it is the complete corresponding source of the FFmpeg binaries.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parent.parent
PINS = json.loads((ROOT / 'native/browser/macos-ffmpeg.json').read_text())
ORDER = ['opus', 'libvpx', 'ffmpeg']
JOBS = '4'


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def archive(work, name):
    url = PINS['packages'][name]['url']
    return work / 'archives' / (name + '-' + url.rsplit('/', 1)[1].removeprefix(name + '-'))


def fetch(work):
    (work / 'archives').mkdir(parents=True, exist_ok=True)
    for name in ORDER:
        pin = PINS['packages'][name]
        target = archive(work, name)
        if not target.exists() or digest(target) != pin['sha256']:
            partial = target.with_name(target.name + '.partial')
            with urllib.request.urlopen(pin['url'], timeout=300) as response, partial.open('wb') as out:
                shutil.copyfileobj(response, out)
            if digest(partial) != pin['sha256']:
                partial.unlink()
                raise ValueError(f'{name}: sha256 differs from the pin')
            partial.rename(target)
        print(json.dumps({'package': name, 'archive': str(target), 'sha256': pin['sha256']}), flush=True)


def run(command, cwd, env):
    print('+', ' '.join(str(x) for x in command), flush=True)
    subprocess.run([str(x) for x in command], cwd=cwd, env=env, check=True)


def extract(work, name):
    target = work / 'src' / name
    if target.exists():
        shutil.rmtree(target)
    target.mkdir(parents=True)
    path = archive(work, name)
    if digest(path) != PINS['packages'][name]['sha256']:
        raise ValueError(f'{name}: sha256 differs from the pin; fetch again')
    with tarfile.open(path) as tar:
        tar.extractall(target, filter='data')
    (top,) = [p for p in target.iterdir() if p.is_dir()]
    return top


def build(work, openssl):
    prefix = work / 'prefix'
    if prefix.exists():
        shutil.rmtree(prefix)
    (prefix / 'lib/pkgconfig').mkdir(parents=True)
    minimum = PINS['deploymentTarget']
    flags = f'-arch arm64 -mmacosx-version-min={minimum} -O2'
    env = {key: value for key, value in os.environ.items() if not key.startswith(('CFLAGS', 'LDFLAGS', 'CPPFLAGS'))}
    env.update({'MACOSX_DEPLOYMENT_TARGET': minimum, 'CFLAGS': flags, 'LDFLAGS': flags,
                # Nothing from Homebrew: only this prefix and the kit's OpenSSL.
                'PKG_CONFIG_LIBDIR': f'{prefix}/lib/pkgconfig:{openssl}/lib/pkgconfig', 'PKG_CONFIG_PATH': ''})

    source = extract(work, 'opus')
    run(['./configure', f'--prefix={prefix}', '--disable-shared', '--enable-static', '--disable-doc',
         '--disable-extra-programs'], source, env)
    run(['make', f'-j{JOBS}'], source, env)
    run(['make', 'install'], source, env)

    source = extract(work, 'libvpx')
    # darwin23 is macOS 14; libvpx sets the matching minimum itself.
    run(['./configure', f'--prefix={prefix}', '--target=arm64-darwin23-gcc', '--disable-shared', '--enable-static',
         '--enable-pic', '--enable-vp9-highbitdepth', '--disable-examples', '--disable-tools', '--disable-docs',
         '--disable-unit-tests'], source, env)
    run(['make', f'-j{JOBS}'], source, env)
    run(['make', 'install'], source, env)

    source = extract(work, 'ffmpeg')
    configuration = [
        f'--prefix={prefix}', '--cc=clang', '--arch=arm64', '--enable-static', '--disable-shared',
        '--disable-autodetect', '--enable-version3', '--disable-ffplay', '--disable-doc', '--disable-debug',
        '--enable-pthreads', '--enable-zlib', '--enable-bzlib',
        '--enable-videotoolbox', '--enable-audiotoolbox', '--enable-libvpx', '--enable-libopus', '--enable-openssl',
        '--pkg-config-flags=--static', f'--extra-cflags=-mmacosx-version-min={minimum}',
        f'--extra-ldflags=-mmacosx-version-min={minimum}']
    run(['./configure', *configuration], source, env)
    run(['make', f'-j{JOBS}'], source, env)
    run(['make', 'install'], source, env)

    tools = {name: prefix / 'bin' / name for name in ['ffmpeg', 'ffprobe']}
    for name, path in tools.items():
        banner = subprocess.run([path, '-hide_banner', '-buildconf'], capture_output=True, text=True, check=True)
        if '--enable-gpl' in banner.stdout or '--enable-nonfree' in banner.stdout:
            raise ValueError(f'{name} is not an LGPL build')
        linked = subprocess.run(['/usr/bin/otool', '-L', path], capture_output=True, text=True, check=True).stdout
        for line in linked.splitlines()[1:]:
            library = line.strip().split(' ')[0]
            if not library.startswith(('/usr/lib/', '/System/Library/')):
                raise ValueError(f'{name} links a library outside macOS: {library}')
    version = subprocess.run([tools['ffmpeg'], '-hide_banner', '-version'], capture_output=True, text=True,
                             check=True).stdout.splitlines()[0]
    shutil.rmtree(work / 'src')
    manifest = {'deploymentTarget': minimum, 'license': PINS['packages']['ffmpeg']['license'],
                'version': version, 'configuration': configuration, 'packages': PINS['packages'],
                'openssl': json.loads((openssl / 'kit-deps.json').read_text())['packages']['openssl'],
                'tools': {name: digest(path) for name, path in tools.items()}}
    (prefix / 'runtime-ffmpeg.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps({'prefix': str(prefix), 'tools': manifest['tools']}), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('step', choices=['fetch', 'build'])
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--openssl', type=Path, help='prefix from scripts/build-embed-kit-deps-macos.py')
    args = parser.parse_args()
    work = args.work.resolve()
    if args.step == 'fetch':
        fetch(work)
    elif not args.openssl:
        parser.error('build needs --openssl')
    else:
        build(work, args.openssl.resolve())


if __name__ == '__main__':
    main()
