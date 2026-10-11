"""Independent low-impact raw CPU/scheduling/pressure samples, without PSS walks."""
import json,sys,time
from pathlib import Path
import psutil
out=Path(sys.argv[1]);root=psutil.Process(int(sys.argv[2]));known={};iteration=0
with out.open('w') as stream:
 while root.is_running():
  started=time.monotonic();rows=[]
  try:processes=[root,*root.children(recursive=True)]
  except psutil.Error:break
  for process in processes:
   try:
    cpu=process.cpu_times();observed=time.monotonic();total=cpu.user+cpu.system;identity=(process.pid,process.create_time());old=known.get(identity);known[identity]=(total,observed)
    args=process.cmdline();role='node' if process.pid==root.pid else 'sampler' if 'profile-pipeline.py' in ' '.join(args) else 'receiver' if process.name()=='ffmpeg' and '-listen' in args else 'encoder' if process.name()=='ffmpeg' else 'cef-renderer' if '--type=renderer' in args else 'cef-gpu' if '--type=gpu-process' in args else process.name()
    row={'pid':process.pid,'role':role,'cpuSeconds':total,'cpuObservedMonotonic':observed,'cpuPercent':None if old is None else max(0,(total-old[0])/(observed-old[1])*100),'rssBytes':process.memory_info().rss,'threads':process.num_threads()}
    if iteration%5==0:row['fdCount']=process.num_fds()
    sched=Path('/proc')/str(process.pid)/'schedstat'
    if sched.exists():row['mainThreadSchedstat']=sched.read_text().strip()
    rows.append(row)
   except (psutil.Error,OSError):pass
  host={}
  for name in ['cpu.stat','cpu.pressure','memory.events','memory.pressure','io.pressure']:
   path=Path('/sys/fs/cgroup')/name
   if path.exists():host[name]=path.read_text()
  row={'monotonic':started,'sampleDurationMs':(time.monotonic()-started)*1000,'processes':rows,'host':host,'privateMemorySampling':False}
  stream.write(json.dumps(row)+'\n');stream.flush();iteration+=1;time.sleep(max(0,1-(time.monotonic()-started)))
