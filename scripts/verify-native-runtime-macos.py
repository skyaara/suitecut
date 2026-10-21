#!/usr/bin/env python3
"""Extract and relocate a native macOS runtime, then verify actual browser outcomes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', required=True, type=Path)
    parser.add_argument('--client-build', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    extracted = output/'extracted'
    extracted.mkdir()
    with tarfile.open(args.archive) as archive:
        archive.extractall(extracted, filter='data')
    roots = list(extracted.iterdir())
    assert len(roots)==1 and roots[0].is_dir()
    install = output/'relocated installation'
    roots[0].rename(install)
    manifest=json.loads((install/'runtime-manifest.json').read_text())
    for entry in manifest['files']:
        file=install/entry['path']
        if 'symlink' in entry:
            assert os.readlink(file)==entry['symlink']
        else:
            with file.open('rb') as stream:
                assert hashlib.file_digest(stream,'sha256').hexdigest()==entry['sha256'],entry['path']
    env=dict(os.environ,PATH='/usr/bin:/bin')
    for key in list(env):
        if key.startswith(('DYLD_','SUITECUT_')):
            del env[key]
    checks=['all extracted manifest file hashes and symlinks match']

    def run(name, command, timeout=120):
        result=subprocess.run([str(x) for x in command],capture_output=True,cwd=output,env=env,timeout=timeout)
        (output/(name+'.stdout')).write_bytes(result.stdout)
        (output/(name+'.stderr')).write_bytes(result.stderr)
        if result.returncode:
            raise RuntimeError(f'{name}: {result.returncode}: {result.stderr.decode(errors="replace")[-4000:]}')
        return result.stdout

    app=install/'libexec/suitecut/suitecut-browser.app'
    engine=app/'Contents/MacOS/suitecut-browser'
    run('codesign',['/usr/bin/codesign','--verify','--deep','--strict',app])
    checks.append('relocated application and nested code signatures verify')
    run('ffmpeg-version',[install/'bin/ffmpeg','-version'])
    run('ffprobe-version',[install/'bin/ffprobe','-version'])
    checks.append('bundled media tools launch without Homebrew on PATH')
    sessions=Path(tempfile.mkdtemp(prefix='sc-mac-proof-',dir='/private/tmp'))
    base=[install/'bin/suitecut','--session-dir',sessions,'--session','proof','--timeout-ms','30000']
    fixture=output/'control.html'
    fixture.write_text('''<!doctype html><title>Relocated runtime proof</title>
<label>Name<input aria-label="Name"></label><button onclick="document.querySelector('output').textContent=document.querySelector('input').value">Save</button><output></output>''')
    def cli(name,*words):
        reply=json.loads(run(name,[*base,*words]))
        assert reply['ok'],reply
        return reply['result']
    try:
        initial=cli('cli-open','open',fixture.as_uri())['observation']
        control=next(c['ref'] for c in initial['controls'] if c.get('name')=='Name')
        button=next(c['ref'] for c in initial['controls'] if c.get('name')=='Save')
        cli('cli-fill','--response','none','fill',control,'Relocated λ 😀')
        assert cli('cli-value','get','value',control)=='Relocated λ 😀'
        cli('cli-click','--response','none','click',button)
        actual=cli('cli-evaluate','--tools','debug','call','evaluate',json.dumps({'expression':"document.querySelector('output').textContent"}))
        assert actual=='Relocated λ 😀',actual
        cli('cli-screenshot','screenshot','--output',str(output/'relocated.png'))
        status=cli('cli-close','close')
        assert cli('cli-status','status')['running'] is False
        checks.append('named session observes, fills Unicode, clicks with verified page effect, captures PNG and closes')
    finally:
        try:
            cli('cli-final-close','close')
        finally:
            shutil.rmtree(sessions)
    # Reproduce macOS code-signing clone isolation: no sibling runtime/lib.
    standalone = output/'standalone application'/'suitecut-browser.app'
    standalone.parent.mkdir()
    subprocess.run(['/bin/cp','-cR',str(app),str(standalone)],check=True)
    standalone_engine=standalone/'Contents/MacOS/suitecut-browser'
    build=args.client_build.resolve()
    root=Path(__file__).resolve().parent.parent
    run('cpp-real-browser',[build/'native-client-real-browser','--browser',standalone_engine,'--fixture',root/'native/client/tools/fixture.html',
                            '--iterations',30,'--cycles',50,'--output',output/'cpp-control/results.json'])
    proof=json.loads((output/'cpp-control/results.json').read_text())
    assert proof['passed'],proof
    checks.append('standalone app launches and runs without the sibling runtime library directory')
    checks.append(f'C++ facade real-browser proof: {len(proof["checks"])} checks and 50 page/context cycles')
    run('cpp-cross-browser-features',[build/'suitecut-native-cross_browser_features','--backend','cef','--browser',engine,
                                     '--output',output/'cpp-features'])
    features=json.loads((output/'cpp-features/results.json').read_text())
    assert features['ok'],features
    checks.append(f'C++ feature proof: {len(features["checks"])} checks in {len(features["scenarios"])} live HTTP scenarios')
    run('cpp-artifacts',[build/'suitecut-native-cef_artifact_workflows',''+str(engine),output/'cpp-artifacts'])
    artifacts=json.loads((output/'cpp-artifacts/results.json').read_text())
    assert artifacts['ok'],artifacts
    checks.append(f'C++ artifact/frame/popup proof: {len(artifacts["cases"])} live scenarios')
    report={'ok':True,'installation':str(install),'archive':str(args.archive.resolve()),
            'manifestFilesVerified':len(manifest['files']),'checks':checks,
            'nodeOnPath':False,'homebrewOnPath':False,'browserCapabilities':features['ready']['capabilities']}
    (output/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report),flush=True)


if __name__=='__main__':
    main()
