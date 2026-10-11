"""Run serial final-build checks after the long-run media checks have passed."""
import json,os,subprocess,time,shutil,hashlib
from pathlib import Path
from datetime import datetime,timezone
root=Path(__file__).resolve().parents[2]
os.chdir(root)
out=root/'benchmarks/obs-cef/2026-10-09-optimization'
state={'startedAt':datetime.now(timezone.utc).isoformat(),'stages':[]}
def save(): (out/'final-checks-driver.json').write_text(json.dumps(state,indent=2))
save()
while not (out/'soak/result.json').exists(): time.sleep(5)
soak=json.loads((out/'soak/result.json').read_text())
assert soak.get('completedAt') and soak.get('activeMs',0)>=soak['requestedMs']*.99,'Long workload must complete before comparative trials'
state['longRunMediaStatus']=soak['status'];state['longRunMediaError']=soak.get('error');save()
env=os.environ.copy()
env.update(DISPLAY=':99',PULSE_SERVER='unix:'+str(root/'.suitecut/runtime/pulse.sock'),SUITECUT_NATIVE_EXECUTABLE=str(root/'.suitecut/native-build/suitecut-browser'),PLAYWRIGHT_BROWSERS_PATH=str(root/'.suitecut/playwright-browsers'),OBS_PID='42864',XVFB_PID='42850')
def run(label,args,variables=None):
 row={'label':label,'command':args,'environmentOverrides':variables or {},'startedAt':datetime.now(timezone.utc).isoformat(),'status':'running'};state['stages'].append(row);save()
 with (out/'logs'/f'final-{label}.log').open('w') as log:
  result=subprocess.run(args,env={**env,**(variables or {})},stdout=log,stderr=subprocess.STDOUT)
 valid=result.returncode==0
 if valid and 'BENCHMARK_OUTPUT' in (variables or {}):
  rows=json.loads((Path(variables['BENCHMARK_OUTPUT'])/'trials.json').read_text());valid=len(rows)==24 and all(r['status']=='passed' for r in rows)
 if valid and 'FEATURE_OUTPUT' in (variables or {}):
  rows=json.loads((Path(variables['FEATURE_OUTPUT'])/'results.json').read_text());valid=bool(rows) and all(r.get('completedAt') and not r.get('error') and all(c['status']=='passed' for c in r['checks']) for r in rows)
 row.update(exitCode=result.returncode,status='passed' if valid else 'failed',completedAt=datetime.now(timezone.utc).isoformat());save()
 if not valid:state['status']='failed';save();raise SystemExit(1)
 if result.returncode:state['status']='failed';save();raise SystemExit(result.returncode)
run('soak-complete-media',['python3','benchmarks/obs-cef/complete-soak-validation.py'])
run('soak-audio-continuity',['python3','benchmarks/obs-cef/analyze-audio-continuity.py','soak','--diagnostic'])
run('soak-packet-motion',['python3','benchmarks/obs-cef/analyze-soak-media.py'])
run('comparison',['node','benchmarks/obs-cef/run-optimized.mjs'],{'BENCHMARK_OUTPUT':str(out/'comparison-final'),'BENCHMARK_MEDIA':str(root/'.suitecut/obs-cef-optimization/comparison-final'),'BENCHMARK_CASE':'720p60-canvas,1080p60-canvas,4k30-canvas,4k60-scroll','BENCHMARK_TRIALS':'3'})
for trial in range(1,4):
 label=f'features-stream-final-{trial}'
 run(label,['node','benchmarks/obs-cef/features.mjs'],{'FEATURE_OUTPUT':str(out/label),'FEATURE_MEDIA':str(root/'.suitecut/obs-cef-optimization'/label),'FEATURE_MODE':'stream','FEATURE_BACKENDS':'obs,native'})
run('features-record-final',['node','benchmarks/obs-cef/features.mjs'],{'FEATURE_OUTPUT':str(out/'features-record-final'),'FEATURE_MEDIA':str(root/'.suitecut/obs-cef-optimization/features-record-final'),'FEATURE_MODE':'record','FEATURE_BACKENDS':'obs,native'})
backup=root/'.suitecut/obs-cef-optimization/native-checks-before-final'
backup.mkdir(exist_ok=True)
for existing in ['native-verification','native-recording-verification','native-stream-verification']:
 if (root/'.suitecut'/existing).exists():shutil.copytree(root/'.suitecut'/existing,backup/existing,dirs_exist_ok=False)
for name,path in [('browser','native-verification'),('recording','native-recording-verification'),('stream','native-stream-verification')]:
 old=out/'checks/pre-audio-window-fix';old.mkdir(exist_ok=True)
 shutil.copy2(out/'checks'/f'{name}.json',old/f'{name}.json')
 run('native-'+name,['node',f'scripts/verify-native-{name}.mjs'])
 shutil.copy2(root/'.suitecut'/path/'report.json',out/'checks'/f'{name}.json')
state.update(status='passed',completedAt=datetime.now(timezone.utc).isoformat());save()
