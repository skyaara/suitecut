import os,pathlib,json,subprocess,sys
base=pathlib.Path('/tmp/suitecut-deep-audit');out=base/sys.argv[1];out.mkdir(exist_ok=True)
binary=os.environ.get('SUITECUT_DEEP_HELPER','/tmp/suitecut-cef-full-regression-build/suitecut-browser-deep')
wrapper=out/'wrapper';wrapper.write_text('#!/bin/sh\nexec '+binary+' "$@" 2>>'+str(out/'browser-stderr.log')+'\n');wrapper.chmod(0o700)
env=dict(os.environ,**json.load(open(base/'runtime-env.json')),SUITECUT_NATIVE_EXECUTABLE=str(wrapper),SUITECUT_ENGINE_E2E_DIRECTORY=str(out),COMPAT_DIRECTORY=str(out),COMPAT_BACKEND=sys.argv[2],pnpm_config_verify_deps_before_run='warn')
config='benchmarks/native-engine/playwright.config.ts' if sys.argv[2]=='engine' else 'benchmarks/playwright-compatibility/playwright.config.ts'
args=['taskset','-c','0-3','pnpm','exec','playwright','test','--config',config,*sys.argv[3:]]
(out/'command.json').write_text(json.dumps(args))
with (out/'run.log').open('w') as log:
 p=subprocess.run(args,env=env,stdout=log,stderr=subprocess.STDOUT,cwd='/workspace/suitecut')
(out/'exit.txt').write_text(str(p.returncode));print(out,p.returncode)
