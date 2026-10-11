import os,pathlib,subprocess,json,time,fcntl
root=pathlib.Path('/workspace/suitecut');out=root/'benchmarks/release/2026-10-11-suitecut-2.1';os.chdir(root)
os.sched_setaffinity(0,{0,1,2,3});lock=(root/'.suitecut/thread-control.lock').open('a');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
checks=json.loads((out/'release-checks.json').read_text());attempts=json.loads((out/'release-attempts.json').read_text()) if (out/'release-attempts.json').exists() else checks.copy()
env=dict(os.environ,pnpm_config_verify_deps_before_run='warn')
def run(name,args,child_env=None):
 print('START',name,flush=True);started=time.monotonic();number=1+sum(x['name']==name for x in attempts);logfile=f'{name}-attempt{number}.log'
 with (out/'logs'/logfile).open('w') as log:
  p=subprocess.run(args,env=child_env or env,stdout=log,stderr=subprocess.STDOUT)
 item={'name':name,'args':args,'exitCode':p.returncode,'wallSeconds':time.monotonic()-started,'log':'logs/'+logfile,'pnpmEnvironment':'verify_deps_before_run=warn; frozen lockfile separately passed; existing workspace symlinks retained'}
 checks[:]=[x for x in checks if x['name']!=name];checks.append(item);attempts.append(item)
 (out/'release-checks.json').write_text(json.dumps(checks,indent=2)+'\n');(out/'release-attempts.json').write_text(json.dumps(attempts,indent=2)+'\n');print('FINISH',name,p.returncode,flush=True)
 if p.returncode:raise SystemExit(p.returncode)
for name,args in [('prepublish',['pnpm','prepublishOnly']),('pack-release',['pnpm','pack:release'])]:
 if not any(x['name']==name and x['exitCode']==0 for x in checks):run(name,args)
lock.close()
browser_env=dict(env,SUITECUT_RUNTIME_EXECUTABLE=str(root/'.suitecut/native-build/suitecut-browser'))
for name,args in [('installed-native',['scripts/verify-beta-package.mjs','--release']),('engine-e2e',['playwright']),('external-browser',['benchmarks/external-browser/connect.mjs'])]:
 if any(x['name']==name and x['exitCode']==0 for x in checks):continue
 browser_env.update(MEMORY_DIRECTORY=str(out/name),PLAYWRIGHT_BROWSERS_PATH=str(root/'.suitecut/playwright-browsers'),SUITECUT_CONNECT_DIRECTORY=str(out/'external-browser'))
 run(name,['python3','benchmarks/native-memory/runtime.py',*args],browser_env)
