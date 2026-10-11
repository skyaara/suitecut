"""Low-impact multi-root resource accounting; optional PSS in memory runs only."""
import json,os,signal,sys,time
from pathlib import Path
import psutil
out=Path(sys.argv[1]);root_pids=list(map(int,sys.argv[2:]));seen={};index=0;done=False;catalogue={};catalogue_at=-100.0;known_processes={};zombie_pids=set()
signal.signal(signal.SIGTERM,lambda *args:globals().__setitem__('done',True))
private_every=20 if os.environ.get('BENCH_PROFILE')=='memory' else 0
with out.open('w',buffering=1) as stream:
 while not done:
  start=time.monotonic();rows=[]
  if start-catalogue_at>=1:
   entries={int(entry.name):entry.path for entry in os.scandir('/proc') if entry.name.isdecimal()}
   zombie_pids.intersection_update(entries)
   parents={}
   for pid in entries.keys()-zombie_pids:
    try:
     data=(Path(entries[pid])/'stat').read_bytes();fields=data[data.rfind(b')')+2:].split()
     if fields[0]==b'Z':zombie_pids.add(pid);continue
     parents.setdefault(int(fields[1]),[]).append(pid)
    except (OSError,ValueError,IndexError):pass
   selected=set(root_pids);todo=list(root_pids)
   while todo:
    pid=todo.pop()
    for child in parents.get(pid,[]):
     if child not in selected:selected.add(child);todo.append(child)
   catalogue={}
   for pid in selected:
    try:
     if pid not in known_processes:known_processes[pid]=psutil.Process(pid)
     catalogue[pid]=known_processes[pid]
    except psutil.Error:pass
   catalogue_at=start
  procs=catalogue
  for p in procs.values():
   try:
    args=p.cmdline();joined=' '.join(args)
    if 'benchmarks/publication/sample.py' in joined:continue
    cpu=p.cpu_times();total=cpu.user+cpu.system;identity=(p.pid,p.create_time());now=time.monotonic();old=seen.get(identity);seen[identity]=(total,now)
    role='controller' if p.pid==root_pids[0] else 'receiver' if '-listen' in args and p.name()=='ffmpeg' else 'encoder' if p.name()=='ffmpeg' else 'renderer' if '--type=renderer' in args else 'gpu' if '--type=gpu-process' in args else 'audio-utility' if any('audio.mojom.AudioService' in a for a in args) else 'browser' if 'suitecut-browser' in p.name() or p.name()=='chrome' else 'obs' if p.name()=='obs' else 'display' if p.name()=='Xvfb' else 'audio-server' if p.name()=='pulseaudio' else p.name()
    row={'pid':p.pid,'creationTime':identity[1],'role':role,'cpuSeconds':total,'cpuCores':None if old is None else max(0,(total-old[0])/(now-old[1])),'rssBytes':p.memory_info().rss,'threads':p.num_threads()}
    if index%10==0:row['fds']=p.num_fds()
    if private_every and index%private_every==0:
     at=time.monotonic()
     try:
      data={}
      for line in (Path('/proc')/str(p.pid)/'smaps_rollup').read_text().splitlines():
       key,_,value=line.partition(':')
       if key in ['Pss','Private_Clean','Private_Dirty','Private_Hugetlb']:data[key]=int(value.strip().split()[0])*1024
      row.update(pssBytes=data.get('Pss'),ussBytes=sum(data.get(k,0) for k in ['Private_Clean','Private_Dirty','Private_Hugetlb']))
     except (OSError,ValueError):row['privateMemoryUnavailable']=True
     row['privateSampleMs']=(time.monotonic()-at)*1000
    rows.append(row)
   except (psutil.Error,OSError):pass
  aggregate=sum(r['rssBytes'] for r in rows)
  observer=psutil.Process().cpu_times()
  event={'observerCpuSeconds':observer.user+observer.system,'pidInventoryIntervalSeconds':1,'monotonic':start,'sampleDurationMs':(time.monotonic()-start)*1000,'processes':rows,'summedRssBytes':aggregate,'memoryGuardExceeded':aggregate>8*1024**3}
  if index%10==0:
   event['host']={};event['unavailableHostMetrics']=[]
   for name in ['cpu.stat','cpu.pressure','memory.events','memory.pressure','memory.current']:
    try:event['host'][name]=(Path('/sys/fs/cgroup')/name).read_text()
    except OSError:event['unavailableHostMetrics'].append(name)
  stream.write(json.dumps(event)+'\n')
  if event['memoryGuardExceeded']:
   (out.parent/'memory-guard-exceeded.json').write_text(json.dumps(event,indent=2))
   os.kill(root_pids[0],signal.SIGTERM);break
  index+=1;time.sleep(max(0,.5-(time.monotonic()-start)))
