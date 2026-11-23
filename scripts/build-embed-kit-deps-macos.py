#!/usr/bin/env python3
"""Build the static libraries the macOS embed kit links, for macOS 14.

The kit may link only system libraries (scripts/package-embed-kit-macos.py
checks it with otool), and Homebrew's builds target the host's macOS, so
OpenSSL, brotli, libpsl and curl are built here from the sources pinned in
native/browser/macos-kit-deps.json, as static libraries only.

curl is configured like Homebrew's, minus what the engine does not use: the
OpenSSL backend (the engine reads OpenSSL objects through curl), Apple's
trust store for certificate checks (no CA bundle path), Apple's IDN for
CURLU_PUNYCODE, libpsl for its cookie engine and GSS-API. It leaves out
HTTP/2 and HTTP/3 (the engine accepts only HTTP/1.x answers), brotli and
zstd (the engine decodes bodies itself), SSH and LDAP.

  scripts/build-embed-kit-deps-macos.py fetch --work .suitecut/kit-deps
  scripts/heavy.sh scripts/build-embed-kit-deps-macos.py build --work .suitecut/kit-deps

`fetch` downloads and checks each archive; `build` (run it in the build
slot) extracts each into its own folder and installs into WORK/prefix.
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
PINS = json.loads((ROOT / 'native/browser/macos-kit-deps.json').read_text())
ORDER = ['openssl', 'brotli', 'libpsl', 'curl']
JOBS = '2'
# OpenSSL compiles its prefix in (ENGINESDIR, MODULESDIR, OPENSSLDIR). Give
# it a folder no user can create, never this build's path: the kit loads no
# engines or modules and reads no openssl.cnf.
OPENSSL_PREFIX = '/var/empty/suitecut-openssl'


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def archive(work, name):
    url = PINS['packages'][name]['url']
    return work / 'archives' / (name + '-' + url.rsplit('/', 1)[1])


def fetch(work):
    (work / 'archives').mkdir(parents=True, exist_ok=True)
    for name in ORDER:
        pin = PINS['packages'][name]
        target = archive(work, name)
        if not target.exists() or digest(target) != pin['sha256']:
            partial = target.with_name(target.name + '.partial')
            with urllib.request.urlopen(pin['url'], timeout=120) as response, partial.open('wb') as out:
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


def build(work):
    prefix = work / 'prefix'
    if prefix.exists():
        shutil.rmtree(prefix)
    prefix.mkdir(parents=True)
    minimum = PINS['deploymentTarget']
    # Source paths compiled in (__FILE__) name files from WORK, not this machine.
    flags = f'-arch arm64 -mmacosx-version-min={minimum} -O2 -ffile-prefix-map={work}/='
    env = {key: value for key, value in os.environ.items() if not key.startswith(('CFLAGS', 'LDFLAGS', 'CPPFLAGS'))}
    env.update({'MACOSX_DEPLOYMENT_TARGET': minimum, 'CFLAGS': flags, 'LDFLAGS': flags,
                # Nothing from Homebrew: only what is installed into the prefix.
                'PKG_CONFIG_LIBDIR': str(prefix / 'lib/pkgconfig'), 'PKG_CONFIG_PATH': ''})
    cmake = ['cmake', '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', f'-DCMAKE_INSTALL_PREFIX={prefix}',
             f'-DCMAKE_OSX_DEPLOYMENT_TARGET={minimum}', '-DCMAKE_OSX_ARCHITECTURES=arm64',
             f'-DCMAKE_PREFIX_PATH={prefix}', '-DCMAKE_IGNORE_PREFIX_PATH=/opt/homebrew;/usr/local',
             '-DBUILD_SHARED_LIBS=OFF']

    source = extract(work, 'openssl')
    run(['./Configure', 'darwin64-arm64-cc', 'no-shared', 'no-module', 'no-tests', 'no-docs',
         f'--prefix={OPENSSL_PREFIX}', '--libdir=lib', f'--openssldir={OPENSSL_PREFIX}',
         f'-mmacosx-version-min={minimum}', f'-ffile-prefix-map={work}/='], source, env)
    run(['make', f'-j{JOBS}', 'build_libs'], source, env)
    # Install through DESTDIR, then point the pkg-config and CMake files at
    # the real prefix the other builds use.
    stage = work / 'openssl-stage'
    shutil.rmtree(stage, ignore_errors=True)
    run(['make', 'install_dev', f'DESTDIR={stage}'], source, env)
    shutil.copytree(stage / OPENSSL_PREFIX.lstrip('/'), prefix, dirs_exist_ok=True, symlinks=True)
    shutil.rmtree(stage)
    for path in [*(prefix / 'lib/pkgconfig').glob('*.pc'), *(prefix / 'lib/cmake').rglob('*.cmake')]:
        path.write_text(path.read_text().replace(OPENSSL_PREFIX, str(prefix)))

    source = extract(work, 'brotli')
    run(cmake + ['-S', source, '-B', source / 'build', '-DBROTLI_DISABLE_TESTS=ON', '-DBROTLI_BUILD_TOOLS=OFF'],
        source, env)
    run(['cmake', '--build', source / 'build', '--parallel', JOBS], source, env)
    run(['cmake', '--install', source / 'build'], source, env)
    # The engine names one library for each side; static brotli splits the
    # shared tables into brotlicommon, so join them.
    for side in ['dec', 'enc']:
        run(['/usr/bin/libtool', '-static', '-o', prefix / f'lib/libsuitecut-brotli{side}.a',
             prefix / f'lib/libbrotli{side}.a', prefix / 'lib/libbrotlicommon.a'], source, env)

    source = extract(work, 'libpsl')
    # The built-in list only: no IDNA library, so nothing beyond libSystem.
    run(['./configure', f'--prefix={prefix}', '--disable-shared', '--enable-static', '--disable-runtime',
         '--enable-builtin', '--disable-man', '--disable-gtk-doc', '--disable-nls'], source, env)
    run(['make', f'-j{JOBS}', '-C', 'src'], source, env)
    run(['make', '-C', 'src', 'install'], source, env)
    run(['make', '-C', 'include', 'install'], source, env)

    source = extract(work, 'curl')
    run(cmake + ['-S', source, '-B', source / 'build',
                 '-DBUILD_STATIC_LIBS=ON', '-DBUILD_CURL_EXE=OFF', '-DBUILD_TESTING=OFF', '-DBUILD_LIBCURL_DOCS=OFF',
                 '-DBUILD_MISC_DOCS=OFF', '-DENABLE_CURL_MANUAL=OFF', '-DBUILD_EXAMPLES=OFF',
                 '-DCURL_USE_OPENSSL=ON', f'-DOPENSSL_ROOT_DIR={prefix}', '-DOPENSSL_USE_STATIC_LIBS=ON',
                 '-DUSE_APPLE_SECTRUST=ON', '-DCURL_CA_BUNDLE=none', '-DCURL_CA_PATH=none', '-DCURL_CA_FALLBACK=OFF',
                 '-DUSE_APPLE_IDN=ON', '-DUSE_LIBIDN2=OFF', '-DCURL_USE_LIBPSL=ON',
                 f'-DLIBPSL_INCLUDE_DIR={prefix}/include', f'-DLIBPSL_LIBRARY={prefix}/lib/libpsl.a',
                 '-DCURL_USE_GSSAPI=ON', '-DCURL_BROTLI=OFF', '-DCURL_ZSTD=OFF', '-DUSE_NGHTTP2=OFF',
                 '-DUSE_NGTCP2=OFF', '-DUSE_QUICHE=OFF', '-DUSE_OPENSSL_QUIC=OFF', '-DCURL_USE_LIBSSH2=OFF',
                 '-DCURL_USE_LIBSSH=OFF', '-DUSE_LIBRTMP=OFF', '-DCURL_DISABLE_LDAP=ON', '-DENABLE_ARES=OFF',
                 '-DCURL_USE_PKGCONFIG=OFF'],
        source, env)
    run(['cmake', '--build', source / 'build', '--parallel', JOBS], source, env)
    run(['cmake', '--install', source / 'build'], source, env)

    for path in sorted((prefix / 'lib').glob('*.dylib')):
        raise ValueError(f'A shared library was installed: {path}')
    for path in sorted((prefix / 'lib').glob('*.a')):
        if str(work).encode() in path.read_bytes():
            raise ValueError(f'{path.name} names this build\'s folder {work}')
    shutil.rmtree(work / 'src')
    manifest = {'deploymentTarget': minimum, 'packages': PINS['packages'],
                'libraries': {p.name: digest(p) for p in sorted((prefix / 'lib').glob('*.a'))}}
    (prefix / 'kit-deps.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps({'prefix': str(prefix), 'libraries': sorted(manifest['libraries'])}), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('step', choices=['fetch', 'build'])
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    work = args.work.resolve()
    fetch(work) if args.step == 'fetch' else build(work)


if __name__ == '__main__':
    main()
