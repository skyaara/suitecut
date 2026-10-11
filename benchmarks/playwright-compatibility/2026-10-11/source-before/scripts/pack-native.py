"""Prepare complete native source/runtime archives with media enabled; never publish."""
import argparse
import importlib.util
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('cef_pack', ROOT / 'scripts/pack-cef.py')
packer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packer)
ENGINE = ROOT / 'native/engine'
MEDIA_SOURCES = [
    'media_engine.cc', 'media_engine.h', 'media_file.cc', 'media_file.h',
    'media_render.cc', 'media_render.h', 'media_engine_test.cc',
    'media_startup_test.cc', 'media_codec_test.cc', 'media_sync_test.cc',
    'memory_test.cc', 'scaler_failure_test.cc', 'fuzz_bounds.cc', 'fuzz_media.cc',
]
DOCS = ['README.md', 'PROTOCOL.md', 'BROWSER-PROTOCOL.md', 'protocol.schema.json', 'manifest.json']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--runtime-dir', type=Path)
    parser.add_argument('--verification', type=Path)
    args = parser.parse_args()
    out = args.output.resolve()
    manifest = json.loads((ENGINE / 'manifest.json').read_text())
    name = f"{manifest['name']}-{manifest['version']}"
    expected = [out / f'{name}-source.tar.gz', out / 'packages.json', out / 'SHA256SUMS']
    if args.runtime_dir:
        expected.append(out / f'{name}-linux-x86_64.tar.gz')
    if any(p.exists() for p in expected):
        raise ValueError('Use a new output directory; existing packages are preserved')
    entries = {f'native/browser/{file}': ROOT / 'native/browser' / file for file in packer.SOURCES + MEDIA_SOURCES}
    entries.update({file: ENGINE / file for file in DOCS})
    entries['CMakeLists.txt'] = ENGINE / 'CMakeLists.txt'
    entries['LICENSE'] = ROOT / 'LICENSE'
    entries['scripts/install-cef-codecs.py'] = ROOT / 'scripts/install-cef-codecs.py'
    for file in ['probe.py', 'protocol_probe.py']:
        entries[f'tests/{file}'] = ENGINE / 'tests' / file
    if args.verification:
        entries.update({f'verification/{p.relative_to(args.verification)}': p
                        for p in args.verification.rglob('*') if p.is_file()})
    runtime = None
    if args.runtime_dir:
        directory = args.runtime_dir.resolve()
        report = json.loads((args.verification / 'summary.json').read_text()) if args.verification else None
        digest = packer.digest(directory / 'suitecut-browser')
        if not report or report.get('allPassed') is not True or report.get('nativeMedia') is not True or report.get('binarySha256') != digest:
            raise ValueError('Require passing verification for this exact complete native binary')
        closure = json.loads((directory / 'runtime-dependencies.json').read_text())
        if closure['binarySha256'] != digest:
            raise ValueError('Runtime inventory and executable differ')
        pin = json.loads((ROOT / 'native/browser/cef-build-linux-codecs.json').read_text())
        if packer.digest(directory / 'libcef.so') != pin['runtimeLibrarySha256']:
            raise ValueError('CEF library differs from the pinned distribution')
        for library in closure['bundled']:
            if packer.digest(directory / library['path']) != library['sha256']:
                raise ValueError('Bundled dependency changed: ' + library['path'])
        runtime = {f'runtime/{p.relative_to(directory)}': p for p in directory.rglob('*') if p.is_file()}
        runtime.update({file: ENGINE / file for file in DOCS})
        runtime['LICENSE'] = ROOT / 'LICENSE'
        runtime.update({key: value for key, value in entries.items() if key.startswith('verification/')})
        for file in ['cef-build-linux-codecs.json', 'libyuv-build.json']:
            runtime[f'native/browser/{file}'] = ROOT / 'native/browser' / file
    out.mkdir(parents=True, exist_ok=True)
    records = [packer.archive(out / f'{name}-source.tar.gz', entries, name)]
    if runtime:
        records.append(packer.archive(out / f'{name}-linux-x86_64.tar.gz', runtime, name))
    (out / 'packages.json').write_text(json.dumps({'package': manifest, 'archives': records}, indent=2) + '\n')
    (out / 'SHA256SUMS').write_text(''.join(f"{r['sha256']}  {r['file']}\n" for r in records))
    for record in records:
        print(f"{record['file']}: {record['bytes']} bytes, SHA-256 {record['sha256']}")


if __name__ == '__main__':
    main()
