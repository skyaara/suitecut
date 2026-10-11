"""Preserve the failed timestamp diagnostic, then run the validated final build."""
import json,os,time,subprocess,hashlib,shutil
from pathlib import Path
from datetime import datetime,timezone
root=Path(__file__).resolve().parents[2];os.chdir(root)
out=root/'benchmarks/obs-cef/2026-10-09-optimization';media=root/'.suitecut/obs-cef-optimization'
while not (out/'soak/result.json').exists():time.sleep(5)
while json.loads((out/'final-checks-driver.json').read_text()).get('status')!='blocked by failed long-run media validation':time.sleep(1)
env={**os.environ,'DISPLAY':':99','PULSE_SERVER':'unix:'+str(root/'.suitecut/runtime/pulse.sock'),'SUITECUT_NATIVE_EXECUTABLE':str(root/'.suitecut/native-build/suitecut-browser'),'SOAK_LABEL':'capture-stamp-pilot-wide-pulse','SOAK_DURATION_MS':'120000'}
with (out/'logs/capture-stamp-pilot-wide-pulse.log').open('w') as log:
 check=subprocess.run(['node','--expose-gc','benchmarks/obs-cef/soak.mjs'],env=env,stdout=log,stderr=subprocess.STDOUT)
assert check.returncode==0
pilot=json.loads((out/'capture-stamp-pilot-wide-pulse/result.json').read_text())
assert pilot['status']=='passed',pilot.get('error')
old='soak-before-capture-stamp-fix'
assert not (out/old).exists() and not (media/old).exists()
shutil.move(out/'soak',out/old);shutil.move(media/'soak',media/old)
old_prefix=str(media/'soak');new_prefix=str(media/old)
for name in ['result.json','manifest.json']:
 path=out/old/name
 if path.exists():path.write_text(path.read_text().replace(old_prefix,new_prefix))
(out/old/'artifact-relocation.json').write_text(json.dumps({'oldPrefix':old_prefix,'newPrefix':new_prefix,'rawDiagnosticPaths':'Original recorded command paths remain unchanged outside result/manifest.'},indent=2))
for name in ['soak-tested-sources.json','soak-driver.json','final-checks-driver.json','final-checks-coordinator.json']:
 if (out/name).exists():shutil.move(out/name,out/('before-capture-stamp-fix-'+name))
for name in ['soak.log','final-checks-coordinator.log']:
 if (out/'logs'/name).exists():shutil.move(out/'logs'/name,out/'logs'/('before-capture-stamp-fix-'+name))
files=[*sorted((root/'src').glob('native*.ts')),root/'src/live-stream.ts',root/'src/live-audio.ts',root/'src/media.ts',root/'native/browser/browser.cc']
(out/'soak-tested-sources.json').write_text(json.dumps({str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files},indent=2))
env={**os.environ,'DISPLAY':':99','PULSE_SERVER':'unix:'+str(root/'.suitecut/runtime/pulse.sock'),'SUITECUT_NATIVE_EXECUTABLE':str(root/'.suitecut/native-build/suitecut-browser')}
with (out/'logs/soak.log').open('w') as log:
 p=subprocess.Popen(['node','--expose-gc','benchmarks/obs-cef/soak.mjs'],env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
(out/'soak-driver.json').write_text(json.dumps({'pid':p.pid,'detachedProcessGroup':True,'audioTimestampDiagnostics':True,'fixes':['capture clock upper bound','bounded future PCM allowance','selected-pixel capture timestamp association'],'startedAt':datetime.now(timezone.utc).isoformat()}))
with (out/'logs/final-checks-coordinator.log').open('w') as log:
 c=subprocess.Popen(['python3','benchmarks/obs-cef/final-checks.py'],env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
(out/'final-checks-coordinator.json').write_text(json.dumps({'pid':c.pid,'detachedProcessGroup':True,'waitsFor':'final soak/result.json passed'}))
print(json.dumps({'soakPid':p.pid,'coordinatorPid':c.pid}))
