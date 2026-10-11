import os,pathlib,json,subprocess,sys
base=pathlib.Path('/tmp/suitecut-deep-audit');out=base/sys.argv[1];out.mkdir(exist_ok=True)
binary=os.environ.get('SUITECUT_DEEP_HELPER','/tmp/suitecut-cef-full-regression-build/suitecut-browser-deep')
wrapper=out/'wrapper';wrapper.write_text('#!/bin/sh\nexec '+binary+' "$@" 2>>'+str(out/'browser-stderr.log')+'\n');wrapper.chmod(0o700)
env=dict(os.environ,**json.load(open(base/'runtime-env.json')),PWTEST_UNDER_TEST='1',COMPAT_BACKEND=os.environ.get('COMPAT_BACKEND','cef'),COMPAT_DIRECTORY=str(out),SUITECUT_DIRECT_EXECUTABLE=str(wrapper),SUITECUT_CDP_TRACE='1',pnpm_config_verify_deps_before_run='warn')
with (out/'run.log').open('w') as log:
 p=subprocess.run(['taskset','-c','0-3','pnpm','exec','playwright','test','--config','/tmp/suitecut-playwright-upstream/playwright-1.62.1/suitecut-deep.config.ts','--workers=1',*sys.argv[2:]],env=env,stdout=log,stderr=subprocess.STDOUT,cwd='/workspace/suitecut')
(out/'exit.txt').write_text(str(p.returncode));print(out,p.returncode)
