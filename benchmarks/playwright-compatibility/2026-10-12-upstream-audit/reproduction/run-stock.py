from pathlib import Path
import os,psutil,subprocess,json
base=Path('/tmp/suitecut-upstream-full-2026-10-12');env=dict(os.environ);found=False
for process in psutil.process_iter(['cmdline']):
 try:
  if (process.info['cmdline'] or [])[:2]!=['python3',str(base/'runtime.py')]:continue
  for child in process.children(recursive=True):
   if any('playwright/cli.js' in a for a in child.cmdline()):
    context=child.environ()
    for name in ['DISPLAY','PULSE_SERVER','LD_LIBRARY_PATH']:
     if name in context:env[name]=context[name]
    found=True;break
  if found:break
 except psutil.Error:pass
if not found:raise RuntimeError('Native audit display environment is unavailable')
out=base/'stock';out.mkdir(exist_ok=True)
env.update(COMPAT_BACKEND='chromium',COMPAT_DIRECTORY=str(out),MEMORY_DIRECTORY=str(out),PWTEST_UNDER_TEST='1',pnpm_config_verify_deps_before_run='warn')
result=subprocess.run(['taskset','-c','0-3','pnpm','exec','playwright','test','--config','/tmp/suitecut-playwright-upstream/playwright-1.62.1/suitecut-audit.config.ts'],cwd='/workspace/suitecut',env=env,stdout=(base/'stock-run.log').open('w'),stderr=subprocess.STDOUT)
(base/'stock-exit.txt').write_text(str(result.returncode)+'\n')
