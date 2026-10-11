"""Lightweight independent multi-root monitor; no PSS/USS walks during capture."""
import json,sys,time
from pathlib import Path
import psutil
output=Path(sys.argv[1]);roots=[int(x) for x in sys.argv[2:] if x];known={};iteration=0
with output.open('w') as stream:
 while True:
  started=time.monotonic();processes={}
  for pid in roots:
   try:
    parent=psutil.Process(pid)
    for proc in [parent,*parent.children(recursive=True)]:processes[proc.pid]=proc
   except psutil.Error:pass
  rows=[]
  for proc in processes.values():
   try:
    key=(proc.pid,proc.create_time());cpu=proc.cpu_times();total=cpu.user+cpu.system;old=known.get(key);known[key]=(total,started);args=proc.cmdline()
    role='controller' if proc.pid==roots[0] else 'obs' if proc.name()=='obs' else 'sampler' if 'profile-obs-long.py' in ' '.join(args) else 'receiver' if proc.name()=='ffmpeg' and '-listen' in args else 'chromium-renderer' if '--type=renderer' in args else 'chromium-gpu' if '--type=gpu-process' in args else proc.name()
    rows.append({'pid':proc.pid,'role':role,'cpuPercent':0 if old is None else max(0,(total-old[0])/(started-old[1])*100),'rssBytes':proc.memory_info().rss,'threads':proc.num_threads(),'fdCount':proc.num_fds()})
   except psutil.Error:pass
  row={'monotonic':started,'sampleDurationMs':(time.monotonic()-started)*1000,'processes':rows,'privateMemorySampling':False}
  if iteration%10==0:
   row['host']={}
   for name in ['cpu.stat','cpu.pressure','memory.events','memory.pressure']:
    path=Path('/sys/fs/cgroup')/name
    if path.exists():row['host'][name]=path.read_text()
  stream.write(json.dumps(row)+'\n');stream.flush();iteration+=1;time.sleep(max(0,.5-(time.monotonic()-started)))
