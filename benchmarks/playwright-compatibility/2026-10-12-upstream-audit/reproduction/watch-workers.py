import psutil,time,json,subprocess,os,datetime
from pathlib import Path
base=Path('/tmp/suitecut-upstream-full-2026-10-12');samples={};busy={}
while not ((base/'native-exit.txt').exists() and (base/'stock-exit.txt').exists()):
 for p in psutil.process_iter(['pid','cmdline','cpu_times','memory_info']):
  try:
   if not any('playwright/lib/worker/workerProcessEntry.js' in a for a in p.info['cmdline'] or []):continue
   backend=p.environ().get('COMPAT_BACKEND')
   if backend not in ['cef','chromium']:continue
   now=time.monotonic();cpu=sum(p.info['cpu_times'][:2]);previous=samples.get(p.pid);samples[p.pid]=(now,cpu)
   if previous:
    elapsed=now-previous[0];delta=cpu-previous[1]
    busy[p.pid]=busy.get(p.pid,0)+elapsed if delta/elapsed>.45 else 0
   if busy.get(p.pid,0)<200:continue
   children=p.children(recursive=True)
   row={'pid':p.pid,'backend':backend,'reason':'External watchdog: >200 seconds of sustained CPU activity with unresponsive test event loop','cpuSeconds':cpu,'busyWallSeconds':busy[p.pid],'rssBytes':p.info['memory_info'].rss,'childPids':[c.pid for c in children],'timeUtc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'action':'GDB stack sample, then SIGKILL worker / SIGTERM audited descendants'}
   with (base/'worker-watchdog.jsonl').open('a') as f:f.write(json.dumps(row)+'\n')
   env=dict(os.environ,LD_LIBRARY_PATH='/workspace/suitecut/.suitecut/memory-tools/root/usr/lib/x86_64-linux-gnu')
   with (base/f'worker-gdb-{p.pid}.log').open('w') as log:
    try:subprocess.run(['/workspace/suitecut/.suitecut/memory-tools/root/usr/bin/gdb','--batch','-q','-p',str(p.pid),'-ex','set pagination off','-ex','thread 1','-ex','bt 30','-ex','detach'],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=15)
    except subprocess.TimeoutExpired:pass
   p.kill()
   for c in children:
    try:c.terminate()
    except psutil.Error:pass
   samples.pop(p.pid,None);busy.pop(p.pid,None)
  except (psutil.Error,ValueError,ZeroDivisionError):pass
 time.sleep(3)
